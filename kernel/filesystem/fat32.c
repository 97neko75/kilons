#include <stdint.h>
#include <stddef.h>
#include <kilons/io.h>
#include <kilons/console.h>
#include <kilons/fat32.h>
/* ATA primary master, legacy PIO, read-only, LBA28. QEMU -drive if=ide,index=0. */
#define ATA 0x1f0
#define SECTOR 512u
static uint8_t sector[SECTOR];
static uint8_t fatsector[SECTOR];
static uint32_t fat_cache_lba=0xffffffffu;
static uint32_t volume, fat_start, data_start, clusters, root, fat_sectors;
static uint8_t spc, fats;
static int mounted;
static int ata_wait(int data) {
    for (unsigned i=0;i<1000000;i++) {
        uint8_t s=inb(ATA+7);
        if (!(s&0x80)) {
            if (s&0x21) return 0;
            if (!data || (s&0x08)) return 1;
        }
    }
    return 0;
}
static int read_sector(uint32_t lba,uint8_t *dst) {
    if (lba>=0x10000000u) return 0;
    outb(ATA+6,(uint8_t)(0xe0 | ((lba>>24)&15)));
    outb(ATA+2,1);
    outb(ATA+3,(uint8_t)lba);
    outb(ATA+4,(uint8_t)(lba>>8));
    outb(ATA+5,(uint8_t)(lba>>16));
    outb(ATA+7,0x20);
    if (!ata_wait(1)) return 0;
    for(unsigned i=0;i<256;i++) { uint16_t v=inw(ATA); dst[i*2]=(uint8_t)v; dst[i*2+1]=(uint8_t)(v>>8); }
    return 1;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void num(uint32_t n) { char b[11]; unsigned i=10;b[i]=0;do{b[--i]=(char)('0'+n%10);n/=10;}while(n);console_write(b+i); }
static int valid_cluster(uint32_t c) { return c>=2 && c<clusters+2; }
static uint32_t next_cluster(uint32_t c) {
    if(!valid_cluster(c)) return 0;
    uint32_t off=c*4, lba=fat_start+off/SECTOR;
    if(lba>=fat_start+fat_sectors) return 0;
    if(lba!=fat_cache_lba) { if(!read_sector(lba,fatsector))return 0;fat_cache_lba=lba; }
    uint32_t n=rd32(fatsector+(off%SECTOR))&0x0fffffff;
    return valid_cluster(n)?n:0;
}
int fat32_mount(void) {
    mounted=0;fat_cache_lba=0xffffffffu;
    if(!read_sector(0,sector))return 0;
    volume=0;
    /* Accept superfloppy BPB, or first FAT32 MBR partition. */
    if(!(sector[510]==0x55 && sector[511]==0xaa))return 0;
    if(rd16(sector+11)!=512 || sector[16]==0) {
        uint8_t *p=sector+446;
        int found=0;
        for(unsigned i=0;i<4;i++,p+=16) {
            if((p[4]==0x0b || p[4]==0x0c) && rd32(p+8)) {volume=rd32(p+8);found=1;break;}
        }
        if(!found || !read_sector(volume,sector))return 0;
    }
    if(sector[510]!=0x55 || sector[511]!=0xaa || rd16(sector+11)!=512)return 0;
    spc=sector[13];fats=sector[16];
    uint32_t reserved=rd16(sector+14), total=rd32(sector+32);
    fat_sectors=rd32(sector+36);root=rd32(sector+44);
    if(!spc || (spc&(spc-1)) || spc>128 || !fats || !reserved || !fat_sectors || !total || rd16(sector+17)!=0)return 0;
    uint64_t overhead=(uint64_t)reserved+(uint64_t)fats*fat_sectors;
    if(overhead>=total)return 0;
    clusters=(uint32_t)((total-overhead)/spc);
    if(clusters<65525 || clusters>0x0ffffff5u || !valid_cluster(root))return 0;
    uint64_t f=(uint64_t)volume+reserved, data=f+(uint64_t)fats*fat_sectors;
    if(data>=0x10000000u || (uint64_t)volume+total>0x10000000u || ((uint64_t)fat_sectors*512)/4<clusters+2)return 0;
    fat_start=(uint32_t)f;data_start=(uint32_t)data;
    mounted=1;return 1;
}
void fat32_info(void) {
    if(!mounted){console_write("FAT32: not mounted (use mount)\n");return;}
    console_write("FAT32 read-only: volume LBA ");num(volume);
    console_write(", sectors/cluster ");num(spc);
    console_write(", data clusters ");num(clusters);console_putc('\n');
}
static void name_of(const uint8_t *e,char *out) {
    unsigned n=0;
    for(unsigned i=0;i<8 && e[i]!=' ';i++)out[n++]=(char)e[i];
    if(e[8]!=' ') {out[n++]='.';for(unsigned i=8;i<11 && e[i]!=' ';i++)out[n++]=(char)e[i];}
    out[n]=0;
}
static int eqnocase(const char *a,const char *b) {
    while(*a && *b){char x=*a++,y=*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;}return *a==*b;
}
/* Directory traversal bounded to prevent loops on corrupt media. */
static int walk(const char *target,uint32_t *first,uint32_t *size) {
    uint32_t c=root;
    for(uint32_t visited=0;c && visited<clusters;visited++) {
        for(unsigned s=0;s<spc;s++) {
            uint64_t lba=(uint64_t)data_start+(uint64_t)(c-2)*spc+s;
            if(lba>=0x10000000u || !read_sector((uint32_t)lba,sector))return -1;
            for(unsigned off=0;off<512;off+=32) {
                const uint8_t *e=sector+off;
                if(e[0]==0)return 0;
                if(e[0]==0xe5 || e[11]==0x0f || (e[11]&0x08))continue;
                char name[13];name_of(e,name);
                uint32_t fc=((uint32_t)rd16(e+20)<<16)|rd16(e+26);
                if(target) {
                    if(eqnocase(name,target)) {
                        if(e[11]&0x10)return -2;
                        *first=fc;*size=rd32(e+28);return 1;
                    }
                } else {
                    console_write((e[11]&0x10)?"<DIR> ":"      ");console_write(name);
                    if(!(e[11]&0x10)){console_write("  ");num(rd32(e+28));}
                    console_putc('\n');
                }
            }
        }
        c=next_cluster(c);
    }
    return 0;
}
void fat32_ls(void) {if(!mounted){console_write("No FAT32 volume mounted\n");return;}if(walk(0,0,0)<0)console_write("FAT32 directory read error\n");}
void fat32_cat(const char *name) {
    if(!mounted){console_write("No FAT32 volume mounted\n");return;}
    if(!*name){console_write("Usage: cat NAME.TXT (root directory, 8.3 names)\n");return;}
    uint32_t c=0,size=0;int found=walk(name,&c,&size);
    if(found!=1){console_write(found==-2?"cat: directory\n":"cat: file not found or read error\n");return;}
    if(size>65536){console_write("cat: file exceeds 64 KiB display limit\n");return;}
    uint32_t remain=size;
    for(uint32_t visited=0;remain && valid_cluster(c) && visited<clusters;visited++) {
        for(unsigned s=0;s<spc && remain;s++) {
            uint64_t lba=(uint64_t)data_start+(uint64_t)(c-2)*spc+s;
            if(lba>=0x10000000u || !read_sector((uint32_t)lba,sector)){console_write("\ncat: disk read error\n");return;}
            unsigned count=remain<512?remain:512;
            for(unsigned i=0;i<count;i++) {char ch=(char)sector[i];if(ch=='\n'||ch=='\r'||ch=='\t'||(ch>=32&&ch<=126))console_putc(ch);else console_putc('.');}
            remain-=count;
        }
        if(remain)c=next_cluster(c);
    }
    if(remain)console_write("\ncat: truncated cluster chain\n");
    else console_putc('\n');
}

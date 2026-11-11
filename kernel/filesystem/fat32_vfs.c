#include <stddef.h>
#include <stdint.h>
#include <kilons/block.h>
#include <kilons/fat32_vfs.h>
/* FAT32 volume driver: read-only, bounded chain walks, DOS 8.3 names.
 * This deliberately does NOT pretend to implement LFN, FAT writes, or ext4. */
#define SECTOR 512U
#define HANDLES 12U
#define MAX_WALK 700000U
struct fat_volume {
    int device,ready;
    uint32_t total,fat_start,fat_sectors,data_start,root,clusters;
    uint8_t spc;
};
static struct fat_volume vol;
struct fat_entry {char name[13];uint32_t cluster,size;uint64_t ino;int is_dir;};
struct fat_handle {int used;uint32_t first,size;size_t offset;uint64_t ino;};
static struct fat_handle handles[HANDLES];
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int read_sector(uint32_t lba,uint8_t dst[SECTOR]) {
    return vol.ready && lba<vol.total && block_read(vol.device,lba,dst);
}
static int valid(uint32_t cluster){return cluster>=2 && cluster<vol.clusters+2;}
static uint32_t next_cluster(uint32_t cluster) {
    if(!valid(cluster))return 0;
    uint64_t offset=(uint64_t)cluster*4U;
    uint32_t fat_lba=vol.fat_start+(uint32_t)(offset/SECTOR);
    if(fat_lba>=vol.fat_start+vol.fat_sectors)return 0;
    uint8_t sector[SECTOR];
    if(!read_sector(fat_lba,sector))return 0;
    uint32_t next=le32(sector+(offset%SECTOR))&0x0fffffffU;
    return valid(next)?next:0;
}
static int letter_eq(char a,char b) {
    if(a>='a' && a<='z')a=(char)(a-'a'+'A');
    if(b>='a' && b<='z')b=(char)(b-'a'+'A');
    return a==b;
}
static int name_eq(const char *a,const char *b) {
    while(*a && *b)if(!letter_eq(*a++,*b++))return 0;
    return *a==*b;
}
static void short_name(const uint8_t *record,char out[13]) {
    unsigned n=0;
    for(unsigned i=0;i<8 && record[i]!=' ';i++) {
        char ch=(char)record[i];
        if(i==0 && (unsigned char)ch==0x05U)ch=(char)0xe5;
        out[n++]=ch;
    }
    if(record[8]!=' ') {
        out[n++]='.';
        for(unsigned i=8;i<11 && record[i]!=' ';i++)out[n++]=(char)record[i];
    }
    out[n]=0;
}
/* Return 1=entry, 0=end of directory, -1=I/O/corrupt chain. */
static int dir_entry(uint32_t first,unsigned index,struct fat_entry *out) {
    uint32_t c=first;unsigned seen=0;
    uint32_t limit=vol.clusters<MAX_WALK?vol.clusters:MAX_WALK;
    for(uint32_t hop=0;valid(c) && hop<limit;hop++,c=next_cluster(c)) {
        for(unsigned s=0;s<vol.spc;s++) {
            uint64_t lba=(uint64_t)vol.data_start+(uint64_t)(c-2)*vol.spc+s;
            if(lba>=vol.total)return -1;
            uint8_t sector[SECTOR];
            if(!read_sector((uint32_t)lba,sector))return -1;
            for(unsigned j=0;j<SECTOR;j+=32) {
                const uint8_t *d=sector+j;
                if(d[0]==0)return 0;
                if(d[0]==0xe5 || d[11]==0x0f || (d[11]&0x08))continue;
                /* Do not expose . and .. as readdir children; path walker
                 * already implements both without consulting the disk. */
                char name[13];short_name(d,name);
                if((name[0]=='.' && !name[1]) || (name[0]=='.' && name[1]=='.' && !name[2]))continue;
                if(seen++!=index)continue;
                unsigned k=0;while(name[k]){out->name[k]=name[k];k++;}out->name[k]=0;
                out->cluster=((uint32_t)le16(d+20)<<16)|le16(d+26);
                out->size=le32(d+28);out->is_dir=!!(d[11]&0x10);
                out->ino=((uint64_t)lba<<5)|(j/32U);
                return 1;
            }
        }
        if(!next_cluster(c))return 0;
    }
    return -1;
}
/* Backend paths have no leading '/' and use canonical components. */
static int lookup(const char *path,struct fat_entry *out) {
    if(!vol.ready || !path || !out)return -1;
    if(!*path){*out=(struct fat_entry){{0},vol.root,0,1,1};return 0;}
    uint32_t parent=vol.root;
    const char *p=path;
    while(*p) {
        char part[128];unsigned n=0;
        while(*p && *p!='/') {
            if(n+1>=sizeof(part))return -1;
            part[n++]=*p++;
        }
        part[n]=0;
        if(*p=='/')p++;
        int found=0;
        for(unsigned index=0;index<65536U;index++) {
            struct fat_entry entry;
            int rc=dir_entry(parent,index,&entry);
            if(rc<0)return -1;
            if(rc==0)break;
            if(name_eq(entry.name,part)) { *out=entry;found=1;break; }
        }
        if(!found)return -1;
        if(*p) {
            if(!out->is_dir || !valid(out->cluster))return -1;
            parent=out->cluster;
        }
    }
    return 0;
}
int fat32_vfs_attach(int device) {
    uint64_t sectors=0;bool writable=false;uint8_t b[SECTOR];
    vol.ready=0;
    if(!block_info(device,&sectors,&writable) || !sectors || sectors>0xffffffffULL || !block_read(device,0,b))return -1;
    if(b[510]!=0x55 || b[511]!=0xaa || le16(b+11)!=SECTOR || le16(b+17)!=0)return -1;
    uint32_t reserved=le16(b+14),fat=le32(b+36),root=le32(b+44);
    uint32_t total=le16(b+19)?le16(b+19):le32(b+32);
    uint8_t spc=b[13],fats=b[16];
    if(!reserved || !fat || !fats || fats>4 || !spc || spc>128 || (spc&(spc-1)) ||
        !total || total>sectors)return -1;
    uint64_t overhead=(uint64_t)reserved+(uint64_t)fats*fat;
    if(overhead>=total)return -1;
    uint32_t clusters=(uint32_t)((total-overhead)/spc);
    if(clusters<65525 || clusters>0x0ffffff5U || root<2 || root>=clusters+2 ||
       (uint64_t)fat*128U<clusters+2)return -1;
    vol=(struct fat_volume){device,1,total,reserved,fat,(uint32_t)overhead,root,clusters,spc};
    for(unsigned i=0;i<HANDLES;i++)handles[i].used=0;
    return 0;
}
static void info_from_entry(const struct fat_entry *entry,struct ramfs_info *out) {
    out->is_dir=entry->is_dir;out->size=entry->is_dir?0:entry->size;
    out->mode=entry->is_dir?0555U:0444U;out->uid=0;out->gid=0;out->ino=entry->ino;
}
int fat32_vfs_stat(const char *path,struct ramfs_info *out) {
    struct fat_entry e;if(!out || lookup(path,&e)<0)return -1;
    info_from_entry(&e,out);return 0;
}
int fat32_vfs_readdir(const char *path,unsigned index,struct ramfs_dirent *out) {
    struct fat_entry dir,e;
    if(!out || lookup(path,&dir)<0 || !dir.is_dir || !valid(dir.cluster))return -1;
    int rc=dir_entry(dir.cluster,index,&e);
    if(rc==1){unsigned i=0;while(e.name[i]){out->name[i]=e.name[i];i++;}out->name[i]=0;out->is_dir=e.is_dir;}
    return rc;
}
int fat32_vfs_open(const char *path,unsigned flags,int create) {
    if(create || flags!=1U)return -1;
    struct fat_entry e;
    if(lookup(path,&e)<0 || e.is_dir)return -1;
    if(e.size && !valid(e.cluster))return -1;
    for(unsigned i=0;i<HANDLES;i++)if(!handles[i].used) {
        handles[i]=(struct fat_handle){1,e.cluster,e.size,0,e.ino};return (int)i;
    }
    return -1;
}
int fat32_vfs_close(int fd) {
    if(fd<0 || fd>=(int)HANDLES || !handles[fd].used)return -1;
    handles[fd].used=0;return 0;
}
int fat32_vfs_seek(int fd,size_t off) {
    if(fd<0 || fd>=(int)HANDLES || !handles[fd].used)return -1;
    handles[fd].offset=off;return 0;
}
int fat32_vfs_read(int fd,void *dst,size_t count) {
    if(fd<0 || fd>=(int)HANDLES || !handles[fd].used || (!dst && count))return -1;
    struct fat_handle *h=&handles[fd];
    if(h->offset>=h->size)return 0;
    size_t remain=h->size-h->offset;
    if(remain>count)remain=count;
    if(remain>2147483647U)remain=2147483647U;
    if(!remain)return 0;
    uint64_t cluster_bytes=(uint64_t)vol.spc*SECTOR;
    uint64_t skip=h->offset/cluster_bytes;
    if(skip>vol.clusters)return -1;
    uint32_t c=h->first;
    for(uint64_t i=0;i<skip;i++){c=next_cluster(c);if(!c)return -1;}
    size_t copied=0;
    while(copied<remain && valid(c)) {
        size_t within=(h->offset+copied)%cluster_bytes;
        uint64_t lba=(uint64_t)vol.data_start+(uint64_t)(c-2)*vol.spc+within/SECTOR;
        if(lba>=vol.total)return -1;
        uint8_t sector[SECTOR];if(!read_sector((uint32_t)lba,sector))return -1;
        size_t pos=within%SECTOR,n=SECTOR-pos;
        if(n>remain-copied)n=remain-copied;
        for(size_t i=0;i<n;i++)((uint8_t *)dst)[copied+i]=sector[pos+i];
        copied+=n;
        if(copied<remain && (h->offset+copied)%cluster_bytes==0)c=next_cluster(c);
    }
    if(copied!=remain)return -1;
    h->offset+=copied;return (int)copied;
}
int fat32_vfs_fstat(int fd,struct ramfs_info *out) {
    if(!out || fd<0 || fd>=(int)HANDLES || !handles[fd].used)return -1;
    struct fat_handle *h=&handles[fd];
    *out=(struct ramfs_info){0,h->size,0444U,0,0,h->ino};return 0;
}

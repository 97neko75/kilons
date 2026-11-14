#include <stdint.h>
#include <stddef.h>
#include <kilons/io.h>
#include <kilons/iso9660.h>
#include <kilons/console.h>
/* QEMU's -cdrom normally exposes an IDE ATAPI device on secondary master.
 * Probe both channels and both devices, using bounded polling, no DMA/IRQ. */
#define BLOCK 2048U
#define HANDLES 8
static uint8_t block[BLOCK];
static uint16_t port;
static uint8_t drive;
static uint32_t root_lba,root_size,volume_blocks;
static int ready;
struct iso_handle { int used; uint32_t lba,size; size_t offset; };
static struct iso_handle handles[HANDLES];
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int eq(const char *a,const char *b) { while(*a&&*b&&*a==*b){a++;b++;}return *a==*b; }
/* Alternate-status reads provide the required 400 ns settling delay without
 * acknowledging a pending IDE interrupt. Never reject stale ERR on select. */
static uint16_t ctrl_port(void) { return port==0x170 ? 0x376 : 0x3f6; }
static void delay(void) { for(unsigned i=0;i<4;i++) (void)inb(ctrl_port()); }
static int wait_not_busy(void) {
    for(unsigned i=0;i<2000000;i++) {
        uint8_t s=inb(ctrl_port());
        if(s==0xff || s==0x00) return 0;
        if(!(s&0x80)) return 1;
    }
    return 0;
}
static int wait_drq(void) {
    for(unsigned i=0;i<2000000;i++) {
        uint8_t s=inb(ctrl_port());
        if(s==0xff || s==0x00) return 0;
        if(s&0x80) continue; /* BSY makes the other status bits invalid. */
        if(s&0x21) return 0; /* ERR or DF, only after BSY clears. */
        if(s&0x08) return 1;
    }
    return 0;
}
static int wait_complete(void) {
    for(unsigned i=0;i<2000000;i++) {
        uint8_t s=inb(ctrl_port());
        if(s==0xff || s==0x00) return 0;
        if(s&0x80) continue;
        if(s&0x21) return 0;
        if(!(s&0x08)) return 1;
    }
    return 0;
}
/* Failure stages are retained so the boot log identifies the precise
 * command phase rather than reporting every error as "READ(10) failed". */
static const char *read_error;
static int packet_read_once(uint32_t lba,uint8_t *dest) {
    read_error="select / busy timeout";
    outb(port+6,(uint8_t)(0xa0|(drive<<4)));delay();
    if(!wait_not_busy()) return 0;
    /* ATAPI byte-count registers specify the maximum bytes per PIO phase.
     * 0x0800 means one 2048-byte ISO logical block. */
    outb(port+1,0);
    outb(port+4,(uint8_t)(BLOCK&0xff));
    outb(port+5,(uint8_t)(BLOCK>>8));
    outb(port+7,0xa0); /* PACKET */
    read_error="PACKET command phase timeout/error";
    if(!wait_drq()) return 0;
    read_error="PACKET command phase reason mismatch";
    if((inb(port+2)&3)!=1) return 0;
    const uint8_t pkt[12]={0x28,0,
        (uint8_t)(lba>>24),(uint8_t)(lba>>16),
        (uint8_t)(lba>>8),(uint8_t)lba,
        0,0,0,1,0,0};
    for(unsigned i=0;i<6;i++) outw(port,le16(pkt+2*i));

    /* A drive may split a 2048-byte READ(10) into multiple PIO phases.
     * Consume the byte count reported by the drive on each DRQ. */
    unsigned received=0;
    for(unsigned phase=0;phase<16;phase++) {
        read_error="READ(10) data phase timeout/error";
        if(!wait_drq()) return 0;
        read_error="READ(10) data phase reason mismatch";
        if((inb(port+2)&3)!=2) return 0;
        unsigned bytes=(unsigned)inb(port+4)|((unsigned)inb(port+5)<<8);
        read_error="READ(10) invalid data phase length";
        if(bytes==0 || (bytes&1) || bytes>BLOCK-received) return 0;
        for(unsigned i=0;i<bytes/2;i++) {
            uint16_t w=inw(port);
            dest[received+2*i]=(uint8_t)w;
            dest[received+2*i+1]=(uint8_t)(w>>8);
        }
        received+=bytes;
        if(received==BLOCK) {
            read_error="READ(10) command completion error";
            if(!wait_complete()) return 0;
            read_error=0;
            return 1;
        }
    }
    read_error="READ(10) too many data phases";
    return 0;
}
static int packet_read(uint32_t lba,uint8_t *dest) {
    if(!ready || lba>=volume_blocks) return 0;
    /* Retry transient unit attention / not-ready errors after a command.
     * Never retry a partially returned buffer as a successful read. */
    for(unsigned attempt=0;attempt<3;attempt++) {
        if(packet_read_once(lba,dest)) return 1;
        /* Reading the regular status register acknowledges the IDE IRQ.
         * Polling otherwise uses alternate status to avoid that side effect. */
        (void)inb(port+7);
        delay();
    }
    return 0;
}
static void probe_message(const char *msg) {
    console_write("ATAPI: ");console_write(port==0x170?"secondary":"primary");
    console_write(drive?" slave: ":" master: ");console_write(msg);console_write("\n");
}
/* Directory records are sector-aligned; no record may cross a block boundary. */
static int entry(uint32_t dir_lba,uint32_t dir_size,unsigned index,char *name,int *is_dir,uint32_t *lba,uint32_t *size) {
    if(!dir_size || dir_size>16U*1024U*1024U) return -1;
    unsigned count=0;uint32_t loaded=0xffffffffu;
    for(uint32_t off=0;off<dir_size;) {
        uint32_t sector=off/BLOCK, in=off%BLOCK;
        if(sector!=loaded) { if(!packet_read(dir_lba+sector,block)) return -1;loaded=sector; }
        uint8_t len=block[in];
        if(!len) {off=(sector+1)*BLOCK;continue;}
        if(len<34 || len>BLOCK-in || len>dir_size-off) return -1;
        const uint8_t *r=block+in;
        unsigned nl=r[32];
        if(nl && nl<=(unsigned)len-33U && r[0]==len && !(r[25]&0x80)) {
            if(!(nl==1 && (r[33]==0 || r[33]==1))) {
                if(count++==index) {
                    unsigned n=0;
                    for(unsigned j=0;j<nl && n<127;j++) {
                        char c=(char)r[33+j];
                        if(c==';') break;
                        if(c>='A'&&c<='Z') c=(char)(c+32);
                        name[n++]=c;
                    }
                    if(n && name[n-1]=='.') n--;
                    name[n]=0;
                    *is_dir=(r[25]&2)!=0;*lba=le32(r+2);*size=le32(r+10);
                    if(*lba>=volume_blocks || ((uint64_t)*lba+(*size+BLOCK-1)/BLOCK)>volume_blocks) return -1;
                    return 1;
                }
            }
        }
        off+=len;
    }
    return 0;
}
static int lookup(const char *path,uint32_t *lba,uint32_t *size,int *is_dir) {
    if(!ready) return -1;
    *lba=root_lba;*size=root_size;*is_dir=1;
    while(*path=='/')path++;
    if(!*path)return 0;
    for(;;) {
        if(!*is_dir)return -1;
        char component[128];unsigned n=0;
        while(*path && *path!='/') {if(n>=127)return -1;component[n++]=*path++;}
        component[n]=0;
        while(*path=='/')path++;
        unsigned idx=0;int found=0;
        for(;;) {
            char name[128];uint32_t l,s;int dir;
            int rc=entry(*lba,*size,idx++,name,&dir,&l,&s);
            if(rc<=0)break;
            if(eq(component,name)) {*lba=l;*size=s;*is_dir=dir;found=1;break;}
        }
        if(!found)return -1;
        if(!*path)return 0;
    }
}
int iso9660_init(void) {
    ready=0;
    for(unsigned i=0;i<HANDLES;i++)handles[i].used=0;
    const uint16_t ports[2]={0x170,0x1f0};
    for(unsigned ch=0;ch<2;ch++)for(unsigned d=0;d<2;d++) {
        port=ports[ch];drive=(uint8_t)d;
        outb(port+6,(uint8_t)(0xa0|(d<<4)));delay();
        if(!wait_not_busy())continue;
        /* An ATAPI device exposes 14/EB in LBA mid/high.  This is a
         * hint, not a hard requirement: some controllers vary. */
        uint8_t mid=inb(port+4), high=inb(port+5);
        if(mid==0xff && high==0xff)continue;
        outb(port+2,0);outb(port+3,0);outb(port+4,0);outb(port+5,0);
        outb(port+7,0xa1); /* IDENTIFY PACKET DEVICE */
        if(!wait_drq()) {
            if((mid==0x14 && high==0xeb) || (mid==0x69 && high==0x96))
                probe_message("ATAPI signature found; IDENTIFY PACKET failed");
            continue;
        }
        for(unsigned j=0;j<256;j++) (void)inw(port);
        if(!wait_complete()) {probe_message("IDENTIFY completion failed");continue;}
        /* Bound reads before the volume descriptor has supplied a size. */
        ready=1;volume_blocks=0x100000;
        if(!packet_read(16,block)) {
            probe_message("READ(10) sector 16 failed");
            if(read_error) probe_message(read_error);
            ready=0;continue;
        }
        if(block[0]!=1 || block[1]!='C' || block[2]!='D' || block[3]!='0' || block[4]!='0' || block[5]!='1' || block[6]!=1 || le16(block+128)!=BLOCK) {
            probe_message("ISO 9660 primary volume descriptor invalid");ready=0;continue;
        }
        volume_blocks=le32(block+80);
        const uint8_t *r=block+156;
        root_lba=le32(r+2);root_size=le32(r+10);
        if(volume_blocks<18 || r[0]<34 || root_lba>=volume_blocks || !root_size || ((uint64_t)root_lba+(root_size+BLOCK-1)/BLOCK)>volume_blocks) {
            probe_message("ISO 9660 root directory invalid");ready=0;continue;
        }
        probe_message("ISO 9660 volume ready");
        return 1;
    }
    return 0;
}
int iso9660_available(void) {return ready;}
int iso9660_open(const char *path,unsigned flags,int create) {
    if(flags!=VFS_READ || create)return -1;
    uint32_t lba,size;int dir;
    if(lookup(path,&lba,&size,&dir)<0 || dir)return -1;
    for(int i=0;i<HANDLES;i++)if(!handles[i].used) {
        handles[i]=(struct iso_handle){1,lba,size,0};return i;
    }
    return -1;
}
int iso9660_close(int fd) {if(fd<0||fd>=HANDLES||!handles[fd].used)return -1;handles[fd].used=0;return 0;}
int iso9660_seek(int fd,size_t off) {if(fd<0||fd>=HANDLES||!handles[fd].used)return -1;handles[fd].offset=off;return 0;}
int iso9660_read(int fd,void *dst,size_t n) {
    if(fd<0||fd>=HANDLES||!handles[fd].used||(!dst&&n))return -1;
    struct iso_handle *h=&handles[fd];
    if(h->offset>=h->size)return 0;
    if(n>h->size-h->offset)n=h->size-h->offset;
    if(n>0x7fffffffU)n=0x7fffffffU;
    size_t done=0;uint8_t *out=dst;
    while(done<n) {
        uint32_t block_no=h->offset/BLOCK,off=h->offset%BLOCK;
        if(!packet_read(h->lba+block_no,block))return done?(int)done:-1;
        size_t part=BLOCK-off;if(part>n-done)part=n-done;
        for(size_t j=0;j<part;j++)out[done+j]=block[off+j];
        done+=part;h->offset+=(uint32_t)part;
    }
    return (int)done;
}
int iso9660_stat(const char *path,struct vfs_info *info) {
    uint32_t lba,size;int dir;
    if(!info||lookup(path,&lba,&size,&dir)<0)return -1;
    info->is_dir=dir;info->size=size;return 0;
}
int iso9660_readdir(const char *path,unsigned index,struct vfs_dirent *out) {
    uint32_t lba,size,elba,esize;int dir,edir;
    if(!out||lookup(path,&lba,&size,&dir)<0||!dir)return -1;
    int rc=entry(lba,size,index,out->name,&edir,&elba,&esize);
    if(rc==1)out->is_dir=edir;
    return rc;
}

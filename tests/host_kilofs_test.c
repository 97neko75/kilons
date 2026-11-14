#include <kilons/kilofs.h>
#include <kilons/kerrno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BLOCKS 256U
static uint8_t media[BLOCKS][KFS_BLOCK];
static struct kilofs fs;
static int read_media(void *ctx,uint32_t b,void *dst){
    (void)ctx;if(b>=BLOCKS)return -K_EIO;
    memcpy(dst,media[b],KFS_BLOCK);return 0;
}
static int write_media(void *ctx,uint32_t b,const void *src){
    (void)ctx;if(b>=BLOCKS)return -K_EIO;
    memcpy(media[b],src,KFS_BLOCK);return 0;
}
static int flush_media(void *ctx){(void)ctx;return 0;}
static struct kfs_device dev={NULL,BLOCKS,read_media,write_media,flush_media};
int main(void){
    assert(kfs_format(&fs,dev)==0);
    assert(kfs_mkdir(&fs,"docs",0755)==0);
    assert(kfs_mkdir(&fs,"docs/sub",0755)==0);
    assert(kfs_open(&fs,"docs",KFS_READ,0,0)==-K_EISDIR);
    int fd=kfs_open(&fs,"docs/sub/hello",KFS_READ|KFS_WRITE,1,0644);
    assert(fd>=0);
    assert(kfs_write(&fs,fd,"abcdef",6)==6);
    assert(kfs_seek(&fs,fd,2)==0);
    char out[32]={0};assert(kfs_read(&fs,fd,out,4)==4);
    assert(memcmp(out,"cdef",4)==0);
    assert(kfs_close(&fs,fd)==0);
    assert(kfs_truncate(&fs,"docs/sub/hello",9)==0);
    fd=kfs_open(&fs,"docs/sub/hello",KFS_READ|KFS_WRITE,0,0);assert(fd>=0);
    memset(out,0xcc,sizeof(out));
    assert(kfs_read(&fs,fd,out,9)==9);
    assert(memcmp(out,"abcdef",6)==0);
    for(int i=6;i<9;i++)assert(out[i]==0);
    assert(kfs_ftruncate(&fs,fd,3)==0);
    assert(kfs_close(&fs,fd)==0);
    assert(kfs_rename(&fs,"docs/sub/hello","docs/sub/renamed")==0);
    struct kfs_info info;assert(kfs_stat(&fs,"docs/sub/hello",&info)==-K_ENOENT);
    assert(kfs_stat(&fs,"docs/sub/renamed",&info)==0);
    assert(info.size==3&&info.mode==0644);
    assert(kfs_chmod(&fs,"docs/sub/renamed",0600)==0);
    assert(kfs_stat(&fs,"docs/sub/renamed",&info)==0&&info.mode==0600);
    struct kfs_dirent e;assert(kfs_readdir(&fs,"docs/sub",0,&e)==1);
    assert(strcmp(e.name,"renamed")==0);
    assert(kfs_readdir(&fs,"docs/sub",1,&e)==0);
    assert(kfs_rmdir(&fs,"docs/sub")==-K_ENOTEMPTY);
    assert(kfs_link(&fs,"docs/sub/renamed","alias")==0);
    unsigned audit=99;
    assert(kfs_fsck(&fs,&audit)==0 && audit==0);
    assert(kfs_sync(&fs)==0 && kfs_mount(&fs,dev,0)==0);
    assert(kfs_stat(&fs,"alias",&info)==0 && info.links==2);
    int alias=kfs_open(&fs,"alias",KFS_READ,0,0);
    assert(alias>=0 && kfs_unlink(&fs,"alias")==0);
    int other=kfs_open(&fs,"replacement",KFS_WRITE,1,0600);
    assert(other>=0 && kfs_close(&fs,other)==0);
    assert(kfs_rename(&fs,"replacement","docs/sub/renamed")==0);
    assert(kfs_fstat(&fs,alias,&info)==0 && info.links==0 && info.size==3);
    assert(kfs_close(&fs,alias)==0);
    assert(kfs_unlink(&fs,"docs/sub/renamed")==0);
    assert(kfs_rmdir(&fs,"docs/sub")==0);
    assert(kfs_rmdir(&fs,"docs")==0);
    assert(kfs_sync(&fs)==0);
    unsigned errors=99;assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    assert(kfs_symlink(&fs,"../target","link")==0 && kfs_sync(&fs)==0);
    /* Remount must see persisted metadata, not old cache contents. */
    assert(kfs_mount(&fs,dev,0)==0);
    assert(kfs_stat(&fs,"docs",&info)==-K_ENOENT);
    assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    char target[16]={0};
    assert(kfs_stat(&fs,"link",&info)==0 && info.kind==KFS_KIND_SYMLINK);
    assert(kfs_readlink(&fs,"link",target,sizeof target)==9 && !strcmp(target,"../target"));
    assert(kfs_unlink(&fs,"link")==0);
    /* Fill remaining inode namespace, checking ENOSPC and no corruption. */
    char name[32];unsigned created=0;
    for(int i=0;i<200;i++){
        snprintf(name,sizeof(name),"f%03d",i);
        int h=kfs_open(&fs,name,KFS_WRITE,1,0644);
        if(h<0){assert(h==-K_ENOSPC);break;}
        created++;assert(kfs_close(&fs,h)==0);
    }
    assert(created>0);
    assert(kfs_sync(&fs)==0);
    assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    /* Lose one root directory entry while keeping both allocation maps intact. */
    unsigned root_data=media[3][128+32]; /* small fixture, first direct block <256 */
    assert(root_data>=7);
    uint8_t saved[4];memcpy(saved,media[root_data],sizeof saved);
    memset(media[root_data],0,sizeof saved);
    assert(kfs_mount(&fs,dev,1)==0);
    assert(kfs_fsck(&fs,&errors)==0 && errors>0);
    memcpy(media[root_data],saved,sizeof saved);
    /* A data pointer into the block bitmap must be rejected on open. */
    unsigned ino=saved[0];
    assert(ino>1 && ino<128);
    uint8_t *direct=media[3+ino/32]+(ino%32)*128+32;
    direct[0]=1;
    assert(kfs_mount(&fs,dev,1)==0);
    assert(kfs_open(&fs,"f000",KFS_READ,0,0)==-K_EIO);
    assert(kfs_fsck(&fs,&errors)==0 && errors>0);
    direct[0]=0;
    assert(kfs_mount(&fs,dev,1)==0);
    assert(kfs_fsck(&fs,&errors)==0 && errors==0);
    /* Corruption injection: mark live root inode unallocated. */
    media[2][0]&=(uint8_t)~2U;
    assert(kfs_mount(&fs,dev,1)==0);
    assert(kfs_fsck(&fs,&errors)==0&&errors>0);
    puts("host_kilofs_test: PASS");return 0;
}

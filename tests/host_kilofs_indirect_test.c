#include <kilons/kilofs.h>
#include <kilons/kerrno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Sparse writes exercise both levels without requiring a multi-megabyte
 * contiguous file in the test image. */
#define BLOCKS 2200U
static uint8_t medium[BLOCKS][KFS_BLOCK];
static int read_media(void *ctx,uint32_t block,void *dst){
    (void)ctx;if(block>=BLOCKS)return -K_EIO;
    memcpy(dst,medium[block],KFS_BLOCK);return 0;
}
static int write_media(void *ctx,uint32_t block,const void *src){
    (void)ctx;if(block>=BLOCKS)return -K_EIO;
    memcpy(medium[block],src,KFS_BLOCK);return 0;
}
static int flush_media(void *ctx){(void)ctx;return 0;}
static struct kfs_device dev={NULL,BLOCKS,read_media,write_media,flush_media};

int main(void){
    struct kilofs fs={0};assert(kfs_format(&fs,dev)==0);
    int fd=kfs_open(&fs,"large",KFS_READ|KFS_WRITE,1,0644);assert(fd>=0);
    uint8_t block[KFS_BLOCK];
    for(unsigned i=0;i<KFS_BLOCK;i++)block[i]=(uint8_t)(i*13U+7U);
    for(unsigned i=0;i<KFS_DIRECT_BLOCKS+1U;i++)
        assert(kfs_write(&fs,fd,block,sizeof block)==(int)sizeof block);
    assert(kfs_seek(&fs,fd,(uint64_t)(KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK)*KFS_BLOCK+123U)==0);
    assert(kfs_write(&fs,fd,"double-a",8)==8);
    assert(kfs_seek(&fs,fd,(uint64_t)(KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK*2U)*KFS_BLOCK+321U)==0);
    assert(kfs_write(&fs,fd,"double-b",8)==8);
    struct kfs_info info;assert(kfs_fstat(&fs,fd,&info)==0);
    assert(info.size==(uint64_t)(KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK*2U)*KFS_BLOCK+329U);
    assert(info.blocks>=(KFS_DIRECT_BLOCKS+1U+2U+2U)*KFS_BLOCK/512U);
    assert(kfs_seek(&fs,fd,0)==0);
    uint8_t check[KFS_BLOCK];assert(kfs_read(&fs,fd,check,sizeof check)==(int)sizeof check);
    assert(!memcmp(check,block,sizeof check));
    assert(kfs_seek(&fs,fd,(uint64_t)(KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK)*KFS_BLOCK+123U)==0);
    char text[9]={0};assert(kfs_read(&fs,fd,text,8)==8&&!memcmp(text,"double-a",8));
    assert(kfs_seek(&fs,fd,(uint64_t)(KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK*2U)*KFS_BLOCK+321U)==0);
    memset(text,0,sizeof text);assert(kfs_read(&fs,fd,text,8)==8&&!memcmp(text,"double-b",8));
    unsigned errors=99;assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    assert(kfs_sync(&fs)==0&&kfs_close(&fs,fd)==0);
    assert(kfs_mount(&fs,dev,0)==0);
    assert(kfs_stat(&fs,"large",&info)==0&&info.size==(uint64_t)(KFS_DIRECT_BLOCKS+KFS_POINTERS_PER_BLOCK*2U)*KFS_BLOCK+329U);
    assert(kfs_mkdir(&fs,"many",0755)==0);
    char name[32];
    for(unsigned i=0;i<600;i++){
        snprintf(name,sizeof name,"many/entry-%03u",i);
        assert(kfs_link(&fs,"large",name)==0);
    }
    struct kfs_dirent entry;
    assert(kfs_readdir(&fs,"many",599,&entry)==1&&!strcmp(entry.name,"entry-599"));
    assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    assert(kfs_truncate(&fs,"large",(uint64_t)(KFS_DIRECT_BLOCKS+1U)*KFS_BLOCK)==0);
    assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    assert(kfs_truncate(&fs,"large",0)==0);
    assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    assert(kfs_sync(&fs)==0);
    /* A stale indirect root is malformed even when the file size is zero;
     * mount must not make ordinary stat/open paths trust it. */
    medium[3][256U+120U]=(uint8_t)KFS_DATA_START;
    medium[3][256U+121U]=medium[3][256U+122U]=medium[3][256U+123U]=0;
    assert(kfs_mount(&fs,dev,0)==0);
    assert(kfs_stat(&fs,"large",&info)==-K_EIO);
    medium[3][256U+120U]=medium[3][256U+121U]=
        medium[3][256U+122U]=medium[3][256U+123U]=0;
    assert(kfs_mount(&fs,dev,0)==0);
    assert(kfs_stat(&fs,"large",&info)==0&&info.size==0);
    puts("host_kilofs_indirect_test: PASS (direct/single/double indirect, sparse read, truncate, remount)");
    return 0;
}

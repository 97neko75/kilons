/* Simulates device write failure, cache loss on reset and full disk. This is
 * NOT a journal recovery test. Run only in memory, never on a real disk. */
#include <kilons/kilofs.h>
#include <kilons/kerrno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BLOCKS 20U
static uint8_t medium[BLOCKS][KFS_BLOCK];
static struct kilofs fs;
static int fail_writes;
static int read_cb(void *ctx,uint32_t block,void *dst){
    (void)ctx;if(block>=BLOCKS)return -K_EIO;
    memcpy(dst,medium[block],KFS_BLOCK);return 0;
}
static int write_cb(void *ctx,uint32_t block,const void *src){
    (void)ctx;if(block>=BLOCKS||fail_writes)return -K_EIO;
    memcpy(medium[block],src,KFS_BLOCK);return 0;
}
static int flush_cb(void *ctx){(void)ctx;return fail_writes?-K_EIO:0;}
static struct kfs_device dev={NULL,BLOCKS,read_cb,write_cb,flush_cb};
int main(void){
    assert(kfs_format(&fs,dev)==0);
    int fd=kfs_open(&fs,"atomic",KFS_READ|KFS_WRITE,1,0644);assert(fd>=0);
    assert(kfs_write(&fs,fd,"persist",7)==7);
    fail_writes=1;
    assert(kfs_sync(&fs)==-K_EIO);
    fail_writes=0;
    /* Dirty data must remain retryable after a writeback error. */
    assert(kfs_sync(&fs)==0);
    assert(kfs_close(&fs,fd)==0);
    assert(kfs_mount(&fs,dev,0)==0);
    fd=kfs_open(&fs,"atomic",KFS_READ,0,0);assert(fd>=0);
    char b[8]={0};assert(kfs_read(&fs,fd,b,7)==7 && !memcmp(b,"persist",7));
    assert(kfs_close(&fs,fd)==0);
    /* Simulate loss of uncommitted cache state on abrupt reset. */
    fd=kfs_open(&fs,"unsynced",KFS_WRITE,1,0644);assert(fd>=0);
    assert(kfs_write(&fs,fd,"uncertain",9)==9);
    /* Mount resets all in-memory cache without flushing outstanding writes. */
    assert(kfs_mount(&fs,dev,1)==0);
    struct kfs_info info;
    int status=kfs_stat(&fs,"unsynced",&info);
    /* It may or may not survive because cache eviction is allowed. */
    assert(status==0||status==-K_ENOENT);
    unsigned errors=0;assert(kfs_fsck(&fs,&errors)==0);
    /* A metadata bitmap mismatch is an error even if a file still opens. */
    medium[1][0]^=0x80U;
    assert(kfs_mount(&fs,dev,1)==0);
    assert(kfs_fsck(&fs,&errors)==0 && errors>0);
    /* Separate tiny newly formatted volume to test deterministic ENOSPC. */
    struct kfs_device tiny=dev;tiny.total_blocks=8;
    assert(kfs_format(&fs,tiny)==0);
    fd=kfs_open(&fs,"full",KFS_WRITE,1,0644);assert(fd>=0);
    char chunk[KFS_BLOCK]={0};
    assert(kfs_write(&fs,fd,chunk,sizeof(chunk))==-K_ENOSPC);
    assert(kfs_close(&fs,fd)==0);
    assert(kfs_sync(&fs)==0);
    assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    puts("host_kilofs_fault_test: PASS (no journal/recovery)");return 0;
}

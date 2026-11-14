/* Simulates write failures before and after the journal commit marker, cache
 * loss on reset and full disk. Run only in memory, never on a real disk. */
#include <kilons/kilofs.h>
#include <kilons/kerrno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BLOCKS 128U
static uint8_t medium[BLOCKS][KFS_BLOCK];
static struct kilofs fs;
static int fail_writes;
static int fail_after_prepared;
static int fail_after_commit;
static int prepared_seen;
static int committed_seen;
static int read_cb(void *ctx,uint32_t block,void *dst){
    (void)ctx;if(block>=BLOCKS)return -K_EIO;
    memcpy(dst,medium[block],KFS_BLOCK);return 0;
}
static int write_cb(void *ctx,uint32_t block,const void *src){
    (void)ctx;if(block>=BLOCKS||fail_writes)return -K_EIO;
    const uint8_t *bytes=src;
    unsigned state=(unsigned)bytes[8]|((unsigned)bytes[9]<<8)|
                   ((unsigned)bytes[10]<<16)|((unsigned)bytes[11]<<24);
    if(block==KFS_JOURNAL_START&&state==KFS_JOURNAL_PREPARED){
        memcpy(medium[block],src,KFS_BLOCK);prepared_seen=1;return 0;
    }
    if(block==KFS_JOURNAL_START&&state==KFS_JOURNAL_COMMITTED){
        if(fail_after_prepared&&prepared_seen)return -K_EIO;
        memcpy(medium[block],src,KFS_BLOCK);committed_seen=1;return 0;
    }
    if(fail_after_commit&&committed_seen&&
       !(block>=KFS_JOURNAL_START&&block<KFS_JOURNAL_START+KFS_JOURNAL_BLOCKS))return -K_EIO;
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
    char b[10]={0};assert(kfs_read(&fs,fd,b,7)==7 && !memcmp(b,"persist",7));
    assert(kfs_close(&fs,fd)==0);
    /* A prepared record without a commit marker is discarded on reset. */
    fd=kfs_open(&fs,"pending",KFS_WRITE,1,0644);assert(fd>=0);
    assert(kfs_write(&fs,fd,"uncertain",9)==9);
    fail_after_prepared=1;prepared_seen=0;committed_seen=0;
    assert(kfs_sync(&fs)==-K_EIO && prepared_seen);
    fail_after_prepared=0;
    struct kilofs recovered={0};
    assert(kfs_mount(&recovered,dev,0)==0);
    struct kfs_info info;
    assert(kfs_stat(&recovered,"pending",&info)==-K_ENOENT);
    unsigned errors=0;assert(kfs_fsck(&recovered,&errors)==0&&errors==0);
    /* A committed record is replayed when home-block writeback is interrupted. */
    fd=kfs_open(&recovered,"recovered",KFS_READ|KFS_WRITE,1,0644);assert(fd>=0);
    assert(kfs_write(&recovered,fd,"journaled",9)==9);
    fail_after_commit=1;prepared_seen=0;committed_seen=0;
    assert(kfs_sync(&recovered)==-K_EIO && committed_seen);
    fail_after_commit=0;
    struct kilofs after={0};
    assert(kfs_mount(&after,dev,0)==0);
    fd=kfs_open(&after,"recovered",KFS_READ,0,0);assert(fd>=0);
    memset(b,0,sizeof b);assert(kfs_read(&after,fd,b,9)==9&&!memcmp(b,"journaled",9));
    assert(kfs_close(&after,fd)==0);
    assert(kfs_fsck(&after,&errors)==0&&errors==0);
    /* A committed header must not replay a torn journal payload. */
    fd=kfs_open(&after,"checksum",KFS_READ|KFS_WRITE,1,0644);assert(fd>=0);
    assert(kfs_write(&after,fd,"verified",8)==8);
    fail_after_commit=1;prepared_seen=0;committed_seen=0;
    assert(kfs_sync(&after)==-K_EIO&&committed_seen);
    fail_after_commit=0;
    medium[KFS_JOURNAL_START+1][0]^=0x5aU;
    struct kilofs damaged={0};
    assert(kfs_mount(&damaged,dev,0)==-K_EIO);
    medium[KFS_JOURNAL_START+1][0]^=0x5aU;
    assert(kfs_mount(&after,dev,0)==0);
    fd=kfs_open(&after,"checksum",KFS_READ,0,0);assert(fd>=0);
    memset(b,0,sizeof b);assert(kfs_read(&after,fd,b,8)==8&&!memcmp(b,"verified",8));
    assert(kfs_close(&after,fd)==0);
    /* A metadata bitmap mismatch is an error even if a file still opens. */
    medium[1][0]^=0x80U;
    assert(kfs_mount(&after,dev,1)==0);
    assert(kfs_fsck(&after,&errors)==0 && errors>0);
    assert(kfs_fsck_repair(&after,&errors)==-K_EROFS);
    struct kilofs repaired={0};
    assert(kfs_mount(&repaired,dev,0)==0);
    assert(kfs_fsck_repair(&repaired,&errors)==0&&errors==0);
    fd=kfs_open(&repaired,"recovered",KFS_READ,0,0);assert(fd>=0);
    assert(kfs_fsck_repair(&repaired,&errors)==-K_EBUSY);
    assert(kfs_close(&repaired,fd)==0);
    /* Extra allocated bits are ambiguous orphan data and must be refused. */
    unsigned extra=KFS_DATA_START+20;
    medium[1][extra/8]|=(uint8_t)(1U<<(extra%8));
    struct kilofs unsafe={0};
    assert(kfs_mount(&unsafe,dev,0)==0);
    assert(kfs_fsck_repair(&unsafe,&errors)==-K_EIO&&errors>0);
    medium[1][extra/8]&=(uint8_t)~(1U<<(extra%8));
    assert(kfs_mount(&repaired,dev,1)==0);
    assert(kfs_fsck(&repaired,&errors)==0&&errors==0);
    /* Separate tiny newly formatted volume to test deterministic ENOSPC. */
    struct kfs_device tiny=dev;
    tiny.total_blocks=KFS_DATA_START+1;
    assert(kfs_format(&fs,tiny)==0);
    fd=kfs_open(&fs,"full",KFS_WRITE,1,0644);assert(fd>=0);
    char chunk[KFS_BLOCK]={0};
    assert(kfs_write(&fs,fd,chunk,sizeof(chunk))==-K_ENOSPC);
    assert(kfs_close(&fs,fd)==0);
    assert(kfs_sync(&fs)==0);
    assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    puts("host_kilofs_fault_test: PASS (journal/recovery/write-order/fsck-repair faults)");return 0;
}

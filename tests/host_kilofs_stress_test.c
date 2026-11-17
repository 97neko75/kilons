#include <kilons/kilofs.h>
#include <kilons/kerrno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Repeatedly allocate, remount, truncate and release the same sparse files.
 * The test is intentionally bounded so it remains a fast hosted regression,
 * while still exercising allocator reuse and long-lived metadata state. */
#define BLOCKS 256U
#define CYCLES 64U
#define FILES 4U
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

static void verify_sparse(struct kilofs *fs,const char *path,unsigned cycle,unsigned slot){
    uint64_t offset=(uint64_t)(KFS_DIRECT_BLOCKS+2U+slot)*KFS_BLOCK+cycle;
    int fd=kfs_open(fs,path,KFS_READ,0,0);assert(fd>=0);
    struct kfs_info info;assert(kfs_fstat(fs,fd,&info)==0);
    assert(info.size==offset+1);
    assert(kfs_seek(fs,fd,offset)==0);
    char value=0;assert(kfs_read(fs,fd,&value,1)==1);
    assert(value==(char)('A'+slot));
    assert(kfs_close(fs,fd)==0);
}

int main(void){
    struct kilofs fs={0};
    assert(kfs_format(&fs,dev)==0);
    assert(kfs_mkdir(&fs,"/stress",0755)==0);

    for(unsigned cycle=0;cycle<CYCLES;cycle++){
        char path[32];
        for(unsigned slot=0;slot<FILES;slot++){
            snprintf(path,sizeof path,"/stress/f%u",slot);
            int fd=kfs_open(&fs,path,KFS_READ|KFS_WRITE,1,0644);assert(fd>=0);
            uint64_t offset=(uint64_t)(KFS_DIRECT_BLOCKS+2U+slot)*KFS_BLOCK+cycle;
            char value=(char)('A'+slot);
            assert(kfs_seek(&fs,fd,offset)==0);
            assert(kfs_write(&fs,fd,&value,1)==1);
            assert(kfs_close(&fs,fd)==0);
        }

        /* Closing handles is not enough to make a remount safe: dirty cache
         * state must also be synchronized before the in-memory view resets. */
        assert(kfs_mount(&fs,dev,0)==-K_EBUSY);
        assert(kfs_sync(&fs)==0);
        assert(kfs_mount(&fs,dev,0)==0);
        for(unsigned slot=0;slot<FILES;slot++){
            snprintf(path,sizeof path,"/stress/f%u",slot);
            verify_sparse(&fs,path,cycle,slot);
        }
        unsigned errors=0;
        assert(kfs_fsck(&fs,&errors)==0&&errors==0);

        for(unsigned slot=0;slot<FILES;slot++){
            snprintf(path,sizeof path,"/stress/f%u",slot);
            assert(kfs_truncate(&fs,path,7)==0);
            int fd=kfs_open(&fs,path,KFS_READ|KFS_WRITE,0,0);assert(fd>=0);
            assert(kfs_ftruncate(&fs,fd,0)==0);
            assert(kfs_close(&fs,fd)==0);
            assert(kfs_unlink(&fs,path)==0);
        }
        assert(kfs_sync(&fs)==0);
        assert(kfs_fsck(&fs,&errors)==0&&errors==0);
        assert(kfs_mount(&fs,dev,0)==0);
        for(unsigned slot=0;slot<FILES;slot++){
            snprintf(path,sizeof path,"/stress/f%u",slot);
            struct kfs_info missing={0};
            assert(kfs_stat(&fs,path,&missing)==-K_ENOENT);
        }
    }

    unsigned errors=0;
    assert(kfs_fsck(&fs,&errors)==0&&errors==0);
    assert(kfs_sync(&fs)==0);
    puts("host_kilofs_stress_test: PASS (allocation reuse, sparse remount, truncate and fsck loops)");
    return 0;
}

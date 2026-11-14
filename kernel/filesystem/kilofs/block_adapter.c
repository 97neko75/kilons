#include <kilons/kilofs_block.h>
#include <kilons/block.h>
#include <kilons/kerrno.h>
static int kfs_bread(void *p,uint32_t block,void *out){
    struct kfs_block_context *c=p;uint8_t *dst=out;
    for(unsigned s=0;s<8;s++)if(!block_read(c->block_id,(uint64_t)block*8+s,dst+s*512))return -K_EIO;
    return 0;
}
static int kfs_bwrite(void *p,uint32_t block,const void *in){
    struct kfs_block_context *c=p;const uint8_t *src=in;
    for(unsigned s=0;s<8;s++)if(!block_write(c->block_id,(uint64_t)block*8+s,src+s*512))return -K_EIO;
    return 0;
}
static int kfs_bflush(void *p){
    struct kfs_block_context *c=p;
    return block_flush(c->block_id)?0:-K_EIO;
}
int kfs_bind_block(struct kfs_device *out,struct kfs_block_context *ctx,int id,int rw){
    uint64_t sectors;bool writable;
    if(!out||!ctx||!block_info(id,&sectors,&writable)||sectors%8||sectors/8>KFS_MAX_BLOCKS||
       sectors/8<8)return -K_EINVAL;
    if(rw&&!writable)return -K_EROFS;
    ctx->block_id=id;
    out->context=ctx;out->total_blocks=(uint32_t)(sectors/8);
    out->read=kfs_bread;out->write=rw?kfs_bwrite:0;out->flush=kfs_bflush;
    return 0;
}

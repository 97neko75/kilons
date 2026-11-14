#ifndef KILOFS_INTERNAL_H
#define KILOFS_INTERNAL_H
#include <kilons/kilofs.h>
#include <kilons/kerrno.h>
#define KFS_MAGIC 0x3153464bU /* 'KFS1' little endian */
#define KFS_JOURNAL_MAGIC 0x314c4e4aU /* 'JNL1' little endian */
#define KFS_JOURNAL_VERSION 2U
#define KFS_ROOT 1U
#define KFS_INODE_BYTES 128U
#define KFS_DIRENT_BYTES 128U
static inline uint32_t kfs_file_block_limit(const struct kilofs *fs){
    if(!fs||fs->dev.total_blocks<=fs->data_start)return 0;
    uint32_t available=fs->dev.total_blocks-fs->data_start;
    return available<KFS_MAX_FILE_BLOCKS?available:KFS_MAX_FILE_BLOCKS;
}
static inline uint64_t kfs_file_size_limit(const struct kilofs *fs){
    return (uint64_t)kfs_file_block_limit(fs)*KFS_BLOCK;
}
static inline uint16_t kfs_u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static inline uint32_t kfs_u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline uint64_t kfs_u64(const uint8_t *p){return (uint64_t)kfs_u32(p)|((uint64_t)kfs_u32(p+4)<<32);}
static inline void kfs_p16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static inline void kfs_p32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static inline void kfs_p64(uint8_t *p,uint64_t n){kfs_p32(p,(uint32_t)n);kfs_p32(p+4,(uint32_t)(n>>32));}
static inline void kfs_zero(void *p,size_t n){uint8_t *q=p;for(size_t i=0;i<n;i++)q[i]=0;}
static inline void kfs_copy(void *a,const void *b,size_t n){uint8_t *p=a;const uint8_t *q=b;for(size_t i=0;i<n;i++)p[i]=q[i];}
static inline int kfs_equal(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static inline int kfs_name_bytes_valid(const uint8_t *p,unsigned len){
    if(!p||!len||len>KFS_NAME_MAX)return 0;
    if(len==1&&p[0]=='.')return 0;
    if(len==2&&p[0]=='.'&&p[1]=='.')return 0;
    for(unsigned i=0;i<len;i++)if(!p[i]||p[i]=='/')return 0;
    return 1;
}
int kfs_cache_read(struct kilofs *,uint32_t,uint8_t *);
int kfs_cache_write(struct kilofs *,uint32_t,const uint8_t *);
int kfs_cache_flush(struct kilofs *);
int kfs_journal_recover(struct kilofs *);
int kfs_inode_load(struct kilofs *,uint32_t,struct kfs_node *);
int kfs_inode_save(struct kilofs *,uint32_t,const struct kfs_node *);
int kfs_inode_allocate(struct kilofs *,uint32_t *);
int kfs_inode_free(struct kilofs *,uint32_t);
int kfs_block_allocate(struct kilofs *,uint32_t *);
int kfs_block_free(struct kilofs *,uint32_t);
int kfs_dir_find(struct kilofs *,uint32_t,const char *,uint32_t *,uint8_t *);
int kfs_dir_add(struct kilofs *,uint32_t,const char *,uint32_t,uint8_t);
int kfs_dir_remove(struct kilofs *,uint32_t,const char *);
int kfs_dir_index(struct kilofs *,uint32_t,unsigned,struct kfs_dirent *);
int kfs_path(struct kilofs *,const char *,uint32_t *);
int kfs_parent(struct kilofs *,const char *,uint32_t *,char[KFS_NAME_MAX+1]);
int kfs_read_bytes(struct kilofs *,struct kfs_node *,uint64_t,void *,size_t);
int kfs_write_bytes(struct kilofs *,struct kfs_node *,uint64_t,const void *,size_t);
int kfs_data_block_get(struct kilofs *,struct kfs_node *,uint32_t,int,uint32_t *);
int kfs_data_block_release(struct kilofs *,struct kfs_node *,uint32_t);
int kfs_node_block_count(struct kilofs *,const struct kfs_node *,unsigned *);
int kfs_resize(struct kilofs *,uint32_t,struct kfs_node *,uint64_t);
int kfs_drop_inode(struct kilofs *,uint32_t,struct kfs_node *);
#endif

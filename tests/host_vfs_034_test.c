/* v0.3.4 VFS pathname/permissions/lseek/cache + MBR FAT32 integration.
 * Synthetic disk is a host memory callback; no physical drive is written. */
#include <kilons/vfs.h>
#include <kilons/ramfs.h>
#include <kilons/block.h>
#include <kilons/kerrno.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define START 2048U
#define TOTAL 70000U
#define DATA 632U
static void le16(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static void le32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static void dirent(uint8_t *p,const char name[11],unsigned attr,uint32_t cluster,uint32_t size){
    memcpy(p,name,11);p[11]=(uint8_t)attr;le16(p+20,cluster>>16);le16(p+26,cluster);le32(p+28,size);
}
static bool disk_read(void *ctx,uint64_t lba,uint8_t out[512]){
    (void)ctx;memset(out,0,512);
    if(lba==0){out[510]=0x55;out[511]=0xaa;out[446+4]=0x0c;le32(out+446+8,START);le32(out+446+12,TOTAL);return true;}
    if(lba==START){out[510]=0x55;out[511]=0xaa;le16(out+11,512);out[13]=1;le16(out+14,32);out[16]=1;le32(out+32,TOTAL);le32(out+36,600);le32(out+44,2);return true;}
    if(lba==START+32){
        le32(out,0x0ffffff8);le32(out+4,0x0fffffff);
        for(unsigned i=2;i<=5;i++)le32(out+4*i,0x0fffffff);
        return true;
    }
    if(lba==START+DATA){
        dirent(out,"HELLO   TXT",0x20,3,5);
        dirent(out+32,"SUBDIR     ",0x10,4,0);
        return true;
    }
    if(lba==START+DATA+1){memcpy(out,"hello",5);return true;}
    if(lba==START+DATA+2){dirent(out,"CHILD   TXT",0x20,5,3);return true;}
    if(lba==START+DATA+3){memcpy(out,"xyz",3);return true;}
    return true;
}
int main(void){
    block_init();ramfs_init();vfs_init();
    assert(vfs_mkdir("/tmp/tree")==0);
    assert(vfs_stat("/tmp/not-here/../tree",&(struct vfs_info){0})==-1 && vfs_errno()==K_ENOENT);
    assert(vfs_stat("/tmp/tree",&(struct vfs_info){0})==0);
    int file=vfs_open("/tmp/tree/file",VFS_READ|VFS_WRITE,1);
    assert(file>=3);
    assert(vfs_stat("/tmp/tree/file/../file",&(struct vfs_info){0})==-1 && vfs_errno()==K_ENOTDIR);
    assert(vfs_stat("/tmp/tree/file/",&(struct vfs_info){0})==-1 && vfs_errno()==K_ENOTDIR);
    assert(vfs_write(file,"abcdef",6)==6);
    assert(vfs_lseek(file,-2,2)==4);
    char data[8]={0};assert(vfs_read(file,data,2)==2 && !memcmp(data,"ef",2));
    assert(vfs_lseek(file,-4,1)==2);
    assert(vfs_retain(file)==0);
    assert(vfs_read(file,data,2)==2 && !memcmp(data,"cd",2));
    assert(vfs_lseek(file,0,2)==6);
    assert(vfs_lseek(file,-7,2)==-K_EINVAL);
    assert(vfs_lseek(file,5,3)==-K_EINVAL);
    assert(vfs_lseek(file,32770,0)==32770);
    assert(vfs_read(file,data,1)==0);
    assert(vfs_write(file,"A",1)==-1 && vfs_errno()==K_ENOSPC);
    assert(vfs_close(file)==0 && vfs_close(file)==0);
    assert(vfs_rename("/tmp/tree/file","/tmp/tree/new")==0);
    assert(vfs_stat("/tmp/tree/file",&(struct vfs_info){0})==-1 && vfs_errno()==K_ENOENT);
    assert(vfs_stat("/tmp/tree/new",&(struct vfs_info){0})==0);
    assert(vfs_unlink("/tmp/tree/new")==0);
    assert(vfs_rmdir("/tmp/tree/.")==-1 && vfs_errno()==K_EINVAL);
    assert(vfs_rename("/tmp/tree/.","/tmp/moved")==-1 && vfs_errno()==K_EINVAL);
    assert(vfs_mkdir("/tmp/empty")==0);
    assert(vfs_rename("/tmp/tree","/tmp/empty")==0);
    assert(vfs_rmdir("/tmp/empty")==0);
    int old=vfs_open("/tmp/old",VFS_READ|VFS_WRITE,1);
    int fresh=vfs_open("/tmp/fresh",VFS_READ|VFS_WRITE,1);
    assert(old>=3 && fresh>=3 && vfs_write(old,"old",3)==3);
    assert(vfs_write(fresh,"new",3)==3);
    assert(vfs_rename("/tmp/fresh","/tmp/old")==0);
    assert(vfs_seek(old,0)==0 && vfs_read(old,data,3)==3 && !memcmp(data,"old",3));
    assert(vfs_close(old)==0 && vfs_close(fresh)==0);
    old=vfs_open("/tmp/old",VFS_READ,0);
    assert(vfs_read(old,data,3)==3 && !memcmp(data,"new",3));
    assert(vfs_close(old)==0 && vfs_unlink("/tmp/old")==0);
    assert(vfs_mkdir("/tmp/trailing-dir/")==0);
    assert(vfs_stat("/tmp/trailing-dir/",&(struct vfs_info){0})==0);
    assert(vfs_open("/tmp/trailing-dir/newfile/",VFS_WRITE,1)==-1 && vfs_errno()==K_ENOTDIR);
    assert(vfs_rmdir("/tmp/trailing-dir/")==0);
    vfs_set_credentials(1000,1000);
    assert(vfs_mkdir_mode("/tmp/user",0700)==0);
    file=vfs_open_mode("/tmp/user/secret",VFS_READ|VFS_WRITE,1,0600);
    assert(file>=3);
    struct vfs_info info;assert(vfs_stat("/tmp/user/secret",&info)==0);
    assert(info.uid==1000 && info.mode==0600 && !info.is_dir);
    assert(vfs_write(file,"sec",3)==3);assert(vfs_close(file)==0);
    vfs_set_credentials(2000,2000);
    assert(vfs_stat("/tmp/user/secret",&info)==-1 && vfs_errno()==K_EACCES);
    assert(vfs_open("/tmp/user/secret",VFS_READ,0)==-1 && vfs_errno()==K_EACCES);
    assert(vfs_chmod("/tmp/user",0755)==-1 && vfs_errno()==K_EPERM);
    vfs_set_credentials(1000,1000);
    assert(vfs_chmod("/tmp/user",0755)==0);
    vfs_set_credentials(2000,2000);
    assert(vfs_stat("/tmp/user/secret",&info)==0);
    assert(vfs_open("/tmp/user/secret",VFS_READ,0)==-1 && vfs_errno()==K_EACCES);
    assert(vfs_mkdir("/tmp/user/other")==-1 && vfs_errno()==K_EACCES);
    vfs_set_credentials(1000,1000);
    assert(vfs_chmod("/tmp/user",0400)==0);
    struct vfs_dirent listing;
    assert(vfs_readdir("/tmp/user",0,&listing)==1 && !strcmp(listing.name,"secret"));
    assert(vfs_stat("/tmp/user/secret",&info)==-1 && vfs_errno()==K_EACCES);
    assert(vfs_chmod("/tmp/user",0755)==0);
    assert(vfs_chmod("/tmp/user/secret",0644)==0);
    vfs_set_credentials(2000,2000);
    file=vfs_open("/tmp/user/secret",VFS_READ,0);assert(file>=3);assert(vfs_close(file)==0);
    assert(vfs_open("/tmp/user/secret",VFS_WRITE,0)==-1 && vfs_errno()==K_EACCES);
    vfs_set_credentials(1000,1000);
    assert(vfs_unlink("/tmp/user/secret")==0);
    assert(vfs_rmdir("/tmp/user")==0);
    /* /tmp is world-writable but sticky: peers may not remove/rename
     * another user's entries, even when parent W+X grants traversal. */
    int owned=vfs_open("/tmp/sticky-owned",VFS_READ|VFS_WRITE,1);
    assert(owned>=3 && vfs_close(owned)==0);
    assert(vfs_mkdir("/tmp/sticky-directory")==0);
    vfs_set_credentials(2000,2000);
    assert(vfs_unlink("/tmp/sticky-owned")==-1 && vfs_errno()==K_EPERM);
    assert(vfs_rename("/tmp/sticky-owned","/tmp/sticky-renamed")==-1 && vfs_errno()==K_EPERM);
    assert(vfs_rmdir("/tmp/sticky-directory")==-1 && vfs_errno()==K_EPERM);
    vfs_set_credentials(1000,1000);
    assert(vfs_rmdir("/tmp/sticky-directory")==0);
    assert(vfs_unlink("/tmp/sticky-owned")==0);
    /* Synthetic MBR partition, all reads go through the block mapping. */
    struct block_device d={START+TOTAL+5,disk_read,0,0};
    int disk=block_register(&d);assert(disk>=1);
    assert(block_register_mbr_partition(disk,1)==-1);
    int part=block_register_mbr_partition(disk,0);assert(part>=2);
    uint64_t sectors=0;bool writable=true;
    assert(block_info(part,&sectors,&writable) && sectors==TOTAL && !writable);
    uint8_t sector[512];assert(block_read(part,0,sector) && sector[13]==1);
    assert(!block_write(part,0,sector));
    assert(vfs_mount_fat32("/tmp",part)==-1 && vfs_errno()==K_EACCES);
    assert(vfs_mkdir("/mnt/disk")==-1 && vfs_errno()==K_EACCES); /* unprivileged /mnt */
    vfs_set_credentials(0,0);assert(vfs_mkdir("/mnt/disk")==0);
    assert(vfs_mount_fat32("/mnt/disk",part)==0);
    assert(vfs_mount_fat32("/mnt/disk",part)==-1);
    assert(vfs_stat("/mnt/disk/SUBDIR/CHILD.TXT",&info)==0 && info.size==3);
    struct vfs_dirent entry;
    assert(vfs_readdir("/mnt/disk",0,&entry)==1 && !strcmp(entry.name,"HELLO.TXT"));
    assert(vfs_readdir("/mnt/disk",1,&entry)==1 && !strcmp(entry.name,"SUBDIR") && entry.is_dir);
    assert(vfs_readdir("/mnt/disk",2,&entry)==0);
    file=vfs_open("/mnt/disk/HELLO.TXT",VFS_READ,0);assert(file>=3);
    memset(data,0,sizeof(data));assert(vfs_read(file,data,5)==5 && !memcmp(data,"hello",5));
    assert(vfs_lseek(file,-2,2)==3);
    assert(vfs_read(file,data,2)==2 && !memcmp(data,"lo",2));
    assert(vfs_close(file)==0);
    file=vfs_open("/mnt/disk/subdir/child.txt",VFS_READ,0);assert(file>=3);
    assert(vfs_read(file,data,3)==3 && !memcmp(data,"xyz",3));assert(vfs_close(file)==0);
    assert(vfs_open("/mnt/disk/HELLO.TXT",VFS_WRITE,0)==-1 && vfs_errno()==K_EROFS);
    assert(vfs_open("/mnt/disk/MISSING",VFS_READ,1)==-1 && vfs_errno()==K_EROFS);
    assert(vfs_access("/mnt/disk/HELLO.TXT",2)==-1 && vfs_errno()==K_EROFS);
    assert(vfs_unlink("/mnt/disk/HELLO.TXT")==-1 && vfs_errno()==K_EROFS);
    assert(vfs_rmdir("/mnt/disk")==-1);
    puts("v0.3.4 pathname/inode/dentry/permissions/lseek/MBR/read-only FAT32 PASS");
    return 0;
}

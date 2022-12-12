#ifndef _NEWFS_H_
#define _NEWFS_H_

#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "fuse.h"
#include <stddef.h>
#include "ddriver.h"
#include "errno.h"
#include "types.h"

#define NEWFS_MAGIC           200110132       /* TODO: Define by yourself */
#define NEWFS_DEFAULT_PERM    0777   /* 全权限打开 */

/******************************************************************************
* SECTION: macro debug
*******************************************************************************/
#define NFS_DBG(fmt, ...) do { printf("NFS_DBG: " fmt, ##__VA_ARGS__); } while(0) 

/******************************************************************************
* SECTION: newfs_utils.c
*******************************************************************************/
char* 			   nfs_get_fname(const char* path);		// 获取文件名
int 			   fs_calc_lvl(const char * path);		// 计算路径的层级
int 			   nfs_driver_read(int offset, uint8_t *out_content, int size);		// 驱动读
int 			   nfs_driver_write(int offset, uint8_t *in_content, int size);		// 驱动写
int 			   nfs_alloc_dentry(struct nfs_inode * inode, struct nfs_dentry * dentry);	// 为一个inode分配dentry，采用头插法
int 			   nfs_drop_dentry(struct nfs_inode * inode, struct nfs_dentry * dentry);	// 将dentry从inode的dentrys中取出
struct nfs_inode*  nfs_alloc_inode(struct nfs_dentry * dentry);		// 分配一个inode，占用位图
int 			   nfs_sync_inode(struct nfs_inode * inode);		// 将内存inode及其下方结构全部刷回磁盘
int 			   nfs_drop_inode(struct nfs_inode * inode);		// 删除内存中的一个inode， 暂时不释放
struct nfs_inode*  nfs_read_inode(struct nfs_dentry * dentry, int ino);	// dentry指向ino，读取该inode
struct nfs_dentry* nfs_get_dentry(struct nfs_inode * inode, int dir);	// 获得指向该inode的dentry

struct nfs_dentry* nfs_lookup(const char * path, boolean * is_find, boolean* is_root);	// 查找路径对应文件，存在返回其dentry，不存在返回父目录
int 			   nfs_mount(struct custom_options options);	// 挂载nfs
int 			   nfs_umount();								// 卸载nfs
/******************************************************************************
* SECTION: newfs.c
*******************************************************************************/
void* 			   nfs_init(struct fuse_conn_info *);	// 挂载nfs
void  			   nfs_destroy(void *);					// 卸载nfs
int   			   nfs_mkdir(const char *, mode_t);		// 创建目录
int   			   nfs_getattr(const char *, struct stat *);	// 获取文件/目录属性
int   			   nfs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
						                struct fuse_file_info *);	// 获取目录项
int   			   nfs_mknod(const char *, mode_t, dev_t);	// 创建文件
int   			   nfs_write(const char *, const char *, size_t, off_t,
					                  struct fuse_file_info *);	// 写入文件
int   			   nfs_read(const char *, char *, size_t, off_t,
					                 struct fuse_file_info *);	// 读取文件
int   			   nfs_access(const char *, int);	// 访问文件
int   			   nfs_unlink(const char *);	// 删除文件
int   			   nfs_rmdir(const char *);		// 删除目录
int   			   nfs_rename(const char *, const char *);	// 重命名文件
int   			   nfs_utimens(const char *, const struct timespec tv[2]);	// 修改时间，为了不让touch报错
int   			   nfs_truncate(const char *, off_t);	// 改变文件大小
			
int   			   nfs_open(const char *, struct fuse_file_info *);		// 打开文件
int   			   nfs_opendir(const char *, struct fuse_file_info *);	//打开目录

#endif  /* _nfs_H_ */
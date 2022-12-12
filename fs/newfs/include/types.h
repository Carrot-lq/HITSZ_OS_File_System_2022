#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef uint16_t     flag16;

typedef enum nfs_file_type {
    NFS_REG_FILE,   // 文件
    NFS_DIR         // 目录
} NFS_FILE_TYPE;
/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NFS_MAGIC_NUM           200110132   // 自定义幻数
#define NFS_SUPER_OFS           0           // 超级块磁盘偏移，0
#define NFS_ROOT_INO            0           // 根目录inode编号，0

// 自行规定位图的大小
#define NFS_SUPER_BLK           1           // 超级块数
#define NFS_MAP_INODE_BLK       1           // inode位图所占块数
#define NFS_MAP_DATA_BLK        1           // data位图所占块数
#define NFS_INODE_BLK           512         // inode最大块数
#define NFS_DATA_BLK            2048        // data最大块数

#define NFS_ERROR_NONE          0
#define NFS_ERROR_ACCESS        EACCES
#define NFS_ERROR_SEEK          ESPIPE     
#define NFS_ERROR_ISDIR         EISDIR
#define NFS_ERROR_NOSPACE       ENOSPC
#define NFS_ERROR_EXISTS        EEXIST
#define NFS_ERROR_NOTFOUND      ENOENT
#define NFS_ERROR_UNSUPPORTED   ENXIO
#define NFS_ERROR_IO            EIO     /* Error Input/Output */
#define NFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NFS_MAX_FILE_NAME       128
//#define NFS_INODE_PER_FILE      1
#define NFS_DATA_PER_FILE       4       // 文件最大为4*1024KB
#define NFS_DEFAULT_PERM        0777    // 全权限打开

#define NFS_IOC_MAGIC           'S'
#define NFS_IOC_SEEK            _IO(NFS_IOC_MAGIC, 0)

#define NFS_FLAG_BUF_DIRTY      0x1
#define NFS_FLAG_BUF_OCCUPY     0x2
/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define NFS_IO_SZ()                     (nfs_super.sz_io)       // 读写IO单位大小
#define NFS_BLK_SZ()                    (nfs_super.sz_blk)      // 块大小
#define NFS_DISK_SZ()                   (nfs_super.sz_disk)     // 磁盘大小
#define NFS_DRIVER()                    (nfs_super.driver_fd)   // 驱动的文件描述符

#define NFS_ROUND_DOWN(value, round)    (value % round == 0 ? value : (value / round) * round)
#define NFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + 1) * round)

// #define NFS_BLKS_SZ(blks)               (blks * NFS_IO_SZ())
#define NFS_BLKS_SZ(blks)               (blks * NFS_BLK_SZ())   // 若干块的空间大小
#define NFS_ASSIGN_FNAME(pnfs_dentry, _fname)   memcpy(pnfs_dentry->fname, _fname, strlen(_fname))
// #define NFS_INO_OFS(ino)                (nfs_super.data_offset + ino * NFS_BLKS_SZ((
//                                         NFS_INODE_PER_FILE + NFS_DATA_PER_FILE)))
#define NFS_INO_OFS(ino)                (nfs_super.inode_offset + NFS_BLKS_SZ(ino)) // 第ino个inode块磁盘偏移
#define NFS_DATA_OFS(ino)               (nfs_super.data_offset + NFS_BLKS_SZ(ino))  // 第ino个数据块磁盘偏移

#define NFS_IS_DIR(pinode)              (pinode->dentry->ftype == NFS_DIR)
#define NFS_IS_REG(pinode)              (pinode->dentry->ftype == NFS_REG_FILE)
/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct nfs_dentry;
struct nfs_inode;
struct nfs_super;

struct custom_options {
	const char*        device;                      // 驱动的路径
};

struct nfs_super {
    int                driver_fd;       // 驱动的文件描述符
    int                sz_io;           // 读写IO单位大小 (512B)
    // 驱动读写IO单位为sz_io(512B)，ext2块大小为1024B
    // 需将涉及块大小（除驱动读写IO外）的NFS_IO_SZ()（即sz_io）修改为NFS_BLK_SZ()（即sz_blk）
    int                sz_blk;          // 块大小(1024B)    
    int                sz_disk;         // 磁盘大小(4MB)

    int                max_ino;         // inode最大数目
    int                max_data;        // data最大数目
    
    uint8_t*           map_inode;       // inode位图起始地址
    uint8_t*           map_data;        // data位图起始地址
    
    boolean            is_mounted;      // 是否被挂载

    struct nfs_dentry* root_dentry;     // 根目录

    // 需与磁盘同步内容
    int                sz_usage;        // 已用空间大小

    int                map_inode_blks;  // inode位图占用的块数
    int                map_inode_offset;// inode位图在磁盘上的偏移
    
    int                map_data_blks;   // data位图占用的块数
    int                map_data_offset; // data位图在磁盘上的偏移
    
    int                inode_offset;    // inode起始位置在磁盘上的偏移
    int                data_offset;     // data起始位置在磁盘上的偏移

};

struct nfs_inode {
    struct nfs_dentry* dentry;                      // 指向该inode的目录项（父节点目录中某一目录项）
    struct nfs_dentry* dentrys;                     // 若为目录，该目录中所有目录项的链表起始地址
    uint8_t*           block_pointer[NFS_DATA_PER_FILE];    // 指向的数据块指针（固定分配6个）
    // 需与磁盘同步内容
    int                ino;                         // ino编号
    int                size;                        // 文件已占用空间
    int                dir_cnt;                     // 若为目录，目录项dentry数目
    int                blockno[NFS_DATA_PER_FILE];  // 指向的数据块在磁盘中的块号
};

struct nfs_dentry {
    struct nfs_dentry* parent;                      // 父亲inode的dentry
    struct nfs_dentry* brother;                     // 兄弟inode的dentry
    struct nfs_inode*  inode;                       // 指向的inode
    // 需与磁盘同步内容
    char               fname[NFS_MAX_FILE_NAME];    // 文件名
    int                ino;                         // 指向的ino编号
    NFS_FILE_TYPE      ftype;                       // 文件类型（文件/目录）
};

static inline struct nfs_dentry* new_dentry(char * fname, NFS_FILE_TYPE ftype) {
    struct nfs_dentry * dentry = (struct nfs_dentry *)malloc(sizeof(struct nfs_dentry));
    memset(dentry, 0, sizeof(struct nfs_dentry));
    NFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1; 
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;
    return dentry;
}
/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct nfs_super_d
{
    // 幻数，用于判断是否为第一次读取磁盘
    uint32_t           magic_num;           // 幻数
    // 需与内存同步内容
    int                sz_usage;            // 已用空间大小
    
    int                map_inode_blks;      // inode位图占用的块数
    int                map_inode_offset;    // inode位图在磁盘上的偏移

    int                map_data_blks;       // data位图占用的块数
    int                map_data_offset;     // data位图在磁盘上的偏移

    int                inode_offset;        // inode起始位置在磁盘上的偏移
    int                data_offset;         // data起始位置在磁盘上的偏移
};

struct nfs_inode_d
{
    // 需与内存同步内容
    int                ino;                 // 在inode位图中的下标
    int                size;                // 文件已占用空间
    NFS_FILE_TYPE      ftype;               // 文件类型（文件/目录）
    int                dir_cnt;             // 若为目录，目录项dentry数目
    int                blockno[NFS_DATA_PER_FILE]; // 指向的数据块在磁盘中的块号
};  

struct nfs_dentry_d
{   
    // 需与内存同步内容
    char               fname[NFS_MAX_FILE_NAME];    // 指向的inode的文件名
    NFS_FILE_TYPE      ftype;                       // 指向的inode的文件类型
    int                ino;                         // 指向的ino编号
};  

#endif /* _TYPES_H_ */
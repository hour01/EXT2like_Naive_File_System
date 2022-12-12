#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;

typedef enum file_type {
    NFS_FILE,           // 普通文件
    NFS_DIR,             // 目录文件
    NFS_SYM_LINK
} FILE_TYPE;

/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT8_BITS              8
#define NO_DATA_BLK_IDX         -1

#define NFS_MAGIC_NUM           0x52415453  
#define NFS_SUPER_OFS           0
#define NFS_ROOT_INO            0

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

#define MAX_NAME_LEN    128
#define NFS_MAX_INODE   1024            // 最大文件数
#define INODE_DATA_BLK  6               //每个inode对应数据块索引数

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define NFS_IO_SZ()                     (super.sz_io)
#define DRIVER_IO_SZ()                  (super.sz_io/2)
#define NFS_DISK_SZ()                   (super.sz_disk)
#define NFS_DRIVER()                    (super.driver_fd)
#define NFS_MAX_DATA_BLK_NUM()          (super.data_blks)

#define NFS_ROUND_DOWN(value, round)    (value % round == 0 ? value : (value / round) * round)
#define NFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + 1) * round)
#define NFS_BLKS_SZ(blks)               (blks * NFS_IO_SZ())
#define NFS_ASSIGN_FNAME(pnfs_dentry, _fname)\
                                        memcpy(pnfs_dentry->name, _fname, strlen(_fname))

#define NFS_INO_OFS(ino)                (super.inode_offset + ino * NFS_IO_SZ())

#define NFS_IS_DIR(pinode)              (pinode->dentry->ftype == NFS_DIR)
#define NFS_IS_FILE(pinode)              (pinode->dentry->ftype == NFS_FILE)
#define NFS_IS_SYM_LINK(pinode)           (pinode->dentry->ftype == NFS_SYM_LINK)
/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct custom_options {
	const char*        device;
};

struct nfs_super {
    uint32_t           magic;              //幻数
    int                driver_fd;

    int                sz_io;              // NFS块大小
    int                sz_disk;            // 磁盘块大小
    int                sz_usage;
    int                max_ino;            // 最大支持文件数
    int                data_blks;          // 数据块数量

    uint8_t*           map_inode;           // inode位图
    int                map_inode_blks;      // inode位图占用的块数
    int                map_inode_offset;    // inode位图在磁盘上的偏移

    uint8_t*           map_data;            // 数据块位图
    int                map_data_blks;       // data位图占用的块数
    int                map_data_offset;     // data位图在磁盘上的偏移

    int                inode_offset;        // inode块在磁盘上偏移
    int                data_offset;         // data块在磁盘上偏移

    boolean            is_mounted;

    struct nfs_dentry* root_dentry;
};

struct nfs_inode {
    uint32_t            ino;
    int                 size;                 // 文件已占用空间
    int                 dir_cnt;              // 目录项数量
    struct nfs_dentry*  dentry;               // 指向该inode的dentry
    struct nfs_dentry*  dentrys;              // 所有目录项  
    int                 block_pointer[INODE_DATA_BLK];     // 数据块指针
    char               target_path[MAX_NAME_LEN];/* store traget path when it is a symlink */
};

struct nfs_dentry {
    char               name[MAX_NAME_LEN];
    uint32_t           ino;
    struct nfs_dentry* parent;                        /* 父亲Inode的dentry */
    struct nfs_dentry* brother;                       /* 兄弟 */
    struct nfs_inode*  inode;                         /* 指向inode */
    FILE_TYPE          ftype;
};

static inline struct nfs_dentry* new_dentry(char * fname, FILE_TYPE ftype) {
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
    uint32_t           magic_num;                   // 幻数
    int                sz_usage;

    int                max_ino;                     // 最多支持的文件数

    int                map_inode_blks;              // inode位图占用的块数
    int                map_inode_offset;            // inode位图在磁盘上的偏移

    int                map_data_blks;               // data位图占用的块数
    int                map_data_offset;             // data位图在磁盘上的偏移

    int                data_blks;                   //数据块总数
};

struct nfs_inode_d
{
    int                ino;                             // 在inode位图中的下标
    int                size;                            // 文件已占用空间
    FILE_TYPE          ftype;                           // 文件类型（目录类型、普通文件类型）
    int                dir_cnt;                         // 如果是目录类型文件，下面有几个目录项
    int                block_pointer[INODE_DATA_BLK];   // 数据块指针（可固定分配）
    char               target_path[MAX_NAME_LEN];       /* store traget path when it is a symlink */
};  

struct nfs_dentry_d
{
    char               fname[MAX_NAME_LEN];          // 指向的ino文件名
    FILE_TYPE          ftype;                         // 指向的ino文件类型
    int                ino;                           // 指向的ino号
    int                valid;                         // 该目录项是否有效
};  

#endif /* _TYPES_H_ */
#include "../include/nfs.h"

extern struct nfs_super  super; 

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* nfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int nfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}

/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int nfs_driver_read(int offset, uint8_t *out_content, int size) {
    // 保证每次从驱动中读出完整一块
    int      offset_aligned = NFS_ROUND_DOWN(offset, DRIVER_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), DRIVER_IO_SZ());

    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;

    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_read(NFS_DRIVER(), cur, DRIVER_IO_SZ());
        cur          += DRIVER_IO_SZ();
        size_aligned -= DRIVER_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int nfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, DRIVER_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), DRIVER_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    nfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);

    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_write(NFS_DRIVER(), cur, DRIVER_IO_SZ());
        cur          += DRIVER_IO_SZ();
        size_aligned -= DRIVER_IO_SZ();   
    }

    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 为一个inode分配dentry，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int nfs_alloc_dentry(struct nfs_inode* inode, struct nfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}

/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct nfs_dentry* 
 */
struct nfs_dentry* nfs_get_dentry(struct nfs_inode * inode, int dir) {
    struct nfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

/**
 * @brief 分配一个data_block，占用位图
 * 
 * @param inode 分配的data_block属于该inode
 * @param index 该data_block挂在该inode第index数据块索引上
 * @return int
 */
int nfs_alloc_data_blk(struct nfs_inode* inode, int index) {
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int dno_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    if(index >= INODE_DATA_BLK)
        return -NFS_ERROR_NOSPACE;

    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.map_data_blks) 
                        && dno_cursor < NFS_MAX_DATA_BLK_NUM(); byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS
                          && dno_cursor < NFS_MAX_DATA_BLK_NUM(); bit_cursor++) {
            if((super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前dno_cursor位置空闲 */
                super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            dno_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || dno_cursor == NFS_MAX_DATA_BLK_NUM())
        return -NFS_ERROR_NOSPACE;

    inode->block_pointer[index] = dno_cursor;
    

    return NFS_ERROR_NONE;
}

/**
 * @brief 向indoe写入
 * 
 * @param inode 
 * @param in_content 
 * @param size 
 * @param offset : 文件内部的offset
 * @return int 
 */
int nfs_inode_write(struct nfs_inode * inode, uint8_t *in_content, int size, int offset) {

    // 根据off确定要写入的起始位置
    // 在第几个数据块和块内偏移
    int index = NFS_ROUND_DOWN(offset,NFS_IO_SZ())/NFS_IO_SZ();
    int off_blk = offset - index*NFS_IO_SZ();

    // 检查有效性
	if (inode->size < offset || offset+size > NFS_IO_SZ()*INODE_DATA_BLK || index >= INODE_DATA_BLK 
            || off_blk >= NFS_IO_SZ() ) {
		return -NFS_ERROR_UNSUPPORTED;
	}

    //  记录一次循环写入的大小
    int size_write = 0;
    for(int size_residual = size; size_residual > 0; size_residual -= size_write)    
    {
        if (index >= INODE_DATA_BLK || inode->block_pointer[index] == NO_DATA_BLK_IDX)
        {
            if(nfs_alloc_data_blk(inode,index) != NFS_ERROR_NONE)
                return -NFS_ERROR_NOSPACE;;
        }

        // 本次写入的数据大小
        size_write = (NFS_IO_SZ()-off_blk) > size_residual ? size_residual : (NFS_IO_SZ()-off_blk);
        nfs_driver_write(super.data_offset+(inode->block_pointer[index])*NFS_IO_SZ()+off_blk,
                            in_content, size_write);

        // 一个数据块装不下，准备写入下一个数据块 
        index++, off_blk = 0, in_content += size_write;
    }
    
	inode->size = offset + size > inode->size ? offset + size : inode->size;
  
    return NFS_ERROR_NONE;
}


/**
 * @brief 从indoe读出
 * 
 * @param inode 
 * @param out_content 
 * @param size 
 * @param offset : 文件内部的offset
 * @return int 
 */
int nfs_inode_read(struct nfs_inode * inode, uint8_t *out_content, int size, int offset) {

    // 根据off确定要读出的起始位置
    // 在第几个数据块和块内偏移
    int index = NFS_ROUND_DOWN(offset,NFS_IO_SZ())/NFS_IO_SZ();
    int off_blk = offset - index*NFS_IO_SZ();

    // 检查有效性
	if (inode->size < offset || index >= INODE_DATA_BLK || inode->block_pointer[index] == NO_DATA_BLK_IDX) {
		return -NFS_ERROR_UNSUPPORTED;
	}

    // 最多只能读含有的size
    size = size < inode->size ? size : inode->size;
    //  记录一次循环读出的大小
    int size_read = 0;
    for(int size_residual = size; size_residual > 0; size_residual -= size_read)    
    {
        if (index >= INODE_DATA_BLK || inode->block_pointer[index] == NO_DATA_BLK_IDX)
        {
            return -NFS_ERROR_UNSUPPORTED;
        }

        // 本次读出的数据大小
        size_read = (NFS_IO_SZ()-off_blk) > size_residual ? size_residual : (NFS_IO_SZ()-off_blk);
        nfs_driver_read(super.data_offset+(inode->block_pointer[index])*NFS_IO_SZ()+off_blk,
                            out_content, size_read);

        // 一个数据块装不下，准备写入下一个数据块 
        index++, off_blk = 0, out_content += size_read;
    }
  
    return NFS_ERROR_NONE;
}


/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return sfs_inode
 */
struct nfs_inode* nfs_alloc_inode(struct nfs_dentry * dentry) {
    struct nfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.map_inode_blks)
                        && ino_cursor < super.max_ino; byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS
                          && ino_cursor < super.max_ino; bit_cursor++) {
            if((super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == super.max_ino)
        return -NFS_ERROR_NOSPACE;

    inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    // 初始化为没有分配任何数据块
    memset(inode->block_pointer,NO_DATA_BLK_IDX,sizeof(inode->block_pointer));

    return inode;
}


/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int nfs_sync_inode(struct nfs_inode * inode) {
    struct nfs_inode_d  inode_d;
    struct nfs_dentry*  dentry_cursor;
    struct nfs_dentry_d dentry_d;
    int ino             = inode->ino;

    int offset;
    /* Cycle 1: 写 数据 */
    if (NFS_IS_DIR(inode)) {                          
        dentry_cursor = inode->dentrys;
        offset = 0;
        while (dentry_cursor != NULL)
        {
            memcpy(dentry_d.fname, dentry_cursor->name, MAX_NAME_LEN);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;

            if(nfs_inode_write(inode, (uint8_t *)&dentry_d, 
                                    sizeof(struct nfs_dentry_d), offset) != NFS_ERROR_NONE) {
                return -NFS_ERROR_IO;                     
            }
            
            if (dentry_cursor->inode != NULL) {
                nfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct nfs_dentry_d);
        }
    }
    else if (NFS_IS_FILE(inode)) {
        // 不用管，对inode写入时是直接写入磁盘，只需要保证前面将数据块指针复制正确即可
    }

    /* Cycle 2: 写 INODE */
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    memcpy(inode_d.target_path, inode->target_path, MAX_NAME_LEN);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;

    // 将数据块指针写回disk
    for(int i=0;i<INODE_DATA_BLK;i++)
            inode_d.block_pointer[i] = inode->block_pointer[i];
    if (nfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    return NFS_ERROR_NONE;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct nfs_inode* 
 */
struct nfs_inode* nfs_read_inode(struct nfs_dentry * dentry, int ino) {
    struct nfs_inode* inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode));
    struct nfs_inode_d inode_d;
    struct nfs_dentry* sub_dentry;
    struct nfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    if (nfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        return NULL;                    
    }
    // inode->dir_cnt 会在nfs_alloc_dentry中自动更新。
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, MAX_NAME_LEN);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    dentry->ino = ino;
    // 复制数据块指针
    for(int i=0;i<INODE_DATA_BLK;i++)
        inode->block_pointer[i] = inode_d.block_pointer[i];
    if (NFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        for (i = 0; i < dir_cnt; i++)
        {
            if (nfs_inode_read(inode, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d),
                        i * sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE) {
                return NULL;                    
            }
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            nfs_alloc_dentry(inode, sub_dentry);
        }
    }

    return inode;
}


/**
 * @brief 
 * path: /qwe/ad  total_lvl = 2,
 *      1) odefind /'s in       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct nfs_inode* 
 */
struct nfs_dentry* nfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct nfs_dentry* dentry_cursor = super.root_dentry;
    struct nfs_dentry* dentry_ret = NULL;
    struct nfs_inode*  inode; 
    int   total_lvl = nfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            dentry_cursor->inode = nfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NFS_IS_FILE(inode) && lvl < total_lvl) {
            dentry_ret = inode->dentry;
            break;
        }
        if (NFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = nfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}

/**
 * @brief 挂载nfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data |
 * 
 * IO_SZ = 2*BLK_SZ
 * 
 * 每个Inode占用一个2个Blk
 * @param options 
 * @return int 
 */
int nfs_mount(struct custom_options options){
    int                 ret = NFS_ERROR_NONE;
    int                 driver_fd;
    int                 driver_io_sz;
    struct nfs_super_d  nfs_super_d; 
    struct nfs_dentry*  root_dentry;
    struct nfs_inode*   root_inode;

    int                 blk_num;
    int                 map_inode_blks;
    int                 inode_blks;
    int                 map_data_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    super.is_mounted = FALSE;

    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    super.driver_fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &driver_io_sz);

    // 1024B
    super.sz_io = driver_io_sz*2;
    
    root_dentry = new_dentry("/", NFS_DIR);

    if (nfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&nfs_super_d), 
                        sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }   
                                                      /* 读取super */
    if (nfs_super_d.magic_num != NFS_MAGIC_NUM) {     /* 幻数无 */

        // 磁盘中 NFS总块数 ···· 4K (总容量4MB，磁盘块512B)
        blk_num  =  NFS_DISK_SZ() / NFS_IO_SZ();

        // super block 占NFS的块数···· 1
        super_blks = NFS_ROUND_UP(sizeof(struct nfs_super_d), NFS_IO_SZ()) / NFS_IO_SZ();

        // inode_map 占NFS的块数···· 1
        map_inode_blks = NFS_ROUND_UP(NFS_ROUND_UP(NFS_MAX_INODE, 8)/8, NFS_IO_SZ()) 
                                    / NFS_IO_SZ();
        // inode 块数 ····· 1024
        inode_blks = NFS_MAX_INODE;

        // data_map 块数 ····· 
        map_data_blks = 1;

        // data 块数  
        nfs_super_d.data_blks = blk_num - super_blks - map_inode_blks - inode_blks - map_data_blks;
                                                      /* 布局layout */
        nfs_super_d.max_ino = NFS_MAX_INODE; 
        nfs_super_d.map_inode_offset = NFS_SUPER_OFS + NFS_BLKS_SZ(super_blks);
        nfs_super_d.map_data_offset = nfs_super_d.map_inode_offset + NFS_BLKS_SZ(map_inode_blks);
        nfs_super_d.map_inode_blks  = map_inode_blks;
        nfs_super_d.map_data_blks  = map_data_blks;
        nfs_super_d.sz_usage    = 0;
        is_init = TRUE;
    }
    super.sz_usage   = nfs_super_d.sz_usage;      /* 建立 in-memory 结构 */
    super.data_blks = nfs_super_d.data_blks;
    super.max_ino = nfs_super_d.max_ino;
    
    super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_inode_blks));
    super.map_inode_blks = nfs_super_d.map_inode_blks;
    super.map_inode_offset = nfs_super_d.map_inode_offset;

    super.map_data = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_data_blks));
    super.map_data_blks = nfs_super_d.map_data_blks;
    super.map_data_offset = nfs_super_d.map_data_offset;

    // 数据块的起始偏移
    super.inode_offset =  nfs_super_d.map_data_offset + NFS_BLKS_SZ(nfs_super_d.map_data_blks);
    super.data_offset =  super.inode_offset + NFS_BLKS_SZ(nfs_super_d.max_ino);

    // 读取出位图
    if (nfs_driver_read(nfs_super_d.map_inode_offset, (uint8_t *)(super.map_inode), 
                        NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }
    if (nfs_driver_read(nfs_super_d.map_data_offset, (uint8_t *)(super.map_data), 
                        NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (is_init) {                                    /* 分配根节点 */
        root_inode = nfs_alloc_inode(root_dentry);
        nfs_sync_inode(root_inode);
    }

    root_inode            = nfs_read_inode(root_dentry, NFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    super.root_dentry = root_dentry;
    super.is_mounted  = TRUE;

    return ret;
}


/**
 * @brief 
 * 
 * @return int 
 */
int nfs_umount() {
    struct nfs_super_d  nfs_super_d; 

    if (!super.is_mounted) {
        return NFS_ERROR_NONE;
    }

    nfs_sync_inode(super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                    
    nfs_super_d.magic_num           = NFS_MAGIC_NUM;
    nfs_super_d.map_inode_blks      = super.map_inode_blks;
    nfs_super_d.map_inode_offset    = super.map_inode_offset;
    nfs_super_d.map_data_blks      = super.map_data_blks;
    nfs_super_d.map_data_offset    = super.map_data_offset;
    nfs_super_d.sz_usage            = super.sz_usage;
    nfs_super_d.max_ino    = super.max_ino;
    nfs_super_d.data_blks = super.data_blks;

    if (nfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&nfs_super_d, 
                     sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (nfs_driver_write(nfs_super_d.map_inode_offset, (uint8_t *)(super.map_inode), 
                         NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (nfs_driver_write(nfs_super_d.map_data_offset, (uint8_t *)(super.map_data), 
                         NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    free(super.map_inode);
    free(super.map_data);
    ddriver_close(NFS_DRIVER());

    return NFS_ERROR_NONE;
}
#include "../include/newfs.h"

extern struct nfs_super      nfs_super; 
extern struct custom_options nfs_options;

// 获取文件名
char* nfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 */
int nfs_calc_lvl(const char * path) {
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

// 驱动读
int nfs_driver_read(int offset, uint8_t *out_content, int size) {
    //int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_IO_SZ());
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());     // 块大小为1024B
    int      bias           = offset - offset_aligned;
    //int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_IO_SZ());
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLK_SZ());    // 块大小为1024B
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(NFS_DRIVER(), cur, NFS_IO_SZ());
        // 驱动读写IO单位为512
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NFS_ERROR_NONE;
}

// 驱动写
int nfs_driver_write(int offset, uint8_t *in_content, int size) {
    //int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_IO_SZ());
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());     // 块大小为1024B
    int      bias           = offset - offset_aligned;
    //int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_IO_SZ());
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLK_SZ());    // 块大小为1024B
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    nfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(NFS_DRIVER(), cur, NFS_IO_SZ());
        // 驱动读写IO单位为512
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }

    free(temp_content);
    return NFS_ERROR_NONE;
}

// 为一个目录的inode分配给定dentry至dentrys，采用头插法
int nfs_alloc_dentry(struct nfs_inode* inode, struct nfs_dentry* dentry) {
    // 若目录链表为空，则直接指向dentry
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    // 若不为空，将dentry插到链表头
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    // dentry数目加1
    inode->dir_cnt++;
    return inode->dir_cnt;
}
// 将给定dentry从inode的dentrys中取出
int nfs_drop_dentry(struct nfs_inode * inode, struct nfs_dentry * dentry) {
    boolean is_find = FALSE;
    struct nfs_dentry* dentry_cursor;
    dentry_cursor = inode->dentrys;
    // 若链表头即为目标entry，去除它
    if (dentry_cursor == dentry) {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else {
        // 否则遍历链表寻找目标
        while (dentry_cursor)
        {
            // 找到目标entry，去除它
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    // 未找到，报错
    if (!is_find) {
        return -NFS_ERROR_NOTFOUND;
    }
    // entry数目减1
    inode->dir_cnt--;
    return inode->dir_cnt;
}
/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return nfs_inode
 */
struct nfs_inode* nfs_alloc_inode(struct nfs_dentry * dentry) {
    struct nfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    // ino编号指针
    int ino_cursor  = 0;
    // 数据块编号指针
    int blockno_cursor  = 0;

    boolean is_find_free_entry = FALSE;
    boolean is_find_enough_free_block = FALSE;

    // 从inode位图中寻找空闲
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((nfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                // 找到空闲位置，设为1
                nfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }
    // 若无空闲，报错
    if (!is_find_free_entry || ino_cursor == nfs_super.max_ino)
        return -NFS_ERROR_NOSPACE;
    // 分配inode并初始化
    inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    // 使dentry指向inode
    dentry->inode = inode;
    dentry->ino   = inode->ino;
    // 使inode指回dentry
    inode->dentry = dentry;

    // 从数据位图中寻找空闲，共需NFS_DATA_PER_FILE(固定为6)个
    int cnt = 0;
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_data_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((nfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                // 找到空闲位置，设为1
                nfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                // 记录找到的块号，直到找够
                inode->blockno[cnt++] = blockno_cursor;
                if(cnt == NFS_DATA_PER_FILE){
                    is_find_enough_free_block = TRUE;
                    break;
                }
            }
            blockno_cursor++;
        }
        // 已找够，退出循环
        if (is_find_enough_free_block) {
            break;
        }
    }
    // 若无空闲，报错
    if (!is_find_enough_free_block || blockno_cursor == nfs_super.max_data)
        return -NFS_ERROR_NOSPACE;
    
    // 对于数据块指针block_pointer[]，预分配内存空间
    for(int i = 0; i < NFS_DATA_PER_FILE; i++){
        inode->block_pointer[i] = (uint8_t*)malloc(NFS_BLK_SZ());
    }
    return inode;
}
// 将内存中的inode及其中待同步数据与磁盘中的inode_d同步
int nfs_sync_inode(struct nfs_inode * inode) {
    struct nfs_inode_d  inode_d;
    struct nfs_dentry*  dentry_cursor;
    struct nfs_dentry_d dentry_d;
    int ino             = inode->ino;
    // 同步相关属性
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    for (int i = 0; i < NFS_DATA_PER_FILE; i++) {
        inode_d.blockno[i] = inode->blockno[i];
    }
    int offset;
    
    // 写回inode
    if (nfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return -NFS_ERROR_IO;
    }

    // 对于目录，逐条写回子目录项
    if (NFS_IS_DIR(inode)) {
        int blockno_cursor = 0;
        dentry_cursor = inode->dentrys;
        offset        = NFS_DATA_OFS(inode->blockno[blockno_cursor]);
        while (dentry_cursor != NULL)
        {
            // 若当前块剩余空间不足，切换至下一块
            if(offset + sizeof(struct nfs_dentry_d) >= NFS_DATA_OFS(inode->blockno[blockno_cursor] + 1)){
                blockno_cursor++;
                offset = NFS_DATA_OFS(inode->blockno[blockno_cursor]);
            }
            // 复制子文件的信息至dentry_d
            memcpy(dentry_d.fname, dentry_cursor->fname, NFS_MAX_FILE_NAME);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            // 写回dentry_d
            if (nfs_driver_write(offset, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;                     
            }
            offset += sizeof(struct nfs_dentry_d);
            // 递归同步子文件inode
            if (dentry_cursor->inode != NULL) {
                nfs_sync_inode(dentry_cursor->inode);
            }
            // 下一个子文件目录项
            dentry_cursor = dentry_cursor->brother;
        }
    }
    // 对于文件类型，直接复制数据
    else if (NFS_IS_REG(inode)) {
        // 写数据，复制数据块
        for (int i = 0; i < NFS_DATA_PER_FILE; i++)
        {
            if (nfs_driver_write(NFS_DATA_OFS(inode->blockno[i]), (uint8_t *)inode->block_pointer[i],
                             NFS_BLKS_SZ(1)) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;
            }
        }
    }
    return NFS_ERROR_NONE;
}
/**
 * @brief 删除内存中的一个inode， 暂时不释放
 * Case 1: Reg File
 * 
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Reg Dentry)
 *                       |
 *                      Inode  (Reg File)
 * 
 *  1) Step 1. Erase Bitmap     
 *  2) Step 2. Free Inode                      (Function of nfs_drop_inode)
 * ------------------------------------------------------------------------
 *  3) *Setp 3. Free Dentry belonging to Inode (Outsider)
 * ========================================================================
 * Case 2: Dir
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Dir Dentry)
 *                       |
 *                      Inode  (Dir)
 *                    /     \
 *                Dentry -> Dentry
 * 
 *   Recursive
 * @param inode 
 * @return int 
 */
int nfs_drop_inode(struct nfs_inode * inode) {
    struct nfs_dentry*  dentry_cursor;
    struct nfs_dentry*  dentry_to_free;
    struct nfs_inode*   inode_cursor;

    int byte_cursor = 0; 
    int bit_cursor  = 0;
    int ino_cursor  = 0;
    int blockno_cursor  = 0;
    boolean is_find = FALSE;
    // inode为根目录，报错
    if (inode == nfs_super.root_dentry->inode) {
        return NFS_ERROR_INVAL;
    }
    // inode为目录
    if (NFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;
        // 递归向下drop
        while (dentry_cursor)
        {
            inode_cursor = dentry_cursor->inode;
            nfs_drop_inode(inode_cursor);
            nfs_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }
    }
    // inode为文件
    else if (NFS_IS_REG(inode)) {
        // 调整inode位图
        for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_inode_blks); 
            byte_cursor++)                            
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if (ino_cursor == inode->ino) {
                     nfs_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                     is_find = TRUE;
                     break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE) {
                break;
            }
        }
        // 释放数据块，todo
        
        // 最后释放inode
        free(inode);
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
    if (nfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }

    // 从inode_d复制相关属性至内存inode
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for(int i = 0 ;i < NFS_DATA_PER_FILE; i++){
        inode->blockno[i] = inode_d.blockno[i];
    }

    // 若inode为目录
    if (NFS_IS_DIR(inode)) {
        // 逐条复制子目录项
        
        inode->dir_cnt = inode_d.dir_cnt;
        int blockno_cursor = 0;
        int offset = NFS_DATA_OFS(inode->blockno[blockno_cursor]);

        for (int i = 0; i < inode->dir_cnt; i++)
        {   
            // 若一块已读完，选择下一块
            if(offset + sizeof(struct nfs_dentry_d) >=  NFS_DATA_OFS(inode->blockno[blockno_cursor] + 1)){
                blockno_cursor++;
                offset = NFS_DATA_OFS(inode->blockno[blockno_cursor]);
            }
            // 读取dentry_d
            if (nfs_driver_read(offset, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;                    
            }
            offset += sizeof(struct nfs_dentry_d);
            // 根据dentry_d为子目录项创建新内存目录项
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            nfs_alloc_dentry(inode, sub_dentry);
        }
    }
    // 若inode为文件
    else if (NFS_IS_REG(inode)) {
        // 复制数据
        for(int i = 0; i < NFS_DATA_PER_FILE; i++){
            inode->block_pointer[i] = (uint8_t *)malloc(NFS_BLK_SZ());
            if (nfs_driver_read(NFS_DATA_OFS(inode->blockno[i]), (uint8_t *)inode->block_pointer[i], 
                            NFS_BLKS_SZ(1)) != NFS_ERROR_NONE) {
            NFS_DBG("[%s] io error\n", __func__);
            return NULL;                    
            }
        }
    }
    return inode;
}

// 获得目录inode中指定编号dir的dentry
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
 * 解析路径path，以'/'划分为每一级的目录/文件名
 * 若路径中某一段匹配上文件，则返回其dentry
 * 否则返回匹配上的最后一级目录的dentry
 * 当返回根目录时is_root为TRUE
 * 当整个path路径对应目标存在，成功找到时，is_find为TRUE
 * 
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 */
struct nfs_dentry* nfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct nfs_dentry* dentry_cursor = nfs_super.root_dentry;
    struct nfs_dentry* dentry_ret = NULL;
    struct nfs_inode*  inode; 
    int   total_lvl = nfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);
    
    // 层级为0，所寻找的为根目录
    if (total_lvl == 0) {
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = nfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        // 根据path所解析得每层的文件名字，一层一层寻找
        lvl++;
        // Cache机制，没有实现cache所以直接无视
        if (dentry_cursor->inode == NULL) {
            nfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }
        // 向下寻找过程中最深的匹配节点（以根节点开始）
        inode = dentry_cursor->inode;
        // 深度未达到，路径中的目录名却与文件名匹配，非目标所求
        // 返回值为该文件的dentry
        if (NFS_IS_REG(inode) && lvl < total_lvl) {
            NFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        // 当前匹配节点为目录
        if (NFS_IS_DIR(inode)) {
            // 遍历目录中所有目录项，匹配下一级
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;
            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            // 未匹配上下一级名字
            // 返回值为当前匹配节点的dentry，也即已匹配上的最后一级目录的dentry
            if (!is_hit) {
                *is_find = FALSE;
                NFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }
            // 若匹配上且深度达到目标深度，即成功找到，返回该dentry
            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    // Cache机制，dentry对应inode还没读进来
    // 直接无视即可
    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = nfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}
/**
 * @brief 挂载nfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Inode | Data |
 * 
 * IO_SZ = 512B
 * BLK_SZ = 1024B
 * 
 * 每个Inode占用一个Blk
 */
int nfs_mount(struct custom_options options){
    int                 ret = NFS_ERROR_NONE;
    int                 driver_fd;
    struct nfs_super_d  nfs_super_d; 
    struct nfs_dentry*  root_dentry;
    struct nfs_inode*   root_inode;

    int                 inode_blks;
    int                 map_inode_blks;
    int                 data_blks;
    int                 map_data_blks;
    int                 super_blks;
    boolean             is_init = FALSE;

    nfs_super.is_mounted = FALSE;

    // 打开驱动
    driver_fd = ddriver_open(options.device);
    if (driver_fd < 0) {
        return driver_fd;
    }
    // 向内存超级块标记驱动并写入磁盘大小，单次IO大小，块大小
    nfs_super.driver_fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &nfs_super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &nfs_super.sz_io);
    nfs_super.sz_blk = nfs_super.sz_io * 2; // ext2文件系统块大小为1024B
    
    // 创建根目录dentry
    root_dentry = new_dentry("/", NFS_DIR);

    // 读取磁盘超级块暂存至内存
    if (nfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&nfs_super_d), 
                        sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    // 若无幻数，即第一次读取磁盘，需初始化
    if (nfs_super_d.magic_num != NFS_MAGIC_NUM) {
        // 直接规定各部分大小
        super_blks = NFS_SUPER_BLK;     // 超级块占用1块
        inode_blks  =  NFS_INODE_BLK;    // inode数量
        map_inode_blks = NFS_MAP_INODE_BLK; // inode位图占用1块
        data_blks = NFS_DATA_BLK;        // 数据块数量
        map_data_blks = NFS_MAP_DATA_BLK;   // 数据位图占用1块

        // 设置内存超级块属性
        nfs_super.max_ino = inode_blks;
        nfs_super.max_data = data_blks;

        // 暂存在内存的磁盘超级块layout
        nfs_super_d.magic_num = NFS_MAGIC_NUM;          // 幻数
        nfs_super_d.sz_usage = 0;    // 已用空间大小

        nfs_super_d.map_inode_blks = map_inode_blks;   // inode位图占用1块
        nfs_super_d.map_inode_offset = NFS_SUPER_OFS + NFS_BLKS_SZ(super_blks); // inode位图起始位置，在超级块之后

        nfs_super_d.map_data_blks = map_data_blks;  // 数据位图占用1块
        nfs_super_d.map_data_offset = nfs_super_d.map_inode_offset + NFS_BLKS_SZ(map_inode_blks);// 数据位图起始位置，在inode位图之后

        nfs_super_d.inode_offset = nfs_super_d.map_data_offset + NFS_BLKS_SZ(map_data_blks);    // inode块起始位置，在数据位图之后
        nfs_super_d.data_offset = nfs_super_d.inode_offset + NFS_BLKS_SZ(inode_blks);    // 数据块起始位置，在所有inode块之后
        
        is_init = TRUE;
    }
    
    // 内存超级块nfs_super分配空间
    nfs_super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_inode_blks));
    nfs_super.map_data = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_data_blks));

    // 内存超级块nfs_super同步磁盘块nfs_super_d数据
    nfs_super.sz_usage   = nfs_super_d.sz_usage;
    
    nfs_super.map_inode_blks = nfs_super_d.map_inode_blks;
    nfs_super.map_inode_offset = nfs_super_d.map_inode_offset;

    nfs_super.map_data_blks = nfs_super_d.map_data_blks;
    nfs_super.map_data_offset = nfs_super_d.map_data_offset;

    nfs_super.inode_offset = nfs_super_d.inode_offset;
    nfs_super.data_offset = nfs_super_d.data_offset;

    // 读取inode位图与数据位图
    if (nfs_driver_read(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), 
                        NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }
    if (nfs_driver_read(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data), 
                        NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    // 若磁盘刚初始化，分配根节点，并同步回磁盘
    if (is_init) {
        root_inode = nfs_alloc_inode(root_dentry);
        nfs_sync_inode(root_inode);
    }
    // 若磁盘已有数据，读取根节点
    root_inode            = nfs_read_inode(root_dentry, NFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    nfs_super.root_dentry = root_dentry;
    
    // 设置内存块为已挂载成功
    nfs_super.is_mounted  = TRUE;

    return ret;
}

/**
 * 卸载nfs
 */
int nfs_umount() {
    struct nfs_super_d  nfs_super_d; 
    // 若未挂载，直接退出
    if (!nfs_super.is_mounted) {
        return NFS_ERROR_NONE;
    }
    // 从根节点向下刷写节点，在sync函数中递归了整个inode树
    nfs_sync_inode(nfs_super.root_dentry->inode);
    
    // 磁盘超级块nfs_super_d数据同步内存超级块nfs_super
    nfs_super_d.magic_num           = NFS_MAGIC_NUM;
    nfs_super_d.sz_usage            = nfs_super.sz_usage;
    nfs_super_d.map_inode_blks      = nfs_super.map_inode_blks;
    nfs_super_d.map_inode_offset    = nfs_super.map_inode_offset;
    nfs_super_d.map_data_blks       = nfs_super.map_data_blks;
    nfs_super_d.map_data_offset     = nfs_super.map_data_offset;
    nfs_super_d.inode_offset        = nfs_super.inode_offset;
    nfs_super_d.data_offset         = nfs_super.data_offset;

    // 写回超级块
    if (nfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&nfs_super_d, 
                     sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }
    // 写回inode位图
    if (nfs_driver_write(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), 
                         NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }
    // 写回数据位图
    if (nfs_driver_write(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data), 
                         NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }
    // 释放位图内存空间，关驱动，卸载成功
    free(nfs_super.map_inode);
    free(nfs_super.map_data);
    ddriver_close(NFS_DRIVER());

    return NFS_ERROR_NONE;
}

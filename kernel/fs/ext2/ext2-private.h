/*
 * ext2/ext2-private.h - ext2 partition descriptors & helper functions
 *
 * Copyright (c) 2008,2009,2010,2011,2012 Ghassan Almaless
 * Copyright (c) 2011,2012 UPMC Sorbonne Universites
 *
 * This file is part of ALMOS-kernel.
 *
 * ALMOS-kernel is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.0 of the License.
 *
 * ALMOS-kernel is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ALMOS-kernel; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _EXT2_PRIVATE_H_
#define _EXT2_PRIVATE_H_

#include <types.h>
#include <spinlock.h>
#include <bits.h>
#include <mapper.h>
#include <pmm.h>
#include <wait_queue.h>
#include <rwlock.h>

/* SuperBlk flags */
#define EXT2_VALID_FS           1
#define EXT2_ERROR_FS           2
#define EXT2_ERRORS_CONTINUE    1
#define EXT2_ERRORS_RO          2
#define EXT2_ERRORS_PANIC       3
#define EXT2_GOOD_OLD_REV       0
#define EXT2_DYNAMIC_REV        1
#define EXT2_DEF_RESUID         0

/* Access Rights */
#define EXT2_S_IXOTH       0x0001
#define EXT2_S_IWOTH       0x0002
#define EXT2_S_IROTH       0x0004
#define EXT2_S_IXGRP       0x0008
#define EXT2_S_IWGRP       0x0010
#define EXT2_S_IRGRP       0x0020
#define EXT2_S_IXUSR       0x0040
#define EXT2_S_IWUSR       0x0080
#define EXT2_S_IRUSR       0x0100

/* Process execution usr/grp overrid */
#define EXT2_S_ISVTX       0x0200
#define EXT2_S_ISGID       0x0400
#define EXT2_S_ISUID       0x0800

/* File format */
#define EXT2_S_IFIFO       0x1000
#define EXT2_S_IFCHR       0x2000
#define EXT2_S_IFDIR       0x4000
#define EXT2_S_IFBLK       0x6000
#define EXT2_S_IFREG       0x8000
#define EXT2_S_IFLNK       0xA000
#define EXT2_S_IFSOCK      0xC000
#define EXT2_S_SHIFT       0xC
#define EXT2_S_MASK        0xF000
#define EXT2_SFT_SHIFT     0xD
#define EXT2_SFT_MASK      0x7

/* File type */
#define EXT2_FT_UNKNOWN    0x00
#define EXT2_FT_REGFILE    0x01
#define EXT2_FT_DIR        0x02
#define EXT2_FT_CHRDEV     0x03
#define EXT2_FT_BLKDEV     0x04
#define EXT2_FT_FIFO       0x05
#define EXT2_FT_SOCK       0x06
#define EXT2_FT_SYMLINK    0x07

/* Dir flags */
#define EXT2_BTREE_FL        0x00010000
#define EXT2_INDEX_FL        0x00010000
#define EXT2_JOURNAL_DATA_FL 0x00040000

/* Super Block Descriptor */
struct ext2_super_blk_s 
{
	uint32_t s_inodes_count;	   /* Total number of inodes */
	uint32_t s_blocks_count;	   /* Total number of blocks */
	uint32_t s_r_blocks_count;	   /* Total number of blocks reserved for the super user */
	uint32_t s_free_blocks_count;	   /* Total number of free blocks */
	uint32_t s_free_inodes_count;	   /* Total number of free inodes */
	uint32_t s_first_data_block;	   /* Id of the block containing the superblock structure */
	uint32_t s_log_block_size;	   /* Used to compute block size = 1024 << s_log_block_size */
	uint32_t s_log_frag_size;	   /* Used to compute fragment size */
	uint32_t s_blocks_per_group;	   /* Total number of blocks per group */
	int32_t  s_frags_per_group;	   /* Total number of fragments per group */
	uint32_t s_inodes_per_group;	   /* Total number of inodes per group */
	uint32_t s_mtime;		   /* Last time the file system was mounted */
	uint32_t s_wtime;		   /* Last write access to the file system */
	uint16_t s_mnt_count;	           /* How many `mount' since the last was full verification */
	uint16_t s_max_mnt_count;	   /* Max count between mount */
	uint16_t s_magic;		   /* (0xEF53) */
	uint16_t s_state;		   /* File system state */
	uint16_t s_errors;		   /* Behaviour when detecting errors */
	uint16_t s_minor_rev_level;	   /* Minor revision level */
	uint32_t s_lastcheck;	           /* Last check */
	uint32_t s_checkinterval;	   /* Max. time between checks */
	uint32_t s_creator_os;	   /* 4 standarized OS [0-4] */
	uint32_t s_rev_level;	           /* Revision level */
	uint16_t s_def_resuid;	   /* Default uid for reserved blocks */
	uint16_t s_def_resgid;	   /* Default gid for reserved blocks */
	uint32_t s_first_ino;	           /* First inode useable for standard files */
	uint16_t s_inode_size;	   /* Inode size */
	uint16_t s_block_group_nr;	   /* Block group hosting this superblock structure */
	uint32_t s_feature_compat;       /* Hints */
	uint32_t s_feature_incompat;     /* Hints */
	uint32_t s_feature_ro_compat;    /* Hints */
	uint8_t  s_uuid[16];		   /* Volume id */
	char     s_volume_name[16];	   /* Volume name */
	char     s_last_mounted[64];	   /* Path where the file system was last mounted */
	uint32_t s_algo_bitmap;	   /* For compression */
	uint8_t  s_prealloc_blocks;	   /* Number of blocks to preallocate */
	uint8_t  s_prealloc_dir_blocks;  /* Number of blocks to preallocate for directories */
	uint16_t s_padding;		   /* Alignment to word */
	uint32_t s_reserved[204];	   /* Nulls pad out to 1024 bytes */
} __attribute__ ((packed));


/* Blocks group descriptor */
struct ext2_group_desc_s
{
	uint32_t g_block_bitmap;	   /* Id of the first block of the "block bitmap" */
	uint32_t g_inode_bitmap;	   /* Id of the first block of the "inode bitmap" */
	uint32_t g_inode_table;	   /* Id of the first block of the "inode table" */
	uint16_t g_free_blocks_count;    /* Total number of free blocks */
	uint16_t g_free_inodes_count;    /* Total number of free inodes */
	uint16_t g_used_dirs_count;	   /* Number of inodes allocated to directories */
	uint16_t g_flags;		   /* ALMOS specific usage, ext2 rsrvd */
	uint16_t g_next_ino;	           /* ALMOS specific usage, ext2 rsrvd */
	uint16_t g_next_blk;		   /* ALMOS specific usage, ext2 rsrvd */
	uint32_t g_lock;		   /* ALMOS specific usage, ext2 rsrvd */
	uint32_t g_id;		   /* ALMOS specific usage, ext2 rsrvd */
} __attribute__ ((packed));


/* Inode Descriptor */
struct ext2_inode_s
{
	uint16_t i_mode;              /* File format & access rigths */
	uint16_t i_uid;		/* User ID associated with this file */
	uint32_t i_size;		/* File size (except in rev1 for regfiles, lower-32bit) */
	uint32_t i_atime;		/* Seconds number of last time this inode was accessed */
	uint32_t i_ctime;		/* Seconds number when inode was created */
	uint32_t i_mtime;		/* Seconds number when inode was modified */
	uint32_t i_dtime;		/* Seconds number when inode was deleted */
	uint16_t i_gid;		/* POSIX group having access to this file */
	uint16_t i_links;	        /* References number to this inode */
	uint32_t i_blocks;		/* Total number of 512-bytes reserved data blocks of this inode */
	uint32_t i_flags;		/* Access Behavior to inode data */
	uint32_t i_osd1;		/* OS dependant value */
	uint32_t i_block[15];		/* Pointers to data blocks */
	uint32_t i_generation;	/* NFS: file version */
	uint32_t i_file_acl;		/* Block number containing the extended attributes */
	uint32_t i_dir_acl;		/* Rev1: upper-32bit for regular file size */
	uint32_t i_faddr;		/* Fragment location */
	uint8_t  i_osd2[12];		/* OS dependant value */
} __attribute__ ((packed));

#define EXT2_ROOT_INO          2
#define EXT2_MAX_NAME_LEN      256
#define EXT2_PREALLOC_BLKS_NR  4
#define EXT2_DIRECT_BLKS_NR    12
#define EXT2_GF_SYNC           0x01
#define EXT2_GF_DIRTY          0x02
#define EXT2_GF_VALID          0x04
#define EXT2_GF_BUSY           0x08
#define EXT2_GF_FREE           0x10
#define EXT2_GF_WRITE          0x20
#define EXT2_GF_READ           0x40

/* Directory Entry Descriptor */
struct ext2_dirEntry_s
{
	uint32_t d_inode;		/* Inode number of the file entry */
	uint16_t d_rec_len;		/* Recored length */
	uint8_t  d_name_len;		/* Total number of charecters in entry name */
	uint8_t  d_file_type;		/* File type  */
	uint8_t  d_name[0];		/* File name */
} __attribute__ ((packed));

struct ext2_group_s
{
	/* public */
	struct ext2_group_desc_s info;
};

#define EXT2_LRU_BITMAP_SZ (PMM_PAGE_SIZE / (sizeof(struct ext2_group_s) * 8))
#define EXT2_LRU_BTMP_SZ   EXT2_LRU_BITMAP_SZ
struct ext2_group_cache_s
{
	spinlock_t lock;
	uint_t count;
	struct wait_queue_s wait_queue;
	BITMAP_DECLARE(lru_bitmap,EXT2_LRU_BTMP_SZ);
	struct ext2_group_s tbl[0];
};

struct device_s;
struct mapper_s;

struct ext2_context_s 
{
	spinlock_t lock;
	struct ext2_super_blk_s *sb;
	struct ext2_group_cache_s *group_cache;
	struct device_s *dev;
	struct mapper_s *mapper;
	uint_t last_free_blk_grp;
	uint_t last_free_ino_grp;
	uint_t flags;
	uint_t bytes_per_sector;
	uint_t blk_size;
	uint_t blks_per_page;
	uint_t blks_per_grp;
	uint_t inode_size;
	uint_t inodes_per_blk;
	uint_t inodes_per_grp;
	uint_t group_count;
	struct page_s *group_cache_pg;
};

struct ext2_node_s
{
	//struct mutex_s mutex;
	struct rwlock_s rwlock;
	struct vfs_node_s *vfs_node;
	struct ext2_context_s *ctx;
	struct ext2_inode_s inode;
	uint_t ino_tbl_blkid;
	uint_t ino;
	uint_t flags;
};

struct ext2_file_s
{
	struct ext2_node_s *node;
	uint_t last_blk;
	uint_t last_pg;
};

struct ext2_blk_req_s
{
	/* public */
	struct ext2_context_s *ctx;	/* In */
	uint_t blkid;		        /* In */
	uint_t flags;			/* In */
	void   *ptr;			/* Out */

	/* private */
	struct page_s *page;
};

extern const struct vfs_node_op_s ext2_n_op;
extern const struct vfs_file_op_s ext2_f_op;
extern const struct mapper_op_s ext2_node_mapper_op;

error_t ext2_node_io(struct ext2_node_s *parent, struct ext2_node_s *node, uint_t flags);

error_t ext2_node_read(struct vfs_node_s *parent, struct vfs_node_s *node);

error_t ext2_blk_get(struct ext2_blk_req_s *rq);
error_t ext2_blk_put(struct ext2_blk_req_s *rq);

error_t ext2_group_get(struct ext2_context_s *ctx, uint_t grp_id, struct ext2_group_s **ptr);
error_t ext2_group_put(struct ext2_context_s *ctx, struct ext2_group_s *ptr, uint_t flags);

error_t ext2_balloc(struct ext2_context_s *ctx, uint_t grp_id, uint_t *blkid);
error_t ext2_ialloc(struct ext2_context_s *ctx, uint_t grp_id, bool_t isDir, uint_t *ino);

error_t ext2_bfree(struct ext2_context_s *ctx, uint_t blkid);
error_t ext2_ifree(struct ext2_context_s *ctx, uint_t ino);

error_t ext2_group_cache_init(struct ext2_context_s *ctx);
error_t ext2_group_cache_destroy(struct ext2_context_s *ctx);

static inline sint_t ext2_get_indirect_level(uint_t blk_index, uint_t blk_size_log);

void ext2_sb_print(struct ext2_super_blk_s *sb);
void ext2_block_group_print(struct ext2_group_desc_s *ptr, uint_t index);
void ext2_inode_print(struct ext2_inode_s *inode);
void ext2_dirEntry_print(struct ext2_dirEntry_s *entry);

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

static inline sint_t ext2_get_indirect_level(uint_t blk_index, uint_t blk_size_log)
{
	register uint_t i;
	register uint_t count;
	register uint_t log;

	if(blk_index < 12)
		return 0;

	log = blk_size_log - 2;
	blk_index -= 12;

	for(i = 1; i < 4; i++)
	{
		count = 1 << (i * log);

		if(blk_index < count)
			return i;

		blk_index -= count;
	}

	return -1;
}

#endif	/* _EXT2_PRIVATE_H_ */

#ifndef SQUASHFS_FS
#define SQUASHFS_FS
/*
 * Squashfs
 *
 * Copyright (c) 2002, 2003, 2004, 2005 Phillip Lougher <phillip@lougher.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * squashfs_fs.h
 */

#ifdef	CONFIG_SQUASHFS_VMALLOC
#define SQUASHFS_ALLOC(a)		vmalloc(a)
#define SQUASHFS_FREE(a)		vfree(a)
#else
#define SQUASHFS_ALLOC(a)		kmalloc(a, GFP_KERNEL)
#define SQUASHFS_FREE(a)		kfree(a)
#endif
#define SQUASHFS_CACHED_FRAGMENTS	CONFIG_SQUASHFS_FRAGMENT_CACHE_SIZE	
#define SQUASHFS_MAJOR			2
#define SQUASHFS_MINOR			1
#define SQUASHFS_MAGIC			0x73717368
#define SQUASHFS_MAGIC_SWAP		0x68737173
#define SQUASHFS_START			0

/* size of metadata (inode and directory) blocks */
#define SQUASHFS_METADATA_SIZE		8192
#define SQUASHFS_METADATA_LOG		13

/* default size of data blocks */
#define SQUASHFS_FILE_SIZE		65536
#define SQUASHFS_FILE_LOG		16

#define SQUASHFS_FILE_MAX_SIZE		65536

/* Max number of uids and gids */
#define SQUASHFS_UIDS			256
#define SQUASHFS_GUIDS			255

/* Max length of filename (not 255) */
#define SQUASHFS_NAME_LEN		256

#define SQUASHFS_INVALID		((long long) 0xffffffffffff)
#define SQUASHFS_INVALID_BLK		((long long) 0xffffffff)
#define SQUASHFS_USED_BLK		((long long) 0xfffffffe)

/* Filesystem flags */
#define SQUASHFS_NOI			0
#define SQUASHFS_NOD			1
#define SQUASHFS_CHECK			2
#define SQUASHFS_NOF			3
#define SQUASHFS_NO_FRAG		4
#define SQUASHFS_ALWAYS_FRAG		5
#define SQUASHFS_DUPLICATE		6
#define SQUASHFS_BIT(flag, bit)		((flag >> bit) & 1)
#define SQUASHFS_UNCOMPRESSED_INODES(flags)	SQUASHFS_BIT(flags, SQUASHFS_NOI)
#define SQUASHFS_UNCOMPRESSED_DATA(flags)	SQUASHFS_BIT(flags, SQUASHFS_NOD)
#define SQUASHFS_UNCOMPRESSED_FRAGMENTS(flags)	SQUASHFS_BIT(flags, SQUASHFS_NOF)
#define SQUASHFS_NO_FRAGMENTS(flags)		SQUASHFS_BIT(flags, SQUASHFS_NO_FRAG)
#define SQUASHFS_ALWAYS_FRAGMENTS(flags)	SQUASHFS_BIT(flags, SQUASHFS_ALWAYS_FRAG)
#define SQUASHFS_DUPLICATES(flags)		SQUASHFS_BIT(flags, SQUASHFS_DUPLICATE)
#define SQUASHFS_CHECK_DATA(flags)		SQUASHFS_BIT(flags, SQUASHFS_CHECK)
#define SQUASHFS_MKFLAGS(noi, nod, check_data, nof, no_frag, always_frag, duplicate_checking)	(noi | (nod << 1) | (check_data << 2) | (nof << 3) | (no_frag << 4) | (always_frag << 5) | (duplicate_checking << 6))

/* Max number of types and file types */
#define SQUASHFS_DIR_TYPE		1
#define SQUASHFS_FILE_TYPE		2
#define SQUASHFS_SYMLINK_TYPE		3
#define SQUASHFS_BLKDEV_TYPE		4
#define SQUASHFS_CHRDEV_TYPE		5
#define SQUASHFS_FIFO_TYPE		6
#define SQUASHFS_SOCKET_TYPE		7
#define SQUASHFS_LDIR_TYPE		8

/* 1.0 filesystem type definitions */
#define SQUASHFS_TYPES			5
#define SQUASHFS_IPC_TYPE		0

/* Flag whether block is compressed or uncompressed, bit is set if block is uncompressed */
#define SQUASHFS_COMPRESSED_BIT		(1 << 15)
#define SQUASHFS_COMPRESSED_SIZE(B)	(((B) & ~SQUASHFS_COMPRESSED_BIT) ? \
					(B) & ~SQUASHFS_COMPRESSED_BIT : SQUASHFS_COMPRESSED_BIT)

#define SQUASHFS_COMPRESSED(B)		(!((B) & SQUASHFS_COMPRESSED_BIT))

#define SQUASHFS_COMPRESSED_BIT_BLOCK		(1 << 24)
#define SQUASHFS_COMPRESSED_SIZE_BLOCK(B)	(((B) & ~SQUASHFS_COMPRESSED_BIT_BLOCK) ? \
					(B) & ~SQUASHFS_COMPRESSED_BIT_BLOCK : SQUASHFS_COMPRESSED_BIT_BLOCK)

#define SQUASHFS_COMPRESSED_BLOCK(B)		(!((B) & SQUASHFS_COMPRESSED_BIT_BLOCK))

/*
 * Inode number ops.  Inodes consist of a compressed block number, and an uncompressed
 * offset within that block
 */
#define SQUASHFS_INODE_BLK(a)		((unsigned int) ((a) >> 16))
#define SQUASHFS_INODE_OFFSET(a)	((unsigned int) ((a) & 0xffff))
#define SQUASHFS_MKINODE(A, B)		((squashfs_inode)(((squashfs_inode) (A) << 16)\
					+ (B)))

/* Compute 32 bit VFS inode number from squashfs inode number */
#define SQUASHFS_MK_VFS_INODE(a, b)	((unsigned int) (((a) << 8) + ((b) >> 2) + 1))

/* Translate between VFS mode and squashfs mode */
#define SQUASHFS_MODE(a)		((a) & 0xfff)

/* fragment and fragment table defines */
typedef unsigned int			squashfs_fragment_index;
#define SQUASHFS_FRAGMENT_BYTES(A)	(A * sizeof(squashfs_fragment_entry))
#define SQUASHFS_FRAGMENT_INDEX(A)	(SQUASHFS_FRAGMENT_BYTES(A) / SQUASHFS_METADATA_SIZE)
#define SQUASHFS_FRAGMENT_INDEX_OFFSET(A)	(SQUASHFS_FRAGMENT_BYTES(A) % SQUASHFS_METADATA_SIZE)
#define SQUASHFS_FRAGMENT_INDEXES(A)	((SQUASHFS_FRAGMENT_BYTES(A) + SQUASHFS_METADATA_SIZE - 1) / SQUASHFS_METADATA_SIZE)
#define SQUASHFS_FRAGMENT_INDEX_BYTES(A)	(SQUASHFS_FRAGMENT_INDEXES(A) * sizeof(squashfs_fragment_index))

/* cached data constants for filesystem */
#define SQUASHFS_CACHED_BLKS		8

#define SQUASHFS_MAX_FILE_SIZE_LOG	32
#define SQUASHFS_MAX_FILE_SIZE		((long long) 1 << (SQUASHFS_MAX_FILE_SIZE_LOG - 1))

#define SQUASHFS_MARKER_BYTE		0xff


/*
 * definitions for structures on disk
 */

typedef unsigned int		squashfs_block;
typedef long long		squashfs_inode;

typedef unsigned int		squashfs_uid;

typedef struct squashfs_super_block {
#ifdef CONFIG_SQUASHFS_CRAMFS_MAGIC
	unsigned char       cramfs_magic[4];
	unsigned char       cramfs_size[4];
#endif
	unsigned int		s_magic;
	unsigned int		inodes;
	unsigned int		bytes_used;
	unsigned int		uid_start;
	unsigned int		guid_start;
	unsigned int		inode_table_start;
	unsigned int		directory_table_start;
	unsigned int		s_major:16;
	unsigned int		s_minor:16;
	unsigned int		block_size_1:16;
	unsigned int		block_log:16;
	unsigned int		flags:8;
	unsigned int		no_uids:8;
	unsigned int		no_guids:8;
	unsigned int		mkfs_time /* time of filesystem creation */;
	squashfs_inode		root_inode;
	unsigned int		block_size;
	unsigned int		fragments;
	unsigned int		fragment_table_start;
} __attribute__ ((packed)) squashfs_super_block;

typedef struct {
	unsigned int		index:27;
	unsigned int		start_block:29;
	unsigned char		size;
	unsigned char		name[0];
} __attribute__ ((packed)) squashfs_dir_index;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:8; /* index into uid table */
	unsigned int		guid:8; /* index into guid table */
} __attribute__ ((packed)) squashfs_base_inode_header;

typedef squashfs_base_inode_header squashfs_ipc_inode_header;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:8; /* index into uid table */
	unsigned int		guid:8; /* index into guid table */
	unsigned short		rdev;
} __attribute__ ((packed)) squashfs_dev_inode_header;
	
typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:8; /* index into uid table */
	unsigned int		guid:8; /* index into guid table */
	unsigned short		symlink_size;
	char			symlink[0];
} __attribute__ ((packed)) squashfs_symlink_inode_header;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:8; /* index into uid table */
	unsigned int		guid:8; /* index into guid table */
	unsigned int		mtime;
	squashfs_block		start_block;
	unsigned int		fragment;
	unsigned int		offset;
	unsigned int		file_size:SQUASHFS_MAX_FILE_SIZE_LOG;
	unsigned short		block_list[0];
} __attribute__ ((packed)) squashfs_reg_inode_header;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:8; /* index into uid table */
	unsigned int		guid:8; /* index into guid table */
	unsigned int		file_size:19;
	unsigned int		offset:13;
	unsigned int		mtime;
	unsigned int		start_block:24;
} __attribute__  ((packed)) squashfs_dir_inode_header;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:8; /* index into uid table */
	unsigned int		guid:8; /* index into guid table */
	unsigned int		file_size:27;
	unsigned int		offset:13;
	unsigned int		mtime;
	unsigned int		start_block:24;
	unsigned int		i_count:16;
	squashfs_dir_index	index[0];
} __attribute__  ((packed)) squashfs_ldir_inode_header;

typedef union {
	squashfs_base_inode_header	base;
	squashfs_dev_inode_header	dev;
	squashfs_symlink_inode_header	symlink;
	squashfs_reg_inode_header	reg;
	squashfs_dir_inode_header	dir;
	squashfs_ldir_inode_header	ldir;
	squashfs_ipc_inode_header	ipc;
} squashfs_inode_header;
	
typedef struct {
	unsigned int		offset:13;
	unsigned int		type:3;
	unsigned int		size:8;
	char			name[0];
} __attribute__ ((packed)) squashfs_dir_entry;

typedef struct {
	unsigned int		count:8;
	unsigned int		start_block:24;
} __attribute__ ((packed)) squashfs_dir_header;

typedef struct {
	unsigned int		start_block;
	unsigned int		size;
} __attribute__ ((packed)) squashfs_fragment_entry;

extern int squashfs_uncompress_block(void *d, int dstlen, void *s, int srclen);
extern int squashfs_uncompress_init(void);
extern int squashfs_uncompress_exit(void);

/*
 * macros to convert each packed bitfield structure from little endian to big
 * endian and vice versa.  These are needed when creating or using a filesystem on a
 * machine with different byte ordering to the target architecture.
 *
 */

#define SQUASHFS_SWAP_SUPER_BLOCK(s, d) {\
	SQUASHFS_MEMSET(s, d, sizeof(squashfs_super_block));\
	SQUASHFS_SWAP((s)->s_magic, d, 0, 32);\
	SQUASHFS_SWAP((s)->inodes, d, 32, 32);\
	SQUASHFS_SWAP((s)->bytes_used, d, 64, 32);\
	SQUASHFS_SWAP((s)->uid_start, d, 96, 32);\
	SQUASHFS_SWAP((s)->guid_start, d, 128, 32);\
	SQUASHFS_SWAP((s)->inode_table_start, d, 160, 32);\
	SQUASHFS_SWAP((s)->directory_table_start, d, 192, 32);\
	SQUASHFS_SWAP((s)->s_major, d, 224, 16);\
	SQUASHFS_SWAP((s)->s_minor, d, 240, 16);\
	SQUASHFS_SWAP((s)->block_size_1, d, 256, 16);\
	SQUASHFS_SWAP((s)->block_log, d, 272, 16);\
	SQUASHFS_SWAP((s)->flags, d, 288, 8);\
	SQUASHFS_SWAP((s)->no_uids, d, 296, 8);\
	SQUASHFS_SWAP((s)->no_guids, d, 304, 8);\
	SQUASHFS_SWAP((s)->mkfs_time, d, 312, 32);\
	SQUASHFS_SWAP((s)->root_inode, d, 344, 64);\
	SQUASHFS_SWAP((s)->block_size, d, 408, 32);\
	SQUASHFS_SWAP((s)->fragments, d, 440, 32);\
	SQUASHFS_SWAP((s)->fragment_table_start, d, 472, 32);\
}

#define SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, n) {\
	SQUASHFS_MEMSET(s, d, n);\
	SQUASHFS_SWAP((s)->inode_type, d, 0, 4);\
	SQUASHFS_SWAP((s)->mode, d, 4, 12);\
	SQUASHFS_SWAP((s)->uid, d, 16, 8);\
	SQUASHFS_SWAP((s)->guid, d, 24, 8);\
}

#define SQUASHFS_SWAP_IPC_INODE_HEADER(s, d) SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_ipc_inode_header))

#define SQUASHFS_SWAP_DEV_INODE_HEADER(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_dev_inode_header));\
	SQUASHFS_SWAP((s)->rdev, d, 32, 16);\
}

#define SQUASHFS_SWAP_SYMLINK_INODE_HEADER(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_symlink_inode_header));\
	SQUASHFS_SWAP((s)->symlink_size, d, 32, 16);\
}

#define SQUASHFS_SWAP_REG_INODE_HEADER(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_reg_inode_header));\
	SQUASHFS_SWAP((s)->mtime, d, 32, 32);\
	SQUASHFS_SWAP((s)->start_block, d, 64, 32);\
	SQUASHFS_SWAP((s)->fragment, d, 96, 32);\
	SQUASHFS_SWAP((s)->offset, d, 128, 32);\
	SQUASHFS_SWAP((s)->file_size, d, 160, SQUASHFS_MAX_FILE_SIZE_LOG);\
}

#define SQUASHFS_SWAP_DIR_INODE_HEADER(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_dir_inode_header));\
	SQUASHFS_SWAP((s)->file_size, d, 32, 19);\
	SQUASHFS_SWAP((s)->offset, d, 51, 13);\
	SQUASHFS_SWAP((s)->mtime, d, 64, 32);\
	SQUASHFS_SWAP((s)->start_block, d, 96, 24);\
}

#define SQUASHFS_SWAP_LDIR_INODE_HEADER(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_ldir_inode_header));\
	SQUASHFS_SWAP((s)->file_size, d, 32, 27);\
	SQUASHFS_SWAP((s)->offset, d, 59, 13);\
	SQUASHFS_SWAP((s)->mtime, d, 72, 32);\
	SQUASHFS_SWAP((s)->start_block, d, 104, 24);\
	SQUASHFS_SWAP((s)->i_count, d, 128, 16);\
}

#define SQUASHFS_SWAP_DIR_INDEX(s, d) {\
	SQUASHFS_MEMSET(s, d, sizeof(squashfs_dir_index));\
	SQUASHFS_SWAP((s)->index, d, 0, 27);\
	SQUASHFS_SWAP((s)->start_block, d, 27, 29);\
	SQUASHFS_SWAP((s)->size, d, 56, 8);\
}

#define SQUASHFS_SWAP_DIR_HEADER(s, d) {\
	SQUASHFS_MEMSET(s, d, sizeof(squashfs_dir_header));\
	SQUASHFS_SWAP((s)->count, d, 0, 8);\
	SQUASHFS_SWAP((s)->start_block, d, 8, 24);\
}

#define SQUASHFS_SWAP_DIR_ENTRY(s, d) {\
	SQUASHFS_MEMSET(s, d, sizeof(squashfs_dir_entry));\
	SQUASHFS_SWAP((s)->offset, d, 0, 13);\
	SQUASHFS_SWAP((s)->type, d, 13, 3);\
	SQUASHFS_SWAP((s)->size, d, 16, 8);\
}

#define SQUASHFS_SWAP_FRAGMENT_ENTRY(s, d) {\
	SQUASHFS_MEMSET(s, d, sizeof(squashfs_fragment_entry));\
	SQUASHFS_SWAP((s)->start_block, d, 0, 32);\
	SQUASHFS_SWAP((s)->size, d, 32, 32);\
}

#define SQUASHFS_SWAP_SHORTS(s, d, n) {\
	int entry;\
	int bit_position;\
	SQUASHFS_MEMSET(s, d, n * 2);\
	for(entry = 0, bit_position = 0; entry < n; entry++, bit_position += 16)\
		SQUASHFS_SWAP(s[entry], d, bit_position, 16);\
}

#define SQUASHFS_SWAP_INTS(s, d, n) {\
	int entry;\
	int bit_position;\
	SQUASHFS_MEMSET(s, d, n * 4);\
	for(entry = 0, bit_position = 0; entry < n; entry++, bit_position += 32)\
		SQUASHFS_SWAP(s[entry], d, bit_position, 32);\
}

#define SQUASHFS_SWAP_DATA(s, d, n, bits) {\
	int entry;\
	int bit_position;\
	SQUASHFS_MEMSET(s, d, n * bits / 8);\
	for(entry = 0, bit_position = 0; entry < n; entry++, bit_position += bits)\
		SQUASHFS_SWAP(s[entry], d, bit_position, bits);\
}

#define SQUASHFS_SWAP_FRAGMENT_INDEXES(s, d, n) SQUASHFS_SWAP_INTS(s, d, n)

#ifdef SQUASHFS_1_0_COMPATIBILITY
typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:4; /* index into uid table */
	unsigned int		guid:4; /* index into guid table */
} __attribute__ ((packed)) squashfs_base_inode_header_1;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:4; /* index into uid table */
	unsigned int		guid:4; /* index into guid table */
	unsigned int		type:4;
	unsigned int		offset:4;
} __attribute__ ((packed)) squashfs_ipc_inode_header_1;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:4; /* index into uid table */
	unsigned int		guid:4; /* index into guid table */
	unsigned short		rdev;
} __attribute__ ((packed)) squashfs_dev_inode_header_1;
	
typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:4; /* index into uid table */
	unsigned int		guid:4; /* index into guid table */
	unsigned short		symlink_size;
	char			symlink[0];
} __attribute__ ((packed)) squashfs_symlink_inode_header_1;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:4; /* index into uid table */
	unsigned int		guid:4; /* index into guid table */
	unsigned int		mtime;
	squashfs_block		start_block;
	unsigned int		file_size:SQUASHFS_MAX_FILE_SIZE_LOG;
	unsigned short		block_list[0];
} __attribute__ ((packed)) squashfs_reg_inode_header_1;

typedef struct {
	unsigned int		inode_type:4;
	unsigned int		mode:12; /* protection */
	unsigned int		uid:4; /* index into uid table */
	unsigned int		guid:4; /* index into guid table */
	unsigned int		file_size:19;
	unsigned int		offset:13;
	unsigned int		mtime;
	unsigned int		start_block:24;
} __attribute__  ((packed)) squashfs_dir_inode_header_1;

#define SQUASHFS_SWAP_BASE_INODE_HEADER_1(s, d, n) {\
	SQUASHFS_MEMSET(s, d, n);\
	SQUASHFS_SWAP((s)->inode_type, d, 0, 4);\
	SQUASHFS_SWAP((s)->mode, d, 4, 12);\
	SQUASHFS_SWAP((s)->uid, d, 16, 4);\
	SQUASHFS_SWAP((s)->guid, d, 20, 4);\
}

#define SQUASHFS_SWAP_IPC_INODE_HEADER_1(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER_1(s, d, sizeof(squashfs_ipc_inode_header_1));\
	SQUASHFS_SWAP((s)->type, d, 24, 4);\
	SQUASHFS_SWAP((s)->offset, d, 28, 4);\
}

#define SQUASHFS_SWAP_DEV_INODE_HEADER_1(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER_1(s, d, sizeof(squashfs_dev_inode_header_1));\
	SQUASHFS_SWAP((s)->rdev, d, 24, 16);\
}

#define SQUASHFS_SWAP_SYMLINK_INODE_HEADER_1(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_symlink_inode_header_1));\
	SQUASHFS_SWAP((s)->symlink_size, d, 24, 16);\
}

#define SQUASHFS_SWAP_REG_INODE_HEADER_1(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_reg_inode_header_1));\
	SQUASHFS_SWAP((s)->mtime, d, 24, 32);\
	SQUASHFS_SWAP((s)->start_block, d, 56, 32);\
	SQUASHFS_SWAP((s)->file_size, d, 88, SQUASHFS_MAX_FILE_SIZE_LOG);\
}

#define SQUASHFS_SWAP_DIR_INODE_HEADER_1(s, d) {\
	SQUASHFS_SWAP_BASE_INODE_HEADER(s, d, sizeof(squashfs_dir_inode_header_1));\
	SQUASHFS_SWAP((s)->file_size, d, 24, 19);\
	SQUASHFS_SWAP((s)->offset, d, 43, 13);\
	SQUASHFS_SWAP((s)->mtime, d, 56, 32);\
	SQUASHFS_SWAP((s)->start_block, d, 88, 24);\
}
#endif

#ifdef __KERNEL__
/*
 * macros used to swap each structure entry, taking into account
 * bitfields and different bitfield placing conventions on differing architectures
 */
#include <asm/byteorder.h>
#ifdef __BIG_ENDIAN
	/* convert from little endian to big endian */
#define SQUASHFS_SWAP(value, p, pos, tbits) _SQUASHFS_SWAP(value, p, pos, tbits, b_pos)
#else
	/* convert from big endian to little endian */ 
#define SQUASHFS_SWAP(value, p, pos, tbits) _SQUASHFS_SWAP(value, p, pos, tbits, 64 - tbits - b_pos)
#endif

#define _SQUASHFS_SWAP(value, p, pos, tbits, SHIFT) {\
	int bits;\
	int b_pos = pos % 8;\
	unsigned long long val = 0;\
	unsigned char *s = (unsigned char *)p + (pos / 8);\
	unsigned char *d = ((unsigned char *) &val) + 7;\
	for(bits = 0; bits < (tbits + b_pos); bits += 8) \
		*d-- = *s++;\
	value = (val >> (SHIFT))/* & ((1 << tbits) - 1)*/;\
}
#define SQUASHFS_MEMSET(s, d, n)	memset(s, 0, n);
#endif
#endif

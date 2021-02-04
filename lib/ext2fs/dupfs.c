/*
 * dupfs.c --- duplicate a ext2 filesystem handle
 *
 * Copyright (C) 1997, 1998, 2001, 2003, 2005 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef CONFIG_PFSCK
#include <pthread.h>
#endif
#include "ext2_fs.h"
#include "ext2fsP.h"

errcode_t ext2fs_dup_handle(ext2_filsys src, ext2_filsys *dest)
{
	ext2_filsys	fs;
	errcode_t	retval;

	EXT2_CHECK_MAGIC(src, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	retval = ext2fs_get_mem(sizeof(struct struct_ext2_filsys), &fs);
	if (retval)
		return retval;

	*fs = *src;
	fs->device_name = 0;
	fs->super = 0;
	fs->orig_super = 0;
	fs->group_desc = 0;
	fs->inode_map = 0;
	fs->block_map = 0;
	fs->badblocks = 0;
	fs->dblist = 0;
	fs->mmp_buf = 0;
	fs->mmp_cmp = 0;
	fs->mmp_fd = -1;

	io_channel_bumpcount(fs->io);
	if (fs->icache)
		fs->icache->refcount++;

	retval = ext2fs_get_mem(strlen(src->device_name)+1, &fs->device_name);
	if (retval)
		goto errout;
	strcpy(fs->device_name, src->device_name);

	retval = ext2fs_get_mem(SUPERBLOCK_SIZE, &fs->super);
	if (retval)
		goto errout;
	memcpy(fs->super, src->super, SUPERBLOCK_SIZE);

	if (src->orig_super) {
		retval = ext2fs_get_mem(SUPERBLOCK_SIZE, &fs->orig_super);
		if (retval)
			goto errout;
		memcpy(fs->orig_super, src->orig_super, SUPERBLOCK_SIZE);
	}

	retval = ext2fs_get_array(fs->desc_blocks, fs->blocksize,
				&fs->group_desc);
	if (retval)
		goto errout;

	if (src->group_desc)
		memcpy(fs->group_desc, src->group_desc,
		       (size_t) fs->desc_blocks * fs->blocksize);

	if (src->inode_map) {
		retval = ext2fs_copy_bitmap(src->inode_map, &fs->inode_map);
		if (retval)
			goto errout;
	}
	if (src->block_map) {
		retval = ext2fs_copy_bitmap(src->block_map, &fs->block_map);
		if (retval)
			goto errout;
	}
	if (src->badblocks) {
		retval = ext2fs_badblocks_copy(src->badblocks, &fs->badblocks);
		if (retval)
			goto errout;
	}
	if (src->dblist) {
		retval = ext2fs_copy_dblist(src->dblist, &fs->dblist);
		if (retval)
			goto errout;
	}
	if (src->mmp_buf) {
		retval = ext2fs_get_mem(src->blocksize, &fs->mmp_buf);
		if (retval)
			goto errout;
		memcpy(fs->mmp_buf, src->mmp_buf, src->blocksize);
	}
	if (src->mmp_fd >= 0) {
		fs->mmp_fd = dup(src->mmp_fd);
		if (fs->mmp_fd < 0) {
			retval = EXT2_ET_MMP_OPEN_DIRECT;
			goto errout;
		}
	}
	if (src->mmp_cmp) {
		int align = ext2fs_get_dio_alignment(src->mmp_fd);

		retval = ext2fs_get_memalign(src->blocksize, align,
					     &fs->mmp_cmp);
		if (retval)
			goto errout;
		memcpy(fs->mmp_cmp, src->mmp_cmp, src->blocksize);
	}
	*dest = fs;
	return 0;
errout:
	ext2fs_free(fs);
	return retval;

}

//#ifdef HAVE_PTHREAD
/*
 * This routine makes a clone of the provided fs structure and
 * use the provided set of flags to decide what needs to be copied.
 */
errcode_t ext2fs_clone_fs(ext2_filsys fs, ext2_filsys *dest, int flags)
{
	errcode_t retval;
	ext2_filsys child_fs;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	retval = ext2fs_get_mem(sizeof(struct struct_ext2_filsys), &child_fs);
	if (retval) {
		printf ("not enough memory\n");
		return retval;
	}
	// make an exact copy implying lists and memory structures are shared
	memcpy(child_fs, fs, sizeof(struct struct_ext2_filsys));
	child_fs->inode_map = NULL;
	child_fs->block_map = NULL;
	child_fs->badblocks = NULL;

	pthread_mutex_lock(&fs->refcount_mutex);
	fs->refcount++;
	pthread_mutex_unlock(&fs->refcount_mutex);

	if (EXT2FS_CLONE_DBLIST & flags && fs->dblist) {
		retval = ext2fs_copy_dblist(fs->dblist, &child_fs->dblist);
		if (retval)
	 		return retval;
		child_fs->dblist->fs = child_fs;
	}

	if (EXT2FS_CLONE_BLOCK & flags && fs->block_map) {
	    retval = ext2fs_copy_bitmap(fs->block_map, &child_fs->block_map);
	    if (retval)
	        return retval;
	    child_fs->block_map->fs = child_fs;
	}

	if (EXT2FS_CLONE_INODE & flags && fs->inode_map) {
		retval = ext2fs_copy_bitmap(fs->inode_map, &child_fs->inode_map);
		if (retval)
	 		return retval;
		child_fs->inode_map->fs = child_fs;
	}

	if (EXT2FS_CLONE_BADBLOCKS & flags && fs->badblocks) {
		retval = ext2fs_badblocks_copy(fs->badblocks, &child_fs->badblocks);
		if (retval)
			return retval;
	}

	/* icache will be rebuilt if needed, so do not copy from @src */
	child_fs->icache = NULL;

	child_fs->clone_flags = flags;
	child_fs->parent = fs;
	*dest = child_fs;

	return 0;
}

static errcode_t e2fsck_merge_bitmap(ext2_filsys fs, ext2fs_generic_bitmap *src,
                      ext2fs_generic_bitmap *dest)
{
	errcode_t ret = 0;

	if (*src) {
		if (*dest == NULL) {
			*dest = *src;
			*src = NULL;
		} else {
			ret = ext2fs_merge_bitmap(*src, *dest, NULL, NULL);
			if (ret)
				return ret;
		}
		(*dest)->fs = fs;
	}

	return 0;
}

errcode_t ext2fs_free_fs(ext2_filsys fs)
{
	errcode_t retval = 0;
	ext2_filsys dest = fs->parent; // dest should be parent_FS
	ext2_filsys src = fs;
	int flags = fs->clone_flags;

	// should be able to free fs if it has no children
	pthread_mutex_lock(&fs->refcount_mutex);
	if (fs->refcount > 0) {
		return retval; // need to free children fs before freeing the top level fs.
	}
	pthread_mutex_unlock(&fs->refcount_mutex);

	if (EXT2FS_CLONE_INODE & flags && src->inode_map) {
		if (dest->inode_map == NULL) {
			dest->inode_map = src->inode_map;
			src->inode_map = NULL;
		} else {
			retval = ext2fs_merge_bitmap(src->inode_map, dest->inode_map, NULL, NULL);
			if (retval)
				goto out;
		}
		dest->inode_map->fs = dest;
	}

	if (EXT2FS_CLONE_BLOCK & flags && fs->block_map) {
		if (dest->block_map == NULL) {
			dest->block_map = src->block_map;
			src->block_map = NULL;
		} else {
			retval = ext2fs_merge_bitmap(src->block_map, dest->block_map, NULL, NULL);
			if (retval)
				goto out;
		}
		dest->block_map->fs = dest;
	}

	if (EXT2FS_CLONE_DBLIST & flags && src->dblist) {
		if (dest->dblist) {
			retval = ext2fs_merge_dblist(src->dblist,
				dest->dblist);
			if (retval)
				goto out;
		} else {
			dest->dblist = src->dblist;
			dest->dblist->fs = dest;
			src->dblist = NULL;
		}
	}

	if (EXT2FS_CLONE_BADBLOCKS & flags && src->badblocks) {
		if (dest->badblocks == NULL)
			retval = ext2fs_badblocks_copy(src->badblocks,
					&dest->badblocks);
		else
			retval = ext2fs_badblocks_merge(src->badblocks,
					dest->badblocks);
	}

	dest->flags |= src->flags;
	if (!(dest->flags & EXT2_FLAG_VALID))
		ext2fs_unmark_valid(dest);

	if (src->icache) {
		ext2fs_free_inode_cache(src->icache);
		src->icache = NULL;
	}

out:
	io_channel_close(src->io);

	if (EXT2FS_CLONE_INODE & flags && src->inode_map)
		ext2fs_free_generic_bmap(src->inode_map);
	if (EXT2FS_CLONE_BLOCK & flags && src->block_map)
		ext2fs_free_generic_bmap(src->block_map);
	if (EXT2FS_CLONE_BADBLOCKS & flags  && src->badblocks)
		ext2fs_badblocks_list_free(src->badblocks);
	if (EXT2FS_CLONE_DBLIST & flags && src->dblist)
		ext2fs_free_dblist(src->dblist);

	ext2fs_free_mem(&src);

	return retval;
}

//#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel 4.9 equivalents of the helpers EROFS from 5.4 uses.
 */
#ifndef __EROFS_COMPAT_H
#define __EROFS_COMPAT_H

#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/bitops.h>

/* superblock flags were renamed from MS_* to SB_* in 4.14 */
#ifndef SB_RDONLY
#define SB_RDONLY	MS_RDONLY
#define SB_NOATIME	MS_NOATIME
#define SB_POSIXACL	MS_POSIXACL
static inline bool sb_rdonly(const struct super_block *sb)
{
	return sb->s_flags & MS_RDONLY;
}
#endif

#ifndef lru_to_page
#define lru_to_page(head) (list_entry((head)->prev, struct page, lru))
#endif

/* include/linux/wait_bit.h, 5.0 */
static inline void clear_and_wake_up_bit(int bit, void *word)
{
	clear_bit_unlock(bit, word);
	/* See wake_up_bit() for which memory barrier you need to use. */
	smp_mb__after_atomic();
	wake_up_bit(word, bit);
}

/* tagged pointers from include/linux/xarray.h, 4.20 */
static inline void *xa_tag_pointer(void *p, unsigned long tag)
{
	return (void *)((unsigned long)p | tag);
}

static inline void *xa_untag_pointer(void *entry)
{
	return (void *)((unsigned long)entry & ~3UL);
}

static inline unsigned int xa_pointer_tag(void *entry)
{
	return (unsigned long)entry & 3UL;
}

/* include/linux/fs_types.h, 5.0; EROFS_FT_* use the same numbers as FT_* */
#define FT_DIR		2
static inline unsigned char fs_ftype_to_dtype(unsigned int filetype)
{
	static const unsigned char fs_dtype_by_ftype[] = {
		DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR,
		DT_BLK, DT_FIFO, DT_SOCK, DT_LNK,
	};

	if (filetype >= ARRAY_SIZE(fs_dtype_by_ftype))
		return DT_UNKNOWN;
	return fs_dtype_by_ftype[filetype];
}

/* bio->bi_bdev became bio->bi_disk + bi_partno in 4.14 */
#define bio_set_dev(bio, bdev)	((bio)->bi_bdev = (bdev))

#endif

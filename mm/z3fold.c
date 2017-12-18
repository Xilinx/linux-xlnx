/*
 * z3fold.c
 *
 * Author: Vitaly Wool <vitaly.wool@konsulko.com>
 * Copyright (C) 2016, Sony Mobile Communications Inc.
 *
 * This implementation is based on zbud written by Seth Jennings.
 *
 * z3fold is an special purpose allocator for storing compressed pages. It
 * can store up to three compressed pages per page which improves the
 * compression ratio of zbud while retaining its main concepts (e. g. always
 * storing an integral number of objects per page) and simplicity.
 * It still has simple and deterministic reclaim properties that make it
 * preferable to a higher density approach (with no requirement on integral
 * number of object per page) when reclaim is used.
 *
 * As in zbud, pages are divided into "chunks".  The size of the chunks is
 * fixed at compile time and is determined by NCHUNKS_ORDER below.
 *
 * z3fold doesn't export any API and is meant to be used via zpool API.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/zpool.h>

/*****************
 * Structures
*****************/
/*
 * NCHUNKS_ORDER determines the internal allocation granularity, effectively
 * adjusting internal fragmentation.  It also determines the number of
 * freelists maintained in each pool. NCHUNKS_ORDER of 6 means that the
 * allocation granularity will be in chunks of size PAGE_SIZE/64. As one chunk
 * in allocated page is occupied by z3fold header, NCHUNKS will be calculated
 * to 63 which shows the max number of free chunks in z3fold page, also there
 * will be 63 freelists per pool.
 */
#define NCHUNKS_ORDER	6

#define CHUNK_SHIFT	(PAGE_SHIFT - NCHUNKS_ORDER)
#define CHUNK_SIZE	(1 << CHUNK_SHIFT)
#define ZHDR_SIZE_ALIGNED CHUNK_SIZE
#define NCHUNKS		((PAGE_SIZE - ZHDR_SIZE_ALIGNED) >> CHUNK_SHIFT)

#define BUDDY_MASK	((1 << NCHUNKS_ORDER) - 1)

struct z3fold_pool;
struct z3fold_ops {
	int (*evict)(struct z3fold_pool *pool, unsigned long handle);
};

/**
 * struct z3fold_pool - stores metadata for each z3fold pool
 * @lock:	protects all pool fields and first|last_chunk fields of any
 *		z3fold page in the pool
 * @unbuddied:	array of lists tracking z3fold pages that contain 2- buddies;
 *		the lists each z3fold page is added to depends on the size of
 *		its free region.
 * @buddied:	list tracking the z3fold pages that contain 3 buddies;
 *		these z3fold pages are full
 * @lru:	list tracking the z3fold pages in LRU order by most recently
 *		added buddy.
 * @pages_nr:	number of z3fold pages in the pool.
 * @ops:	pointer to a structure of user defined operations specified at
 *		pool creation time.
 *
 * This structure is allocated at pool creation time and maintains metadata
 * pertaining to a particular z3fold pool.
 */
struct z3fold_pool {
	spinlock_t lock;
	struct list_head unbuddied[NCHUNKS];
	struct list_head buddied;
	struct list_head lru;
	u64 pages_nr;
	const struct z3fold_ops *ops;
	struct zpool *zpool;
	const struct zpool_ops *zpool_ops;
};

enum buddy {
	HEADLESS = 0,
	FIRST,
	MIDDLE,
	LAST,
	BUDDIES_MAX
};

/*
 * struct z3fold_header - z3fold page metadata occupying the first chunk of each
 *			z3fold page, except for HEADLESS pages
 * @buddy:	links the z3fold page into the relevant list in the pool
 * @first_chunks:	the size of the first buddy in chunks, 0 if free
 * @middle_chunks:	the size of the middle buddy in chunks, 0 if free
 * @last_chunks:	the size of the last buddy in chunks, 0 if free
 * @first_num:		the starting number (for the first handle)
 */
struct z3fold_header {
	struct list_head buddy;
	unsigned short first_chunks;
	unsigned short middle_chunks;
	unsigned short last_chunks;
	unsigned short start_middle;
	unsigned short first_num:NCHUNKS_ORDER;
};

/*
 * Internal z3fold page flags
 */
enum z3fold_page_flags {
	UNDER_RECLAIM = 0,
	PAGE_HEADLESS,
	MIDDLE_CHUNK_MAPPED,
};

/*****************
 * Helpers
*****************/

/* Converts an allocation size in bytes to size in z3fold chunks */
static int size_to_chunks(size_t size)
{
	return (size + CHUNK_SIZE - 1) >> CHUNK_SHIFT;
}

#define for_each_unbuddied_list(_iter, _begin) \
	for ((_iter) = (_begin); (_iter) < NCHUNKS; (_iter)++)

/* Initializes the z3fold header of a newly allocated z3fold page */
static struct z3fold_header *init_z3fold_page(struct page *page)
{
	struct z3fold_header *zhdr = page_address(page);

	INIT_LIST_HEAD(&page->lru);
	clear_bit(UNDER_RECLAIM, &page->private);
	clear_bit(PAGE_HEADLESS, &page->private);
	clear_bit(MIDDLE_CHUNK_MAPPED, &page->private);

	zhdr->first_chunks = 0;
	zhdr->middle_chunks = 0;
	zhdr->last_chunks = 0;
	zhdr->first_num = 0;
	zhdr->start_middle = 0;
	INIT_LIST_HEAD(&zhdr->buddy);
	return zhdr;
}

/* Resets the struct page fields and frees the page */
static void free_z3fold_page(struct z3fold_header *zhdr)
{
	__free_page(virt_to_page(zhdr));
}

/*
 * Encodes the handle of a particular buddy within a z3fold page
 * Pool lock should be held as this function accesses first_num
 */
static unsigned long encode_handle(struct z3fold_header *zhdr, enum buddy bud)
{
	unsigned long handle;

	handle = (unsigned long)zhdr;
	if (bud != HEADLESS)
		handle += (bud + zhdr->first_num) & BUDDY_MASK;
	return handle;
}

/* Returns the z3fold page where a given handle is stored */
static struct z3fold_header *handle_to_z3fold_header(unsigned long handle)
{
	return (struct z3fold_header *)(handle & PAGE_MASK);
}

/* Returns buddy number */
static enum buddy handle_to_buddy(unsigned long handle)
{
	struct z3fold_header *zhdr = handle_to_z3fold_header(handle);
	return (handle - zhdr->first_num) & BUDDY_MASK;
}

/*
 * Returns the number of free chunks in a z3fold page.
 * NB: can't be used with HEADLESS pages.
 */
static int num_free_chunks(struct z3fold_header *zhdr)
{
	int nfree;
	/*
	 * If there is a middle object, pick up the bigger free space
	 * either before or after it. Otherwise just subtract the number
	 * of chunks occupied by the first and the last objects.
	 */
	if (zhdr->middle_chunks != 0) {
		int nfree_before = zhdr->first_chunks ?
			0 : zhdr->start_middle - 1;
		int nfree_after = zhdr->last_chunks ?
			0 : NCHUNKS - zhdr->start_middle - zhdr->middle_chunks;
		nfree = max(nfree_before, nfree_after);
	} else
		nfree = NCHUNKS - zhdr->first_chunks - zhdr->last_chunks;
	return nfree;
}

/*****************
 * API Functions
*****************/
/**
 * z3fold_create_pool() - create a new z3fold pool
 * @gfp:	gfp flags when allocating the z3fold pool structure
 * @ops:	user-defined operations for the z3fold pool
 *
 * Return: pointer to the new z3fold pool or NULL if the metadata allocation
 * failed.
 */
static struct z3fold_pool *z3fold_create_pool(gfp_t gfp,
		const struct z3fold_ops *ops)
{
	struct z3fold_pool *pool;
	int i;

	pool = kzalloc(sizeof(struct z3fold_pool), gfp);
	if (!pool)
		return NULL;
	spin_lock_init(&pool->lock);
	for_each_unbuddied_list(i, 0)
		INIT_LIST_HEAD(&pool->unbuddied[i]);
	INIT_LIST_HEAD(&pool->buddied);
	INIT_LIST_HEAD(&pool->lru);
	pool->pages_nr = 0;
	pool->ops = ops;
	return pool;
}

/**
 * z3fold_destroy_pool() - destroys an existing z3fold pool
 * @pool:	the z3fold pool to be destroyed
 *
 * The pool should be emptied before this function is called.
 */
static void z3fold_destroy_pool(struct z3fold_pool *pool)
{
	kfree(pool);
}

/* Has to be called with lock held */
static int z3fold_compact_page(struct z3fold_header *zhdr)
{
	struct page *page = virt_to_page(zhdr);
	void *beg = zhdr;


	if (!test_bit(MIDDLE_CHUNK_MAPPED, &page->private) &&
	    zhdr->middle_chunks != 0 &&
	    zhdr->first_chunks == 0 && zhdr->last_chunks == 0) {
		memmove(beg + ZHDR_SIZE_ALIGNED,
			beg + (zhdr->start_middle << CHUNK_SHIFT),
			zhdr->middle_chunks << CHUNK_SHIFT);
		zhdr->first_chunks = zhdr->middle_chunks;
		zhdr->middle_chunks = 0;
		zhdr->start_middle = 0;
		zhdr->first_num++;
		return 1;
	}
	return 0;
}

/**
 * z3fold_alloc() - allocates a region of a given size
 * @pool:	z3fold pool from which to allocate
 * @size:	size in bytes of the desired allocation
 * @gfp:	gfp flags used if the pool needs to grow
 * @handle:	handle of the new allocation
 *
 * This function will attempt to find a free region in the pool large enough to
 * satisfy the allocation request.  A search of the unbuddied lists is
 * performed first. If no suitable free region is found, then a new page is
 * allocated and added to the pool to satisfy the request.
 *
 * gfp should not set __GFP_HIGHMEM as highmem pages cannot be used
 * as z3fold pool pages.
 *
 * Return: 0 if success and handle is set, otherwise -EINVAL if the size or
 * gfp arguments are invalid or -ENOMEM if the pool was unable to allocate
 * a new page.
 */
static int z3fold_alloc(struct z3fold_pool *pool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	int chunks = 0, i, freechunks;
	struct z3fold_header *zhdr = NULL;
	enum buddy bud;
	struct page *page;

	if (!size || (gfp & __GFP_HIGHMEM))
		return -EINVAL;

	if (size > PAGE_SIZE)
		return -ENOSPC;

	if (size > PAGE_SIZE - ZHDR_SIZE_ALIGNED - CHUNK_SIZE)
		bud = HEADLESS;
	else {
		chunks = size_to_chunks(size);
		spin_lock(&pool->lock);

		/* First, try to find an unbuddied z3fold page. */
		zhdr = NULL;
		for_each_unbuddied_list(i, chunks) {
			if (!list_empty(&pool->unbuddied[i])) {
				zhdr = list_first_entry(&pool->unbuddied[i],
						struct z3fold_header, buddy);
				page = virt_to_page(zhdr);
				if (zhdr->first_chunks == 0) {
					if (zhdr->middle_chunks != 0 &&
					    chunks >= zhdr->start_middle)
						bud = LAST;
					else
						bud = FIRST;
				} else if (zhdr->last_chunks == 0)
					bud = LAST;
				else if (zhdr->middle_chunks == 0)
					bud = MIDDLE;
				else {
					pr_err("No free chunks in unbuddied\n");
					WARN_ON(1);
					continue;
				}
				list_del(&zhdr->buddy);
				goto found;
			}
		}
		bud = FIRST;
		spin_unlock(&pool->lock);
	}

	/* Couldn't find unbuddied z3fold page, create new one */
	page = alloc_page(gfp);
	if (!page)
		return -ENOMEM;
	spin_lock(&pool->lock);
	pool->pages_nr++;
	zhdr = init_z3fold_page(page);

	if (bud == HEADLESS) {
		set_bit(PAGE_HEADLESS, &page->private);
		goto headless;
	}

found:
	if (bud == FIRST)
		zhdr->first_chunks = chunks;
	else if (bud == LAST)
		zhdr->last_chunks = chunks;
	else {
		zhdr->middle_chunks = chunks;
		zhdr->start_middle = zhdr->first_chunks + 1;
	}

	if (zhdr->first_chunks == 0 || zhdr->last_chunks == 0 ||
			zhdr->middle_chunks == 0) {
		/* Add to unbuddied list */
		freechunks = num_free_chunks(zhdr);
		list_add(&zhdr->buddy, &pool->unbuddied[freechunks]);
	} else {
		/* Add to buddied list */
		list_add(&zhdr->buddy, &pool->buddied);
	}

headless:
	/* Add/move z3fold page to beginning of LRU */
	if (!list_empty(&page->lru))
		list_del(&page->lru);

	list_add(&page->lru, &pool->lru);

	*handle = encode_handle(zhdr, bud);
	spin_unlock(&pool->lock);

	return 0;
}

/**
 * z3fold_free() - frees the allocation associated with the given handle
 * @pool:	pool in which the allocation resided
 * @handle:	handle associated with the allocation returned by z3fold_alloc()
 *
 * In the case that the z3fold page in which the allocation resides is under
 * reclaim, as indicated by the PG_reclaim flag being set, this function
 * only sets the first|last_chunks to 0.  The page is actually freed
 * once both buddies are evicted (see z3fold_reclaim_page() below).
 */
static void z3fold_free(struct z3fold_pool *pool, unsigned long handle)
{
	struct z3fold_header *zhdr;
	int freechunks;
	struct page *page;
	enum buddy bud;

	spin_lock(&pool->lock);
	zhdr = handle_to_z3fold_header(handle);
	page = virt_to_page(zhdr);

	if (test_bit(PAGE_HEADLESS, &page->private)) {
		/* HEADLESS page stored */
		bud = HEADLESS;
	} else {
		bud = handle_to_buddy(handle);

		switch (bud) {
		case FIRST:
			zhdr->first_chunks = 0;
			break;
		case MIDDLE:
			zhdr->middle_chunks = 0;
			zhdr->start_middle = 0;
			break;
		case LAST:
			zhdr->last_chunks = 0;
			break;
		default:
			pr_err("%s: unknown bud %d\n", __func__, bud);
			WARN_ON(1);
			spin_unlock(&pool->lock);
			return;
		}
	}

	if (test_bit(UNDER_RECLAIM, &page->private)) {
		/* z3fold page is under reclaim, reclaim will free */
		spin_unlock(&pool->lock);
		return;
	}

	if (bud != HEADLESS) {
		/* Remove from existing buddy list */
		list_del(&zhdr->buddy);
	}

	if (bud == HEADLESS ||
	    (zhdr->first_chunks == 0 && zhdr->middle_chunks == 0 &&
			zhdr->last_chunks == 0)) {
		/* z3fold page is empty, free */
		list_del(&page->lru);
		clear_bit(PAGE_HEADLESS, &page->private);
		free_z3fold_page(zhdr);
		pool->pages_nr--;
	} else {
		z3fold_compact_page(zhdr);
		/* Add to the unbuddied list */
		freechunks = num_free_chunks(zhdr);
		list_add(&zhdr->buddy, &pool->unbuddied[freechunks]);
	}

	spin_unlock(&pool->lock);
}

/**
 * z3fold_reclaim_page() - evicts allocations from a pool page and frees it
 * @pool:	pool from which a page will attempt to be evicted
 * @retires:	number of pages on the LRU list for which eviction will
 *		be attempted before failing
 *
 * z3fold reclaim is different from normal system reclaim in that it is done
 * from the bottom, up. This is because only the bottom layer, z3fold, has
 * information on how the allocations are organized within each z3fold page.
 * This has the potential to create interesting locking situations between
 * z3fold and the user, however.
 *
 * To avoid these, this is how z3fold_reclaim_page() should be called:

 * The user detects a page should be reclaimed and calls z3fold_reclaim_page().
 * z3fold_reclaim_page() will remove a z3fold page from the pool LRU list and
 * call the user-defined eviction handler with the pool and handle as
 * arguments.
 *
 * If the handle can not be evicted, the eviction handler should return
 * non-zero. z3fold_reclaim_page() will add the z3fold page back to the
 * appropriate list and try the next z3fold page on the LRU up to
 * a user defined number of retries.
 *
 * If the handle is successfully evicted, the eviction handler should
 * return 0 _and_ should have called z3fold_free() on the handle. z3fold_free()
 * contains logic to delay freeing the page if the page is under reclaim,
 * as indicated by the setting of the PG_reclaim flag on the underlying page.
 *
 * If all buddies in the z3fold page are successfully evicted, then the
 * z3fold page can be freed.
 *
 * Returns: 0 if page is successfully freed, otherwise -EINVAL if there are
 * no pages to evict or an eviction handler is not registered, -EAGAIN if
 * the retry limit was hit.
 */
static int z3fold_reclaim_page(struct z3fold_pool *pool, unsigned int retries)
{
	int i, ret = 0, freechunks;
	struct z3fold_header *zhdr;
	struct page *page;
	unsigned long first_handle = 0, middle_handle = 0, last_handle = 0;

	spin_lock(&pool->lock);
	if (!pool->ops || !pool->ops->evict || list_empty(&pool->lru) ||
			retries == 0) {
		spin_unlock(&pool->lock);
		return -EINVAL;
	}
	for (i = 0; i < retries; i++) {
		page = list_last_entry(&pool->lru, struct page, lru);
		list_del(&page->lru);

		/* Protect z3fold page against free */
		set_bit(UNDER_RECLAIM, &page->private);
		zhdr = page_address(page);
		if (!test_bit(PAGE_HEADLESS, &page->private)) {
			list_del(&zhdr->buddy);
			/*
			 * We need encode the handles before unlocking, since
			 * we can race with free that will set
			 * (first|last)_chunks to 0
			 */
			first_handle = 0;
			last_handle = 0;
			middle_handle = 0;
			if (zhdr->first_chunks)
				first_handle = encode_handle(zhdr, FIRST);
			if (zhdr->middle_chunks)
				middle_handle = encode_handle(zhdr, MIDDLE);
			if (zhdr->last_chunks)
				last_handle = encode_handle(zhdr, LAST);
		} else {
			first_handle = encode_handle(zhdr, HEADLESS);
			last_handle = middle_handle = 0;
		}

		spin_unlock(&pool->lock);

		/* Issue the eviction callback(s) */
		if (middle_handle) {
			ret = pool->ops->evict(pool, middle_handle);
			if (ret)
				goto next;
		}
		if (first_handle) {
			ret = pool->ops->evict(pool, first_handle);
			if (ret)
				goto next;
		}
		if (last_handle) {
			ret = pool->ops->evict(pool, last_handle);
			if (ret)
				goto next;
		}
next:
		spin_lock(&pool->lock);
		clear_bit(UNDER_RECLAIM, &page->private);
		if ((test_bit(PAGE_HEADLESS, &page->private) && ret == 0) ||
		    (zhdr->first_chunks == 0 && zhdr->last_chunks == 0 &&
		     zhdr->middle_chunks == 0)) {
			/*
			 * All buddies are now free, free the z3fold page and
			 * return success.
			 */
			clear_bit(PAGE_HEADLESS, &page->private);
			free_z3fold_page(zhdr);
			pool->pages_nr--;
			spin_unlock(&pool->lock);
			return 0;
		}  else if (!test_bit(PAGE_HEADLESS, &page->private)) {
			if (zhdr->first_chunks != 0 &&
			    zhdr->last_chunks != 0 &&
			    zhdr->middle_chunks != 0) {
				/* Full, add to buddied list */
				list_add(&zhdr->buddy, &pool->buddied);
			} else {
				z3fold_compact_page(zhdr);
				/* add to unbuddied list */
				freechunks = num_free_chunks(zhdr);
				list_add(&zhdr->buddy,
					 &pool->unbuddied[freechunks]);
			}
		}

		/* add to beginning of LRU */
		list_add(&page->lru, &pool->lru);
	}
	spin_unlock(&pool->lock);
	return -EAGAIN;
}

/**
 * z3fold_map() - maps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be mapped
 *
 * Extracts the buddy number from handle and constructs the pointer to the
 * correct starting chunk within the page.
 *
 * Returns: a pointer to the mapped allocation
 */
static void *z3fold_map(struct z3fold_pool *pool, unsigned long handle)
{
	struct z3fold_header *zhdr;
	struct page *page;
	void *addr;
	enum buddy buddy;

	spin_lock(&pool->lock);
	zhdr = handle_to_z3fold_header(handle);
	addr = zhdr;
	page = virt_to_page(zhdr);

	if (test_bit(PAGE_HEADLESS, &page->private))
		goto out;

	buddy = handle_to_buddy(handle);
	switch (buddy) {
	case FIRST:
		addr += ZHDR_SIZE_ALIGNED;
		break;
	case MIDDLE:
		addr += zhdr->start_middle << CHUNK_SHIFT;
		set_bit(MIDDLE_CHUNK_MAPPED, &page->private);
		break;
	case LAST:
		addr += PAGE_SIZE - (zhdr->last_chunks << CHUNK_SHIFT);
		break;
	default:
		pr_err("unknown buddy id %d\n", buddy);
		WARN_ON(1);
		addr = NULL;
		break;
	}
out:
	spin_unlock(&pool->lock);
	return addr;
}

/**
 * z3fold_unmap() - unmaps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be unmapped
 */
static void z3fold_unmap(struct z3fold_pool *pool, unsigned long handle)
{
	struct z3fold_header *zhdr;
	struct page *page;
	enum buddy buddy;

	spin_lock(&pool->lock);
	zhdr = handle_to_z3fold_header(handle);
	page = virt_to_page(zhdr);

	if (test_bit(PAGE_HEADLESS, &page->private)) {
		spin_unlock(&pool->lock);
		return;
	}

	buddy = handle_to_buddy(handle);
	if (buddy == MIDDLE)
		clear_bit(MIDDLE_CHUNK_MAPPED, &page->private);
	spin_unlock(&pool->lock);
}

/**
 * z3fold_get_pool_size() - gets the z3fold pool size in pages
 * @pool:	pool whose size is being queried
 *
 * Returns: size in pages of the given pool.  The pool lock need not be
 * taken to access pages_nr.
 */
static u64 z3fold_get_pool_size(struct z3fold_pool *pool)
{
	return pool->pages_nr;
}

/*****************
 * zpool
 ****************/

static int z3fold_zpool_evict(struct z3fold_pool *pool, unsigned long handle)
{
	if (pool->zpool && pool->zpool_ops && pool->zpool_ops->evict)
		return pool->zpool_ops->evict(pool->zpool, handle);
	else
		return -ENOENT;
}

static const struct z3fold_ops z3fold_zpool_ops = {
	.evict =	z3fold_zpool_evict
};

static void *z3fold_zpool_create(const char *name, gfp_t gfp,
			       const struct zpool_ops *zpool_ops,
			       struct zpool *zpool)
{
	struct z3fold_pool *pool;

	pool = z3fold_create_pool(gfp, zpool_ops ? &z3fold_zpool_ops : NULL);
	if (pool) {
		pool->zpool = zpool;
		pool->zpool_ops = zpool_ops;
	}
	return pool;
}

static void z3fold_zpool_destroy(void *pool)
{
	z3fold_destroy_pool(pool);
}

static int z3fold_zpool_malloc(void *pool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	return z3fold_alloc(pool, size, gfp, handle);
}
static void z3fold_zpool_free(void *pool, unsigned long handle)
{
	z3fold_free(pool, handle);
}

static int z3fold_zpool_shrink(void *pool, unsigned int pages,
			unsigned int *reclaimed)
{
	unsigned int total = 0;
	int ret = -EINVAL;

	while (total < pages) {
		ret = z3fold_reclaim_page(pool, 8);
		if (ret < 0)
			break;
		total++;
	}

	if (reclaimed)
		*reclaimed = total;

	return ret;
}

static void *z3fold_zpool_map(void *pool, unsigned long handle,
			enum zpool_mapmode mm)
{
	return z3fold_map(pool, handle);
}
static void z3fold_zpool_unmap(void *pool, unsigned long handle)
{
	z3fold_unmap(pool, handle);
}

static u64 z3fold_zpool_total_size(void *pool)
{
	return z3fold_get_pool_size(pool) * PAGE_SIZE;
}

static struct zpool_driver z3fold_zpool_driver = {
	.type =		"z3fold",
	.owner =	THIS_MODULE,
	.create =	z3fold_zpool_create,
	.destroy =	z3fold_zpool_destroy,
	.malloc =	z3fold_zpool_malloc,
	.free =		z3fold_zpool_free,
	.shrink =	z3fold_zpool_shrink,
	.map =		z3fold_zpool_map,
	.unmap =	z3fold_zpool_unmap,
	.total_size =	z3fold_zpool_total_size,
};

MODULE_ALIAS("zpool-z3fold");

static int __init init_z3fold(void)
{
	/* Make sure the z3fold header will fit in one chunk */
	BUILD_BUG_ON(sizeof(struct z3fold_header) > ZHDR_SIZE_ALIGNED);
	zpool_register_driver(&z3fold_zpool_driver);

	return 0;
}

static void __exit exit_z3fold(void)
{
	zpool_unregister_driver(&z3fold_zpool_driver);
}

module_init(init_z3fold);
module_exit(exit_z3fold);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vitaly Wool <vitalywool@gmail.com>");
MODULE_DESCRIPTION("3-Fold Allocator for Compressed Pages");

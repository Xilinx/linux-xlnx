#include <linux/export.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/splice.h>
#include <net/checksum.h>

#define PIPE_PARANOIA /* for now */

#define iterate_iovec(i, n, __v, __p, skip, STEP) {	\
	size_t left;					\
	size_t wanted = n;				\
	__p = i->iov;					\
	__v.iov_len = min(n, __p->iov_len - skip);	\
	if (likely(__v.iov_len)) {			\
		__v.iov_base = __p->iov_base + skip;	\
		left = (STEP);				\
		__v.iov_len -= left;			\
		skip += __v.iov_len;			\
		n -= __v.iov_len;			\
	} else {					\
		left = 0;				\
	}						\
	while (unlikely(!left && n)) {			\
		__p++;					\
		__v.iov_len = min(n, __p->iov_len);	\
		if (unlikely(!__v.iov_len))		\
			continue;			\
		__v.iov_base = __p->iov_base;		\
		left = (STEP);				\
		__v.iov_len -= left;			\
		skip = __v.iov_len;			\
		n -= __v.iov_len;			\
	}						\
	n = wanted - n;					\
}

#define iterate_kvec(i, n, __v, __p, skip, STEP) {	\
	size_t wanted = n;				\
	__p = i->kvec;					\
	__v.iov_len = min(n, __p->iov_len - skip);	\
	if (likely(__v.iov_len)) {			\
		__v.iov_base = __p->iov_base + skip;	\
		(void)(STEP);				\
		skip += __v.iov_len;			\
		n -= __v.iov_len;			\
	}						\
	while (unlikely(n)) {				\
		__p++;					\
		__v.iov_len = min(n, __p->iov_len);	\
		if (unlikely(!__v.iov_len))		\
			continue;			\
		__v.iov_base = __p->iov_base;		\
		(void)(STEP);				\
		skip = __v.iov_len;			\
		n -= __v.iov_len;			\
	}						\
	n = wanted;					\
}

#define iterate_bvec(i, n, __v, __bi, skip, STEP) {	\
	struct bvec_iter __start;			\
	__start.bi_size = n;				\
	__start.bi_bvec_done = skip;			\
	__start.bi_idx = 0;				\
	for_each_bvec(__v, i->bvec, __bi, __start) {	\
		if (!__v.bv_len)			\
			continue;			\
		(void)(STEP);				\
	}						\
}

#define iterate_all_kinds(i, n, v, I, B, K) {			\
	size_t skip = i->iov_offset;				\
	if (unlikely(i->type & ITER_BVEC)) {			\
		struct bio_vec v;				\
		struct bvec_iter __bi;				\
		iterate_bvec(i, n, v, __bi, skip, (B))		\
	} else if (unlikely(i->type & ITER_KVEC)) {		\
		const struct kvec *kvec;			\
		struct kvec v;					\
		iterate_kvec(i, n, v, kvec, skip, (K))		\
	} else {						\
		const struct iovec *iov;			\
		struct iovec v;					\
		iterate_iovec(i, n, v, iov, skip, (I))		\
	}							\
}

#define iterate_and_advance(i, n, v, I, B, K) {			\
	if (unlikely(i->count < n))				\
		n = i->count;					\
	if (i->count) {						\
		size_t skip = i->iov_offset;			\
		if (unlikely(i->type & ITER_BVEC)) {		\
			const struct bio_vec *bvec = i->bvec;	\
			struct bio_vec v;			\
			struct bvec_iter __bi;			\
			iterate_bvec(i, n, v, __bi, skip, (B))	\
			i->bvec = __bvec_iter_bvec(i->bvec, __bi);	\
			i->nr_segs -= i->bvec - bvec;		\
			skip = __bi.bi_bvec_done;		\
		} else if (unlikely(i->type & ITER_KVEC)) {	\
			const struct kvec *kvec;		\
			struct kvec v;				\
			iterate_kvec(i, n, v, kvec, skip, (K))	\
			if (skip == kvec->iov_len) {		\
				kvec++;				\
				skip = 0;			\
			}					\
			i->nr_segs -= kvec - i->kvec;		\
			i->kvec = kvec;				\
		} else {					\
			const struct iovec *iov;		\
			struct iovec v;				\
			iterate_iovec(i, n, v, iov, skip, (I))	\
			if (skip == iov->iov_len) {		\
				iov++;				\
				skip = 0;			\
			}					\
			i->nr_segs -= iov - i->iov;		\
			i->iov = iov;				\
		}						\
		i->count -= n;					\
		i->iov_offset = skip;				\
	}							\
}

static size_t copy_page_to_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *from;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_pages_writeable(buf, copy)) {
		kaddr = kmap_atomic(page);
		from = kaddr + offset;

		/* first chunk, usually the only one */
		left = __copy_to_user_inatomic(buf, from, copy);
		copy -= left;
		skip += copy;
		from += copy;
		bytes -= copy;

		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = __copy_to_user_inatomic(buf, from, copy);
			copy -= left;
			skip = copy;
			from += copy;
			bytes -= copy;
		}
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = from - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	/* Too bad - revert to non-atomic kmap */

	kaddr = kmap(page);
	from = kaddr + offset;
	left = __copy_to_user(buf, from, copy);
	copy -= left;
	skip += copy;
	from += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = __copy_to_user(buf, from, copy);
		copy -= left;
		skip = copy;
		from += copy;
		bytes -= copy;
	}
	kunmap(page);

done:
	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}

static size_t copy_page_from_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *to;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_pages_readable(buf, copy)) {
		kaddr = kmap_atomic(page);
		to = kaddr + offset;

		/* first chunk, usually the only one */
		left = __copy_from_user_inatomic(to, buf, copy);
		copy -= left;
		skip += copy;
		to += copy;
		bytes -= copy;

		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = __copy_from_user_inatomic(to, buf, copy);
			copy -= left;
			skip = copy;
			to += copy;
			bytes -= copy;
		}
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = to - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	/* Too bad - revert to non-atomic kmap */

	kaddr = kmap(page);
	to = kaddr + offset;
	left = __copy_from_user(to, buf, copy);
	copy -= left;
	skip += copy;
	to += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = __copy_from_user(to, buf, copy);
		copy -= left;
		skip = copy;
		to += copy;
		bytes -= copy;
	}
	kunmap(page);

done:
	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}

#ifdef PIPE_PARANOIA
static bool sanity(const struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	int idx = i->idx;
	int next = pipe->curbuf + pipe->nrbufs;
	if (i->iov_offset) {
		struct pipe_buffer *p;
		if (unlikely(!pipe->nrbufs))
			goto Bad;	// pipe must be non-empty
		if (unlikely(idx != ((next - 1) & (pipe->buffers - 1))))
			goto Bad;	// must be at the last buffer...

		p = &pipe->bufs[idx];
		if (unlikely(p->offset + p->len != i->iov_offset))
			goto Bad;	// ... at the end of segment
	} else {
		if (idx != (next & (pipe->buffers - 1)))
			goto Bad;	// must be right after the last buffer
	}
	return true;
Bad:
	printk(KERN_ERR "idx = %d, offset = %zd\n", i->idx, i->iov_offset);
	printk(KERN_ERR "curbuf = %d, nrbufs = %d, buffers = %d\n",
			pipe->curbuf, pipe->nrbufs, pipe->buffers);
	for (idx = 0; idx < pipe->buffers; idx++)
		printk(KERN_ERR "[%p %p %d %d]\n",
			pipe->bufs[idx].ops,
			pipe->bufs[idx].page,
			pipe->bufs[idx].offset,
			pipe->bufs[idx].len);
	WARN_ON(1);
	return false;
}
#else
#define sanity(i) true
#endif

static inline int next_idx(int idx, struct pipe_inode_info *pipe)
{
	return (idx + 1) & (pipe->buffers - 1);
}

static size_t copy_page_to_iter_pipe(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	struct pipe_buffer *buf;
	size_t off;
	int idx;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	if (!sanity(i))
		return 0;

	off = i->iov_offset;
	idx = i->idx;
	buf = &pipe->bufs[idx];
	if (off) {
		if (offset == off && buf->page == page) {
			/* merge with the last one */
			buf->len += bytes;
			i->iov_offset += bytes;
			goto out;
		}
		idx = next_idx(idx, pipe);
		buf = &pipe->bufs[idx];
	}
	if (idx == pipe->curbuf && pipe->nrbufs)
		return 0;
	pipe->nrbufs++;
	buf->ops = &page_cache_pipe_buf_ops;
	get_page(buf->page = page);
	buf->offset = offset;
	buf->len = bytes;
	i->iov_offset = offset + bytes;
	i->idx = idx;
out:
	i->count -= bytes;
	return bytes;
}

/*
 * Fault in one or more iovecs of the given iov_iter, to a maximum length of
 * bytes.  For each iovec, fault in each page that constitutes the iovec.
 *
 * Return 0 on success, or non-zero if the memory could not be accessed (i.e.
 * because it is an invalid address).
 */
int iov_iter_fault_in_readable(struct iov_iter *i, size_t bytes)
{
	size_t skip = i->iov_offset;
	const struct iovec *iov;
	int err;
	struct iovec v;

	if (!(i->type & (ITER_BVEC|ITER_KVEC))) {
		iterate_iovec(i, bytes, v, iov, skip, ({
			err = fault_in_pages_readable(v.iov_base, v.iov_len);
			if (unlikely(err))
			return err;
		0;}))
	}
	return 0;
}
EXPORT_SYMBOL(iov_iter_fault_in_readable);

void iov_iter_init(struct iov_iter *i, int direction,
			const struct iovec *iov, unsigned long nr_segs,
			size_t count)
{
	/* It will get better.  Eventually... */
	if (segment_eq(get_fs(), KERNEL_DS)) {
		direction |= ITER_KVEC;
		i->type = direction;
		i->kvec = (struct kvec *)iov;
	} else {
		i->type = direction;
		i->iov = iov;
	}
	i->nr_segs = nr_segs;
	i->iov_offset = 0;
	i->count = count;
}
EXPORT_SYMBOL(iov_iter_init);

static void memcpy_from_page(char *to, struct page *page, size_t offset, size_t len)
{
	char *from = kmap_atomic(page);
	memcpy(to, from + offset, len);
	kunmap_atomic(from);
}

static void memcpy_to_page(struct page *page, size_t offset, const char *from, size_t len)
{
	char *to = kmap_atomic(page);
	memcpy(to + offset, from, len);
	kunmap_atomic(to);
}

static void memzero_page(struct page *page, size_t offset, size_t len)
{
	char *addr = kmap_atomic(page);
	memset(addr + offset, 0, len);
	kunmap_atomic(addr);
}

static inline bool allocated(struct pipe_buffer *buf)
{
	return buf->ops == &default_pipe_buf_ops;
}

static inline void data_start(const struct iov_iter *i, int *idxp, size_t *offp)
{
	size_t off = i->iov_offset;
	int idx = i->idx;
	if (off && (!allocated(&i->pipe->bufs[idx]) || off == PAGE_SIZE)) {
		idx = next_idx(idx, i->pipe);
		off = 0;
	}
	*idxp = idx;
	*offp = off;
}

static size_t push_pipe(struct iov_iter *i, size_t size,
			int *idxp, size_t *offp)
{
	struct pipe_inode_info *pipe = i->pipe;
	size_t off;
	int idx;
	ssize_t left;

	if (unlikely(size > i->count))
		size = i->count;
	if (unlikely(!size))
		return 0;

	left = size;
	data_start(i, &idx, &off);
	*idxp = idx;
	*offp = off;
	if (off) {
		left -= PAGE_SIZE - off;
		if (left <= 0) {
			pipe->bufs[idx].len += size;
			return size;
		}
		pipe->bufs[idx].len = PAGE_SIZE;
		idx = next_idx(idx, pipe);
	}
	while (idx != pipe->curbuf || !pipe->nrbufs) {
		struct page *page = alloc_page(GFP_USER);
		if (!page)
			break;
		pipe->nrbufs++;
		pipe->bufs[idx].ops = &default_pipe_buf_ops;
		pipe->bufs[idx].page = page;
		pipe->bufs[idx].offset = 0;
		if (left <= PAGE_SIZE) {
			pipe->bufs[idx].len = left;
			return size;
		}
		pipe->bufs[idx].len = PAGE_SIZE;
		left -= PAGE_SIZE;
		idx = next_idx(idx, pipe);
	}
	return size - left;
}

static size_t copy_pipe_to_iter(const void *addr, size_t bytes,
				struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	size_t n, off;
	int idx;

	if (!sanity(i))
		return 0;

	bytes = n = push_pipe(i, bytes, &idx, &off);
	if (unlikely(!n))
		return 0;
	for ( ; n; idx = next_idx(idx, pipe), off = 0) {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - off);
		memcpy_to_page(pipe->bufs[idx].page, off, addr, chunk);
		i->idx = idx;
		i->iov_offset = off + chunk;
		n -= chunk;
		addr += chunk;
	}
	i->count -= bytes;
	return bytes;
}

size_t copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	const char *from = addr;
	if (unlikely(i->type & ITER_PIPE))
		return copy_pipe_to_iter(addr, bytes, i);
	iterate_and_advance(i, bytes, v,
		__copy_to_user(v.iov_base, (from += v.iov_len) - v.iov_len,
			       v.iov_len),
		memcpy_to_page(v.bv_page, v.bv_offset,
			       (from += v.bv_len) - v.bv_len, v.bv_len),
		memcpy(v.iov_base, (from += v.iov_len) - v.iov_len, v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL(copy_to_iter);

size_t copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	char *to = addr;
	if (unlikely(i->type & ITER_PIPE)) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, v,
		__copy_from_user((to += v.iov_len) - v.iov_len, v.iov_base,
				 v.iov_len),
		memcpy_from_page((to += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy((to += v.iov_len) - v.iov_len, v.iov_base, v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL(copy_from_iter);

size_t copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	char *to = addr;
	if (unlikely(i->type & ITER_PIPE)) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, v,
		__copy_from_user_nocache((to += v.iov_len) - v.iov_len,
					 v.iov_base, v.iov_len),
		memcpy_from_page((to += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy((to += v.iov_len) - v.iov_len, v.iov_base, v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL(copy_from_iter_nocache);

size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (i->type & (ITER_BVEC|ITER_KVEC)) {
		void *kaddr = kmap_atomic(page);
		size_t wanted = copy_to_iter(kaddr + offset, bytes, i);
		kunmap_atomic(kaddr);
		return wanted;
	} else if (likely(!(i->type & ITER_PIPE)))
		return copy_page_to_iter_iovec(page, offset, bytes, i);
	else
		return copy_page_to_iter_pipe(page, offset, bytes, i);
}
EXPORT_SYMBOL(copy_page_to_iter);

size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (unlikely(i->type & ITER_PIPE)) {
		WARN_ON(1);
		return 0;
	}
	if (i->type & (ITER_BVEC|ITER_KVEC)) {
		void *kaddr = kmap_atomic(page);
		size_t wanted = copy_from_iter(kaddr + offset, bytes, i);
		kunmap_atomic(kaddr);
		return wanted;
	} else
		return copy_page_from_iter_iovec(page, offset, bytes, i);
}
EXPORT_SYMBOL(copy_page_from_iter);

static size_t pipe_zero(size_t bytes, struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	size_t n, off;
	int idx;

	if (!sanity(i))
		return 0;

	bytes = n = push_pipe(i, bytes, &idx, &off);
	if (unlikely(!n))
		return 0;

	for ( ; n; idx = next_idx(idx, pipe), off = 0) {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - off);
		memzero_page(pipe->bufs[idx].page, off, chunk);
		i->idx = idx;
		i->iov_offset = off + chunk;
		n -= chunk;
	}
	i->count -= bytes;
	return bytes;
}

size_t iov_iter_zero(size_t bytes, struct iov_iter *i)
{
	if (unlikely(i->type & ITER_PIPE))
		return pipe_zero(bytes, i);
	iterate_and_advance(i, bytes, v,
		__clear_user(v.iov_base, v.iov_len),
		memzero_page(v.bv_page, v.bv_offset, v.bv_len),
		memset(v.iov_base, 0, v.iov_len)
	)

	return bytes;
}
EXPORT_SYMBOL(iov_iter_zero);

size_t iov_iter_copy_from_user_atomic(struct page *page,
		struct iov_iter *i, unsigned long offset, size_t bytes)
{
	char *kaddr = kmap_atomic(page), *p = kaddr + offset;
	if (unlikely(i->type & ITER_PIPE)) {
		kunmap_atomic(kaddr);
		WARN_ON(1);
		return 0;
	}
	iterate_all_kinds(i, bytes, v,
		__copy_from_user_inatomic((p += v.iov_len) - v.iov_len,
					  v.iov_base, v.iov_len),
		memcpy_from_page((p += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len),
		memcpy((p += v.iov_len) - v.iov_len, v.iov_base, v.iov_len)
	)
	kunmap_atomic(kaddr);
	return bytes;
}
EXPORT_SYMBOL(iov_iter_copy_from_user_atomic);

static void pipe_advance(struct iov_iter *i, size_t size)
{
	struct pipe_inode_info *pipe = i->pipe;
	struct pipe_buffer *buf;
	int idx = i->idx;
	size_t off = i->iov_offset, orig_sz;
	
	if (unlikely(i->count < size))
		size = i->count;
	orig_sz = size;

	if (size) {
		if (off) /* make it relative to the beginning of buffer */
			size += off - pipe->bufs[idx].offset;
		while (1) {
			buf = &pipe->bufs[idx];
			if (size <= buf->len)
				break;
			size -= buf->len;
			idx = next_idx(idx, pipe);
		}
		buf->len = size;
		i->idx = idx;
		off = i->iov_offset = buf->offset + size;
	}
	if (off)
		idx = next_idx(idx, pipe);
	if (pipe->nrbufs) {
		int unused = (pipe->curbuf + pipe->nrbufs) & (pipe->buffers - 1);
		/* [curbuf,unused) is in use.  Free [idx,unused) */
		while (idx != unused) {
			pipe_buf_release(pipe, &pipe->bufs[idx]);
			idx = next_idx(idx, pipe);
			pipe->nrbufs--;
		}
	}
	i->count -= orig_sz;
}

void iov_iter_advance(struct iov_iter *i, size_t size)
{
	if (unlikely(i->type & ITER_PIPE)) {
		pipe_advance(i, size);
		return;
	}
	iterate_and_advance(i, size, v, 0, 0, 0)
}
EXPORT_SYMBOL(iov_iter_advance);

/*
 * Return the count of just the current iov_iter segment.
 */
size_t iov_iter_single_seg_count(const struct iov_iter *i)
{
	if (unlikely(i->type & ITER_PIPE))
		return i->count;	// it is a silly place, anyway
	if (i->nr_segs == 1)
		return i->count;
	else if (i->type & ITER_BVEC)
		return min(i->count, i->bvec->bv_len - i->iov_offset);
	else
		return min(i->count, i->iov->iov_len - i->iov_offset);
}
EXPORT_SYMBOL(iov_iter_single_seg_count);

void iov_iter_kvec(struct iov_iter *i, int direction,
			const struct kvec *kvec, unsigned long nr_segs,
			size_t count)
{
	BUG_ON(!(direction & ITER_KVEC));
	i->type = direction;
	i->kvec = kvec;
	i->nr_segs = nr_segs;
	i->iov_offset = 0;
	i->count = count;
}
EXPORT_SYMBOL(iov_iter_kvec);

void iov_iter_bvec(struct iov_iter *i, int direction,
			const struct bio_vec *bvec, unsigned long nr_segs,
			size_t count)
{
	BUG_ON(!(direction & ITER_BVEC));
	i->type = direction;
	i->bvec = bvec;
	i->nr_segs = nr_segs;
	i->iov_offset = 0;
	i->count = count;
}
EXPORT_SYMBOL(iov_iter_bvec);

void iov_iter_pipe(struct iov_iter *i, int direction,
			struct pipe_inode_info *pipe,
			size_t count)
{
	BUG_ON(direction != ITER_PIPE);
	i->type = direction;
	i->pipe = pipe;
	i->idx = (pipe->curbuf + pipe->nrbufs) & (pipe->buffers - 1);
	i->iov_offset = 0;
	i->count = count;
}
EXPORT_SYMBOL(iov_iter_pipe);

unsigned long iov_iter_alignment(const struct iov_iter *i)
{
	unsigned long res = 0;
	size_t size = i->count;

	if (!size)
		return 0;

	if (unlikely(i->type & ITER_PIPE)) {
		if (i->iov_offset && allocated(&i->pipe->bufs[i->idx]))
			return size | i->iov_offset;
		return size;
	}
	iterate_all_kinds(i, size, v,
		(res |= (unsigned long)v.iov_base | v.iov_len, 0),
		res |= v.bv_offset | v.bv_len,
		res |= (unsigned long)v.iov_base | v.iov_len
	)
	return res;
}
EXPORT_SYMBOL(iov_iter_alignment);

unsigned long iov_iter_gap_alignment(const struct iov_iter *i)
{
        unsigned long res = 0;
	size_t size = i->count;
	if (!size)
		return 0;

	if (unlikely(i->type & ITER_PIPE)) {
		WARN_ON(1);
		return ~0U;
	}

	iterate_all_kinds(i, size, v,
		(res |= (!res ? 0 : (unsigned long)v.iov_base) |
			(size != v.iov_len ? size : 0), 0),
		(res |= (!res ? 0 : (unsigned long)v.bv_offset) |
			(size != v.bv_len ? size : 0)),
		(res |= (!res ? 0 : (unsigned long)v.iov_base) |
			(size != v.iov_len ? size : 0))
		);
		return res;
}
EXPORT_SYMBOL(iov_iter_gap_alignment);

static inline size_t __pipe_get_pages(struct iov_iter *i,
				size_t maxsize,
				struct page **pages,
				int idx,
				size_t *start)
{
	struct pipe_inode_info *pipe = i->pipe;
	ssize_t n = push_pipe(i, maxsize, &idx, start);
	if (!n)
		return -EFAULT;

	maxsize = n;
	n += *start;
	while (n > 0) {
		get_page(*pages++ = pipe->bufs[idx].page);
		idx = next_idx(idx, pipe);
		n -= PAGE_SIZE;
	}

	return maxsize;
}

static ssize_t pipe_get_pages(struct iov_iter *i,
		   struct page **pages, size_t maxsize, unsigned maxpages,
		   size_t *start)
{
	unsigned npages;
	size_t capacity;
	int idx;

	if (!sanity(i))
		return -EFAULT;

	data_start(i, &idx, start);
	/* some of this one + all after this one */
	npages = ((i->pipe->curbuf - idx - 1) & (i->pipe->buffers - 1)) + 1;
	capacity = min(npages,maxpages) * PAGE_SIZE - *start;

	return __pipe_get_pages(i, min(maxsize, capacity), pages, idx, start);
}

ssize_t iov_iter_get_pages(struct iov_iter *i,
		   struct page **pages, size_t maxsize, unsigned maxpages,
		   size_t *start)
{
	if (maxsize > i->count)
		maxsize = i->count;

	if (!maxsize)
		return 0;

	if (unlikely(i->type & ITER_PIPE))
		return pipe_get_pages(i, pages, maxsize, maxpages, start);
	iterate_all_kinds(i, maxsize, v, ({
		unsigned long addr = (unsigned long)v.iov_base;
		size_t len = v.iov_len + (*start = addr & (PAGE_SIZE - 1));
		int n;
		int res;

		if (len > maxpages * PAGE_SIZE)
			len = maxpages * PAGE_SIZE;
		addr &= ~(PAGE_SIZE - 1);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		res = get_user_pages_fast(addr, n, (i->type & WRITE) != WRITE, pages);
		if (unlikely(res < 0))
			return res;
		return (res == n ? len : res * PAGE_SIZE) - *start;
	0;}),({
		/* can't be more than PAGE_SIZE */
		*start = v.bv_offset;
		get_page(*pages = v.bv_page);
		return v.bv_len;
	}),({
		return -EFAULT;
	})
	)
	return 0;
}
EXPORT_SYMBOL(iov_iter_get_pages);

static struct page **get_pages_array(size_t n)
{
	struct page **p = kmalloc(n * sizeof(struct page *), GFP_KERNEL);
	if (!p)
		p = vmalloc(n * sizeof(struct page *));
	return p;
}

static ssize_t pipe_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	struct page **p;
	size_t n;
	int idx;
	int npages;

	if (!sanity(i))
		return -EFAULT;

	data_start(i, &idx, start);
	/* some of this one + all after this one */
	npages = ((i->pipe->curbuf - idx - 1) & (i->pipe->buffers - 1)) + 1;
	n = npages * PAGE_SIZE - *start;
	if (maxsize > n)
		maxsize = n;
	else
		npages = DIV_ROUND_UP(maxsize + *start, PAGE_SIZE);
	p = get_pages_array(npages);
	if (!p)
		return -ENOMEM;
	n = __pipe_get_pages(i, maxsize, p, idx, start);
	if (n > 0)
		*pages = p;
	else
		kvfree(p);
	return n;
}

ssize_t iov_iter_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	struct page **p;

	if (maxsize > i->count)
		maxsize = i->count;

	if (!maxsize)
		return 0;

	if (unlikely(i->type & ITER_PIPE))
		return pipe_get_pages_alloc(i, pages, maxsize, start);
	iterate_all_kinds(i, maxsize, v, ({
		unsigned long addr = (unsigned long)v.iov_base;
		size_t len = v.iov_len + (*start = addr & (PAGE_SIZE - 1));
		int n;
		int res;

		addr &= ~(PAGE_SIZE - 1);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		p = get_pages_array(n);
		if (!p)
			return -ENOMEM;
		res = get_user_pages_fast(addr, n, (i->type & WRITE) != WRITE, p);
		if (unlikely(res < 0)) {
			kvfree(p);
			return res;
		}
		*pages = p;
		return (res == n ? len : res * PAGE_SIZE) - *start;
	0;}),({
		/* can't be more than PAGE_SIZE */
		*start = v.bv_offset;
		*pages = p = get_pages_array(1);
		if (!p)
			return -ENOMEM;
		get_page(*p = v.bv_page);
		return v.bv_len;
	}),({
		return -EFAULT;
	})
	)
	return 0;
}
EXPORT_SYMBOL(iov_iter_get_pages_alloc);

size_t csum_and_copy_from_iter(void *addr, size_t bytes, __wsum *csum,
			       struct iov_iter *i)
{
	char *to = addr;
	__wsum sum, next;
	size_t off = 0;
	sum = *csum;
	if (unlikely(i->type & ITER_PIPE)) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, v, ({
		int err = 0;
		next = csum_and_copy_from_user(v.iov_base, 
					       (to += v.iov_len) - v.iov_len,
					       v.iov_len, 0, &err);
		if (!err) {
			sum = csum_block_add(sum, next, off);
			off += v.iov_len;
		}
		err ? v.iov_len : 0;
	}), ({
		char *p = kmap_atomic(v.bv_page);
		next = csum_partial_copy_nocheck(p + v.bv_offset,
						 (to += v.bv_len) - v.bv_len,
						 v.bv_len, 0);
		kunmap_atomic(p);
		sum = csum_block_add(sum, next, off);
		off += v.bv_len;
	}),({
		next = csum_partial_copy_nocheck(v.iov_base,
						 (to += v.iov_len) - v.iov_len,
						 v.iov_len, 0);
		sum = csum_block_add(sum, next, off);
		off += v.iov_len;
	})
	)
	*csum = sum;
	return bytes;
}
EXPORT_SYMBOL(csum_and_copy_from_iter);

size_t csum_and_copy_to_iter(const void *addr, size_t bytes, __wsum *csum,
			     struct iov_iter *i)
{
	const char *from = addr;
	__wsum sum, next;
	size_t off = 0;
	sum = *csum;
	if (unlikely(i->type & ITER_PIPE)) {
		WARN_ON(1);	/* for now */
		return 0;
	}
	iterate_and_advance(i, bytes, v, ({
		int err = 0;
		next = csum_and_copy_to_user((from += v.iov_len) - v.iov_len,
					     v.iov_base, 
					     v.iov_len, 0, &err);
		if (!err) {
			sum = csum_block_add(sum, next, off);
			off += v.iov_len;
		}
		err ? v.iov_len : 0;
	}), ({
		char *p = kmap_atomic(v.bv_page);
		next = csum_partial_copy_nocheck((from += v.bv_len) - v.bv_len,
						 p + v.bv_offset,
						 v.bv_len, 0);
		kunmap_atomic(p);
		sum = csum_block_add(sum, next, off);
		off += v.bv_len;
	}),({
		next = csum_partial_copy_nocheck((from += v.iov_len) - v.iov_len,
						 v.iov_base,
						 v.iov_len, 0);
		sum = csum_block_add(sum, next, off);
		off += v.iov_len;
	})
	)
	*csum = sum;
	return bytes;
}
EXPORT_SYMBOL(csum_and_copy_to_iter);

int iov_iter_npages(const struct iov_iter *i, int maxpages)
{
	size_t size = i->count;
	int npages = 0;

	if (!size)
		return 0;

	if (unlikely(i->type & ITER_PIPE)) {
		struct pipe_inode_info *pipe = i->pipe;
		size_t off;
		int idx;

		if (!sanity(i))
			return 0;

		data_start(i, &idx, &off);
		/* some of this one + all after this one */
		npages = ((pipe->curbuf - idx - 1) & (pipe->buffers - 1)) + 1;
		if (npages >= maxpages)
			return maxpages;
	} else iterate_all_kinds(i, size, v, ({
		unsigned long p = (unsigned long)v.iov_base;
		npages += DIV_ROUND_UP(p + v.iov_len, PAGE_SIZE)
			- p / PAGE_SIZE;
		if (npages >= maxpages)
			return maxpages;
	0;}),({
		npages++;
		if (npages >= maxpages)
			return maxpages;
	}),({
		unsigned long p = (unsigned long)v.iov_base;
		npages += DIV_ROUND_UP(p + v.iov_len, PAGE_SIZE)
			- p / PAGE_SIZE;
		if (npages >= maxpages)
			return maxpages;
	})
	)
	return npages;
}
EXPORT_SYMBOL(iov_iter_npages);

const void *dup_iter(struct iov_iter *new, struct iov_iter *old, gfp_t flags)
{
	*new = *old;
	if (unlikely(new->type & ITER_PIPE)) {
		WARN_ON(1);
		return NULL;
	}
	if (new->type & ITER_BVEC)
		return new->bvec = kmemdup(new->bvec,
				    new->nr_segs * sizeof(struct bio_vec),
				    flags);
	else
		/* iovec and kvec have identical layout */
		return new->iov = kmemdup(new->iov,
				   new->nr_segs * sizeof(struct iovec),
				   flags);
}
EXPORT_SYMBOL(dup_iter);

/**
 * import_iovec() - Copy an array of &struct iovec from userspace
 *     into the kernel, check that it is valid, and initialize a new
 *     &struct iov_iter iterator to access it.
 *
 * @type: One of %READ or %WRITE.
 * @uvector: Pointer to the userspace array.
 * @nr_segs: Number of elements in userspace array.
 * @fast_segs: Number of elements in @iov.
 * @iov: (input and output parameter) Pointer to pointer to (usually small
 *     on-stack) kernel array.
 * @i: Pointer to iterator that will be initialized on success.
 *
 * If the array pointed to by *@iov is large enough to hold all @nr_segs,
 * then this function places %NULL in *@iov on return. Otherwise, a new
 * array will be allocated and the result placed in *@iov. This means that
 * the caller may call kfree() on *@iov regardless of whether the small
 * on-stack array was used or not (and regardless of whether this function
 * returns an error or not).
 *
 * Return: 0 on success or negative error code on error.
 */
int import_iovec(int type, const struct iovec __user * uvector,
		 unsigned nr_segs, unsigned fast_segs,
		 struct iovec **iov, struct iov_iter *i)
{
	ssize_t n;
	struct iovec *p;
	n = rw_copy_check_uvector(type, uvector, nr_segs, fast_segs,
				  *iov, &p);
	if (n < 0) {
		if (p != *iov)
			kfree(p);
		*iov = NULL;
		return n;
	}
	iov_iter_init(i, type, p, nr_segs, n);
	*iov = p == *iov ? NULL : p;
	return 0;
}
EXPORT_SYMBOL(import_iovec);

#ifdef CONFIG_COMPAT
#include <linux/compat.h>

int compat_import_iovec(int type, const struct compat_iovec __user * uvector,
		 unsigned nr_segs, unsigned fast_segs,
		 struct iovec **iov, struct iov_iter *i)
{
	ssize_t n;
	struct iovec *p;
	n = compat_rw_copy_check_uvector(type, uvector, nr_segs, fast_segs,
				  *iov, &p);
	if (n < 0) {
		if (p != *iov)
			kfree(p);
		*iov = NULL;
		return n;
	}
	iov_iter_init(i, type, p, nr_segs, n);
	*iov = p == *iov ? NULL : p;
	return 0;
}
#endif

int import_single_range(int rw, void __user *buf, size_t len,
		 struct iovec *iov, struct iov_iter *i)
{
	if (len > MAX_RW_COUNT)
		len = MAX_RW_COUNT;
	if (unlikely(!access_ok(!rw, buf, len)))
		return -EFAULT;

	iov->iov_base = buf;
	iov->iov_len = len;
	iov_iter_init(i, rw, iov, 1, len);
	return 0;
}
EXPORT_SYMBOL(import_single_range);

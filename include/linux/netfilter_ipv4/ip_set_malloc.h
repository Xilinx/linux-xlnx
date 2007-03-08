#ifndef _IP_SET_MALLOC_H
#define _IP_SET_MALLOC_H

#ifdef __KERNEL__

/* Memory allocation and deallocation */
static size_t max_malloc_size = 0;

static inline void init_max_malloc_size(void)
{
#define CACHE(x) max_malloc_size = x;
#include <linux/kmalloc_sizes.h>
#undef CACHE
}

static inline void * ip_set_malloc(size_t bytes)
{
	if (bytes > max_malloc_size)
		return vmalloc(bytes);
	else
		return kmalloc(bytes, GFP_KERNEL);
}

static inline void ip_set_free(void * data, size_t bytes)
{
	if (bytes > max_malloc_size)
		vfree(data);
	else
		kfree(data);
}

struct harray {
	size_t max_elements;
	void *arrays[0];
};

static inline void * 
harray_malloc(size_t hashsize, size_t typesize, int flags)
{
	struct harray *harray;
	size_t max_elements, size, i, j;

	if (!max_malloc_size)
		init_max_malloc_size();

	if (typesize > max_malloc_size)
		return NULL;

	max_elements = max_malloc_size/typesize;
	size = hashsize/max_elements;
	if (hashsize % max_elements)
		size++;
	
	/* Last pointer signals end of arrays */
	harray = kmalloc(sizeof(struct harray) + (size + 1) * sizeof(void *),
			 flags);

	if (!harray)
		return NULL;
	
	for (i = 0; i < size - 1; i++) {
		harray->arrays[i] = kmalloc(max_elements * typesize, flags);
		if (!harray->arrays[i])
			goto undo;
		memset(harray->arrays[i], 0, max_elements * typesize);
	}
	harray->arrays[i] = kmalloc((hashsize - i * max_elements) * typesize, 
				    flags);
	if (!harray->arrays[i])
		goto undo;
	memset(harray->arrays[i], 0, (hashsize - i * max_elements) * typesize);

	harray->max_elements = max_elements;
	harray->arrays[size] = NULL;
	
	return (void *)harray;

    undo:
    	for (j = 0; j < i; j++) {
    		kfree(harray->arrays[j]);
    	}
    	kfree(harray);
    	return NULL;
}

static inline void harray_free(void *h)
{
	struct harray *harray = (struct harray *) h;
	size_t i;
	
    	for (i = 0; harray->arrays[i] != NULL; i++)
    		kfree(harray->arrays[i]);
    	kfree(harray);
}

static inline void harray_flush(void *h, size_t hashsize, size_t typesize)
{
	struct harray *harray = (struct harray *) h;
	size_t i;
	
    	for (i = 0; harray->arrays[i+1] != NULL; i++)
		memset(harray->arrays[i], 0, harray->max_elements * typesize);
	memset(harray->arrays[i], 0, 
	       (hashsize - i * harray->max_elements) * typesize);
}

#define HARRAY_ELEM(h, type, which)				\
({								\
	struct harray *__h = (struct harray *)(h);		\
	((type)((__h)->arrays[(which)/(__h)->max_elements])	\
		+ (which)%(__h)->max_elements);			\
})

#endif				/* __KERNEL__ */

#endif /*_IP_SET_MALLOC_H*/

#ifndef  __LZMA_WRAPPER_H__
#define  __LZMA_WRAPPER_H__


#define LZMA_OK		0
#define LZMA_ERROR	-1
#define LZMA_TOO_BIG	-2

int lzma_inflate(unsigned char *source, int s_len, unsigned char *dest, int *d_len);
int lzma_init(unsigned char *data, int size);
int lzma_workspace_size(void);

#endif /*__LZMA_WRAPPER_H__*/

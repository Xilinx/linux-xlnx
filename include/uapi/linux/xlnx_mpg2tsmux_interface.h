/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

/*
 * Xilinx mpeg2 transport stream muxer ioctl calls
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Author:	Venkateshwar Rao G <venkateshwar.rao.gannava@xilinx.com>
 */

#ifndef __XLNX_MPG2TSMUX_INTERFACE_H__
#define __XLNX_MPG2TSMUX_INTERFACE_H__

#include <linux/types.h>
#include <linux/ioctl.h>

/**
 * enum ts_mux_command - command for stream context
 * @CREATE_TS_MISC: create misc
 * @CREATE_TS_VIDEO_KEYFRAME: create video key frame
 * @CREATE_TS_VIDEO_NON_KEYFRAME: create non key frame
 * @CREATE_TS_AUDIO: create audio
 * @WRITE_PAT: write pat
 * @WRITE_PMT: write pmt
 * @WRITE_SI: write si
 * @INVALID: invalid
 */
enum ts_mux_command {
	CREATE_TS_MISC = 0,
	CREATE_TS_VIDEO_KEYFRAME,
	CREATE_TS_VIDEO_NON_KEYFRAME,
	CREATE_TS_AUDIO,
	WRITE_PAT,
	WRITE_PMT,
	WRITE_SI,
	INVALID
};

/**
 * struct stream_context_in - struct to enqueue a stream context descriptor
 * @command: stream context type
 * @stream_id: stream identification number
 * @extended_stream_id: extended stream id
 * @is_pcr_stream: flag for pcr stream
 * @is_valid_pts: flag for valid pts
 * @is_valid_dts: flag for valid dts
 * @is_dmabuf: flag to set if external src buffer is DMA allocated
 * @pid: packet id number
 * @size_data_in: size in bytes of input buffer
 * @pts: presentation time stamp
 * @dts: display time stamp
 * @srcbuf_id: source buffer id after mmap
 * @insert_pcr: flag for inserting pcr in stream context
 * @pcr_extension: pcr extension number
 * @pcr_base: pcr base number
 */
struct stream_context_in {
	enum ts_mux_command command;
	__u8 stream_id;
	__u8 extended_stream_id;
	int is_pcr_stream;
	int is_valid_pts;
	int is_valid_dts;
	int is_dmabuf;
	__u16 pid;
	__u64 size_data_in;
	__u64 pts;
	__u64 dts;
	__u32 srcbuf_id;
	int insert_pcr;
	__u16 pcr_extension;
	__u64 pcr_base;
};

/**
 * struct mux_context_in - struct to enqueue a mux context descriptor
 * @is_dmabuf: flag to set if external src buffer is DMA allocated
 * @dstbuf_id: destination buffer id after mmap
 * @dmabuf_size: size in bytes of output buffer
 */
struct muxer_context_in {
	int is_dmabuf;
	__u32 dstbuf_id;
	__u32 dmabuf_size;
};

/**
 * enum xlnx_tsmux_status - ip status
 * @MPG2MUX_BUSY: device busy
 * @MPG2MUX_READY: device ready
 * @MPG2MUX_ERROR: error state
 */
enum xlnx_tsmux_status {
	MPG2MUX_BUSY = 0,
	MPG2MUX_READY,
	MPG2MUX_ERROR
};

/**
 * struct strc_bufs_info - struct to specify bufs requirement
 * @num_buf: number of buffers
 * @buf_size: size of each buffer
 */
struct strc_bufs_info {
	__u32 num_buf;
	__u32 buf_size;
};

/**
 * struct strc_out_buf - struct to get output buffer info
 * @buf_id: buf id into which output is written
 * @buf_write: output bytes written in buf
 */
struct out_buffer {
	__u32 buf_id;
	__u32 buf_write;
};

/**
 * enum strmtbl_cnxt - streamid table operation
 * @NO_UPDATE: no table update
 * @ADD_TO_TBL: add the entry to table
 * @DEL_FR_TBL: delete the entry from table
 */
enum strmtbl_cnxt {
	NO_UPDATE = 0,
	ADD_TO_TBL,
	DEL_FR_TBL,
};

/**
 * struct strm_tbl_info - struct to enqueue/dequeue streamid in table
 * @strmtbl_ctxt: enqueue/dequeue stream id
 * @pid: stream id
 */
struct strc_strminfo {
	enum strmtbl_cnxt strmtbl_ctxt;
	__u16 pid;
};

/**
 * enum xlnx_tsmux_dma_dir - dma direction
 * @DMA_TO_MPG2MUX: memory to device
 * @DMA_FROM_MPG2MUX: device to memory
 */
enum xlnx_tsmux_dma_dir {
	DMA_TO_MPG2MUX = 1,
	DMA_FROM_MPG2MUX,
};

/**
 * enum xlnx_tsmux_dmabuf_flags - dma buffer handling
 * @DMABUF_ERROR: buffer error
 * @DMABUF_CONTIG: contig buffer
 * @DMABUF_NON_CONTIG: non contigs buffer
 * @DMABUF_ATTACHED: buffer attached
 */
enum xlnx_tsmux_dmabuf_flags {
	DMABUF_ERROR = 1,
	DMABUF_CONTIG = 2,
	DMABUF_NON_CONTIG = 4,
	DMABUF_ATTACHED = 8,
};

/**
 * struct xlnx_tsmux_dmabuf_info - struct to verify dma buf before enque
 * @buf_fd: file descriptor
 * @dir: direction of the dma buffer
 * @flags: flags returned by the driver
 */
struct xlnx_tsmux_dmabuf_info {
	int buf_fd;
	enum xlnx_tsmux_dma_dir dir;
	enum xlnx_tsmux_dmabuf_flags flags;
};

/* MPG2MUX IOCTL CALL LIST */

#define MPG2MUX_MAGIC 'M'

/**
 * MPG2MUX_INBUFALLOC - src buffer allocation
 */
#define MPG2MUX_INBUFALLOC _IOWR(MPG2MUX_MAGIC, 1, struct strc_bufs_info *)

/**
 * MPG2MUX_INBUFDEALLOC - deallocates the all src buffers
 */
#define MPG2MUX_INBUFDEALLOC _IO(MPG2MUX_MAGIC, 2)

/**
 * MPG2MUX_OUTBUFALLOC - allocates DMA able memory for dst
 */
#define MPG2MUX_OUTBUFALLOC _IOWR(MPG2MUX_MAGIC, 3, struct strc_bufs_info *)

/**
 * MPG2MUX_OUTBUFDEALLOC - deallocates the all dst buffers allocated
 */
#define MPG2MUX_OUTBUFDEALLOC _IO(MPG2MUX_MAGIC, 4)

/**
 * MPG2MUX_STBLALLOC - allocates DMA able memory for streamid table
 */
#define MPG2MUX_STBLALLOC _IOW(MPG2MUX_MAGIC, 5, unsigned short *)

/**
 * MPG2MUX_STBLDEALLOC - deallocates streamid table memory
 */
#define MPG2MUX_STBLDEALLOC _IO(MPG2MUX_MAGIC, 6)

/**
 * MPG2MUX_TBLUPDATE - enqueue or dequeue in streamid table
 */
#define MPG2MUX_TBLUPDATE _IOW(MPG2MUX_MAGIC, 7, struct strc_strminfo *)

/**
 * MPG2MUX_SETSTRM - enqueue a stream descriptor in stream context
 *		linked list along with src buf address
 */
#define MPG2MUX_SETSTRM _IOW(MPG2MUX_MAGIC, 8, struct stream_context_in *)

/**
 * MPG2MUX_START - starts muxer IP after configuring stream
 *		and mux context registers
 */
#define MPG2MUX_START _IO(MPG2MUX_MAGIC, 9)

/**
 * MPG2MUX_STOP - stops the muxer IP
 */
#define MPG2MUX_STOP _IO(MPG2MUX_MAGIC, 10)

/**
 * MPG2MUX_STATUS - command to get the status of IP
 */
#define MPG2MUX_STATUS _IOR(MPG2MUX_MAGIC, 11, unsigned short *)

/**
 * MPG2MUX_GETOUTBUF - get the output buffer id with size of output data
 */
#define MPG2MUX_GETOUTBUF _IOW(MPG2MUX_MAGIC, 12, struct out_buffer *)

/**
 * MPG2MUX_SETMUX - enqueue a mux descriptor with dst buf address
 */
#define MPG2MUX_SETMUX _IOW(MPG2MUX_MAGIC, 13, struct muxer_context_in *)

/**
 * MPG2MUX_VRFY_DMABUF - status of a given dma buffer fd
 */
#define MPG2MUX_VDBUF _IOWR(MPG2MUX_MAGIC, 14, struct xlnx_tsmux_dmabuf_info *)

#endif

.. -*- coding: utf-8; mode: rst -*-

.. _buffer:

*******
Buffers
*******

A buffer contains data exchanged by application and driver using one of
the Streaming I/O methods. In the multi-planar API, the data is held in
planes, while the buffer structure acts as a container for the planes.
Only pointers to buffers (planes) are exchanged, the data itself is not
copied. These pointers, together with meta-information like timestamps
or field parity, are stored in a struct :c:type:`v4l2_buffer`,
argument to the :ref:`VIDIOC_QUERYBUF`,
:ref:`VIDIOC_QBUF` and
:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl. In the multi-planar API,
some plane-specific members of struct :c:type:`v4l2_buffer`,
such as pointers and sizes for each plane, are stored in struct
struct :c:type:`v4l2_plane` instead. In that case, struct
struct :c:type:`v4l2_buffer` contains an array of plane structures.

Dequeued video buffers come with timestamps. The driver decides at which
part of the frame and with which clock the timestamp is taken. Please
see flags in the masks ``V4L2_BUF_FLAG_TIMESTAMP_MASK`` and
``V4L2_BUF_FLAG_TSTAMP_SRC_MASK`` in :ref:`buffer-flags`. These flags
are always valid and constant across all buffers during the whole video
stream. Changes in these flags may take place as a side effect of
:ref:`VIDIOC_S_INPUT <VIDIOC_G_INPUT>` or
:ref:`VIDIOC_S_OUTPUT <VIDIOC_G_OUTPUT>` however. The
``V4L2_BUF_FLAG_TIMESTAMP_COPY`` timestamp type which is used by e.g. on
mem-to-mem devices is an exception to the rule: the timestamp source
flags are copied from the OUTPUT video buffer to the CAPTURE video
buffer.


.. c:type:: v4l2_buffer

struct v4l2_buffer
==================

.. tabularcolumns:: |p{2.8cm}|p{2.5cm}|p{1.3cm}|p{10.5cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_buffer
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 2 1 10

    * - __u32
      - ``index``
      -
      - Number of the buffer, set by the application except when calling
	:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>`, then it is set by the
	driver. This field can range from zero to the number of buffers
	allocated with the :ref:`VIDIOC_REQBUFS` ioctl
	(struct :c:type:`v4l2_requestbuffers`
	``count``), plus any buffers allocated with
	:ref:`VIDIOC_CREATE_BUFS` minus one.
    * - __u32
      - ``type``
      -
      - Type of the buffer, same as struct
	:c:type:`v4l2_format` ``type`` or struct
	:c:type:`v4l2_requestbuffers` ``type``, set
	by the application. See :c:type:`v4l2_buf_type`
    * - __u32
      - ``bytesused``
      -
      - The number of bytes occupied by the data in the buffer. It depends
	on the negotiated data format and may change with each buffer for
	compressed variable size data like JPEG images. Drivers must set
	this field when ``type`` refers to a capture stream, applications
	when it refers to an output stream. If the application sets this
	to 0 for an output stream, then ``bytesused`` will be set to the
	size of the buffer (see the ``length`` field of this struct) by
	the driver. For multiplanar formats this field is ignored and the
	``planes`` pointer is used instead.
    * - __u32
      - ``flags``
      -
      - Flags set by the application or driver, see :ref:`buffer-flags`.
    * - __u32
      - ``field``
      -
      - Indicates the field order of the image in the buffer, see
	:c:type:`v4l2_field`. This field is not used when the buffer
	contains VBI data. Drivers must set it when ``type`` refers to a
	capture stream, applications when it refers to an output stream.
    * - struct timeval
      - ``timestamp``
      -
      - For capture streams this is time when the first data byte was
	captured, as returned by the :c:func:`clock_gettime()` function
	for the relevant clock id; see ``V4L2_BUF_FLAG_TIMESTAMP_*`` in
	:ref:`buffer-flags`. For output streams the driver stores the
	time at which the last data byte was actually sent out in the
	``timestamp`` field. This permits applications to monitor the
	drift between the video and system clock. For output streams that
	use ``V4L2_BUF_FLAG_TIMESTAMP_COPY`` the application has to fill
	in the timestamp which will be copied by the driver to the capture
	stream.
    * - struct :c:type:`v4l2_timecode`
      - ``timecode``
      -
      - When ``type`` is ``V4L2_BUF_TYPE_VIDEO_CAPTURE`` and the
	``V4L2_BUF_FLAG_TIMECODE`` flag is set in ``flags``, this
	structure contains a frame timecode. In
	:c:type:`V4L2_FIELD_ALTERNATE <v4l2_field>` mode the top and
	bottom field contain the same timecode. Timecodes are intended to
	help video editing and are typically recorded on video tapes, but
	also embedded in compressed formats like MPEG. This field is
	independent of the ``timestamp`` and ``sequence`` fields.
    * - __u32
      - ``sequence``
      -
      - Set by the driver, counting the frames (not fields!) in sequence.
	This field is set for both input and output devices.
    * - :cspan:`3`

	In :c:type:`V4L2_FIELD_ALTERNATE <v4l2_field>` mode the top and
	bottom field have the same sequence number. The count starts at
	zero and includes dropped or repeated frames. A dropped frame was
	received by an input device but could not be stored due to lack of
	free buffer space. A repeated frame was displayed again by an
	output device because the application did not pass new data in
	time.

	.. note::

	   This may count the frames received e.g. over USB, without
	   taking into account the frames dropped by the remote hardware due
	   to limited compression throughput or bus bandwidth. These devices
	   identify by not enumerating any video standards, see
	   :ref:`standard`.

    * - __u32
      - ``memory``
      -
      - This field must be set by applications and/or drivers in
	accordance with the selected I/O method. See :c:type:`v4l2_memory`
    * - union
      - ``m``
    * -
      - __u32
      - ``offset``
      - For the single-planar API and when ``memory`` is
	``V4L2_MEMORY_MMAP`` this is the offset of the buffer from the
	start of the device memory. The value is returned by the driver
	and apart of serving as parameter to the
	:ref:`mmap() <func-mmap>` function not useful for applications.
	See :ref:`mmap` for details
    * -
      - unsigned long
      - ``userptr``
      - For the single-planar API and when ``memory`` is
	``V4L2_MEMORY_USERPTR`` this is a pointer to the buffer (casted to
	unsigned long type) in virtual memory, set by the application. See
	:ref:`userp` for details.
    * -
      - struct v4l2_plane
      - ``*planes``
      - When using the multi-planar API, contains a userspace pointer to
	an array of struct :c:type:`v4l2_plane`. The size of
	the array should be put in the ``length`` field of this
	struct :c:type:`v4l2_buffer` structure.
    * -
      - int
      - ``fd``
      - For the single-plane API and when ``memory`` is
	``V4L2_MEMORY_DMABUF`` this is the file descriptor associated with
	a DMABUF buffer.
    * - __u32
      - ``length``
      -
      - Size of the buffer (not the payload) in bytes for the
	single-planar API. This is set by the driver based on the calls to
	:ref:`VIDIOC_REQBUFS` and/or
	:ref:`VIDIOC_CREATE_BUFS`. For the
	multi-planar API the application sets this to the number of
	elements in the ``planes`` array. The driver will fill in the
	actual number of valid elements in that array.
    * - __u32
      - ``reserved2``
      -
      - A place holder for future extensions. Drivers and applications
	must set this to 0.
    * - __u32
      - ``reserved``
      -
      - A place holder for future extensions. Drivers and applications
	must set this to 0.



.. c:type:: v4l2_plane

struct v4l2_plane
=================

.. tabularcolumns:: |p{3.5cm}|p{3.5cm}|p{3.5cm}|p{7.0cm}|

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 1 2

    * - __u32
      - ``bytesused``
      -
      - The number of bytes occupied by data in the plane (its payload).
	Drivers must set this field when ``type`` refers to a capture
	stream, applications when it refers to an output stream. If the
	application sets this to 0 for an output stream, then
	``bytesused`` will be set to the size of the plane (see the
	``length`` field of this struct) by the driver.

	.. note::

	   Note that the actual image data starts at ``data_offset``
	   which may not be 0.
    * - __u32
      - ``length``
      -
      - Size in bytes of the plane (not its payload). This is set by the
	driver based on the calls to
	:ref:`VIDIOC_REQBUFS` and/or
	:ref:`VIDIOC_CREATE_BUFS`.
    * - union
      - ``m``
      -
      -
    * -
      - __u32
      - ``mem_offset``
      - When the memory type in the containing struct
	:c:type:`v4l2_buffer` is ``V4L2_MEMORY_MMAP``, this
	is the value that should be passed to :ref:`mmap() <func-mmap>`,
	similar to the ``offset`` field in struct
	:c:type:`v4l2_buffer`.
    * -
      - unsigned long
      - ``userptr``
      - When the memory type in the containing struct
	:c:type:`v4l2_buffer` is ``V4L2_MEMORY_USERPTR``,
	this is a userspace pointer to the memory allocated for this plane
	by an application.
    * -
      - int
      - ``fd``
      - When the memory type in the containing struct
	:c:type:`v4l2_buffer` is ``V4L2_MEMORY_DMABUF``,
	this is a file descriptor associated with a DMABUF buffer, similar
	to the ``fd`` field in struct :c:type:`v4l2_buffer`.
    * - __u32
      - ``data_offset``
      -
      - Offset in bytes to video data in the plane. Drivers must set this
	field when ``type`` refers to a capture stream, applications when
	it refers to an output stream.

	.. note::

	   That data_offset is included  in ``bytesused``. So the
	   size of the image in the plane is ``bytesused``-``data_offset``
	   at offset ``data_offset`` from the start of the plane.
    * - __u32
      - ``reserved[11]``
      -
      - Reserved for future use. Should be zeroed by drivers and
	applications.



.. c:type:: v4l2_buf_type

enum v4l2_buf_type
==================

.. cssclass:: longtable

.. tabularcolumns:: |p{7.2cm}|p{0.6cm}|p{9.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       4 1 9

    * - ``V4L2_BUF_TYPE_VIDEO_CAPTURE``
      - 1
      - Buffer of a single-planar video capture stream, see
	:ref:`capture`.
    * - ``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE``
      - 9
      - Buffer of a multi-planar video capture stream, see
	:ref:`capture`.
    * - ``V4L2_BUF_TYPE_VIDEO_OUTPUT``
      - 2
      - Buffer of a single-planar video output stream, see
	:ref:`output`.
    * - ``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE``
      - 10
      - Buffer of a multi-planar video output stream, see :ref:`output`.
    * - ``V4L2_BUF_TYPE_VIDEO_OVERLAY``
      - 3
      - Buffer for video overlay, see :ref:`overlay`.
    * - ``V4L2_BUF_TYPE_VBI_CAPTURE``
      - 4
      - Buffer of a raw VBI capture stream, see :ref:`raw-vbi`.
    * - ``V4L2_BUF_TYPE_VBI_OUTPUT``
      - 5
      - Buffer of a raw VBI output stream, see :ref:`raw-vbi`.
    * - ``V4L2_BUF_TYPE_SLICED_VBI_CAPTURE``
      - 6
      - Buffer of a sliced VBI capture stream, see :ref:`sliced`.
    * - ``V4L2_BUF_TYPE_SLICED_VBI_OUTPUT``
      - 7
      - Buffer of a sliced VBI output stream, see :ref:`sliced`.
    * - ``V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY``
      - 8
      - Buffer for video output overlay (OSD), see :ref:`osd`.
    * - ``V4L2_BUF_TYPE_SDR_CAPTURE``
      - 11
      - Buffer for Software Defined Radio (SDR) capture stream, see
	:ref:`sdr`.
    * - ``V4L2_BUF_TYPE_SDR_OUTPUT``
      - 12
      - Buffer for Software Defined Radio (SDR) output stream, see
	:ref:`sdr`.



.. _buffer-flags:

Buffer Flags
============

.. tabularcolumns:: |p{7.0cm}|p{2.2cm}|p{8.3cm}|

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * .. _`V4L2-BUF-FLAG-MAPPED`:

      - ``V4L2_BUF_FLAG_MAPPED``
      - 0x00000001
      - The buffer resides in device memory and has been mapped into the
	application's address space, see :ref:`mmap` for details.
	Drivers set or clear this flag when the
	:ref:`VIDIOC_QUERYBUF`,
	:ref:`VIDIOC_QBUF` or
	:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl is called. Set by the
	driver.
    * .. _`V4L2-BUF-FLAG-QUEUED`:

      - ``V4L2_BUF_FLAG_QUEUED``
      - 0x00000002
      - Internally drivers maintain two buffer queues, an incoming and
	outgoing queue. When this flag is set, the buffer is currently on
	the incoming queue. It automatically moves to the outgoing queue
	after the buffer has been filled (capture devices) or displayed
	(output devices). Drivers set or clear this flag when the
	``VIDIOC_QUERYBUF`` ioctl is called. After (successful) calling
	the ``VIDIOC_QBUF``\ ioctl it is always set and after
	``VIDIOC_DQBUF`` always cleared.
    * .. _`V4L2-BUF-FLAG-DONE`:

      - ``V4L2_BUF_FLAG_DONE``
      - 0x00000004
      - When this flag is set, the buffer is currently on the outgoing
	queue, ready to be dequeued from the driver. Drivers set or clear
	this flag when the ``VIDIOC_QUERYBUF`` ioctl is called. After
	calling the ``VIDIOC_QBUF`` or ``VIDIOC_DQBUF`` it is always
	cleared. Of course a buffer cannot be on both queues at the same
	time, the ``V4L2_BUF_FLAG_QUEUED`` and ``V4L2_BUF_FLAG_DONE`` flag
	are mutually exclusive. They can be both cleared however, then the
	buffer is in "dequeued" state, in the application domain so to
	say.
    * .. _`V4L2-BUF-FLAG-ERROR`:

      - ``V4L2_BUF_FLAG_ERROR``
      - 0x00000040
      - When this flag is set, the buffer has been dequeued successfully,
	although the data might have been corrupted. This is recoverable,
	streaming may continue as normal and the buffer may be reused
	normally. Drivers set this flag when the ``VIDIOC_DQBUF`` ioctl is
	called.
    * .. _`V4L2-BUF-FLAG-KEYFRAME`:

      - ``V4L2_BUF_FLAG_KEYFRAME``
      - 0x00000008
      - Drivers set or clear this flag when calling the ``VIDIOC_DQBUF``
	ioctl. It may be set by video capture devices when the buffer
	contains a compressed image which is a key frame (or field), i. e.
	can be decompressed on its own. Also known as an I-frame.
	Applications can set this bit when ``type`` refers to an output
	stream.
    * .. _`V4L2-BUF-FLAG-PFRAME`:

      - ``V4L2_BUF_FLAG_PFRAME``
      - 0x00000010
      - Similar to ``V4L2_BUF_FLAG_KEYFRAME`` this flags predicted frames
	or fields which contain only differences to a previous key frame.
	Applications can set this bit when ``type`` refers to an output
	stream.
    * .. _`V4L2-BUF-FLAG-BFRAME`:

      - ``V4L2_BUF_FLAG_BFRAME``
      - 0x00000020
      - Similar to ``V4L2_BUF_FLAG_KEYFRAME`` this flags a bi-directional
	predicted frame or field which contains only the differences
	between the current frame and both the preceding and following key
	frames to specify its content. Applications can set this bit when
	``type`` refers to an output stream.
    * .. _`V4L2-BUF-FLAG-TIMECODE`:

      - ``V4L2_BUF_FLAG_TIMECODE``
      - 0x00000100
      - The ``timecode`` field is valid. Drivers set or clear this flag
	when the ``VIDIOC_DQBUF`` ioctl is called. Applications can set
	this bit and the corresponding ``timecode`` structure when
	``type`` refers to an output stream.
    * .. _`V4L2-BUF-FLAG-PREPARED`:

      - ``V4L2_BUF_FLAG_PREPARED``
      - 0x00000400
      - The buffer has been prepared for I/O and can be queued by the
	application. Drivers set or clear this flag when the
	:ref:`VIDIOC_QUERYBUF`,
	:ref:`VIDIOC_PREPARE_BUF <VIDIOC_QBUF>`,
	:ref:`VIDIOC_QBUF` or
	:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl is called.
    * .. _`V4L2-BUF-FLAG-NO-CACHE-INVALIDATE`:

      - ``V4L2_BUF_FLAG_NO_CACHE_INVALIDATE``
      - 0x00000800
      - Caches do not have to be invalidated for this buffer. Typically
	applications shall use this flag if the data captured in the
	buffer is not going to be touched by the CPU, instead the buffer
	will, probably, be passed on to a DMA-capable hardware unit for
	further processing or output.
    * .. _`V4L2-BUF-FLAG-NO-CACHE-CLEAN`:

      - ``V4L2_BUF_FLAG_NO_CACHE_CLEAN``
      - 0x00001000
      - Caches do not have to be cleaned for this buffer. Typically
	applications shall use this flag for output buffers if the data in
	this buffer has not been created by the CPU but by some
	DMA-capable unit, in which case caches have not been used.
    * .. _`V4L2-BUF-FLAG-LAST`:

      - ``V4L2_BUF_FLAG_LAST``
      - 0x00100000
      - Last buffer produced by the hardware. mem2mem codec drivers set
	this flag on the capture queue for the last buffer when the
	:ref:`VIDIOC_QUERYBUF` or
	:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl is called. Due to
	hardware limitations, the last buffer may be empty. In this case
	the driver will set the ``bytesused`` field to 0, regardless of
	the format. Any Any subsequent call to the
	:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl will not block anymore,
	but return an ``EPIPE`` error code.
    * .. _`V4L2-BUF-FLAG-TIMESTAMP-MASK`:

      - ``V4L2_BUF_FLAG_TIMESTAMP_MASK``
      - 0x0000e000
      - Mask for timestamp types below. To test the timestamp type, mask
	out bits not belonging to timestamp type by performing a logical
	and operation with buffer flags and timestamp mask.
    * .. _`V4L2-BUF-FLAG-TIMESTAMP-UNKNOWN`:

      - ``V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN``
      - 0x00000000
      - Unknown timestamp type. This type is used by drivers before Linux
	3.9 and may be either monotonic (see below) or realtime (wall
	clock). Monotonic clock has been favoured in embedded systems
	whereas most of the drivers use the realtime clock. Either kinds
	of timestamps are available in user space via
	:c:func:`clock_gettime` using clock IDs ``CLOCK_MONOTONIC``
	and ``CLOCK_REALTIME``, respectively.
    * .. _`V4L2-BUF-FLAG-TIMESTAMP-MONOTONIC`:

      - ``V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC``
      - 0x00002000
      - The buffer timestamp has been taken from the ``CLOCK_MONOTONIC``
	clock. To access the same clock outside V4L2, use
	:c:func:`clock_gettime`.
    * .. _`V4L2-BUF-FLAG-TIMESTAMP-COPY`:

      - ``V4L2_BUF_FLAG_TIMESTAMP_COPY``
      - 0x00004000
      - The CAPTURE buffer timestamp has been taken from the corresponding
	OUTPUT buffer. This flag applies only to mem2mem devices.
    * .. _`V4L2-BUF-FLAG-TSTAMP-SRC-MASK`:

      - ``V4L2_BUF_FLAG_TSTAMP_SRC_MASK``
      - 0x00070000
      - Mask for timestamp sources below. The timestamp source defines the
	point of time the timestamp is taken in relation to the frame.
	Logical 'and' operation between the ``flags`` field and
	``V4L2_BUF_FLAG_TSTAMP_SRC_MASK`` produces the value of the
	timestamp source. Applications must set the timestamp source when
	``type`` refers to an output stream and
	``V4L2_BUF_FLAG_TIMESTAMP_COPY`` is set.
    * .. _`V4L2-BUF-FLAG-TSTAMP-SRC-EOF`:

      - ``V4L2_BUF_FLAG_TSTAMP_SRC_EOF``
      - 0x00000000
      - End Of Frame. The buffer timestamp has been taken when the last
	pixel of the frame has been received or the last pixel of the
	frame has been transmitted. In practice, software generated
	timestamps will typically be read from the clock a small amount of
	time after the last pixel has been received or transmitten,
	depending on the system and other activity in it.
    * .. _`V4L2-BUF-FLAG-TSTAMP-SRC-SOE`:

      - ``V4L2_BUF_FLAG_TSTAMP_SRC_SOE``
      - 0x00010000
      - Start Of Exposure. The buffer timestamp has been taken when the
	exposure of the frame has begun. This is only valid for the
	``V4L2_BUF_TYPE_VIDEO_CAPTURE`` buffer type.



.. c:type:: v4l2_memory

enum v4l2_memory
================

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_MEMORY_MMAP``
      - 1
      - The buffer is used for :ref:`memory mapping <mmap>` I/O.
    * - ``V4L2_MEMORY_USERPTR``
      - 2
      - The buffer is used for :ref:`user pointer <userp>` I/O.
    * - ``V4L2_MEMORY_OVERLAY``
      - 3
      - [to do]
    * - ``V4L2_MEMORY_DMABUF``
      - 4
      - The buffer is used for :ref:`DMA shared buffer <dmabuf>` I/O.



Timecodes
=========

The struct :c:type:`v4l2_timecode` structure is designed to hold a
:ref:`smpte12m` or similar timecode. (struct
struct :c:type:`timeval` timestamps are stored in struct
:c:type:`v4l2_buffer` field ``timestamp``.)


.. c:type:: v4l2_timecode

struct v4l2_timecode
--------------------

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``type``
      - Frame rate the timecodes are based on, see :ref:`timecode-type`.
    * - __u32
      - ``flags``
      - Timecode flags, see :ref:`timecode-flags`.
    * - __u8
      - ``frames``
      - Frame count, 0 ... 23/24/29/49/59, depending on the type of
	timecode.
    * - __u8
      - ``seconds``
      - Seconds count, 0 ... 59. This is a binary, not BCD number.
    * - __u8
      - ``minutes``
      - Minutes count, 0 ... 59. This is a binary, not BCD number.
    * - __u8
      - ``hours``
      - Hours count, 0 ... 29. This is a binary, not BCD number.
    * - __u8
      - ``userbits``\ [4]
      - The "user group" bits from the timecode.



.. _timecode-type:

Timecode Types
--------------

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_TC_TYPE_24FPS``
      - 1
      - 24 frames per second, i. e. film.
    * - ``V4L2_TC_TYPE_25FPS``
      - 2
      - 25 frames per second, i. e. PAL or SECAM video.
    * - ``V4L2_TC_TYPE_30FPS``
      - 3
      - 30 frames per second, i. e. NTSC video.
    * - ``V4L2_TC_TYPE_50FPS``
      - 4
      -
    * - ``V4L2_TC_TYPE_60FPS``
      - 5
      -



.. _timecode-flags:

Timecode Flags
--------------

.. tabularcolumns:: |p{6.6cm}|p{1.4cm}|p{9.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_TC_FLAG_DROPFRAME``
      - 0x0001
      - Indicates "drop frame" semantics for counting frames in 29.97 fps
	material. When set, frame numbers 0 and 1 at the start of each
	minute, except minutes 0, 10, 20, 30, 40, 50 are omitted from the
	count.
    * - ``V4L2_TC_FLAG_COLORFRAME``
      - 0x0002
      - The "color frame" flag.
    * - ``V4L2_TC_USERBITS_field``
      - 0x000C
      - Field mask for the "binary group flags".
    * - ``V4L2_TC_USERBITS_USERDEFINED``
      - 0x0000
      - Unspecified format.
    * - ``V4L2_TC_USERBITS_8BITCHARS``
      - 0x0008
      - 8-bit ISO characters.

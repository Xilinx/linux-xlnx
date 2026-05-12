.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _npu-hardware-formats:

********************************************
NPU(Neural Processing Unit) Hardware Formats
********************************************

HCWNC4 and HCWNC8 are channel-blocked memory layouts optimized for NPU processing.
They group pixel channels into fixed blocks (4 or 8), which enables efficient aligned memory
transactions and vectorized operations.

Dimension Definitions
=====================

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Dimension
     - Description
   * - H
     - Height (rows)
   * - W
     - Width (columns)
   * - N
     - Batch size
   * - C
     - Channel
   * - CB
     - Channel block size (4 or 8)

Format variants and FourCC tags
===============================

The ``_X_Y`` suffix means that each channel block contains ``X`` total slots,
of which ``Y`` slots are active data channels. Some ``*_4_3`` and ``*_3_3``
formats share the same byte pattern in the layout tables, or match a related
``*_4_4`` pattern with extra slots zeroed. However, the ``_4_3``, ``_4_4``,
and ``_3_3`` suffixes still mark distinct ``V4L2_PIX_FMT_*`` codes that inform
user space and the NPU stack both how many logical channels are active and how
padding is handled. Always use the FourCC reported by the driver for the buffer.

The four-character tags (for example ``'HC48'``, ``'HC43'``, ``'HC44'``, ``'HB86'``)
are the values defined in ``videodev2.h``. The digits encode the registered layout
and do not always mirror the ``_X_Y`` suffix literally.

.. _npu-channel-byte-layout:

Channel byte layout in tables
=============================

For **8 bits per channel**, each channel occupies one memory byte. The ``Byte 0``,
``Byte 1``, ... columns list those channel bytes in increasing address order (R,
then G, then B, then A or padding as in each table). There is no multi-byte
endianness within a channel.

For **BF16, FP16, and FP32** channels, ``Byte 0``, ``Byte 1``, and so on list
storage in increasing memory address **within that channel**. The
lowest-address byte holds the least significant byte of the scalar (for example
bits 7-0 of a 16-bit channel appear before bits 15-8; FP32 uses 7-0 through
31-24 in the same LSB-first order). This is **little-endian byte order within each
channel** (network byte order for the channel word), and is **independent of host
CPU endianness**.

User space on little-endian application processors must not assume that a raw
buffer matches the in-memory layout of a native ``__bf16``,
``__fp16``, or ``float`` without an explicit unpack or byte reorder step.

HCWNC4 Format
=============

HCWNC4 (Height-Channel-Width-Batch-Channel4) is a channel-blocked format that groups
channels into blocks of 4 for vectorized processing.

**Structure:** [H, C/CB, W, N, CB] (where CB = 4; see "Channel block size (CB)" in the table above)

**Channel Focused Memory Organization:**

- Channel dimension is the primary target for the transformation.

- Channels are grouped into blocks of 4, with padding added if the channel count is not
  divisible by 4.


HCWNC8 Format
=============

HCWNC8 (Height-Channel-Width-Batch-Channel8) extends the channel-blocked concept of HCWNC4 to
groups of 8 channels for higher throughput, especially on NPU architectures with 8-wide vector
processing.

**Structure:** [H, C/CB, W, N, CB] (where CB = 8; see "Channel block size (CB)" in the table above)

**Memory Organization:**

- The channel dimension is blocked in groups of 8.
- Each block stores 8 channels from the same spatial location (height and width) for a batch
  instance.
- If the number of channels (C) is not divisible by 8, padding is added in the last block.

**Padding Requirements:**

- Images or tensors with fewer than 8 channels must be zero-padded (or with a specified pad value)
  in the channel dimension to complete the block.
- For example, a 3-channel RGB image would be represented as "RGB00000" (channels R, G, B followed
  by five zero-pads, making a total of 8).

Floating-point bit layout reference
===================================

The HCWNC4 and  HCWNC8 float layouts on this page use
the same BF16, FP16, and FP32 channel encodings.

Below is the bit layout for each float-related data type, where S is the sign
bit, E is the exponent, and M is the mantissa (see also
:doc:`pixfmt-float-bitfield-tables`):

.. include:: pixfmt-float-bitfield-tables.rst
   :start-after: FLOAT_BITFIELD_LIST_TABLES_BEGIN

HCWNC4 8 Bits Per Channel
=========================

These formats store RGB (and optional alpha or padding) data in 4 consecutive bytes per pixel.
Channel order in memory is R, G, B, (A or 0), with each channel represented by 8 bits (1 byte).
Table byte columns follow :ref:`npu-channel-byte-layout`.

.. raw:: latex

    \small

.. flat-table:: RGB/A Formats With 8 Bits Per Component
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0 in memory
      - Byte 1
      - Byte 2
      - Byte 3
    * .. _V4L2-PIX-FMT-HCWNC4-8-4-4:

      - ``V4L2_PIX_FMT_HCWNC4_8_4_4``
      - 'HC48'

      - R\ :sub:`7-0`
      - G\ :sub:`7-0`
      - B\ :sub:`7-0`
      - A\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-8-4-3:

      - ``V4L2_PIX_FMT_HCWNC4_8_4_3``
      - 'HC43'

      - R\ :sub:`7-0`
      - G\ :sub:`7-0`
      - B\ :sub:`7-0`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-8-3-3:

      - ``V4L2_PIX_FMT_HCWNC4_8_3_3``
      - 'HC44'

      - R\ :sub:`7-0`
      - G\ :sub:`7-0`
      - B\ :sub:`7-0`
      - 0\ :sub:`7-0`


HCWNC4 16 Bits Per Channel
==========================

These formats store RGB (and optional alpha or padding) data in 4 consecutive 16-bit words per
pixel.
Channels appear in memory in the order R, G, B, (A or 0), using either bfloat16 or fp16.
Per-channel byte layout is :ref:`npu-channel-byte-layout` (tables list bit fields accordingly).

.. raw:: latex

    \small

.. flat-table:: RGB/A Formats With 16 Bits Per Component
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0 in memory
      - Byte 1
      - Byte 2
      - Byte 3
      - Byte 4
      - Byte 5
      - Byte 6
      - Byte 7

    * .. _V4L2-PIX-FMT-HCWNC4-BF16-4-4:

      - ``V4L2_PIX_FMT_HCWNC4_BF16_4_4``
      - 'HB46'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - A\ :sub:`15-8`
      - A\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-FP16-4-4:

      - ``V4L2_PIX_FMT_HCWNC4_FP16_4_4``
      - 'HF46'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - A\ :sub:`15-8`
      - A\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-BF16-4-3:

      - ``V4L2_PIX_FMT_HCWNC4_BF16_4_3``
      - 'HB43'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-FP16-4-3:

      - ``V4L2_PIX_FMT_HCWNC4_FP16_4_3``
      - 'HF43'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-BF16-3-3:

      - ``V4L2_PIX_FMT_HCWNC4_BF16_3_3``
      - 'HB44'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-FP16-3-3:

      - ``V4L2_PIX_FMT_HCWNC4_FP16_3_3``
      - 'HF44'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

HCWNC4 32 Bits Per Channel
==========================

These formats store RGB (and optional alpha or padding) data in 4 consecutive FP32 words per pixel.
Channel order in memory is R, G, B, (A or 0), with each channel represented by 32 bits (4 bytes).
Per-channel byte layout is :ref:`npu-channel-byte-layout`.

.. raw:: latex

    \small

.. flat-table:: RGB/A Formats With 32 Bits Per Component
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0 in memory
      - Byte 1
      - Byte 2
      - Byte 3
      - Byte 4
      - Byte 5
      - Byte 6
      - Byte 7
      - Byte 8
      - Byte 9
      - Byte 10
      - Byte 11
      - Byte 12
      - Byte 13
      - Byte 14
      - Byte 15

    * .. _V4L2-PIX-FMT-HCWNC4-FP32-4-4:

      - ``V4L2_PIX_FMT_HCWNC4_FP32_4_4``
      - 'H432'
      - R\ :sub:`31-24`
      - R\ :sub:`23-16`
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`31-24`
      - G\ :sub:`23-16`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`31-24`
      - B\ :sub:`23-16`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - A\ :sub:`31-24`
      - A\ :sub:`23-16`
      - A\ :sub:`15-8`
      - A\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-FP32-4-3:

      - ``V4L2_PIX_FMT_HCWNC4_FP32_4_3``
      - 'H433'
      - R\ :sub:`31-24`
      - R\ :sub:`23-16`
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`31-24`
      - G\ :sub:`23-16`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`31-24`
      - B\ :sub:`23-16`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC4-FP32-3-3:

      - ``V4L2_PIX_FMT_HCWNC4_FP32_3_3``
      - 'H434'
      - R\ :sub:`31-24`
      - R\ :sub:`23-16`
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`31-24`
      - G\ :sub:`23-16`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`31-24`
      - B\ :sub:`23-16`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

HCWNC8 8 Bits Per Channel
=========================

These formats store RGB (and optional alpha or padding) data in 8 consecutive bytes per
pixel.
Channel order in memory is R, G, B, (A or 0), with each channel represented by 8 bits (1 byte).
Table byte columns follow :ref:`npu-channel-byte-layout`.

.. raw:: latex

    \small

.. flat-table:: RGB/A Formats With 8 Bits Per Component
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0 in memory
      - Byte 1
      - Byte 2
      - Byte 3
      - Byte 4
      - Byte 5
      - Byte 6
      - Byte 7

    * .. _V4L2-PIX-FMT-HCWNC8-8-4-4:

      - ``V4L2_PIX_FMT_HCWNC8_8_4_4``
      - 'HC88'

      - R\ :sub:`7-0`
      - G\ :sub:`7-0`
      - B\ :sub:`7-0`
      - A\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-8-4-3:

      - ``V4L2_PIX_FMT_HCWNC8_8_4_3``
      - 'HC83'

      - R\ :sub:`7-0`
      - G\ :sub:`7-0`
      - B\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-8-3-3:

      - ``V4L2_PIX_FMT_HCWNC8_8_3_3``
      - 'HC84'

      - R\ :sub:`7-0`
      - G\ :sub:`7-0`
      - B\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`
      - 0\ :sub:`7-0`


HCWNC8 16 Bits Per Channel
==========================

These formats store RGB (and optional alpha or padding) data in 8 consecutive 16-bit words per
pixel.
Channel order in memory is R, G, B, (A or 0), with each channel represented by 16 bits (2 bytes).
Per-channel byte layout is :ref:`npu-channel-byte-layout`.

.. raw:: latex

    \small

.. flat-table:: RGB/A Formats With 16 Bits Per Component
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0 in memory
      - Byte 1
      - Byte 2
      - Byte 3
      - Byte 4
      - Byte 5
      - Byte 6
      - Byte 7
      - Byte 8
      - Byte 9
      - Byte 10
      - Byte 11
      - Byte 12
      - Byte 13
      - Byte 14
      - Byte 15

    * .. _V4L2-PIX-FMT-HCWNC8-BF16-4-4:

      - ``V4L2_PIX_FMT_HCWNC8_BF16_4_4``
      - 'HB86'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - A\ :sub:`15-8`
      - A\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-FP16-4-4:

      - ``V4L2_PIX_FMT_HCWNC8_FP16_4_4``
      - 'HF86'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - A\ :sub:`15-8`
      - A\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-BF16-4-3:

      - ``V4L2_PIX_FMT_HCWNC8_BF16_4_3``
      - 'HB83'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-FP16-4-3:

      - ``V4L2_PIX_FMT_HCWNC8_FP16_4_3``
      - 'HF83'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-BF16-3-3:

      - ``V4L2_PIX_FMT_HCWNC8_BF16_3_3``
      - 'HB84'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-FP16-3-3:

      - ``V4L2_PIX_FMT_HCWNC8_FP16_3_3``
      - 'HF84'
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

HCWNC8 32 Bits Per Channel
==========================

These formats store RGB (and optional alpha or padding) data in 8 consecutive FP32 words per pixel.
Channel order in memory is R, G, B, (A or 0), with each channel represented by 32 bits (4 bytes).
Per-channel byte layout is :ref:`npu-channel-byte-layout`.

.. raw:: latex

    \small

.. flat-table:: RGB/A Formats With 32 Bits Per Component
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Byte 0 in memory
      - Byte 1
      - Byte 2
      - Byte 3
      - Byte 4
      - Byte 5
      - Byte 6
      - Byte 7
      - Byte 8
      - Byte 9
      - Byte 10
      - Byte 11
      - Byte 12
      - Byte 13
      - Byte 14
      - Byte 15
      - Byte 16
      - Byte 17
      - Byte 18
      - Byte 19
      - Byte 20
      - Byte 21
      - Byte 22
      - Byte 23
      - Byte 24
      - Byte 25
      - Byte 26
      - Byte 27
      - Byte 28
      - Byte 29
      - Byte 30
      - Byte 31

    * .. _V4L2-PIX-FMT-HCWNC8-FP32-4-4:

      - ``V4L2_PIX_FMT_HCWNC8_FP32_4_4``
      - 'H832'
      - R\ :sub:`31-24`
      - R\ :sub:`23-16`
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`31-24`
      - G\ :sub:`23-16`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`31-24`
      - B\ :sub:`23-16`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - A\ :sub:`31-24`
      - A\ :sub:`23-16`
      - A\ :sub:`15-8`
      - A\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-FP32-4-3:

      - ``V4L2_PIX_FMT_HCWNC8_FP32_4_3``
      - 'H833'
      - R\ :sub:`31-24`
      - R\ :sub:`23-16`
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`31-24`
      - G\ :sub:`23-16`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`31-24`
      - B\ :sub:`23-16`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

    * .. _V4L2-PIX-FMT-HCWNC8-FP32-3-3:

      - ``V4L2_PIX_FMT_HCWNC8_FP32_3_3``
      - 'H834'
      - R\ :sub:`31-24`
      - R\ :sub:`23-16`
      - R\ :sub:`15-8`
      - R\ :sub:`7-0`
      - G\ :sub:`31-24`
      - G\ :sub:`23-16`
      - G\ :sub:`15-8`
      - G\ :sub:`7-0`
      - B\ :sub:`31-24`
      - B\ :sub:`23-16`
      - B\ :sub:`15-8`
      - B\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`
      - 0\ :sub:`31-24`
      - 0\ :sub:`23-16`
      - 0\ :sub:`15-8`
      - 0\ :sub:`7-0`

.. raw:: latex

    \normalsize

.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _planar-rgb:

***********************
Planar RGB/RGBA formats
***********************

Planar formats split RGB/RGBA data in separate memory regions.

- Planar RGB formats (such as ``V4L2_PIX_FMT_RGB24P``, ``V4L2_PIX_FMT_RGB_BF48P``,
  ``V4L2_PIX_FMT_RGB_FP48P``, ``V4L2_PIX_FMT_RGB_FP323232P``, and the matching
  ``V4L2_PIX_FMT_RGB24P_4_3``, ``V4L2_PIX_FMT_RGB_BF48P_4_3``,
  ``V4L2_PIX_FMT_RGB_FP48P_4_3``, ``V4L2_PIX_FMT_RGB_FP323232P_4_3``) use three
  planes, one per color channel (blue, green, then red). The ``*_4_3`` variants
  have the same in-memory plane layout as their non-``_4_3`` siblings, but they
  carry a different user-visible contract: the buffer represents a 4-channel
  tensor/image with only 3 active channels, and the missing fourth channel is
  implicitly padding (typically treated as zero by hardware/driver). Applications
  should select the ``*_4_3`` FourCC when the pipeline (driver/hardware/userspace
  stack) needs to distinguish 3-active-in-4-slot buffers from plain 3-channel
  planar RGB, even though the stored planes are identical.
- Planar RGBA formats (``V4L2_PIX_FMT_RGBA32P``, ``V4L2_PIX_FMT_RGBA_BF64P``,
  ``V4L2_PIX_FMT_RGBA_FP64P``, ``V4L2_PIX_FMT_RGBA_FP32323232P``) use four planes
  so the alpha channel is stored separately from the color channels.

Each plane stores components in pixel order, with possible
padding at the end of lines. Every plane has the same stride padding
and line length.

Below is the bit layout for each float-related data type, where S is the sign
bit, E is the exponent, and M is the mantissa (see also
:doc:`pixfmt-float-bitfield-tables`):

.. include:: pixfmt-float-bitfield-tables.rst
   :start-after: FLOAT_BITFIELD_LIST_TABLES_BEGIN

.. _V4L2-PIX-FMT-RGB24P:

V4L2_PIX_FMT_RGB24P
===================

Planar RGB 24 format. Each channel (Red, Green, Blue) is stored in a separate plane,
with 8 bits per channel.
Lines contain the same number of pixels and bytes of the other planes, and the planes
contain the same number of lines as the other planes.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGB24P Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 4:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 8:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 12:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 4:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 8:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 12:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 4:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 8:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 12:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`

.. _V4L2-PIX-FMT-RGB-BF48P:

V4L2_PIX_FMT_RGB_BF48P
======================

Planar 48-bit RGB format with **bfloat16 (BF16)** samples per channel. Each channel
(Red, Green, Blue) is stored in a separate plane, two bytes per pixel per channel.
All planes have the same dimensions; each scan line has the same byte length in
every plane.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGB_BF48P Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 8:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 16:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 24:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 8:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 16:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 24:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 8:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 16:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 24:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`

.. _V4L2-PIX-FMT-RGB-FP48P:

V4L2_PIX_FMT_RGB_FP48P
======================

Planar 48-bit RGB format with **IEEE 754 binary16 (FP16, half-precision)** samples
per channel. Each channel (Red, Green, Blue) is stored in a separate plane, two bytes
per pixel per channel. All planes have the same dimensions; each scan line has the
same byte length in every plane.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGB_FP48P Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 8:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 16:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 24:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 8:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 16:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 24:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 8:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 16:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 24:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`

.. _V4L2-PIX-FMT-RGB-FP323232P:

V4L2_PIX_FMT_RGB_FP323232P
==========================

Planar 96-bit RGB format. Each channel (Red, Green, Blue) is stored in a separate
plane, with 32 bits per channel using IEEE 754 binary32 (FP32, single-precision)
floating point values. All planes have the same dimensions: each line and each plane
contains the same number of pixels and bytes as the others.
This format provides high color precision and is commonly used for high-end image
processing applications.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGB_FP323232P Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 16:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 32:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 48:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 16:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 32:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 48:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 16:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 32:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 48:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`

.. _V4L2-PIX-FMT-RGBA32P:

V4L2_PIX_FMT_RGBA32P
====================

Planar RGBA 32 format.
Each channel (Red, Green, Blue, Alpha) is stored in a separate plane, with 8 bits
per channel.
Each plane contains the same number of pixels and bytes as the other planes and
all planes have the same number of lines.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGBA32P Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 4:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 8:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 12:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 4:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 8:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 12:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 4:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 8:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 12:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`
    * -
    * - start3 + 0:
      - A'\ :sub:`00`
      - A'\ :sub:`01`
      - A'\ :sub:`02`
      - A'\ :sub:`03`
    * - start3 + 4:
      - A'\ :sub:`10`
      - A'\ :sub:`11`
      - A'\ :sub:`12`
      - A'\ :sub:`13`
    * - start3 + 8:
      - A'\ :sub:`20`
      - A'\ :sub:`21`
      - A'\ :sub:`22`
      - A'\ :sub:`23`
    * - start3 + 12:
      - A'\ :sub:`30`
      - A'\ :sub:`31`
      - A'\ :sub:`32`
      - A'\ :sub:`33`

.. _V4L2-PIX-FMT-RGBA-BF64P:

V4L2_PIX_FMT_RGBA_BF64P
=======================

Planar 64-bit RGBA format with **bfloat16 (BF16)** samples per channel. Each channel
(Red, Green, Blue, Alpha) is stored in a separate plane, two bytes per pixel per
channel. All planes have the same dimensions; each scan line has the same byte
length in every plane.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGBA_BF64P Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 8:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 16:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 24:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 8:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 16:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 24:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 8:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 16:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 24:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`
    * -
    * - start3 + 0:
      - A'\ :sub:`00`
      - A'\ :sub:`01`
      - A'\ :sub:`02`
      - A'\ :sub:`03`
    * - start3 + 8:
      - A'\ :sub:`10`
      - A'\ :sub:`11`
      - A'\ :sub:`12`
      - A'\ :sub:`13`
    * - start3 + 16:
      - A'\ :sub:`20`
      - A'\ :sub:`21`
      - A'\ :sub:`22`
      - A'\ :sub:`23`
    * - start3 + 24:
      - A'\ :sub:`30`
      - A'\ :sub:`31`
      - A'\ :sub:`32`
      - A'\ :sub:`33`

.. _V4L2-PIX-FMT-RGBA-FP64P:

V4L2_PIX_FMT_RGBA_FP64P
=======================

Planar 64-bit RGBA format with **IEEE 754 binary16 (FP16, half-precision)** samples
per channel. Each channel (Red, Green, Blue, Alpha) is stored in a separate plane,
two bytes per pixel per channel. All planes have the same dimensions; each scan line
has the same byte length in every plane.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGBA_FP64P Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 8:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 16:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 24:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 8:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 16:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 24:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 8:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 16:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 24:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`
    * -
    * - start3 + 0:
      - A'\ :sub:`00`
      - A'\ :sub:`01`
      - A'\ :sub:`02`
      - A'\ :sub:`03`
    * - start3 + 8:
      - A'\ :sub:`10`
      - A'\ :sub:`11`
      - A'\ :sub:`12`
      - A'\ :sub:`13`
    * - start3 + 16:
      - A'\ :sub:`20`
      - A'\ :sub:`21`
      - A'\ :sub:`22`
      - A'\ :sub:`23`
    * - start3 + 24:
      - A'\ :sub:`30`
      - A'\ :sub:`31`
      - A'\ :sub:`32`
      - A'\ :sub:`33`

.. _V4L2-PIX-FMT-RGBA-FP32323232P:

V4L2_PIX_FMT_RGBA_FP32323232P
=============================

Planar 128-bit RGBA format. Each channel (Red, Green, Blue, Alpha) is stored in a
separate plane, with 32 bits per channel using IEEE 754 binary32 (FP32, single-precision)
floating point values. All planes have the same dimensions: each line and each plane
contains the same number of pixels and bytes as the others.
This format provides high color precision and is commonly used for high-end image
processing applications.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGBA_FP32323232P Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 16:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 32:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 48:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 16:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 32:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 48:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 16:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 32:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 48:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`
    * -
    * - start3 + 0:
      - A'\ :sub:`00`
      - A'\ :sub:`01`
      - A'\ :sub:`02`
      - A'\ :sub:`03`
    * - start3 + 16:
      - A'\ :sub:`10`
      - A'\ :sub:`11`
      - A'\ :sub:`12`
      - A'\ :sub:`13`
    * - start3 + 32:
      - A'\ :sub:`20`
      - A'\ :sub:`21`
      - A'\ :sub:`22`
      - A'\ :sub:`23`
    * - start3 + 48:
      - A'\ :sub:`30`
      - A'\ :sub:`31`
      - A'\ :sub:`32`
      - A'\ :sub:`33`

.. _V4L2-PIX-FMT-RGB24P-4-3:

V4L2_PIX_FMT_RGB24P_4_3
=======================

Planar RGB 24 format with the ``*_4_3`` layout contract (see the introduction).
Each channel (Red, Green, Blue) is stored in a separate plane with 8 bits per channel.
All planes have the same dimensions: each line and each plane contains the same number
of pixels and bytes as the others. This variant is used where drivers or hardware
require the explicit ``_4_3`` contract alongside ``V4L2_PIX_FMT_RGB24P``-style
plane ordering.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGB24P_4_3 Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 4:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 8:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 12:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 4:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 8:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 12:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 4:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 8:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 12:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`

.. _V4L2-PIX-FMT-RGB-BF48P-4-3:

V4L2_PIX_FMT_RGB_BF48P_4_3
==========================

Planar 48-bit RGB format (4x3 packing) with **bfloat16 (BF16)** samples; each channel
(Red, Green, Blue) is in a separate 16-bit plane. All planes and lines have equal
pixels and bytes for consistent layout. Used where planar RGB data in explicit 4x3
organization is needed.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGB_BF48P_4_3 Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 8:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 16:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 24:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 8:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 16:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 24:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 8:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 16:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 24:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`

.. _V4L2-PIX-FMT-RGB-FP48P-4-3:

V4L2_PIX_FMT_RGB_FP48P_4_3
==========================

Planar 48-bit RGB format (4x3 packing) with **IEEE 754 binary16 (FP16, half-precision)**
samples; each channel (Red, Green, Blue) is in a separate 16-bit plane. All planes
and lines have equal pixels and bytes for consistent layout. Used where planar RGB
data in explicit 4x3 organization is needed.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGB_FP48P_4_3 Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 8:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 16:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 24:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 8:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 16:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 24:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 8:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 16:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 24:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`

.. _V4L2-PIX-FMT-RGB-FP323232P-4-3:

V4L2_PIX_FMT_RGB_FP323232P_4_3
==============================

Planar 96-bit RGB format (4x3 packing), with each channel (Red, Green, Blue) in a
separate FP32 plane.
All planes and lines have equal pixels and bytes for consistent layout.
Used where planar RGB data in explicit 4x3 organization is needed.

.. flat-table:: Sample 4x4 V4L2_PIX_FMT_RGB_FP323232P_4_3 Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - R'\ :sub:`00`
      - R'\ :sub:`01`
      - R'\ :sub:`02`
      - R'\ :sub:`03`
    * - start0 + 16:
      - R'\ :sub:`10`
      - R'\ :sub:`11`
      - R'\ :sub:`12`
      - R'\ :sub:`13`
    * - start0 + 32:
      - R'\ :sub:`20`
      - R'\ :sub:`21`
      - R'\ :sub:`22`
      - R'\ :sub:`23`
    * - start0 + 48:
      - R'\ :sub:`30`
      - R'\ :sub:`31`
      - R'\ :sub:`32`
      - R'\ :sub:`33`
    * -
    * - start1 + 0:
      - G'\ :sub:`00`
      - G'\ :sub:`01`
      - G'\ :sub:`02`
      - G'\ :sub:`03`
    * - start1 + 16:
      - G'\ :sub:`10`
      - G'\ :sub:`11`
      - G'\ :sub:`12`
      - G'\ :sub:`13`
    * - start1 + 32:
      - G'\ :sub:`20`
      - G'\ :sub:`21`
      - G'\ :sub:`22`
      - G'\ :sub:`23`
    * - start1 + 48:
      - G'\ :sub:`30`
      - G'\ :sub:`31`
      - G'\ :sub:`32`
      - G'\ :sub:`33`
    * -
    * - start2 + 0:
      - B'\ :sub:`00`
      - B'\ :sub:`01`
      - B'\ :sub:`02`
      - B'\ :sub:`03`
    * - start2 + 16:
      - B'\ :sub:`10`
      - B'\ :sub:`11`
      - B'\ :sub:`12`
      - B'\ :sub:`13`
    * - start2 + 32:
      - B'\ :sub:`20`
      - B'\ :sub:`21`
      - B'\ :sub:`22`
      - B'\ :sub:`23`
    * - start2 + 48:
      - B'\ :sub:`30`
      - B'\ :sub:`31`
      - B'\ :sub:`32`
      - B'\ :sub:`33`

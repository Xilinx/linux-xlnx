.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-mbus-format:

Media Bus Formats
=================

.. c:type:: v4l2_mbus_framefmt

.. tabularcolumns:: |p{2.0cm}|p{4.0cm}|p{11.3cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_mbus_framefmt
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``width``
      - Image width in pixels.
    * - __u32
      - ``height``
      - Image height in pixels. If ``field`` is one of ``V4L2_FIELD_TOP``,
	``V4L2_FIELD_BOTTOM`` or ``V4L2_FIELD_ALTERNATE`` then height
	refers to the number of lines in the field, otherwise it refers to
	the number of lines in the frame (which is twice the field height
	for interlaced formats).
    * - __u32
      - ``code``
      - Format code, from enum
	:ref:`v4l2_mbus_pixelcode <v4l2-mbus-pixelcode>`.
    * - __u32
      - ``field``
      - Field order, from enum :c:type:`v4l2_field`. See
	:ref:`field-order` for details. Zero for metadata mbus codes.
    * - __u32
      - ``colorspace``
      - Image colorspace, from enum :c:type:`v4l2_colorspace`.
        Must be set by the driver for subdevices. If the application sets the
	flag ``V4L2_MBUS_FRAMEFMT_SET_CSC`` then the application can set this
	field on the source pad to request a specific colorspace for the media
	bus data. If the driver cannot handle the requested conversion, it will
	return another supported colorspace. The driver indicates that colorspace
	conversion is supported by setting the flag
	V4L2_SUBDEV_MBUS_CODE_CSC_COLORSPACE in the corresponding struct
	:c:type:`v4l2_subdev_mbus_code_enum` during enumeration.
	See :ref:`v4l2-subdev-mbus-code-flags`. Zero for metadata mbus codes.
    * - union {
      - (anonymous)
    * - __u16
      - ``ycbcr_enc``
      - Y'CbCr encoding, from enum :c:type:`v4l2_ycbcr_encoding`.
        This information supplements the ``colorspace`` and must be set by
	the driver for subdevices, see :ref:`colorspaces`. If the application
	sets the flag ``V4L2_MBUS_FRAMEFMT_SET_CSC`` then the application can set
	this field on a source pad to request a specific Y'CbCr encoding
	for the media bus data. If the driver cannot handle the requested
	conversion, it will return another supported encoding.
	This field is ignored for HSV media bus formats. The driver indicates
	that ycbcr_enc conversion is supported by setting the flag
	V4L2_SUBDEV_MBUS_CODE_CSC_YCBCR_ENC in the corresponding struct
	:c:type:`v4l2_subdev_mbus_code_enum` during enumeration.
	See :ref:`v4l2-subdev-mbus-code-flags`. Zero for metadata mbus codes.
    * - __u16
      - ``hsv_enc``
      - HSV encoding, from enum :c:type:`v4l2_hsv_encoding`.
        This information supplements the ``colorspace`` and must be set by
	the driver for subdevices, see :ref:`colorspaces`. If the application
	sets the flag ``V4L2_MBUS_FRAMEFMT_SET_CSC`` then the application can set
	this field on a source pad to request a specific HSV encoding
	for the media bus data. If the driver cannot handle the requested
	conversion, it will return another supported encoding.
	This field is ignored for Y'CbCr media bus formats. The driver indicates
	that hsv_enc conversion is supported by setting the flag
	V4L2_SUBDEV_MBUS_CODE_CSC_HSV_ENC in the corresponding struct
	:c:type:`v4l2_subdev_mbus_code_enum` during enumeration.
	See :ref:`v4l2-subdev-mbus-code-flags`. Zero for metadata mbus codes.
    * - }
      -
    * - __u16
      - ``quantization``
      - Quantization range, from enum :c:type:`v4l2_quantization`.
        This information supplements the ``colorspace`` and must be set by
	the driver for subdevices, see :ref:`colorspaces`. If the application
	sets the flag ``V4L2_MBUS_FRAMEFMT_SET_CSC`` then the application can set
	this field on a source pad to request a specific quantization
	for the media bus data. If the driver cannot handle the requested
	conversion, it will return another supported quantization.
	The driver indicates that quantization conversion is supported by
	setting the flag V4L2_SUBDEV_MBUS_CODE_CSC_QUANTIZATION in the
	corresponding struct :c:type:`v4l2_subdev_mbus_code_enum`
	during enumeration. See :ref:`v4l2-subdev-mbus-code-flags`. Zero for
	metadata mbus codes.
    * - __u16
      - ``xfer_func``
      - Transfer function, from enum :c:type:`v4l2_xfer_func`.
        This information supplements the ``colorspace`` and must be set by
	the driver for subdevices, see :ref:`colorspaces`. If the application
	sets the flag ``V4L2_MBUS_FRAMEFMT_SET_CSC`` then the application can set
	this field on a source pad to request a specific transfer
	function for the media bus data. If the driver cannot handle the requested
	conversion, it will return another supported transfer function.
	The driver indicates that the transfer function conversion is supported by
	setting the flag V4L2_SUBDEV_MBUS_CODE_CSC_XFER_FUNC in the
	corresponding struct :c:type:`v4l2_subdev_mbus_code_enum`
	during enumeration. See :ref:`v4l2-subdev-mbus-code-flags`. Zero for
	metadata mbus codes.
    * - __u16
      - ``flags``
      - flags See:  :ref:v4l2-mbus-framefmt-flags
    * - __u16
      - ``reserved``\ [10]
      - Reserved for future extensions. Applications and drivers must set
	the array to zero.

.. _v4l2-mbus-framefmt-flags:

.. tabularcolumns:: |p{6.5cm}|p{1.6cm}|p{9.2cm}|

.. flat-table:: v4l2_mbus_framefmt Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * .. _`mbus-framefmt-set-csc`:

      - ``V4L2_MBUS_FRAMEFMT_SET_CSC``
      - 0x0001
      - Set by the application. It is only used for source pads and is
	ignored for sink pads. If set, then request the subdevice to do
	colorspace conversion from the received colorspace to the requested
	colorspace values. If the colorimetry field (``colorspace``, ``xfer_func``,
	``ycbcr_enc``, ``hsv_enc`` or ``quantization``) is set to ``*_DEFAULT``,
	then that colorimetry setting will remain unchanged from what was received.
	So in order to change the quantization, only the ``quantization`` field shall
	be set to non default value (``V4L2_QUANTIZATION_FULL_RANGE`` or
	``V4L2_QUANTIZATION_LIM_RANGE``) and all other colorimetry fields shall
	be set to ``*_DEFAULT``.

	To check which conversions are supported by the hardware for the current
	media bus frame format, see :ref:`v4l2-subdev-mbus-code-flags`.


.. _v4l2-mbus-pixelcode:

Media Bus Pixel Codes
---------------------

The media bus pixel codes describe image formats as flowing over
physical buses (both between separate physical components and inside
SoC devices). This should not be confused with the V4L2 pixel formats
that describe, using four character codes, image formats as stored in
memory.

While there is a relationship between image formats on buses and image
formats in memory (a raw Bayer image won't be magically converted to
JPEG just by storing it to memory), there is no one-to-one
correspondence between them.

The media bus pixel codes document parallel formats. Should the pixel data be
transported over a serial bus, the media bus pixel code that describes a
parallel format that transfers a sample on a single clock cycle is used. For
instance, both MEDIA_BUS_FMT_BGR888_1X24 and MEDIA_BUS_FMT_BGR888_3X8 are used
on parallel busses for transferring an 8 bits per sample BGR data, whereas on
serial busses the data in this format is only referred to using
MEDIA_BUS_FMT_BGR888_1X24. This is because there is effectively only a single
way to transport that format on the serial busses.

Packed RGB Formats
^^^^^^^^^^^^^^^^^^

Those formats transfer pixel data as red, green and blue components. The
format code is made of the following information.

-  The red, green and blue components order code, as encoded in a pixel
   sample. Possible values are RGB and BGR.

-  The number of bits per component, for each component. The values can
   be different for all components. Common values are 555 and 565.

-  The number of bus samples per pixel. Pixels that are wider than the
   bus width must be transferred in multiple samples. Common values are
   1 and 2.

-  The bus width.

-  For formats where the total number of bits per pixel is smaller than
   the number of bus samples per pixel times the bus width, a padding
   value stating if the bytes are padded in their most high order bits
   (PADHI) or low order bits (PADLO). A "C" prefix is used for
   component-wise padding in the most high order bits (CPADHI) or low
   order bits (CPADLO) of each separate component.

-  For formats where the number of bus samples per pixel is larger than
   1, an endianness value stating if the pixel is transferred MSB first
   (BE) or LSB first (LE).

For instance, a format where pixels are encoded as 5-bits red, 5-bits
green and 5-bit blue values padded on the high bit, transferred as 2
8-bit samples per pixel with the most significant bits (padding, red and
half of the green value) transferred first will be named
``MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE``.

The following tables list existing packed RGB formats.

.. HACK: ideally, we would be using adjustbox here. However, Sphinx
.. is a very bad behaviored guy: if the table has more than 30 cols,
.. it switches to long table, and there's no way to override it.


.. tabularcolumns:: |p{5.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-rgb:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: RGB formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-RGB444-1X12:

      - MEDIA_BUS_FMT_RGB444_1X12
      - 0x1016
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB444-2X8-PADHI-BE:

      - MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE
      - 0x1001
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - 0
      - 0
      - 0
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB444-2X8-PADHI-LE:

      - MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE
      - 0x1002
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - 0
      - 0
      - 0
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB555-2X8-PADHI-BE:

      - MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE
      - 0x1003
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`4`
      - g\ :sub:`3`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB555-2X8-PADHI-LE:

      - MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE
      - 0x1004
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`4`
      - g\ :sub:`3`
    * .. _MEDIA-BUS-FMT-RGB565-1X16:

      - MEDIA_BUS_FMT_RGB565_1X16
      - 0x1017
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-BGR565-2X8-BE:

      - MEDIA_BUS_FMT_BGR565_2X8_BE
      - 0x1005
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-BGR565-2X8-LE:

      - MEDIA_BUS_FMT_BGR565_2X8_LE
      - 0x1006
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
    * .. _MEDIA-BUS-FMT-RGB565-2X8-BE:

      - MEDIA_BUS_FMT_RGB565_2X8_BE
      - 0x1007
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB565-2X8-LE:

      - MEDIA_BUS_FMT_RGB565_2X8_LE
      - 0x1008
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
    * .. _MEDIA-BUS-FMT-RGB666-1X18:

      - MEDIA_BUS_FMT_RGB666_1X18
      - 0x1009
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB666-2X9-BE:

      - MEDIA_BUS_FMT_RGB666_2X9_BE
      - 0x1025
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-BGR666-1X18:

      - MEDIA_BUS_FMT_BGR666_1X18
      - 0x1023
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RBG888-1X24:

      - MEDIA_BUS_FMT_RBG888_1X24
      - 0x100e
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB666-1X24_CPADHI:

      - MEDIA_BUS_FMT_RGB666_1X24_CPADHI
      - 0x1015
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - 0
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - 0
      - 0
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - 0
      - 0
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-BGR666-1X24_CPADHI:

      - MEDIA_BUS_FMT_BGR666_1X24_CPADHI
      - 0x1024
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - 0
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - 0
      - 0
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - 0
      - 0
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB565-1X24_CPADHI:

      - MEDIA_BUS_FMT_RGB565_1X24_CPADHI
      - 0x1022
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - 0
      - 0
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - 0
      - 0
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - 0
      - 0
      - 0
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-BGR888-1X24:

      - MEDIA_BUS_FMT_BGR888_1X24
      - 0x1013
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-BGR888-3X8:

      - MEDIA_BUS_FMT_BGR888_3X8
      - 0x101b
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-GBR888-1X24:

      - MEDIA_BUS_FMT_GBR888_1X24
      - 0x1014
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB888-1X24:

      - MEDIA_BUS_FMT_RGB888_1X24
      - 0x100a
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB888-2X12-BE:

      - MEDIA_BUS_FMT_RGB888_2X12_BE
      - 0x100b
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB888-2X12-LE:

      - MEDIA_BUS_FMT_RGB888_2X12_LE
      - 0x100c
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
    * .. _MEDIA-BUS-FMT-RGB888-3X8:

      - MEDIA_BUS_FMT_RGB888_3X8
      - 0x101c
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB666-1X30-CPADLO:

      - MEDIA_BUS_FMT_RGB666_1X30-CPADLO
      - 0x101e
      -
      -
      -
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
    * .. _MEDIA-BUS-FMT-RGB888-1X30-CPADLO:

      - MEDIA_BUS_FMT_RGB888_1X30-CPADLO
      - 0x101f
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - 0
      - 0
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - 0
      - 0
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - 0
      - 0
    * .. _MEDIA-BUS-FMT-ARGB888-1X32:

      - MEDIA_BUS_FMT_ARGB888_1X32
      - 0x100d
      -
      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB888-1X32-PADHI:

      - MEDIA_BUS_FMT_RGB888_1X32_PADHI
      - 0x100f
      -
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB101010-1X30:

      - MEDIA_BUS_FMT_RGB101010_1X30
      - 0x1018
      -
      -
      -
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RBG101010-1X30:

      - MEDIA_BUS_FMT_RBG101010_1X30
      - 0x1100
      -
      - 0
      - 0
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`

.. raw:: latex

    \endgroup


The following table list existing packed 36bit wide RGB formats.

.. tabularcolumns:: |p{4.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-rgb-36:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: 36bit RGB formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`35` Data organization
    * -
      -
      - Bit
      - 35
      - 34
      - 33
      - 32
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-RGB666-1X36-CPADLO:

      - MEDIA_BUS_FMT_RGB666_1X36_CPADLO
      - 0x1020
      -
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
    * .. _MEDIA-BUS-FMT-RGB888-1X36-CPADLO:

      - MEDIA_BUS_FMT_RGB888_1X36_CPADLO
      - 0x1021
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
    * .. _MEDIA-BUS-FMT-RGB121212-1X36:

      - MEDIA_BUS_FMT_RGB121212_1X36
      - 0x1019
      -
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RBG121212-1X36:

      - MEDIA_BUS_FMT_RBG121212_1X36
      - 0x1101
      -
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`

.. raw:: latex

    \endgroup


The following tables list existing and new packed 48-bit-wide RGB media-bus formats.

MEDIA_BUS_FMT_RGB_BF161616_1X48
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This format closely resembles MEDIA_BUS_FMT_RGB161616_1X48, with the difference being that each
16-bit channel stores a Brain Floating Point (BF16) value instead of a plain integer. The
ordering of the color channels places R (red) in the most significant 16 bits, G (green) in the
middle 16 bits, and B (blue) in the least significant 16 bits of each 48-bit pixel sample, thus
spreading the channels sequentially from most to least significant bytes.

MEDIA_BUS_FMT_RGB_FP161616_1X48
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This format follows the structure of MEDIA_BUS_FMT_RGB161616_1X48, but instead of representing
channels as 16-bit integers, each R, G, and B channel has a 16-bit floating-point (FP16, also
referred to as half-precision) value. For each 48-bit pixel, the R channel uses the most
significant 16 bits, G uses the next 16 bits, and B uses the least significant 16 bits. The layout
for FP16, BF16, and FP32 floating point types, where S is sign, E is exponent, and M is mantissa,
is shown below:

.. include:: pixfmt-float-bitfield-tables.rst
   :start-after: FLOAT_BITFIELD_LIST_TABLES_BEGIN

.. tabularcolumns:: |p{4.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-rgb-48:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: 48bit RGB formats
    :header-rows:  3
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 47
      - 46
      - 45
      - 44
      - 43
      - 42
      - 41
      - 40
      - 39
      - 38
      - 37
      - 36
      - 35
      - 34
      - 33
      - 32
    * -
      -
      -
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-RGB161616-1X48:

      - MEDIA_BUS_FMT_RGB161616_1X48
      - 0x101a
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`15`
      - r\ :sub:`14`
      - r\ :sub:`13`
      - r\ :sub:`12`
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * -
      -
      -
      - g\ :sub:`15`
      - g\ :sub:`14`
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`15`
      - b\ :sub:`14`
      - b\ :sub:`13`
      - b\ :sub:`12`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RBG161616-1X48:

      - MEDIA_BUS_FMT_RBG161616_1X48
      - 0x1102
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`15`
      - r\ :sub:`14`
      - r\ :sub:`13`
      - r\ :sub:`12`
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * -
      -
      -
      - b\ :sub:`15`
      - b\ :sub:`14`
      - b\ :sub:`13`
      - b\ :sub:`12`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - g\ :sub:`15`
      - g\ :sub:`14`
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB-BF161616-1X48:

      - MEDIA_BUS_FMT_RGB_BF161616_1X48
      - 0x102e
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`BF15`
      - r\ :sub:`BF14`
      - r\ :sub:`BF13`
      - r\ :sub:`BF12`
      - r\ :sub:`BF11`
      - r\ :sub:`BF10`
      - r\ :sub:`BF9`
      - r\ :sub:`BF8`
      - r\ :sub:`BF7`
      - r\ :sub:`BF6`
      - r\ :sub:`BF5`
      - r\ :sub:`BF4`
      - r\ :sub:`BF3`
      - r\ :sub:`BF2`
      - r\ :sub:`BF1`
      - r\ :sub:`BF0`
    * -
      -
      -
      - g\ :sub:`BF15`
      - g\ :sub:`BF14`
      - g\ :sub:`BF13`
      - g\ :sub:`BF12`
      - g\ :sub:`BF11`
      - g\ :sub:`BF10`
      - g\ :sub:`BF9`
      - g\ :sub:`BF8`
      - g\ :sub:`BF7`
      - g\ :sub:`BF6`
      - g\ :sub:`BF5`
      - g\ :sub:`BF4`
      - g\ :sub:`BF3`
      - g\ :sub:`BF2`
      - g\ :sub:`BF1`
      - g\ :sub:`BF0`
      - b\ :sub:`BF15`
      - b\ :sub:`BF14`
      - b\ :sub:`BF13`
      - b\ :sub:`BF12`
      - b\ :sub:`BF11`
      - b\ :sub:`BF10`
      - b\ :sub:`BF9`
      - b\ :sub:`BF8`
      - b\ :sub:`BF7`
      - b\ :sub:`BF6`
      - b\ :sub:`BF5`
      - b\ :sub:`BF4`
      - b\ :sub:`BF3`
      - b\ :sub:`BF2`
      - b\ :sub:`BF1`
      - b\ :sub:`BF0`
    * .. _MEDIA-BUS-FMT-RGB-FP161616-1X48:

      - MEDIA_BUS_FMT_RGB_FP161616_1X48
      - 0x102f
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`FP15`
      - r\ :sub:`FP14`
      - r\ :sub:`FP13`
      - r\ :sub:`FP12`
      - r\ :sub:`FP11`
      - r\ :sub:`FP10`
      - r\ :sub:`FP9`
      - r\ :sub:`FP8`
      - r\ :sub:`FP7`
      - r\ :sub:`FP6`
      - r\ :sub:`FP5`
      - r\ :sub:`FP4`
      - r\ :sub:`FP3`
      - r\ :sub:`FP2`
      - r\ :sub:`FP1`
      - r\ :sub:`FP0`
    * -
      -
      -
      - g\ :sub:`FP15`
      - g\ :sub:`FP14`
      - g\ :sub:`FP13`
      - g\ :sub:`FP12`
      - g\ :sub:`FP11`
      - g\ :sub:`FP10`
      - g\ :sub:`FP9`
      - g\ :sub:`FP8`
      - g\ :sub:`FP7`
      - g\ :sub:`FP6`
      - g\ :sub:`FP5`
      - g\ :sub:`FP4`
      - g\ :sub:`FP3`
      - g\ :sub:`FP2`
      - g\ :sub:`FP1`
      - g\ :sub:`FP0`
      - b\ :sub:`FP15`
      - b\ :sub:`FP14`
      - b\ :sub:`FP13`
      - b\ :sub:`FP12`
      - b\ :sub:`FP11`
      - b\ :sub:`FP10`
      - b\ :sub:`FP9`
      - b\ :sub:`FP8`
      - b\ :sub:`FP7`
      - b\ :sub:`FP6`
      - b\ :sub:`FP5`
      - b\ :sub:`FP4`
      - b\ :sub:`FP3`
      - b\ :sub:`FP2`
      - b\ :sub:`FP1`
      - b\ :sub:`FP0`

.. raw:: latex

    \endgroup

The following table lists packed 96-bit-wide RGB formats.

MEDIA_BUS_FMT_RGB_FP323232_1X96
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This represents a FP32, 3-channel pixel arrangement. Each 32 bits represent a floating-point
(FP32) value.
With each channel being 32 bits wide, a single full pixel requires 96 bits in total.
The R, G, and B channels are arranged from the most significant to the least significant bits; R
occupies the most significant bytes, G occupies the middle bytes, and B occupies the least
significant bytes.

.. tabularcolumns:: |p{4.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-rgb-96:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: 96bit RGB formats
    :header-rows:  3
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 95
      - 94
      - 93
      - 92
      - 91
      - 90
      - 89
      - 88
      - 87
      - 86
      - 85
      - 84
      - 83
      - 82
      - 81
      - 80
      - 79
      - 78
      - 77
      - 76
      - 75
      - 74
      - 73
      - 72
      - 71
      - 70
      - 69
      - 68
      - 67
      - 66
      - 65
      - 64
    * -
      -
      -
      - 63
      - 62
      - 61
      - 60
      - 59
      - 58
      - 57
      - 56
      - 55
      - 54
      - 53
      - 52
      - 51
      - 50
      - 49
      - 48
      - 47
      - 46
      - 45
      - 44
      - 43
      - 42
      - 41
      - 40
      - 39
      - 38
      - 37
      - 36
      - 35
      - 34
      - 33
      - 32
    * -
      -
      -
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-RGB-FP323232-1X96:

      - MEDIA_BUS_FMT_RGB_FP323232_1X96
      - 0x1030
      -
      - r\ :sub:`31`
      - r\ :sub:`30`
      - r\ :sub:`29`
      - r\ :sub:`28`
      - r\ :sub:`27`
      - r\ :sub:`26`
      - r\ :sub:`25`
      - r\ :sub:`24`
      - r\ :sub:`23`
      - r\ :sub:`22`
      - r\ :sub:`21`
      - r\ :sub:`20`
      - r\ :sub:`19`
      - r\ :sub:`18`
      - r\ :sub:`17`
      - r\ :sub:`16`
      - r\ :sub:`15`
      - r\ :sub:`14`
      - r\ :sub:`13`
      - r\ :sub:`12`
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * -
      -
      -
      - g\ :sub:`31`
      - g\ :sub:`30`
      - g\ :sub:`29`
      - g\ :sub:`28`
      - g\ :sub:`27`
      - g\ :sub:`26`
      - g\ :sub:`25`
      - g\ :sub:`24`
      - g\ :sub:`23`
      - g\ :sub:`22`
      - g\ :sub:`21`
      - g\ :sub:`20`
      - g\ :sub:`19`
      - g\ :sub:`18`
      - g\ :sub:`17`
      - g\ :sub:`16`
      - g\ :sub:`15`
      - g\ :sub:`14`
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      -
      - b\ :sub:`31`
      - b\ :sub:`30`
      - b\ :sub:`29`
      - b\ :sub:`28`
      - b\ :sub:`27`
      - b\ :sub:`26`
      - b\ :sub:`25`
      - b\ :sub:`24`
      - b\ :sub:`23`
      - b\ :sub:`22`
      - b\ :sub:`21`
      - b\ :sub:`20`
      - b\ :sub:`19`
      - b\ :sub:`18`
      - b\ :sub:`17`
      - b\ :sub:`16`
      - b\ :sub:`15`
      - b\ :sub:`14`
      - b\ :sub:`13`
      - b\ :sub:`12`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`

.. raw:: latex

    \endgroup

The following table lists packed 32-bit-wide RGBA formats.

MEDIA_BUS_FMT_RGBA8888_1X32
~~~~~~~~~~~~~~~~~~~~~~~~~~~

This represents an 8-bit, 4-channel pixel arrangement. Each 8 bits represent a plain integer value.
With each channel being 8 bits wide, a single full pixel requires 32 bits in total.
The R, G, B, and A channels are arranged from the most significant to the least significant bits; R
occupies the most significant byte, G occupies the next byte, B occupies the next byte, and A
occupies the least significant byte.

.. tabularcolumns:: |p{4.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-rgba-32:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: 32bit RGBA formats
    :header-rows:  3
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-RGBA8888-1X32:

      - MEDIA_BUS_FMT_RGBA8888_1X32
      - 0x1031
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`

.. raw:: latex

    \endgroup

The following tables list and new packed 64-bit-wide RGBA media-bus formats.

MEDIA_BUS_FMT_RGBA_BF16161616_1X64
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This format packs four 16-bit BF16 channels per 64-bit sample in R-G-B-A order.
The R, G, B, and A channels are arranged from the most significant to the least significant bits; R
occupies the most significant bytes, followed by G, then B, with A in the least significant bytes.

MEDIA_BUS_FMT_RGBA_FP16161616_1X64
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This format packs four 16-bit FP16 channels per 64-bit sample in R-G-B-A order.
The R, G, B, and A channels are arranged from the most significant to the least significant bits; R
occupies the most significant bytes, followed by G, then B, with A in the least significant bytes.

.. tabularcolumns:: |p{4.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-rgba-64:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: 64bit RGBA formats
    :header-rows:  3
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 63
      - 62
      - 61
      - 60
      - 59
      - 58
      - 57
      - 56
      - 55
      - 54
      - 53
      - 52
      - 51
      - 50
      - 49
      - 48
      - 47
      - 46
      - 45
      - 44
      - 43
      - 42
      - 41
      - 40
      - 39
      - 38
      - 37
      - 36
      - 35
      - 34
      - 33
      - 32
    * -
      -
      -
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-RGBA-BF16161616-1X64:

      - MEDIA_BUS_FMT_RGBA_BF16161616_1X64
      - 0x1032
      -
      - r\ :sub:`15`
      - r\ :sub:`14`
      - r\ :sub:`13`
      - r\ :sub:`12`
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`15`
      - g\ :sub:`14`
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      -
      - b\ :sub:`15`
      - b\ :sub:`14`
      - b\ :sub:`13`
      - b\ :sub:`12`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - a\ :sub:`15`
      - a\ :sub:`14`
      - a\ :sub:`13`
      - a\ :sub:`12`
      - a\ :sub:`11`
      - a\ :sub:`10`
      - a\ :sub:`9`
      - a\ :sub:`8`
      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGBA-FP16161616-1X64:

      - MEDIA_BUS_FMT_RGBA_FP16161616_1X64
      - 0x1033
      -
      - r\ :sub:`15`
      - r\ :sub:`14`
      - r\ :sub:`13`
      - r\ :sub:`12`
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
      - g\ :sub:`15`
      - g\ :sub:`14`
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      -
      - b\ :sub:`15`
      - b\ :sub:`14`
      - b\ :sub:`13`
      - b\ :sub:`12`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
      - a\ :sub:`15`
      - a\ :sub:`14`
      - a\ :sub:`13`
      - a\ :sub:`12`
      - a\ :sub:`11`
      - a\ :sub:`10`
      - a\ :sub:`9`
      - a\ :sub:`8`
      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`

.. raw:: latex

    \endgroup

The following table lists packed 128-bit-wide RGBA formats.

MEDIA_BUS_FMT_RGBA_FP32323232_1X128
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This represents a FP32, 4-channel pixel arrangement. Each 32 bits represent a floating-point
(FP32) value.
With each channel being 32 bits wide, a single full pixel requires 128 bits in total.
The R, G, B, and A channels are arranged from the most significant to the least significant bits; R
occupies the most significant bytes, followed by G, then B, with A in the least significant bytes.

.. tabularcolumns:: |p{4.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-rgba-128:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: 128bit RGBA formats
    :header-rows:  3
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 127
      - 126
      - 125
      - 124
      - 123
      - 122
      - 121
      - 120
      - 119
      - 118
      - 117
      - 116
      - 115
      - 114
      - 113
      - 112
      - 111
      - 110
      - 109
      - 108
      - 107
      - 106
      - 105
      - 104
      - 103
      - 102
      - 101
      - 100
      - 99
      - 98
      - 97
      - 96
    * -
      -
      -
      - 95
      - 94
      - 93
      - 92
      - 91
      - 90
      - 89
      - 88
      - 87
      - 86
      - 85
      - 84
      - 83
      - 82
      - 81
      - 80
      - 79
      - 78
      - 77
      - 76
      - 75
      - 74
      - 73
      - 72
      - 71
      - 70
      - 69
      - 68
      - 67
      - 66
      - 65
      - 64
    * -
      -
      -
      - 63
      - 62
      - 61
      - 60
      - 59
      - 58
      - 57
      - 56
      - 55
      - 54
      - 53
      - 52
      - 51
      - 50
      - 49
      - 48
      - 47
      - 46
      - 45
      - 44
      - 43
      - 42
      - 41
      - 40
      - 39
      - 38
      - 37
      - 36
      - 35
      - 34
      - 33
      - 32
    * -
      -
      -
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-RGBA-FP32323232-1X128:

      - MEDIA_BUS_FMT_RGBA_FP32323232_1X128
      - 0x1034
      -
      - r\ :sub:`31`
      - r\ :sub:`30`
      - r\ :sub:`29`
      - r\ :sub:`28`
      - r\ :sub:`27`
      - r\ :sub:`26`
      - r\ :sub:`25`
      - r\ :sub:`24`
      - r\ :sub:`23`
      - r\ :sub:`22`
      - r\ :sub:`21`
      - r\ :sub:`20`
      - r\ :sub:`19`
      - r\ :sub:`18`
      - r\ :sub:`17`
      - r\ :sub:`16`
      - r\ :sub:`15`
      - r\ :sub:`14`
      - r\ :sub:`13`
      - r\ :sub:`12`
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * -
      -
      -
      - g\ :sub:`31`
      - g\ :sub:`30`
      - g\ :sub:`29`
      - g\ :sub:`28`
      - g\ :sub:`27`
      - g\ :sub:`26`
      - g\ :sub:`25`
      - g\ :sub:`24`
      - g\ :sub:`23`
      - g\ :sub:`22`
      - g\ :sub:`21`
      - g\ :sub:`20`
      - g\ :sub:`19`
      - g\ :sub:`18`
      - g\ :sub:`17`
      - g\ :sub:`16`
      - g\ :sub:`15`
      - g\ :sub:`14`
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      -
      - b\ :sub:`31`
      - b\ :sub:`30`
      - b\ :sub:`29`
      - b\ :sub:`28`
      - b\ :sub:`27`
      - b\ :sub:`26`
      - b\ :sub:`25`
      - b\ :sub:`24`
      - b\ :sub:`23`
      - b\ :sub:`22`
      - b\ :sub:`21`
      - b\ :sub:`20`
      - b\ :sub:`19`
      - b\ :sub:`18`
      - b\ :sub:`17`
      - b\ :sub:`16`
      - b\ :sub:`15`
      - b\ :sub:`14`
      - b\ :sub:`13`
      - b\ :sub:`12`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * -
      -
      -
      - a\ :sub:`31`
      - a\ :sub:`30`
      - a\ :sub:`29`
      - a\ :sub:`28`
      - a\ :sub:`27`
      - a\ :sub:`26`
      - a\ :sub:`25`
      - a\ :sub:`24`
      - a\ :sub:`23`
      - a\ :sub:`22`
      - a\ :sub:`21`
      - a\ :sub:`20`
      - a\ :sub:`19`
      - a\ :sub:`18`
      - a\ :sub:`17`
      - a\ :sub:`16`
      - a\ :sub:`15`
      - a\ :sub:`14`
      - a\ :sub:`13`
      - a\ :sub:`12`
      - a\ :sub:`11`
      - a\ :sub:`10`
      - a\ :sub:`9`
      - a\ :sub:`8`
      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`

.. raw:: latex

    \endgroup

The following table lists packed 16-bit-wide Y formats.

MEDIA_BUS_FMT_Y_BF16_1X16
~~~~~~~~~~~~~~~~~~~~~~~~~

This represents a 16-bit, single-channel pixel arrangement. Each 16 bits represent a Brain-Float (BF16) value.
The Y channel occupies the entire 16 bits.

MEDIA_BUS_FMT_Y_FP16_1X16
~~~~~~~~~~~~~~~~~~~~~~~~~

This represents a 16-bit, single-channel pixel arrangement. Each 16 bits represent a 16-bit floating-point (FP16 or half) value.
The Y channel occupies the entire 16 bits.

.. tabularcolumns:: |p{4.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-y-16:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: 16bit Y formats
    :header-rows:  3
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-Y-BF16-1X16:

      - MEDIA_BUS_FMT_Y_BF16_1X16
      - 0x202f
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`BF15`
      - y\ :sub:`BF14`
      - y\ :sub:`BF13`
      - y\ :sub:`BF12`
      - y\ :sub:`BF11`
      - y\ :sub:`BF10`
      - y\ :sub:`BF9`
      - y\ :sub:`BF8`
      - y\ :sub:`BF7`
      - y\ :sub:`BF6`
      - y\ :sub:`BF5`
      - y\ :sub:`BF4`
      - y\ :sub:`BF3`
      - y\ :sub:`BF2`
      - y\ :sub:`BF1`
      - y\ :sub:`BF0`
    * .. _MEDIA-BUS-FMT-Y-FP16-1X16:

      - MEDIA_BUS_FMT_Y_FP16_1X16
      - 0x2030
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`FP15`
      - y\ :sub:`FP14`
      - y\ :sub:`FP13`
      - y\ :sub:`FP12`
      - y\ :sub:`FP11`
      - y\ :sub:`FP10`
      - y\ :sub:`FP9`
      - y\ :sub:`FP8`
      - y\ :sub:`FP7`
      - y\ :sub:`FP6`
      - y\ :sub:`FP5`
      - y\ :sub:`FP4`
      - y\ :sub:`FP3`
      - y\ :sub:`FP2`
      - y\ :sub:`FP1`
      - y\ :sub:`FP0`

.. raw:: latex

    \endgroup

The following table lists packed FP32-wide Y formats.

MEDIA_BUS_FMT_Y_FP32_1X32
~~~~~~~~~~~~~~~~~~~~~~~~~

This represents a FP32, single-channel pixel arrangement. Each 32 bits represent a floating-point (FP32) value.
The Y channel occupies the entire 32 bits.

.. tabularcolumns:: |p{4.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-y-32:

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. flat-table:: 32bit Y formats
    :header-rows:  3
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-Y-FP32-1X32:

      - MEDIA_BUS_FMT_Y_FP32_1X32
      - 0x2031
      -
      - y\ :sub:`31`
      - y\ :sub:`30`
      - y\ :sub:`29`
      - y\ :sub:`28`
      - y\ :sub:`27`
      - y\ :sub:`26`
      - y\ :sub:`25`
      - y\ :sub:`24`
      - y\ :sub:`23`
      - y\ :sub:`22`
      - y\ :sub:`21`
      - y\ :sub:`20`
      - y\ :sub:`19`
      - y\ :sub:`18`
      - y\ :sub:`17`
      - y\ :sub:`16`
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`

.. raw:: latex

    \endgroup

On LVDS buses, usually each sample is transferred serialized in seven
time slots per pixel clock, on three (18-bit) or four (24-bit) or five (30-bit)
differential data pairs at the same time. The remaining bits are used
for control signals as defined by SPWG/PSWG/VESA or JEIDA standards. The
24-bit RGB format serialized in seven time slots on four lanes using
JEIDA defined bit mapping will be named
``MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA``, for example.

.. raw:: latex

    \small

.. _v4l2-mbus-pixelcode-rgb-lvds:

.. flat-table:: LVDS RGB formats
    :header-rows:  2
    :stub-columns: 0

    * - Identifier
      - Code
      -
      -
      - :cspan:`4` Data organization
    * -
      -
      - Timeslot
      - Lane
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-RGB666-1X7X3-SPWG:

      - MEDIA_BUS_FMT_RGB666_1X7X3_SPWG
      - 0x1010
      - 0
      -
      -
      -
      - d
      - b\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      - 1
      -
      -
      -
      - d
      - b\ :sub:`0`
      - r\ :sub:`5`
    * -
      -
      - 2
      -
      -
      -
      - d
      - g\ :sub:`5`
      - r\ :sub:`4`
    * -
      -
      - 3
      -
      -
      -
      - b\ :sub:`5`
      - g\ :sub:`4`
      - r\ :sub:`3`
    * -
      -
      - 4
      -
      -
      -
      - b\ :sub:`4`
      - g\ :sub:`3`
      - r\ :sub:`2`
    * -
      -
      - 5
      -
      -
      -
      - b\ :sub:`3`
      - g\ :sub:`2`
      - r\ :sub:`1`
    * -
      -
      - 6
      -
      -
      -
      - b\ :sub:`2`
      - g\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB888-1X7X4-SPWG:

      - MEDIA_BUS_FMT_RGB888_1X7X4_SPWG
      - 0x1011
      - 0
      -
      -
      - d
      - d
      - b\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      - 1
      -
      -
      - b\ :sub:`7`
      - d
      - b\ :sub:`0`
      - r\ :sub:`5`
    * -
      -
      - 2
      -
      -
      - b\ :sub:`6`
      - d
      - g\ :sub:`5`
      - r\ :sub:`4`
    * -
      -
      - 3
      -
      -
      - g\ :sub:`7`
      - b\ :sub:`5`
      - g\ :sub:`4`
      - r\ :sub:`3`
    * -
      -
      - 4
      -
      -
      - g\ :sub:`6`
      - b\ :sub:`4`
      - g\ :sub:`3`
      - r\ :sub:`2`
    * -
      -
      - 5
      -
      -
      - r\ :sub:`7`
      - b\ :sub:`3`
      - g\ :sub:`2`
      - r\ :sub:`1`
    * -
      -
      - 6
      -
      -
      - r\ :sub:`6`
      - b\ :sub:`2`
      - g\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB888-1X7X4-JEIDA:

      - MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA
      - 0x1012
      - 0
      -
      -
      - d
      - d
      - b\ :sub:`3`
      - g\ :sub:`2`
    * -
      -
      - 1
      -
      -
      - b\ :sub:`1`
      - d
      - b\ :sub:`2`
      - r\ :sub:`7`
    * -
      -
      - 2
      -
      -
      - b\ :sub:`0`
      - d
      - g\ :sub:`7`
      - r\ :sub:`6`
    * -
      -
      - 3
      -
      -
      - g\ :sub:`1`
      - b\ :sub:`7`
      - g\ :sub:`6`
      - r\ :sub:`5`
    * -
      -
      - 4
      -
      -
      - g\ :sub:`0`
      - b\ :sub:`6`
      - g\ :sub:`5`
      - r\ :sub:`4`
    * -
      -
      - 5
      -
      -
      - r\ :sub:`1`
      - b\ :sub:`5`
      - g\ :sub:`4`
      - r\ :sub:`3`
    * -
      -
      - 6
      -
      -
      - r\ :sub:`0`
      - b\ :sub:`4`
      - g\ :sub:`3`
      - r\ :sub:`2`
    * .. _MEDIA-BUS-FMT-RGB101010-1X7X5-SPWG:

      - MEDIA_BUS_FMT_RGB101010_1X7X5_SPWG
      - 0x1026
      - 0
      -
      - d
      - d
      - d
      - b\ :sub:`1`
      - g\ :sub:`0`
    * -
      -
      - 1
      -
      - b\ :sub:`9`
      - b\ :sub:`7`
      - d
      - b\ :sub:`0`
      - r\ :sub:`5`
    * -
      -
      - 2
      -
      - b\ :sub:`8`
      - b\ :sub:`6`
      - d
      - g\ :sub:`5`
      - r\ :sub:`4`
    * -
      -
      - 3
      -
      - g\ :sub:`9`
      - g\ :sub:`7`
      - b\ :sub:`5`
      - g\ :sub:`4`
      - r\ :sub:`3`
    * -
      -
      - 4
      -
      - g\ :sub:`8`
      - g\ :sub:`6`
      - b\ :sub:`4`
      - g\ :sub:`3`
      - r\ :sub:`2`
    * -
      -
      - 5
      -
      - r\ :sub:`9`
      - r\ :sub:`7`
      - b\ :sub:`3`
      - g\ :sub:`2`
      - r\ :sub:`1`
    * -
      -
      - 6
      -
      - r\ :sub:`8`
      - r\ :sub:`6`
      - b\ :sub:`2`
      - g\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-RGB101010-1X7X5-JEIDA:

      - MEDIA_BUS_FMT_RGB101010_1X7X5_JEIDA
      - 0x1027
      - 0
      -
      - d
      - d
      - d
      - b\ :sub:`5`
      - g\ :sub:`4`
    * -
      -
      - 1
      -
      - b\ :sub:`1`
      - b\ :sub:`3`
      - d
      - b\ :sub:`4`
      - r\ :sub:`9`
    * -
      -
      - 2
      -
      - b\ :sub:`0`
      - b\ :sub:`2`
      - d
      - g\ :sub:`9`
      - r\ :sub:`8`
    * -
      -
      - 3
      -
      - g\ :sub:`1`
      - g\ :sub:`3`
      - b\ :sub:`9`
      - g\ :sub:`8`
      - r\ :sub:`7`
    * -
      -
      - 4
      -
      - g\ :sub:`0`
      - g\ :sub:`2`
      - b\ :sub:`8`
      - g\ :sub:`7`
      - r\ :sub:`6`
    * -
      -
      - 5
      -
      - r\ :sub:`1`
      - r\ :sub:`3`
      - b\ :sub:`7`
      - g\ :sub:`6`
      - r\ :sub:`5`
    * -
      -
      - 6
      -
      - r\ :sub:`0`
      - r\ :sub:`2`
      - b\ :sub:`6`
      - g\ :sub:`5`
      - r\ :sub:`4`

.. raw:: latex

    \normalsize


Bayer Formats
^^^^^^^^^^^^^

Those formats transfer pixel data as red, green and blue components. The
format code is made of the following information.

-  The red, green and blue components order code, as encoded in a pixel
   sample. The possible values are shown in :ref:`bayer-patterns`.

-  The number of bits per pixel component. All components are
   transferred on the same number of bits. Common values are 8, 10 and
   12.

-  The compression (optional). If the pixel components are ALAW- or
   DPCM-compressed, a mention of the compression scheme and the number
   of bits per compressed pixel component.

-  The number of bus samples per pixel. Pixels that are wider than the
   bus width must be transferred in multiple samples. Common values are
   1 and 2.

-  The bus width.

-  For formats where the total number of bits per pixel is smaller than
   the number of bus samples per pixel times the bus width, a padding
   value stating if the bytes are padded in their most high order bits
   (PADHI) or low order bits (PADLO).

-  For formats where the number of bus samples per pixel is larger than
   1, an endianness value stating if the pixel is transferred MSB first
   (BE) or LSB first (LE).

For instance, a format with uncompressed 10-bit Bayer components
arranged in a red, green, green, blue pattern transferred as 2 8-bit
samples per pixel with the least significant bits transferred first will
be named ``MEDIA_BUS_FMT_SRGGB10_2X8_PADHI_LE``.


.. _bayer-patterns:

.. kernel-figure:: bayer.svg
    :alt:    bayer.svg
    :align:  center

    **Figure 4.8 Bayer Patterns**

The following table lists existing packed Bayer formats. The data
organization is given as an example for the first pixel only.


.. HACK: ideally, we would be using adjustbox here. However, Sphinx
.. is a very bad behaviored guy: if the table has more than 30 cols,
.. it switches to long table, and there's no way to override it.


.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. tabularcolumns:: |p{6.0cm}|p{0.7cm}|p{0.3cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-bayer:

.. cssclass: longtable

.. flat-table:: Bayer Formats
    :header-rows:  2
    :stub-columns: 0

    * - Identifier
      - Code
      -
      - :cspan:`15` Data organization
    * -
      -
      - Bit
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-SBGGR8-1X8:

      - MEDIA_BUS_FMT_SBGGR8_1X8
      - 0x3001
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGBRG8-1X8:

      - MEDIA_BUS_FMT_SGBRG8_1X8
      - 0x3013
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGRBG8-1X8:

      - MEDIA_BUS_FMT_SGRBG8_1X8
      - 0x3002
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SRGGB8-1X8:

      - MEDIA_BUS_FMT_SRGGB8_1X8
      - 0x3014
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SBGGR10-ALAW8-1X8:

      - MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8
      - 0x3015
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGBRG10-ALAW8-1X8:

      - MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8
      - 0x3016
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGRBG10-ALAW8-1X8:

      - MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8
      - 0x3017
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SRGGB10-ALAW8-1X8:

      - MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8
      - 0x3018
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SBGGR10-DPCM8-1X8:

      - MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8
      - 0x300b
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGBRG10-DPCM8-1X8:

      - MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8
      - 0x300c
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGRBG10-DPCM8-1X8:

      - MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8
      - 0x3009
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SRGGB10-DPCM8-1X8:

      - MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8
      - 0x300d
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SBGGR10-2X8-PADHI-BE:

      - MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE
      - 0x3003
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - b\ :sub:`9`
      - b\ :sub:`8`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SBGGR10-2X8-PADHI-LE:

      - MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE
      - 0x3004
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - b\ :sub:`9`
      - b\ :sub:`8`
    * .. _MEDIA-BUS-FMT-SBGGR10-2X8-PADLO-BE:

      - MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE
      - 0x3005
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`1`
      - b\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
    * .. _MEDIA-BUS-FMT-SBGGR10-2X8-PADLO-LE:

      - MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE
      - 0x3006
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`1`
      - b\ :sub:`0`
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
    * .. _MEDIA-BUS-FMT-SBGGR10-1X10:

      - MEDIA_BUS_FMT_SBGGR10_1X10
      - 0x3007
      -
      -
      -
      -
      -
      -
      -
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGBRG10-1X10:

      - MEDIA_BUS_FMT_SGBRG10_1X10
      - 0x300e
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGRBG10-1X10:

      - MEDIA_BUS_FMT_SGRBG10_1X10
      - 0x300a
      -
      -
      -
      -
      -
      -
      -
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SRGGB10-1X10:

      - MEDIA_BUS_FMT_SRGGB10_1X10
      - 0x300f
      -
      -
      -
      -
      -
      -
      -
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SBGGR12-1X12:

      - MEDIA_BUS_FMT_SBGGR12_1X12
      - 0x3008
      -
      -
      -
      -
      -
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGBRG12-1X12:

      - MEDIA_BUS_FMT_SGBRG12_1X12
      - 0x3010
      -
      -
      -
      -
      -
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGRBG12-1X12:

      - MEDIA_BUS_FMT_SGRBG12_1X12
      - 0x3011
      -
      -
      -
      -
      -
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SRGGB12-1X12:

      - MEDIA_BUS_FMT_SRGGB12_1X12
      - 0x3012
      -
      -
      -
      -
      -
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SBGGR14-1X14:

      - MEDIA_BUS_FMT_SBGGR14_1X14
      - 0x3019
      -
      -
      -
      - b\ :sub:`13`
      - b\ :sub:`12`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGBRG14-1X14:

      - MEDIA_BUS_FMT_SGBRG14_1X14
      - 0x301a
      -
      -
      -
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGRBG14-1X14:

      - MEDIA_BUS_FMT_SGRBG14_1X14
      - 0x301b
      -
      -
      -
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SRGGB14-1X14:

      - MEDIA_BUS_FMT_SRGGB14_1X14
      - 0x301c
      -
      -
      -
      - r\ :sub:`13`
      - r\ :sub:`12`
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SBGGR16-1X16:

      - MEDIA_BUS_FMT_SBGGR16_1X16
      - 0x301d
      -
      - b\ :sub:`15`
      - b\ :sub:`14`
      - b\ :sub:`13`
      - b\ :sub:`12`
      - b\ :sub:`11`
      - b\ :sub:`10`
      - b\ :sub:`9`
      - b\ :sub:`8`
      - b\ :sub:`7`
      - b\ :sub:`6`
      - b\ :sub:`5`
      - b\ :sub:`4`
      - b\ :sub:`3`
      - b\ :sub:`2`
      - b\ :sub:`1`
      - b\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGBRG16-1X16:

      - MEDIA_BUS_FMT_SGBRG16_1X16
      - 0x301e
      -
      - g\ :sub:`15`
      - g\ :sub:`14`
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SGRBG16-1X16:

      - MEDIA_BUS_FMT_SGRBG16_1X16
      - 0x301f
      -
      - g\ :sub:`15`
      - g\ :sub:`14`
      - g\ :sub:`13`
      - g\ :sub:`12`
      - g\ :sub:`11`
      - g\ :sub:`10`
      - g\ :sub:`9`
      - g\ :sub:`8`
      - g\ :sub:`7`
      - g\ :sub:`6`
      - g\ :sub:`5`
      - g\ :sub:`4`
      - g\ :sub:`3`
      - g\ :sub:`2`
      - g\ :sub:`1`
      - g\ :sub:`0`
    * .. _MEDIA-BUS-FMT-SRGGB16-1X16:

      - MEDIA_BUS_FMT_SRGGB16_1X16
      - 0x3020
      -
      - r\ :sub:`15`
      - r\ :sub:`14`
      - r\ :sub:`13`
      - r\ :sub:`12`
      - r\ :sub:`11`
      - r\ :sub:`10`
      - r\ :sub:`9`
      - r\ :sub:`8`
      - r\ :sub:`7`
      - r\ :sub:`6`
      - r\ :sub:`5`
      - r\ :sub:`4`
      - r\ :sub:`3`
      - r\ :sub:`2`
      - r\ :sub:`1`
      - r\ :sub:`0`

.. raw:: latex

    \endgroup


Packed YUV Formats
^^^^^^^^^^^^^^^^^^

Those data formats transfer pixel data as (possibly downsampled) Y, U
and V components. Some formats include dummy bits in some of their
samples and are collectively referred to as "YDYC" (Y-Dummy-Y-Chroma)
formats. One cannot rely on the values of these dummy bits as those are
undefined.

The format code is made of the following information.

-  The Y, U and V components order code, as transferred on the bus.
   Possible values are YUYV, UYVY, YVYU and VYUY for formats with no
   dummy bit, and YDYUYDYV, YDYVYDYU, YUYDYVYD and YVYDYUYD for YDYC
   formats.

-  The number of bits per pixel component. All components are
   transferred on the same number of bits. Common values are 8, 10 and
   12.

-  The number of bus samples per pixel. Pixels that are wider than the
   bus width must be transferred in multiple samples. Common values are
   0.5 (encoded as 0_5; in this case two pixels are transferred per bus
   sample), 1, 1.5 (encoded as 1_5) and 2.

-  The bus width. When the bus width is larger than the number of bits
   per pixel component, several components are packed in a single bus
   sample. The components are ordered as specified by the order code,
   with components on the left of the code transferred in the high order
   bits. Common values are 8 and 16.

For instance, a format where pixels are encoded as 8-bit YUV values
downsampled to 4:2:2 and transferred as 2 8-bit bus samples per pixel in
the U, Y, V, Y order will be named ``MEDIA_BUS_FMT_UYVY8_2X8``.

:ref:`v4l2-mbus-pixelcode-yuv8` lists existing packed YUV formats and
describes the organization of each pixel data in each sample. When a
format pattern is split across multiple samples each of the samples in
the pattern is described.

The role of each bit transferred over the bus is identified by one of
the following codes.

-  y\ :sub:`x` for luma component bit number x

-  u\ :sub:`x` for blue chroma component bit number x

-  v\ :sub:`x` for red chroma component bit number x

-  a\ :sub:`x` for alpha component bit number x

- for non-available bits (for positions higher than the bus width)

-  d for dummy bits

.. HACK: ideally, we would be using adjustbox here. However, this
.. will never work for this table, as, even with tiny font, it is
.. to big for a single page. So, we need to manually adjust the
.. size.

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. tabularcolumns:: |p{5.0cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-yuv8:

.. flat-table:: YUV Formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 10
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-Y8-1X8:

      - MEDIA_BUS_FMT_Y8_1X8
      - 0x2001
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UV8-1X8:

      - MEDIA_BUS_FMT_UV8_1X8
      - 0x2015
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYVY8-1_5X8:

      - MEDIA_BUS_FMT_UYVY8_1_5X8
      - 0x2002
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYUY8-1_5X8:

      - MEDIA_BUS_FMT_VYUY8_1_5X8
      - 0x2003
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUYV8-1_5X8:

      - MEDIA_BUS_FMT_YUYV8_1_5X8
      - 0x2004
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YVYU8-1_5X8:

      - MEDIA_BUS_FMT_YVYU8_1_5X8
      - 0x2005
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYVY8-2X8:

      - MEDIA_BUS_FMT_UYVY8_2X8
      - 0x2006
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYUY8-2X8:

      - MEDIA_BUS_FMT_VYUY8_2X8
      - 0x2007
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUYV8-2X8:

      - MEDIA_BUS_FMT_YUYV8_2X8
      - 0x2008
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YVYU8-2X8:

      - MEDIA_BUS_FMT_YVYU8_2X8
      - 0x2009
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * .. _MEDIA-BUS-FMT-Y10-1X10:

      - MEDIA_BUS_FMT_Y10_1X10
      - 0x200a
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-Y10-2X8-PADHI_LE:

      - MEDIA_BUS_FMT_Y10_2X8_PADHI_LE
      - 0x202c
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - y\ :sub:`9`
      - y\ :sub:`8`
    * .. _MEDIA-BUS-FMT-UYVY10-2X10:

      - MEDIA_BUS_FMT_UYVY10_2X10
      - 0x2018
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYUY10-2X10:

      - MEDIA_BUS_FMT_VYUY10_2X10
      - 0x2019
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUYV10-2X10:

      - MEDIA_BUS_FMT_YUYV10_2X10
      - 0x200b
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YVYU10-2X10:

      - MEDIA_BUS_FMT_YVYU10_2X10
      - 0x200c
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYYUYY10_4X20:

      - MEDIA_BUS_FMT_VYYUYY10_4X20
      - 0x2101
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-Y12-1X12:

      - MEDIA_BUS_FMT_Y12_1X12
      - 0x2013
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYVY12-2X12:

      - MEDIA_BUS_FMT_UYVY12_2X12
      - 0x201c
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYUY12-2X12:

      - MEDIA_BUS_FMT_VYUY12_2X12
      - 0x201d
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUYV12-2X12:

      - MEDIA_BUS_FMT_YUYV12_2X12
      - 0x201e
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YVYU12-2X12:

      - MEDIA_BUS_FMT_YVYU12_2X12
      - 0x201f
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * .. _MEDIA-BUS-FMT-Y14-1X14:

      - MEDIA_BUS_FMT_Y14_1X14
      - 0x202d
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-Y16-1X16:

      - MEDIA_BUS_FMT_Y16_1X16
      - 0x202e
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYVY8-1X16:

      - MEDIA_BUS_FMT_UYVY8_1X16
      - 0x200f
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYUY8-1X16:

      - MEDIA_BUS_FMT_VYUY8_1X16
      - 0x2010
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUYV8-1X16:

      - MEDIA_BUS_FMT_YUYV8_1X16
      - 0x2011
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YVYU8-1X16:

      - MEDIA_BUS_FMT_YVYU8_1X16
      - 0x2012
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YDYUYDYV8-1X16:

      - MEDIA_BUS_FMT_YDYUYDYV8_1X16
      - 0x2014
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - d
      - d
      - d
      - d
      - d
      - d
      - d
      - d
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - d
      - d
      - d
      - d
      - d
      - d
      - d
      - d
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYVY10-1X20:

      - MEDIA_BUS_FMT_UYVY10_1X20
      - 0x201a
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYUY10-1X20:

      - MEDIA_BUS_FMT_VYUY10_1X20
      - 0x201b
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUYV10-1X20:

      - MEDIA_BUS_FMT_YUYV10_1X20
      - 0x200d
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YVYU10-1X20:

      - MEDIA_BUS_FMT_YVYU10_1X20
      - 0x200e
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VUY8-1X24:

      - MEDIA_BUS_FMT_VUY8_1X24
      - 0x201a
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUV8-1X24:

      - MEDIA_BUS_FMT_YUV8_1X24
      - 0x2025
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYYVYY8-0-5X24:

      - MEDIA_BUS_FMT_UYYVYY8_0_5X24
      - 0x2026
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYVY12-1X24:

      - MEDIA_BUS_FMT_UYVY12_1X24
      - 0x2020
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYUY12-1X24:

      - MEDIA_BUS_FMT_VYUY12_1X24
      - 0x2021
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUYV12-1X24:

      - MEDIA_BUS_FMT_YUYV12_1X24
      - 0x2022
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YVYU12-1X24:

      - MEDIA_BUS_FMT_YVYU12_1X24
      - 0x2023
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYYVYY12-4X24:

      - MEDIA_BUS_FMT_UYYVYY12_4X24
      - 0x2103
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VYYUYY8-1X24:

      - MEDIA_BUS_FMT_VYYUYY8_1X24
      - 0x2100
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUV10-1X30:

      - MEDIA_BUS_FMT_YUV10_1X30
      - 0x2016
      -
      -
      -
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VUY10-1X30:

      - MEDIA_BUS_FMT_VUY10_1X30
      - 0x2102
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYYVYY10-0-5X30:

      - MEDIA_BUS_FMT_UYYVYY10_0_5X30
      - 0x2027
      -
      -
      -
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-AYUV8-1X32:

      - MEDIA_BUS_FMT_AYUV8_1X32
      - 0x2017
      -
      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYYVYY16-4X32:

      - MEDIA_BUS_FMT_UYYVYY16_4X32
      - 0x2106
      -
      - u\ :sub:`15`
      - u\ :sub:`14`
      - u\ :sub:`13`
      - u\ :sub:`12`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      - v\ :sub:`15`
      - v\ :sub:`14`
      - v\ :sub:`13`
      - v\ :sub:`12`
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYVY16-2X32:

      - MEDIA_BUS_FMT_UYVY16_2X32
      - 0x2108
      -
      - u\ :sub:`15`
      - u\ :sub:`14`
      - u\ :sub:`13`
      - u\ :sub:`12`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      - v\ :sub:`15`
      - v\ :sub:`14`
      - v\ :sub:`13`
      - v\ :sub:`12`
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`


.. raw:: latex

	\endgroup


The following table list existing packed 36bit wide YUV formats.

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. tabularcolumns:: |p{4.1cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-yuv8-36bit:

.. flat-table:: 36bit YUV Formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`35` Data organization
    * -
      -
      - Bit
      - 35
      - 34
      - 33
      - 32
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 10
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-UYYVYY12-0-5X36:

      - MEDIA_BUS_FMT_UYYVYY12_0_5X36
      - 0x2028
      -
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-YUV12-1X36:

      - MEDIA_BUS_FMT_YUV12_1X36
      - 0x2029
      -
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VUY12-1X36:

      - MEDIA_BUS_FMT_VUY12_1X36
      - 0x2104
      -
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`


.. raw:: latex

	\endgroup


The following table list existing packed 48bit wide YUV formats.

.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. tabularcolumns:: |p{5.6cm}|p{0.7cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-yuv8-48bit:

.. flat-table:: 48bit YUV Formats
    :header-rows:  3
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - 47
      - 46
      - 45
      - 44
      - 43
      - 42
      - 41
      - 40
      - 39
      - 38
      - 37
      - 36
      - 35
      - 34
      - 33
      - 32
    * -
      -
      -
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 10
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-YUV16-1X48:

      - MEDIA_BUS_FMT_YUV16_1X48
      - 0x202a
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`8`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      - u\ :sub:`15`
      - u\ :sub:`14`
      - u\ :sub:`13`
      - u\ :sub:`12`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - v\ :sub:`15`
      - v\ :sub:`14`
      - v\ :sub:`13`
      - v\ :sub:`12`
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * .. _MEDIA-BUS-FMT-VUY16-1X48:

      - MEDIA_BUS_FMT_VUY16_1X48
      - 0x2107
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`15`
      - v\ :sub:`14`
      - v\ :sub:`13`
      - v\ :sub:`12`
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      - u\ :sub:`15`
      - u\ :sub:`14`
      - u\ :sub:`13`
      - u\ :sub:`12`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`8`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * .. _MEDIA-BUS-FMT-UYYVYY16-0-5X48:

      - MEDIA_BUS_FMT_UYYVYY16_0_5X48
      - 0x202b
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - u\ :sub:`15`
      - u\ :sub:`14`
      - u\ :sub:`13`
      - u\ :sub:`12`
      - u\ :sub:`11`
      - u\ :sub:`10`
      - u\ :sub:`9`
      - u\ :sub:`8`
      - u\ :sub:`7`
      - u\ :sub:`6`
      - u\ :sub:`5`
      - u\ :sub:`4`
      - u\ :sub:`3`
      - u\ :sub:`2`
      - u\ :sub:`1`
      - u\ :sub:`0`
    * -
      -
      -
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`8`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
    * -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - v\ :sub:`15`
      - v\ :sub:`14`
      - v\ :sub:`13`
      - v\ :sub:`12`
      - v\ :sub:`11`
      - v\ :sub:`10`
      - v\ :sub:`9`
      - v\ :sub:`8`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`
    * -
      -
      -
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`9`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`
      - y\ :sub:`15`
      - y\ :sub:`14`
      - y\ :sub:`13`
      - y\ :sub:`12`
      - y\ :sub:`11`
      - y\ :sub:`10`
      - y\ :sub:`8`
      - y\ :sub:`8`
      - y\ :sub:`7`
      - y\ :sub:`6`
      - y\ :sub:`5`
      - y\ :sub:`4`
      - y\ :sub:`3`
      - y\ :sub:`2`
      - y\ :sub:`1`
      - y\ :sub:`0`


.. raw:: latex

	\endgroup

HSV/HSL Formats
^^^^^^^^^^^^^^^

Those formats transfer pixel data as RGB values in a
cylindrical-coordinate system using Hue-Saturation-Value or
Hue-Saturation-Lightness components. The format code is made of the
following information.

-  The hue, saturation, value or lightness and optional alpha components
   order code, as encoded in a pixel sample. The only currently
   supported value is AHSV.

-  The number of bits per component, for each component. The values can
   be different for all components. The only currently supported value
   is 8888.

-  The number of bus samples per pixel. Pixels that are wider than the
   bus width must be transferred in multiple samples. The only currently
   supported value is 1.

-  The bus width.

-  For formats where the total number of bits per pixel is smaller than
   the number of bus samples per pixel times the bus width, a padding
   value stating if the bytes are padded in their most high order bits
   (PADHI) or low order bits (PADLO).

-  For formats where the number of bus samples per pixel is larger than
   1, an endianness value stating if the pixel is transferred MSB first
   (BE) or LSB first (LE).

The following table lists existing HSV/HSL formats.


.. raw:: latex

    \begingroup
    \tiny
    \setlength{\tabcolsep}{2pt}

.. tabularcolumns:: |p{3.9cm}|p{0.73cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|p{0.22cm}|

.. _v4l2-mbus-pixelcode-hsv:

.. flat-table:: HSV/HSL formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 28 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    * - Identifier
      - Code
      -
      - :cspan:`31` Data organization
    * -
      -
      - Bit
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-AHSV8888-1X32:

      - MEDIA_BUS_FMT_AHSV8888_1X32
      - 0x6001
      -
      - a\ :sub:`7`
      - a\ :sub:`6`
      - a\ :sub:`5`
      - a\ :sub:`4`
      - a\ :sub:`3`
      - a\ :sub:`2`
      - a\ :sub:`1`
      - a\ :sub:`0`
      - h\ :sub:`7`
      - h\ :sub:`6`
      - h\ :sub:`5`
      - h\ :sub:`4`
      - h\ :sub:`3`
      - h\ :sub:`2`
      - h\ :sub:`1`
      - h\ :sub:`0`
      - s\ :sub:`7`
      - s\ :sub:`6`
      - s\ :sub:`5`
      - s\ :sub:`4`
      - s\ :sub:`3`
      - s\ :sub:`2`
      - s\ :sub:`1`
      - s\ :sub:`0`
      - v\ :sub:`7`
      - v\ :sub:`6`
      - v\ :sub:`5`
      - v\ :sub:`4`
      - v\ :sub:`3`
      - v\ :sub:`2`
      - v\ :sub:`1`
      - v\ :sub:`0`

.. raw:: latex

    \endgroup


JPEG Compressed Formats
^^^^^^^^^^^^^^^^^^^^^^^

Those data formats consist of an ordered sequence of 8-bit bytes
obtained from JPEG compression process. Additionally to the ``_JPEG``
postfix the format code is made of the following information.

-  The number of bus samples per entropy encoded byte.

-  The bus width.

For instance, for a JPEG baseline process and an 8-bit bus width the
format will be named ``MEDIA_BUS_FMT_JPEG_1X8``.

The following table lists existing JPEG compressed formats.


.. _v4l2-mbus-pixelcode-jpeg:

.. tabularcolumns:: |p{6.0cm}|p{1.4cm}|p{9.9cm}|

.. flat-table:: JPEG Formats
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Remarks
    * .. _MEDIA-BUS-FMT-JPEG-1X8:

      - MEDIA_BUS_FMT_JPEG_1X8
      - 0x4001
      - Besides of its usage for the parallel bus this format is
	recommended for transmission of JPEG data over MIPI CSI bus using
	the User Defined 8-bit Data types.



.. _v4l2-mbus-vendor-spec-fmts:

Vendor and Device Specific Formats
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This section lists complex data formats that are either vendor or device
specific.

The following table lists the existing vendor and device specific
formats.


.. _v4l2-mbus-pixelcode-vendor-specific:

.. tabularcolumns:: |p{8.0cm}|p{1.4cm}|p{7.9cm}|

.. flat-table:: Vendor and device specific formats
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Comments
    * .. _MEDIA-BUS-FMT-S5C-UYVY-JPEG-1X8:

      - MEDIA_BUS_FMT_S5C_UYVY_JPEG_1X8
      - 0x5001
      - Interleaved raw UYVY and JPEG image format with embedded meta-data
	used by Samsung S3C73MX camera sensors.

.. _v4l2-mbus-metadata-fmts:

Metadata Formats
^^^^^^^^^^^^^^^^

This section lists all metadata formats.

The following table lists the existing metadata formats.

.. tabularcolumns:: |p{8.0cm}|p{1.4cm}|p{7.9cm}|

.. flat-table:: Metadata formats
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Comments
    * .. _MEDIA-BUS-FMT-METADATA-FIXED:

      - MEDIA_BUS_FMT_METADATA_FIXED
      - 0x7001
      - This format should be used when the same driver handles
	both sides of the link and the bus format is a fixed
	metadata format that is not configurable from userspace.
	Width and height will be set to 0 for this format.

Generic Serial Metadata Formats
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Generic serial metadata formats are used on serial buses where the actual data
content is more or less device specific but the data is transmitted and received
by multiple devices that do not process the data in any way, simply writing
it to system memory for processing in software at the end of the pipeline.

"b" in an array cell signifies a byte of data, followed by the number of the bit
and finally the bit number in subscript. "x" indicates a padding bit.

.. _media-bus-format-generic-meta:

.. cssclass: longtable

.. flat-table:: Generic Serial Metadata Formats
    :header-rows:  2
    :stub-columns: 0

    * - Identifier
      - Code
      -
      - :cspan:`23` Data organization within bus :term:`Data Unit`
    * -
      -
      - Bit
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _MEDIA-BUS-FMT-META-8:

      - MEDIA_BUS_FMT_META_8
      - 0x8001
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b0\ :sub:`7`
      - b0\ :sub:`6`
      - b0\ :sub:`5`
      - b0\ :sub:`4`
      - b0\ :sub:`3`
      - b0\ :sub:`2`
      - b0\ :sub:`1`
      - b0\ :sub:`0`
    * .. _MEDIA-BUS-FMT-META-10:

      - MEDIA_BUS_FMT_META_10
      - 0x8002
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b0\ :sub:`7`
      - b0\ :sub:`6`
      - b0\ :sub:`5`
      - b0\ :sub:`4`
      - b0\ :sub:`3`
      - b0\ :sub:`2`
      - b0\ :sub:`1`
      - b0\ :sub:`0`
      - x
      - x
    * .. _MEDIA-BUS-FMT-META-12:

      - MEDIA_BUS_FMT_META_12
      - 0x8003
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b0\ :sub:`7`
      - b0\ :sub:`6`
      - b0\ :sub:`5`
      - b0\ :sub:`4`
      - b0\ :sub:`3`
      - b0\ :sub:`2`
      - b0\ :sub:`1`
      - b0\ :sub:`0`
      - x
      - x
      - x
      - x
    * .. _MEDIA-BUS-FMT-META-14:

      - MEDIA_BUS_FMT_META_14
      - 0x8004
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b0\ :sub:`7`
      - b0\ :sub:`6`
      - b0\ :sub:`5`
      - b0\ :sub:`4`
      - b0\ :sub:`3`
      - b0\ :sub:`2`
      - b0\ :sub:`1`
      - b0\ :sub:`0`
      - x
      - x
      - x
      - x
      - x
      - x
    * .. _MEDIA-BUS-FMT-META-16:

      - MEDIA_BUS_FMT_META_16
      - 0x8005
      -
      -
      -
      -
      -
      -
      -
      -
      -
      - b0\ :sub:`7`
      - b0\ :sub:`6`
      - b0\ :sub:`5`
      - b0\ :sub:`4`
      - b0\ :sub:`3`
      - b0\ :sub:`2`
      - b0\ :sub:`1`
      - b0\ :sub:`0`
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
    * .. _MEDIA-BUS-FMT-META-20:

      - MEDIA_BUS_FMT_META_20
      - 0x8006
      -
      -
      -
      -
      -
      - b0\ :sub:`7`
      - b0\ :sub:`6`
      - b0\ :sub:`5`
      - b0\ :sub:`4`
      - b0\ :sub:`3`
      - b0\ :sub:`2`
      - b0\ :sub:`1`
      - b0\ :sub:`0`
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
    * .. _MEDIA-BUS-FMT-META-24:

      - MEDIA_BUS_FMT_META_24
      - 0x8007
      -
      - b0\ :sub:`7`
      - b0\ :sub:`6`
      - b0\ :sub:`5`
      - b0\ :sub:`4`
      - b0\ :sub:`3`
      - b0\ :sub:`2`
      - b0\ :sub:`1`
      - b0\ :sub:`0`
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x
      - x

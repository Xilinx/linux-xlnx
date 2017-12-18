.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-UYVY:

**************************
V4L2_PIX_FMT_UYVY ('UYVY')
**************************


Variation of ``V4L2_PIX_FMT_YUYV`` with different order of samples in
memory


Description
===========

In this format each four bytes is two pixels. Each four bytes is two
Y's, a Cb and a Cr. Each Y goes to one of the pixels, and the Cb and Cr
belong to both pixels. As you can see, the Cr and Cb components have
half the horizontal resolution of the Y component.

**Byte Order.**
Each cell is one byte.


.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Cb\ :sub:`00`
      - Y'\ :sub:`00`
      - Cr\ :sub:`00`
      - Y'\ :sub:`01`
      - Cb\ :sub:`01`
      - Y'\ :sub:`02`
      - Cr\ :sub:`01`
      - Y'\ :sub:`03`
    * - start + 8:
      - Cb\ :sub:`10`
      - Y'\ :sub:`10`
      - Cr\ :sub:`10`
      - Y'\ :sub:`11`
      - Cb\ :sub:`11`
      - Y'\ :sub:`12`
      - Cr\ :sub:`11`
      - Y'\ :sub:`13`
    * - start + 16:
      - Cb\ :sub:`20`
      - Y'\ :sub:`20`
      - Cr\ :sub:`20`
      - Y'\ :sub:`21`
      - Cb\ :sub:`21`
      - Y'\ :sub:`22`
      - Cr\ :sub:`21`
      - Y'\ :sub:`23`
    * - start + 24:
      - Cb\ :sub:`30`
      - Y'\ :sub:`30`
      - Cr\ :sub:`30`
      - Y'\ :sub:`31`
      - Cb\ :sub:`31`
      - Y'\ :sub:`32`
      - Cr\ :sub:`31`
      - Y'\ :sub:`33`


**Color Sample Location..**



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * -
      - 0
      -
      - 1
      - 2
      -
      - 3
    * - 0
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 1
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 2
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 3
      - Y
      - C
      - Y
      - Y
      - C
      - Y

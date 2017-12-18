.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_SPU_PALETTE:

=====================
VIDEO_SET_SPU_PALETTE
=====================

Name
----

VIDEO_SET_SPU_PALETTE

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_SET_SPU_PALETTE, struct video_spu_palette *palette )
    :name: VIDEO_SET_SPU_PALETTE


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_SPU_PALETTE for this command.

    -  .. row 3

       -  video_spu_palette_t \*palette

       -  SPU palette according to section ??.


Description
-----------

This ioctl sets the SPU color palette.

.. c:type:: video_spu_palette

.. code-block::c

	typedef struct video_spu_palette {      /* SPU Palette information */
		int length;
		__u8 __user *palette;
	} video_spu_palette_t;

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  input is not a valid palette or driver doesn’t handle SPU.

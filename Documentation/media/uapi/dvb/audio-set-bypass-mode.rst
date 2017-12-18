.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_SET_BYPASS_MODE:

=====================
AUDIO_SET_BYPASS_MODE
=====================

Name
----

AUDIO_SET_BYPASS_MODE

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int ioctl(int fd, AUDIO_SET_BYPASS_MODE, boolean mode)
    :name: AUDIO_SET_BYPASS_MODE

Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  boolean mode

       -  Enables or disables the decoding of the current Audio stream in
	  the DVB subsystem.

          TRUE: Bypass is disabled

          FALSE: Bypass is enabled


Description
-----------

This ioctl call asks the Audio Device to bypass the Audio decoder and
forward the stream without decoding. This mode shall be used if streams
that can’t be handled by the DVB system shall be decoded. Dolby
DigitalTM streams are automatically forwarded by the DVB subsystem if
the hardware can handle it.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

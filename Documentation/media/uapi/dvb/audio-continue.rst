.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_CONTINUE:

==============
AUDIO_CONTINUE
==============

Name
----

AUDIO_CONTINUE

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int  ioctl(int fd, AUDIO_CONTINUE)
    :name: AUDIO_CONTINUE


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

Description
-----------

This ioctl restarts the decoding and playing process previously paused
with AUDIO_PAUSE command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

.. _FE_GET_FRONTEND:

***************
FE_GET_FRONTEND
***************

Name
====

FE_GET_FRONTEND

.. attention:: This ioctl is deprecated.


Synopsis
========

.. c:function:: int ioctl(int fd, FE_GET_FRONTEND, struct dvb_frontend_parameters *p)
    :name: FE_GET_FRONTEND


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.


``p``
    Points to parameters for tuning operation.


Description
===========

This ioctl call queries the currently effective frontend parameters. For
this command, read-only access to the device is sufficient.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  Maximum supported symbol rate reached.

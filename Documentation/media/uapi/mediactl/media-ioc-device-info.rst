.. -*- coding: utf-8; mode: rst -*-

.. _media_ioc_device_info:

***************************
ioctl MEDIA_IOC_DEVICE_INFO
***************************

Name
====

MEDIA_IOC_DEVICE_INFO - Query device information


Synopsis
========

.. c:function:: int ioctl( int fd, MEDIA_IOC_DEVICE_INFO, struct media_device_info *argp )
    :name: MEDIA_IOC_DEVICE_INFO


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <media-func-open>`.

``argp``


Description
===========

All media devices must support the ``MEDIA_IOC_DEVICE_INFO`` ioctl. To
query device information, applications call the ioctl with a pointer to
a struct :c:type:`media_device_info`. The driver
fills the structure and returns the information to the application. The
ioctl never fails.


.. c:type:: media_device_info

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct media_device_info
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  char

       -  ``driver``\ [16]

       -  Name of the driver implementing the media API as a NUL-terminated
	  ASCII string. The driver version is stored in the
	  ``driver_version`` field.

	  Driver specific applications can use this information to verify
	  the driver identity. It is also useful to work around known bugs,
	  or to identify drivers in error reports.

    -  .. row 2

       -  char

       -  ``model``\ [32]

       -  Device model name as a NUL-terminated UTF-8 string. The device
	  version is stored in the ``device_version`` field and is not be
	  appended to the model name.

    -  .. row 3

       -  char

       -  ``serial``\ [40]

       -  Serial number as a NUL-terminated ASCII string.

    -  .. row 4

       -  char

       -  ``bus_info``\ [32]

       -  Location of the device in the system as a NUL-terminated ASCII
	  string. This includes the bus type name (PCI, USB, ...) and a
	  bus-specific identifier.

    -  .. row 5

       -  __u32

       -  ``media_version``

       -  Media API version, formatted with the ``KERNEL_VERSION()`` macro.

    -  .. row 6

       -  __u32

       -  ``hw_revision``

       -  Hardware device revision in a driver-specific format.

    -  .. row 7

       -  __u32

       -  ``driver_version``

       -  Media device driver version, formatted with the
	  ``KERNEL_VERSION()`` macro. Together with the ``driver`` field
	  this identifies a particular driver.

    -  .. row 8

       -  __u32

       -  ``reserved``\ [31]

       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.


The ``serial`` and ``bus_info`` fields can be used to distinguish
between multiple instances of otherwise identical hardware. The serial
number takes precedence when provided and can be assumed to be unique.
If the serial number is an empty string, the ``bus_info`` field can be
used instead. The ``bus_info`` field is guaranteed to be unique, but can
vary across reboots or device unplug/replug.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

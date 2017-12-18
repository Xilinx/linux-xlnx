.. -*- coding: utf-8; mode: rst -*-

.. _FE_DISEQC_RECV_SLAVE_REPLY:

********************************
ioctl FE_DISEQC_RECV_SLAVE_REPLY
********************************

Name
====

FE_DISEQC_RECV_SLAVE_REPLY - Receives reply from a DiSEqC 2.0 command


Synopsis
========

.. c:function:: int ioctl( int fd, FE_DISEQC_RECV_SLAVE_REPLY, struct dvb_diseqc_slave_reply *argp )
    :name: FE_DISEQC_RECV_SLAVE_REPLY


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``argp``
    pointer to struct
    :c:type:`dvb_diseqc_slave_reply`


Description
===========

Receives reply from a DiSEqC 2.0 command.

.. c:type:: dvb_diseqc_slave_reply

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct dvb_diseqc_slave_reply
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  uint8_t

       -  msg[4]

       -  DiSEqC message (framing, data[3])

    -  .. row 2

       -  uint8_t

       -  msg_len

       -  Length of the DiSEqC message. Valid values are 0 to 4, where 0
	  means no msg

    -  .. row 3

       -  int

       -  timeout

       -  Return from ioctl after timeout ms with errorcode when no message
	  was received


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

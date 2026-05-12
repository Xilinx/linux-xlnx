.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _pixfmt-float-bitfield-tables:

*********************************
BF16 / FP16 / FP32 bit reference
*********************************

Shared bit-field tables for bfloat16, IEEE binary16, and IEEE binary32. Other
pages pull in only the tables using ``.. include::`` with ``:start-after:``.

..
   FLOAT_BITFIELD_LIST_TABLES_BEGIN

.. list-table:: BF16 bit layout
   :header-rows: 1
   :widths: 20 20 20 20

   * - Bit range
     - 15
     - 14-7
     - 6-0
   * - Attribute
     - S
     - E
     - M

.. list-table:: FP16 bit layout
   :header-rows: 1
   :widths: 20 20 20 20

   * - Bit range
     - 15
     - 14-10
     - 9-0
   * - Attribute
     - S
     - E
     - M

..
   FLOAT_BITFIELD_FP32_START

.. list-table:: FP32 bit layout
   :header-rows: 1
   :widths: 20 20 20 20

   * - Bit range
     - 31
     - 30-23
     - 22-0
   * - Attribute
     - S
     - E
     - M

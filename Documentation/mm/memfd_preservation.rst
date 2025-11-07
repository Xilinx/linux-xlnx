.. SPDX-License-Identifier: GPL-2.0-or-later

==========================
Memfd Preservation via LUO
==========================

Overview
========

Memory file descriptors (memfd) can be preserved over a kexec using the Live
Update Orchestrator (LUO) file preservation. This allows userspace to transfer
its memory contents to the next kernel after a kexec.

The preservation is not intended to be transparent. Only select properties of
the file are preserved. All others are reset to default. The preserved
properties are described below.

.. note::
   The LUO API is not stabilized yet, so the preserved properties of a memfd are
   also not stable and are subject to backwards incompatible changes.

.. note::
   Currently a memfd backed by Hugetlb is not supported. Memfds created
   with ``MFD_HUGETLB`` will be rejected.

Preserved Properties
====================

The following properties of the memfd are preserved across kexec:

File Contents
  All data stored in the file is preserved.

File Size
  The size of the file is preserved. Holes in the file are filled by allocating
  pages for them during preservation.

File Position
  The current file position is preserved, allowing applications to continue
  reading/writing from their last position.

File Status Flags
  memfds are always opened with ``O_RDWR`` and ``O_LARGEFILE``. This property is
  maintained.

Non-Preserved Properties
========================

All properties which are not preserved must be assumed to be reset to default.
This section describes some of those properties which may be more of note.

``FD_CLOEXEC`` flag
  A memfd can be created with the ``MFD_CLOEXEC`` flag that sets the
  ``FD_CLOEXEC`` on the file. This flag is not preserved and must be set again
  after restore via ``fcntl()``.

Seals
  File seals are not preserved. The file is unsealed on restore and if needed,
  must be sealed again via ``fcntl()``.

Behavior with LUO states
========================

This section described the behavior of the memfd in the different LUO states.

Normal Phase
  During the normal phase, the memfd can be marked for preservation using the
  ``LIVEUPDATE_SESSION_PRESERVE_FD`` ioctl. The memfd acts as a regular memfd
  during this phase with no additional restrictions.

Prepared Phase
  After LUO enters ``LIVEUPDATE_STATE_PREPARED``, the memfd is serialized and
  prepared for the next kernel. During this phase, the below things happen:

  - All the folios are pinned. If some folios reside in ``ZONE_MIGRATE``, they
    are migrated out. This ensures none of the preserved folios land in KHO
    scratch area.
  - Pages in swap are swapped in. Currently, there is no way to pass pages in
    swap over KHO, so all swapped out pages are swapped back in and pinned.
  - The memfd goes into "frozen mapping" mode. The file can no longer grow or
    shrink, or punch holes. This ensures the serialized mappings stay in sync.
    The file can still be read from or written to or mmap-ed.

Freeze Phase
  Updates the current file position in the serialized data to capture any
  changes that occurred between prepare and freeze phases. After this, the FD is
  not allowed to be accessed.

Restoration Phase
  After being restored, the memfd is functional as normal with the properties
  listed above restored.

Cancellation
  If the liveupdate is cancelled after going into prepared phase, the memfd
  functions like in normal phase.

Serialization format
====================

The state is serialized in an FDT with the following structure::

  /dts-v1/;

  / {
      compatible = "memfd-v1";
      pos = <current_file_position>;
      size = <file_size_in_bytes>;
      folios = <array_of_preserved_folio_descriptors>;
  };

Each folio descriptor contains:

- PFN + flags (8 bytes)

  - Physical frame number (PFN) of the preserved folio (bits 63:12).
  - Folio flags (bits 11:0):

    - ``PRESERVED_FLAG_DIRTY`` (bit 0)
    - ``PRESERVED_FLAG_UPTODATE`` (bit 1)

- Folio index within the file (8 bytes).

Limitations
===========

The current implementation has the following limitations:

Size
  Currently the size of the file is limited by the size of the FDT. The FDT can
  be at of most ``MAX_PAGE_ORDER`` order. By default this is 4 MiB with 4K
  pages. Each page in the file is tracked using 16 bytes. This limits the
  maximum size of the file to 1 GiB.

See Also
========

- :doc:`Live Update Orchestrator </core-api/liveupdate>`
- :doc:`/core-api/kho/concepts`

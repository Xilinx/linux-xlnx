/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 *
 * Copyright (C) 2025 Amazon.com Inc. or its affiliates.
 * Pratyush Yadav <ptyadav@amazon.de>
 */

#ifndef _LINUX_LIVEUPDATE_ABI_MEMFD_H
#define _LINUX_LIVEUPDATE_ABI_MEMFD_H

/**
 * DOC: memfd Live Update ABI
 *
 * This header defines the ABI for preserving the state of a memfd across a
 * kexec reboot using the LUO.
 *
 * The state is serialized into a Flattened Device Tree which is then handed
 * over to the next kernel via the KHO mechanism. The FDT is passed as the
 * opaque `data` handle in the file handler callbacks.
 *
 * This interface is a contract. Any modification to the FDT structure,
 * node properties, compatible string, or the layout of the serialization
 * structures defined here constitutes a breaking change. Such changes require
 * incrementing the version number in the MEMFD_LUO_FH_COMPATIBLE string.
 *
 * FDT Structure Overview:
 *   The memfd state is contained within a single FDT with the following layout:
 *
 *   .. code-block:: none
 *
 *     / {
 *         pos = <...>;
 *         size = <...>;
 *         nr_folios = <...>;
 *         folios = < ... binary data ... >;
 *     };
 *
 *   Node Properties:
 *     - pos: u64
 *       The file's current position (f_pos).
 *     - size: u64
 *       The total size of the file in bytes (i_size).
 *     - nr_folios: u64
 *       Number of folios in folios array. Only present when size > 0.
 *     - folios: struct kho_vmalloc
 *       KHO vmalloc preservation for an array of &struct memfd_luo_folio_ser,
 *       one for each preserved folio from the original file's mapping. Only
 *       present when size > 0.
 */

/**
 * struct memfd_luo_folio_ser - Serialized state of a single folio.
 * @foliodesc: A packed 64-bit value containing both the PFN and status flags of
 *             the preserved folio. The upper 52 bits store the PFN, and the
 *             lower 12 bits are reserved for flags (e.g., dirty, uptodate).
 * @index:     The page offset (pgoff_t) of the folio within the original file's
 *             address space. This is used to correctly position the folio
 *             during restoration.
 *
 * This structure represents the minimal information required to restore a
 * single folio in the new kernel. An array of these structs forms the binary
 * data for the "folios" property in the handover FDT.
 */
struct memfd_luo_folio_ser {
	u64 foliodesc;
	u64 index;
};

/* The strings used for memfd KHO FDT sub-tree. */

/* 64-bit pos value for the preserved memfd */
#define MEMFD_FDT_POS		"pos"

/* 64-bit size value of the preserved memfd */
#define MEMFD_FDT_SIZE		"size"

#define MEMFD_FDT_FOLIOS	"folios"

/* Number of folios in the folios array. */
#define MEMFD_FDT_NR_FOLIOS	"nr_folios"

/* The compatibility string for memfd file handler */
#define MEMFD_LUO_FH_COMPATIBLE	"memfd-v1"

#endif /* _LINUX_LIVEUPDATE_ABI_MEMFD_H */

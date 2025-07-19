/* SPDX-License-Identifier: GPL-2.0-only */

/**
 * DOC: erratum_3
 *
 * Erratum 3: Disconnected directory handling
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This fix addresses an issue with disconnected directories that occur when a
 * directory is moved outside the scope of a bind mount.  The change ensures
 * that evaluated access rights exclude those inherited from disconnected file
 * hierarchies (no longer accessible from the related mount point), and instead
 * only consider rights tied to directories that remain visible.  This prevents
 * access inconsistencies caused by missing access rights.
 */
LANDLOCK_ERRATUM(3)

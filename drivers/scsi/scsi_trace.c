/*
 * Copyright (C) 2010 FUJITSU LIMITED
 * Copyright (C) 2010 Tomohiro Kusumi <kusumi.tomohiro@jp.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/trace_seq.h>
#include <asm/unaligned.h>
#include <trace/events/scsi.h>

#define SERVICE_ACTION16(cdb) (cdb[1] & 0x1f)
#define SERVICE_ACTION32(cdb) ((cdb[8] << 8) | cdb[9])

static const char *
scsi_trace_misc(struct trace_seq *, unsigned char *, int);

static const char *
scsi_trace_rw6(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	sector_t lba = 0, txlen = 0;

	lba |= ((cdb[1] & 0x1F) << 16);
	lba |=  (cdb[2] << 8);
	lba |=   cdb[3];
	txlen = cdb[4];

	trace_seq_printf(p, "lba=%llu txlen=%llu",
			 (unsigned long long)lba, (unsigned long long)txlen);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw10(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	sector_t lba = 0, txlen = 0;

	lba |= (cdb[2] << 24);
	lba |= (cdb[3] << 16);
	lba |= (cdb[4] << 8);
	lba |=  cdb[5];
	txlen |= (cdb[7] << 8);
	txlen |=  cdb[8];

	trace_seq_printf(p, "lba=%llu txlen=%llu protect=%u",
			 (unsigned long long)lba, (unsigned long long)txlen,
			 cdb[1] >> 5);

	if (cdb[0] == WRITE_SAME)
		trace_seq_printf(p, " unmap=%u", cdb[1] >> 3 & 1);

	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw12(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	sector_t lba = 0, txlen = 0;

	lba |= (cdb[2] << 24);
	lba |= (cdb[3] << 16);
	lba |= (cdb[4] << 8);
	lba |=  cdb[5];
	txlen |= (cdb[6] << 24);
	txlen |= (cdb[7] << 16);
	txlen |= (cdb[8] << 8);
	txlen |=  cdb[9];

	trace_seq_printf(p, "lba=%llu txlen=%llu protect=%u",
			 (unsigned long long)lba, (unsigned long long)txlen,
			 cdb[1] >> 5);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw16(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	sector_t lba = 0, txlen = 0;

	lba |= ((u64)cdb[2] << 56);
	lba |= ((u64)cdb[3] << 48);
	lba |= ((u64)cdb[4] << 40);
	lba |= ((u64)cdb[5] << 32);
	lba |= (cdb[6] << 24);
	lba |= (cdb[7] << 16);
	lba |= (cdb[8] << 8);
	lba |=  cdb[9];
	txlen |= (cdb[10] << 24);
	txlen |= (cdb[11] << 16);
	txlen |= (cdb[12] << 8);
	txlen |=  cdb[13];

	trace_seq_printf(p, "lba=%llu txlen=%llu protect=%u",
			 (unsigned long long)lba, (unsigned long long)txlen,
			 cdb[1] >> 5);

	if (cdb[0] == WRITE_SAME_16)
		trace_seq_printf(p, " unmap=%u", cdb[1] >> 3 & 1);

	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_rw32(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	sector_t lba = 0, txlen = 0;
	u32 ei_lbrt = 0;

	switch (SERVICE_ACTION32(cdb)) {
	case READ_32:
		cmd = "READ";
		break;
	case VERIFY_32:
		cmd = "VERIFY";
		break;
	case WRITE_32:
		cmd = "WRITE";
		break;
	case WRITE_SAME_32:
		cmd = "WRITE_SAME";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	lba |= ((u64)cdb[12] << 56);
	lba |= ((u64)cdb[13] << 48);
	lba |= ((u64)cdb[14] << 40);
	lba |= ((u64)cdb[15] << 32);
	lba |= (cdb[16] << 24);
	lba |= (cdb[17] << 16);
	lba |= (cdb[18] << 8);
	lba |=  cdb[19];
	ei_lbrt |= (cdb[20] << 24);
	ei_lbrt |= (cdb[21] << 16);
	ei_lbrt |= (cdb[22] << 8);
	ei_lbrt |=  cdb[23];
	txlen |= (cdb[28] << 24);
	txlen |= (cdb[29] << 16);
	txlen |= (cdb[30] << 8);
	txlen |=  cdb[31];

	trace_seq_printf(p, "%s_32 lba=%llu txlen=%llu protect=%u ei_lbrt=%u",
			 cmd, (unsigned long long)lba,
			 (unsigned long long)txlen, cdb[10] >> 5, ei_lbrt);

	if (SERVICE_ACTION32(cdb) == WRITE_SAME_32)
		trace_seq_printf(p, " unmap=%u", cdb[10] >> 3 & 1);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_unmap(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);
	unsigned int regions = cdb[7] << 8 | cdb[8];

	trace_seq_printf(p, "regions=%u", (regions - 8) / 16);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_service_action_in(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	sector_t lba = 0;
	u32 alloc_len = 0;

	switch (SERVICE_ACTION16(cdb)) {
	case SAI_READ_CAPACITY_16:
		cmd = "READ_CAPACITY_16";
		break;
	case SAI_GET_LBA_STATUS:
		cmd = "GET_LBA_STATUS";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	lba |= ((u64)cdb[2] << 56);
	lba |= ((u64)cdb[3] << 48);
	lba |= ((u64)cdb[4] << 40);
	lba |= ((u64)cdb[5] << 32);
	lba |= (cdb[6] << 24);
	lba |= (cdb[7] << 16);
	lba |= (cdb[8] << 8);
	lba |=  cdb[9];
	alloc_len |= (cdb[10] << 24);
	alloc_len |= (cdb[11] << 16);
	alloc_len |= (cdb[12] << 8);
	alloc_len |=  cdb[13];

	trace_seq_printf(p, "%s lba=%llu alloc_len=%u", cmd,
			 (unsigned long long)lba, alloc_len);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_maintenance_in(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u32 alloc_len;

	switch (SERVICE_ACTION16(cdb)) {
	case MI_REPORT_IDENTIFYING_INFORMATION:
		cmd = "REPORT_IDENTIFYING_INFORMATION";
		break;
	case MI_REPORT_TARGET_PGS:
		cmd = "REPORT_TARGET_PORT_GROUPS";
		break;
	case MI_REPORT_ALIASES:
		cmd = "REPORT_ALIASES";
		break;
	case MI_REPORT_SUPPORTED_OPERATION_CODES:
		cmd = "REPORT_SUPPORTED_OPERATION_CODES";
		break;
	case MI_REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS:
		cmd = "REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS";
		break;
	case MI_REPORT_PRIORITY:
		cmd = "REPORT_PRIORITY";
		break;
	case MI_REPORT_TIMESTAMP:
		cmd = "REPORT_TIMESTAMP";
		break;
	case MI_MANAGEMENT_PROTOCOL_IN:
		cmd = "MANAGEMENT_PROTOCOL_IN";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	alloc_len = get_unaligned_be32(&cdb[6]);

	trace_seq_printf(p, "%s alloc_len=%u", cmd, alloc_len);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_maintenance_out(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u32 alloc_len;

	switch (SERVICE_ACTION16(cdb)) {
	case MO_SET_IDENTIFYING_INFORMATION:
		cmd = "SET_IDENTIFYING_INFORMATION";
		break;
	case MO_SET_TARGET_PGS:
		cmd = "SET_TARGET_PORT_GROUPS";
		break;
	case MO_CHANGE_ALIASES:
		cmd = "CHANGE_ALIASES";
		break;
	case MO_SET_PRIORITY:
		cmd = "SET_PRIORITY";
		break;
	case MO_SET_TIMESTAMP:
		cmd = "SET_TIMESTAMP";
		break;
	case MO_MANAGEMENT_PROTOCOL_OUT:
		cmd = "MANAGEMENT_PROTOCOL_OUT";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	alloc_len = get_unaligned_be32(&cdb[6]);

	trace_seq_printf(p, "%s alloc_len=%u", cmd, alloc_len);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_zbc_in(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u64 zone_id;
	u32 alloc_len;
	u8 options;

	switch (SERVICE_ACTION16(cdb)) {
	case ZI_REPORT_ZONES:
		cmd = "REPORT_ZONES";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	zone_id = get_unaligned_be64(&cdb[2]);
	alloc_len = get_unaligned_be32(&cdb[10]);
	options = cdb[14] & 0x3f;

	trace_seq_printf(p, "%s zone=%llu alloc_len=%u options=%u partial=%u",
			 cmd, (unsigned long long)zone_id, alloc_len,
			 options, (cdb[14] >> 7) & 1);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_zbc_out(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p), *cmd;
	u64 zone_id;

	switch (SERVICE_ACTION16(cdb)) {
	case ZO_CLOSE_ZONE:
		cmd = "CLOSE_ZONE";
		break;
	case ZO_FINISH_ZONE:
		cmd = "FINISH_ZONE";
		break;
	case ZO_OPEN_ZONE:
		cmd = "OPEN_ZONE";
		break;
	case ZO_RESET_WRITE_POINTER:
		cmd = "RESET_WRITE_POINTER";
		break;
	default:
		trace_seq_puts(p, "UNKNOWN");
		goto out;
	}

	zone_id = get_unaligned_be64(&cdb[2]);

	trace_seq_printf(p, "%s zone=%llu all=%u", cmd,
			 (unsigned long long)zone_id, cdb[14] & 1);

out:
	trace_seq_putc(p, 0);

	return ret;
}

static const char *
scsi_trace_varlen(struct trace_seq *p, unsigned char *cdb, int len)
{
	switch (SERVICE_ACTION32(cdb)) {
	case READ_32:
	case VERIFY_32:
	case WRITE_32:
	case WRITE_SAME_32:
		return scsi_trace_rw32(p, cdb, len);
	default:
		return scsi_trace_misc(p, cdb, len);
	}
}

static const char *
scsi_trace_misc(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_putc(p, '-');
	trace_seq_putc(p, 0);

	return ret;
}

const char *
scsi_trace_parse_cdb(struct trace_seq *p, unsigned char *cdb, int len)
{
	switch (cdb[0]) {
	case READ_6:
	case WRITE_6:
		return scsi_trace_rw6(p, cdb, len);
	case READ_10:
	case VERIFY:
	case WRITE_10:
	case WRITE_SAME:
		return scsi_trace_rw10(p, cdb, len);
	case READ_12:
	case VERIFY_12:
	case WRITE_12:
		return scsi_trace_rw12(p, cdb, len);
	case READ_16:
	case VERIFY_16:
	case WRITE_16:
	case WRITE_SAME_16:
		return scsi_trace_rw16(p, cdb, len);
	case UNMAP:
		return scsi_trace_unmap(p, cdb, len);
	case SERVICE_ACTION_IN_16:
		return scsi_trace_service_action_in(p, cdb, len);
	case VARIABLE_LENGTH_CMD:
		return scsi_trace_varlen(p, cdb, len);
	case MAINTENANCE_IN:
		return scsi_trace_maintenance_in(p, cdb, len);
	case MAINTENANCE_OUT:
		return scsi_trace_maintenance_out(p, cdb, len);
	case ZBC_IN:
		return scsi_trace_zbc_in(p, cdb, len);
	case ZBC_OUT:
		return scsi_trace_zbc_out(p, cdb, len);
	default:
		return scsi_trace_misc(p, cdb, len);
	}
}

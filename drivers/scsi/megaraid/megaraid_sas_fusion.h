/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2009-2013  LSI Corporation
 *  Copyright (c) 2013-2014  Avago Technologies
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  FILE: megaraid_sas_fusion.h
 *
 *  Authors: Avago Technologies
 *           Manoj Jose
 *           Sumant Patro
 *           Kashyap Desai <kashyap.desai@avagotech.com>
 *           Sumit Saxena <sumit.saxena@avagotech.com>
 *
 *  Send feedback to: megaraidlinux.pdl@avagotech.com
 *
 *  Mail to: Avago Technologies, 350 West Trimble Road, Building 90,
 *  San Jose, California 95131
 */

#ifndef _MEGARAID_SAS_FUSION_H_
#define _MEGARAID_SAS_FUSION_H_

/* Fusion defines */
#define MEGASAS_CHAIN_FRAME_SZ_MIN 1024
#define MFI_FUSION_ENABLE_INTERRUPT_MASK (0x00000009)
#define MEGASAS_MAX_CHAIN_SHIFT			5
#define MEGASAS_MAX_CHAIN_SIZE_UNITS_MASK	0x400000
#define MEGASAS_MAX_CHAIN_SIZE_MASK		0x3E0
#define MEGASAS_256K_IO				128
#define MEGASAS_1MB_IO				(MEGASAS_256K_IO * 4)
#define MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE 256
#define MEGASAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST   0xF0
#define MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST         0xF1
#define MEGASAS_LOAD_BALANCE_FLAG		    0x1
#define MEGASAS_DCMD_MBOX_PEND_FLAG		    0x1
#define HOST_DIAG_WRITE_ENABLE			    0x80
#define HOST_DIAG_RESET_ADAPTER			    0x4
#define MEGASAS_FUSION_MAX_RESET_TRIES		    3
#define MAX_MSIX_QUEUES_FUSION			    128

/* Invader defines */
#define MPI2_TYPE_CUDA				    0x2
#define MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH   0x4000
#define	MR_RL_FLAGS_GRANT_DESTINATION_CPU0	    0x00
#define	MR_RL_FLAGS_GRANT_DESTINATION_CPU1	    0x10
#define	MR_RL_FLAGS_GRANT_DESTINATION_CUDA	    0x80
#define MR_RL_FLAGS_SEQ_NUM_ENABLE		    0x8

/* T10 PI defines */
#define MR_PROT_INFO_TYPE_CONTROLLER                0x8
#define MEGASAS_SCSI_VARIABLE_LENGTH_CMD            0x7f
#define MEGASAS_SCSI_SERVICE_ACTION_READ32          0x9
#define MEGASAS_SCSI_SERVICE_ACTION_WRITE32         0xB
#define MEGASAS_SCSI_ADDL_CDB_LEN                   0x18
#define MEGASAS_RD_WR_PROTECT_CHECK_ALL		    0x20
#define MEGASAS_RD_WR_PROTECT_CHECK_NONE	    0x60

#define MPI2_SUP_REPLY_POST_HOST_INDEX_OFFSET   (0x0000030C)
#define MPI2_REPLY_POST_HOST_INDEX_OFFSET	(0x0000006C)

/*
 * Raid context flags
 */

#define MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT   0x4
#define MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_MASK    0x30
enum MR_RAID_FLAGS_IO_SUB_TYPE {
	MR_RAID_FLAGS_IO_SUB_TYPE_NONE = 0,
	MR_RAID_FLAGS_IO_SUB_TYPE_SYSTEM_PD = 1,
};

/*
 * Request descriptor types
 */
#define MEGASAS_REQ_DESCRIPT_FLAGS_LD_IO           0x7
#define MEGASAS_REQ_DESCRIPT_FLAGS_MFA             0x1
#define MEGASAS_REQ_DESCRIPT_FLAGS_NO_LOCK	   0x2
#define MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT      1

#define MEGASAS_FP_CMD_LEN	16
#define MEGASAS_FUSION_IN_RESET 0
#define THRESHOLD_REPLY_COUNT 50
#define JBOD_MAPS_COUNT	2

enum MR_FUSION_ADAPTER_TYPE {
	THUNDERBOLT_SERIES = 0,
	INVADER_SERIES = 1,
};

/*
 * Raid Context structure which describes MegaRAID specific IO Parameters
 * This resides at offset 0x60 where the SGL normally starts in MPT IO Frames
 */

struct RAID_CONTEXT {
#if   defined(__BIG_ENDIAN_BITFIELD)
	u8	nseg:4;
	u8	Type:4;
#else
	u8	Type:4;
	u8	nseg:4;
#endif
	u8	resvd0;
	__le16	timeoutValue;
	u8      regLockFlags;
	u8      resvd1;
	__le16	VirtualDiskTgtId;
	__le64	regLockRowLBA;
	__le32	regLockLength;
	__le16	nextLMId;
	u8      exStatus;
	u8      status;
	u8      RAIDFlags;
	u8      numSGE;
	__le16	configSeqNum;
	u8      spanArm;
	u8      priority;
	u8	numSGEExt;
	u8      resvd2;
};

#define RAID_CTX_SPANARM_ARM_SHIFT	(0)
#define RAID_CTX_SPANARM_ARM_MASK	(0x1f)

#define RAID_CTX_SPANARM_SPAN_SHIFT	(5)
#define RAID_CTX_SPANARM_SPAN_MASK	(0xE0)

/*
 * define region lock types
 */
enum REGION_TYPE {
	REGION_TYPE_UNUSED       = 0,
	REGION_TYPE_SHARED_READ  = 1,
	REGION_TYPE_SHARED_WRITE = 2,
	REGION_TYPE_EXCLUSIVE    = 3,
};

/* MPI2 defines */
#define MPI2_FUNCTION_IOC_INIT              (0x02) /* IOC Init */
#define MPI2_WHOINIT_HOST_DRIVER            (0x04)
#define MPI2_VERSION_MAJOR                  (0x02)
#define MPI2_VERSION_MINOR                  (0x00)
#define MPI2_VERSION_MAJOR_MASK             (0xFF00)
#define MPI2_VERSION_MAJOR_SHIFT            (8)
#define MPI2_VERSION_MINOR_MASK             (0x00FF)
#define MPI2_VERSION_MINOR_SHIFT            (0)
#define MPI2_VERSION ((MPI2_VERSION_MAJOR << MPI2_VERSION_MAJOR_SHIFT) | \
		      MPI2_VERSION_MINOR)
#define MPI2_HEADER_VERSION_UNIT            (0x10)
#define MPI2_HEADER_VERSION_DEV             (0x00)
#define MPI2_HEADER_VERSION_UNIT_MASK       (0xFF00)
#define MPI2_HEADER_VERSION_UNIT_SHIFT      (8)
#define MPI2_HEADER_VERSION_DEV_MASK        (0x00FF)
#define MPI2_HEADER_VERSION_DEV_SHIFT       (0)
#define MPI2_HEADER_VERSION ((MPI2_HEADER_VERSION_UNIT << 8) | \
			     MPI2_HEADER_VERSION_DEV)
#define MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR      (0x03)
#define MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG        (0x8000)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG          (0x0400)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP       (0x0003)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_APPTAG          (0x0200)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD           (0x0100)
#define MPI2_SCSIIO_EEDPFLAGS_INSERT_OP             (0x0004)
#define MPI2_FUNCTION_SCSI_IO_REQUEST               (0x00) /* SCSI IO */
#define MPI2_FUNCTION_SCSI_TASK_MGMT                (0x01)
#define MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY       (0x03)
#define MPI2_REQ_DESCRIPT_FLAGS_FP_IO               (0x06)
#define MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO                 (0x00)
#define MPI2_SGE_FLAGS_64_BIT_ADDRESSING        (0x02)
#define MPI2_SCSIIO_CONTROL_WRITE               (0x01000000)
#define MPI2_SCSIIO_CONTROL_READ                (0x02000000)
#define MPI2_REQ_DESCRIPT_FLAGS_TYPE_MASK       (0x0E)
#define MPI2_RPY_DESCRIPT_FLAGS_UNUSED          (0x0F)
#define MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS (0x00)
#define MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK       (0x0F)
#define MPI2_WRSEQ_FLUSH_KEY_VALUE              (0x0)
#define MPI2_WRITE_SEQUENCE_OFFSET              (0x00000004)
#define MPI2_WRSEQ_1ST_KEY_VALUE                (0xF)
#define MPI2_WRSEQ_2ND_KEY_VALUE                (0x4)
#define MPI2_WRSEQ_3RD_KEY_VALUE                (0xB)
#define MPI2_WRSEQ_4TH_KEY_VALUE                (0x2)
#define MPI2_WRSEQ_5TH_KEY_VALUE                (0x7)
#define MPI2_WRSEQ_6TH_KEY_VALUE                (0xD)

struct MPI25_IEEE_SGE_CHAIN64 {
	__le64			Address;
	__le32			Length;
	__le16			Reserved1;
	u8                      NextChainOffset;
	u8                      Flags;
};

struct MPI2_SGE_SIMPLE_UNION {
	__le32                     FlagsLength;
	union {
		__le32                 Address32;
		__le64                 Address64;
	} u;
};

struct MPI2_SCSI_IO_CDB_EEDP32 {
	u8                      CDB[20];                    /* 0x00 */
	__be32			PrimaryReferenceTag;        /* 0x14 */
	__be16			PrimaryApplicationTag;      /* 0x18 */
	__be16			PrimaryApplicationTagMask;  /* 0x1A */
	__le32			TransferLength;             /* 0x1C */
};

struct MPI2_SGE_CHAIN_UNION {
	__le16			Length;
	u8                      NextChainOffset;
	u8                      Flags;
	union {
		__le32		Address32;
		__le64		Address64;
	} u;
};

struct MPI2_IEEE_SGE_SIMPLE32 {
	__le32			Address;
	__le32			FlagsLength;
};

struct MPI2_IEEE_SGE_CHAIN32 {
	__le32			Address;
	__le32			FlagsLength;
};

struct MPI2_IEEE_SGE_SIMPLE64 {
	__le64			Address;
	__le32			Length;
	__le16			Reserved1;
	u8                      Reserved2;
	u8                      Flags;
};

struct MPI2_IEEE_SGE_CHAIN64 {
	__le64			Address;
	__le32			Length;
	__le16			Reserved1;
	u8                      Reserved2;
	u8                      Flags;
};

union MPI2_IEEE_SGE_SIMPLE_UNION {
	struct MPI2_IEEE_SGE_SIMPLE32  Simple32;
	struct MPI2_IEEE_SGE_SIMPLE64  Simple64;
};

union MPI2_IEEE_SGE_CHAIN_UNION {
	struct MPI2_IEEE_SGE_CHAIN32   Chain32;
	struct MPI2_IEEE_SGE_CHAIN64   Chain64;
};

union MPI2_SGE_IO_UNION {
	struct MPI2_SGE_SIMPLE_UNION       MpiSimple;
	struct MPI2_SGE_CHAIN_UNION        MpiChain;
	union MPI2_IEEE_SGE_SIMPLE_UNION  IeeeSimple;
	union MPI2_IEEE_SGE_CHAIN_UNION   IeeeChain;
};

union MPI2_SCSI_IO_CDB_UNION {
	u8                      CDB32[32];
	struct MPI2_SCSI_IO_CDB_EEDP32 EEDP32;
	struct MPI2_SGE_SIMPLE_UNION SGE;
};

/****************************************************************************
*  SCSI Task Management messages
****************************************************************************/

/*SCSI Task Management Request Message */
struct MPI2_SCSI_TASK_MANAGE_REQUEST {
	u16 DevHandle;		/*0x00 */
	u8 ChainOffset;		/*0x02 */
	u8 Function;		/*0x03 */
	u8 Reserved1;		/*0x04 */
	u8 TaskType;		/*0x05 */
	u8 Reserved2;		/*0x06 */
	u8 MsgFlags;		/*0x07 */
	u8 VP_ID;		/*0x08 */
	u8 VF_ID;		/*0x09 */
	u16 Reserved3;		/*0x0A */
	u8 LUN[8];		/*0x0C */
	u32 Reserved4[7];	/*0x14 */
	u16 TaskMID;		/*0x30 */
	u16 Reserved5;		/*0x32 */
};


/*SCSI Task Management Reply Message */
struct MPI2_SCSI_TASK_MANAGE_REPLY {
	u16 DevHandle;		/*0x00 */
	u8 MsgLength;		/*0x02 */
	u8 Function;		/*0x03 */
	u8 ResponseCode;	/*0x04 */
	u8 TaskType;		/*0x05 */
	u8 Reserved1;		/*0x06 */
	u8 MsgFlags;		/*0x07 */
	u8 VP_ID;		/*0x08 */
	u8 VF_ID;		/*0x09 */
	u16 Reserved2;		/*0x0A */
	u16 Reserved3;		/*0x0C */
	u16 IOCStatus;		/*0x0E */
	u32 IOCLogInfo;		/*0x10 */
	u32 TerminationCount;	/*0x14 */
	u32 ResponseInfo;	/*0x18 */
};

struct MR_TM_REQUEST {
	char request[128];
};

struct MR_TM_REPLY {
	char reply[128];
};

/* SCSI Task Management Request Message */
struct MR_TASK_MANAGE_REQUEST {
	/*To be type casted to struct MPI2_SCSI_TASK_MANAGE_REQUEST */
	struct MR_TM_REQUEST         TmRequest;
	union {
		struct {
#if   defined(__BIG_ENDIAN_BITFIELD)
			u32 reserved1:30;
			u32 isTMForPD:1;
			u32 isTMForLD:1;
#else
			u32 isTMForLD:1;
			u32 isTMForPD:1;
			u32 reserved1:30;
#endif
			u32 reserved2;
		} tmReqFlags;
		struct MR_TM_REPLY   TMReply;
	};
};

/* TaskType values */

#define MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK           (0x01)
#define MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET        (0x02)
#define MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET         (0x03)
#define MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET   (0x05)
#define MPI2_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET       (0x06)
#define MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK           (0x07)
#define MPI2_SCSITASKMGMT_TASKTYPE_CLR_ACA              (0x08)
#define MPI2_SCSITASKMGMT_TASKTYPE_QRY_TASK_SET         (0x09)
#define MPI2_SCSITASKMGMT_TASKTYPE_QRY_ASYNC_EVENT      (0x0A)

/* ResponseCode values */

#define MPI2_SCSITASKMGMT_RSP_TM_COMPLETE               (0x00)
#define MPI2_SCSITASKMGMT_RSP_INVALID_FRAME             (0x02)
#define MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED          (0x04)
#define MPI2_SCSITASKMGMT_RSP_TM_FAILED                 (0x05)
#define MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED              (0x08)
#define MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN            (0x09)
#define MPI2_SCSITASKMGMT_RSP_TM_OVERLAPPED_TAG         (0x0A)
#define MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC          (0x80)

/*
 * RAID SCSI IO Request Message
 * Total SGE count will be one less than  _MPI2_SCSI_IO_REQUEST
 */
struct MPI2_RAID_SCSI_IO_REQUEST {
	__le16			DevHandle;                      /* 0x00 */
	u8                      ChainOffset;                    /* 0x02 */
	u8                      Function;                       /* 0x03 */
	__le16			Reserved1;                      /* 0x04 */
	u8                      Reserved2;                      /* 0x06 */
	u8                      MsgFlags;                       /* 0x07 */
	u8                      VP_ID;                          /* 0x08 */
	u8                      VF_ID;                          /* 0x09 */
	__le16			Reserved3;                      /* 0x0A */
	__le32			SenseBufferLowAddress;          /* 0x0C */
	__le16			SGLFlags;                       /* 0x10 */
	u8                      SenseBufferLength;              /* 0x12 */
	u8                      Reserved4;                      /* 0x13 */
	u8                      SGLOffset0;                     /* 0x14 */
	u8                      SGLOffset1;                     /* 0x15 */
	u8                      SGLOffset2;                     /* 0x16 */
	u8                      SGLOffset3;                     /* 0x17 */
	__le32			SkipCount;                      /* 0x18 */
	__le32			DataLength;                     /* 0x1C */
	__le32			BidirectionalDataLength;        /* 0x20 */
	__le16			IoFlags;                        /* 0x24 */
	__le16			EEDPFlags;                      /* 0x26 */
	__le32			EEDPBlockSize;                  /* 0x28 */
	__le32			SecondaryReferenceTag;          /* 0x2C */
	__le16			SecondaryApplicationTag;        /* 0x30 */
	__le16			ApplicationTagTranslationMask;  /* 0x32 */
	u8                      LUN[8];                         /* 0x34 */
	__le32			Control;                        /* 0x3C */
	union MPI2_SCSI_IO_CDB_UNION  CDB;			/* 0x40 */
	struct RAID_CONTEXT	RaidContext;                    /* 0x60 */
	union MPI2_SGE_IO_UNION       SGL;			/* 0x80 */
};

/*
 * MPT RAID MFA IO Descriptor.
 */
struct MEGASAS_RAID_MFA_IO_REQUEST_DESCRIPTOR {
	u32     RequestFlags:8;
	u32     MessageAddress1:24;
	u32     MessageAddress2;
};

/* Default Request Descriptor */
struct MPI2_DEFAULT_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	__le16		LMID;                       /* 0x04 */
	__le16		DescriptorTypeDependent;    /* 0x06 */
};

/* High Priority Request Descriptor */
struct MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	__le16		LMID;                       /* 0x04 */
	__le16		Reserved1;                  /* 0x06 */
};

/* SCSI IO Request Descriptor */
struct MPI2_SCSI_IO_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	__le16		LMID;                       /* 0x04 */
	__le16		DevHandle;                  /* 0x06 */
};

/* SCSI Target Request Descriptor */
struct MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	__le16		LMID;                       /* 0x04 */
	__le16		IoIndex;                    /* 0x06 */
};

/* RAID Accelerator Request Descriptor */
struct MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	__le16		LMID;                       /* 0x04 */
	__le16		Reserved;                   /* 0x06 */
};

/* union of Request Descriptors */
union MEGASAS_REQUEST_DESCRIPTOR_UNION {
	struct MPI2_DEFAULT_REQUEST_DESCRIPTOR             Default;
	struct MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR       HighPriority;
	struct MPI2_SCSI_IO_REQUEST_DESCRIPTOR             SCSIIO;
	struct MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR         SCSITarget;
	struct MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR          RAIDAccelerator;
	struct MEGASAS_RAID_MFA_IO_REQUEST_DESCRIPTOR      MFAIo;
	union {
		struct {
			__le32 low;
			__le32 high;
		} u;
		__le64 Words;
	};
};

/* Default Reply Descriptor */
struct MPI2_DEFAULT_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		DescriptorTypeDependent1;   /* 0x02 */
	__le32		DescriptorTypeDependent2;   /* 0x04 */
};

/* Address Reply Descriptor */
struct MPI2_ADDRESS_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	__le32		ReplyFrameAddress;          /* 0x04 */
};

/* SCSI IO Success Reply Descriptor */
struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	__le16		TaskTag;                    /* 0x04 */
	__le16		Reserved1;                  /* 0x06 */
};

/* TargetAssist Success Reply Descriptor */
struct MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	u8              SequenceNumber;             /* 0x04 */
	u8              Reserved1;                  /* 0x05 */
	__le16		IoIndex;                    /* 0x06 */
};

/* Target Command Buffer Reply Descriptor */
struct MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u8              VP_ID;                      /* 0x02 */
	u8              Flags;                      /* 0x03 */
	__le16		InitiatorDevHandle;         /* 0x04 */
	__le16		IoIndex;                    /* 0x06 */
};

/* RAID Accelerator Success Reply Descriptor */
struct MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	__le16		SMID;                       /* 0x02 */
	__le32		Reserved;                   /* 0x04 */
};

/* union of Reply Descriptors */
union MPI2_REPLY_DESCRIPTORS_UNION {
	struct MPI2_DEFAULT_REPLY_DESCRIPTOR                   Default;
	struct MPI2_ADDRESS_REPLY_DESCRIPTOR                   AddressReply;
	struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR           SCSIIOSuccess;
	struct MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR TargetAssistSuccess;
	struct MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR TargetCommandBuffer;
	struct MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR
	RAIDAcceleratorSuccess;
	__le64                                             Words;
};

/* IOCInit Request message */
struct MPI2_IOC_INIT_REQUEST {
	u8                      WhoInit;                        /* 0x00 */
	u8                      Reserved1;                      /* 0x01 */
	u8                      ChainOffset;                    /* 0x02 */
	u8                      Function;                       /* 0x03 */
	__le16			Reserved2;                      /* 0x04 */
	u8                      Reserved3;                      /* 0x06 */
	u8                      MsgFlags;                       /* 0x07 */
	u8                      VP_ID;                          /* 0x08 */
	u8                      VF_ID;                          /* 0x09 */
	__le16			Reserved4;                      /* 0x0A */
	__le16			MsgVersion;                     /* 0x0C */
	__le16			HeaderVersion;                  /* 0x0E */
	u32                     Reserved5;                      /* 0x10 */
	__le16			Reserved6;                      /* 0x14 */
	u8                      Reserved7;                      /* 0x16 */
	u8                      HostMSIxVectors;                /* 0x17 */
	__le16			Reserved8;                      /* 0x18 */
	__le16			SystemRequestFrameSize;         /* 0x1A */
	__le16			ReplyDescriptorPostQueueDepth;  /* 0x1C */
	__le16			ReplyFreeQueueDepth;            /* 0x1E */
	__le32			SenseBufferAddressHigh;         /* 0x20 */
	__le32			SystemReplyAddressHigh;         /* 0x24 */
	__le64			SystemRequestFrameBaseAddress;  /* 0x28 */
	__le64			ReplyDescriptorPostQueueAddress;/* 0x30 */
	__le64			ReplyFreeQueueAddress;          /* 0x38 */
	__le64			TimeStamp;                      /* 0x40 */
};

/* mrpriv defines */
#define MR_PD_INVALID 0xFFFF
#define MAX_SPAN_DEPTH 8
#define MAX_QUAD_DEPTH	MAX_SPAN_DEPTH
#define MAX_RAIDMAP_SPAN_DEPTH (MAX_SPAN_DEPTH)
#define MAX_ROW_SIZE 32
#define MAX_RAIDMAP_ROW_SIZE (MAX_ROW_SIZE)
#define MAX_LOGICAL_DRIVES 64
#define MAX_LOGICAL_DRIVES_EXT 256
#define MAX_RAIDMAP_LOGICAL_DRIVES (MAX_LOGICAL_DRIVES)
#define MAX_RAIDMAP_VIEWS (MAX_LOGICAL_DRIVES)
#define MAX_ARRAYS 128
#define MAX_RAIDMAP_ARRAYS (MAX_ARRAYS)
#define MAX_ARRAYS_EXT	256
#define MAX_API_ARRAYS_EXT (MAX_ARRAYS_EXT)
#define MAX_PHYSICAL_DEVICES 256
#define MAX_RAIDMAP_PHYSICAL_DEVICES (MAX_PHYSICAL_DEVICES)
#define MR_DCMD_LD_MAP_GET_INFO             0x0300e101
#define MR_DCMD_SYSTEM_PD_MAP_GET_INFO      0x0200e102
#define MR_DCMD_CTRL_SHARED_HOST_MEM_ALLOC  0x010e8485   /* SR-IOV HB alloc*/
#define MR_DCMD_LD_VF_MAP_GET_ALL_LDS_111   0x03200200
#define MR_DCMD_LD_VF_MAP_GET_ALL_LDS       0x03150200

struct MR_DEV_HANDLE_INFO {
	__le16	curDevHdl;
	u8      validHandles;
	u8      reserved;
	__le16	devHandle[2];
};

struct MR_ARRAY_INFO {
	__le16	pd[MAX_RAIDMAP_ROW_SIZE];
};

struct MR_QUAD_ELEMENT {
	__le64     logStart;
	__le64     logEnd;
	__le64     offsetInSpan;
	__le32     diff;
	__le32     reserved1;
};

struct MR_SPAN_INFO {
	__le32             noElements;
	__le32             reserved1;
	struct MR_QUAD_ELEMENT quad[MAX_RAIDMAP_SPAN_DEPTH];
};

struct MR_LD_SPAN {
	__le64	 startBlk;
	__le64	 numBlks;
	__le16	 arrayRef;
	u8       spanRowSize;
	u8       spanRowDataSize;
	u8       reserved[4];
};

struct MR_SPAN_BLOCK_INFO {
	__le64          num_rows;
	struct MR_LD_SPAN   span;
	struct MR_SPAN_INFO block_span_info;
};

struct MR_LD_RAID {
	struct {
#if   defined(__BIG_ENDIAN_BITFIELD)
		u32     reserved4:5;
		u32     fpBypassRegionLock:1;
		u32     tmCapable:1;
		u32	fpNonRWCapable:1;
		u32     fpReadAcrossStripe:1;
		u32     fpWriteAcrossStripe:1;
		u32     fpReadCapable:1;
		u32     fpWriteCapable:1;
		u32     encryptionType:8;
		u32     pdPiMode:4;
		u32     ldPiMode:4;
		u32     reserved5:3;
		u32     fpCapable:1;
#else
		u32     fpCapable:1;
		u32     reserved5:3;
		u32     ldPiMode:4;
		u32     pdPiMode:4;
		u32     encryptionType:8;
		u32     fpWriteCapable:1;
		u32     fpReadCapable:1;
		u32     fpWriteAcrossStripe:1;
		u32     fpReadAcrossStripe:1;
		u32	fpNonRWCapable:1;
		u32     tmCapable:1;
		u32     fpBypassRegionLock:1;
		u32     reserved4:5;
#endif
	} capability;
	__le32     reserved6;
	__le64     size;
	u8      spanDepth;
	u8      level;
	u8      stripeShift;
	u8      rowSize;
	u8      rowDataSize;
	u8      writeMode;
	u8      PRL;
	u8      SRL;
	__le16     targetId;
	u8      ldState;
	u8      regTypeReqOnWrite;
	u8      modFactor;
	u8	regTypeReqOnRead;
	__le16     seqNum;

	struct {
		u32 ldSyncRequired:1;
		u32 reserved:31;
	} flags;

	u8	LUN[8]; /* 0x24 8 byte LUN field used for SCSI IO's */
	u8	fpIoTimeoutForLd;/*0x2C timeout value used by driver in FP IO*/
	u8      reserved3[0x80-0x2D]; /* 0x2D */
};

struct MR_LD_SPAN_MAP {
	struct MR_LD_RAID          ldRaid;
	u8                  dataArmMap[MAX_RAIDMAP_ROW_SIZE];
	struct MR_SPAN_BLOCK_INFO  spanBlock[MAX_RAIDMAP_SPAN_DEPTH];
};

struct MR_FW_RAID_MAP {
	__le32                 totalSize;
	union {
		struct {
			__le32         maxLd;
			__le32         maxSpanDepth;
			__le32         maxRowSize;
			__le32         maxPdCount;
			__le32         maxArrays;
		} validationInfo;
		__le32             version[5];
	};

	__le32                 ldCount;
	__le32                 Reserved1;
	u8                  ldTgtIdToLd[MAX_RAIDMAP_LOGICAL_DRIVES+
					MAX_RAIDMAP_VIEWS];
	u8                  fpPdIoTimeoutSec;
	u8                  reserved2[7];
	struct MR_ARRAY_INFO       arMapInfo[MAX_RAIDMAP_ARRAYS];
	struct MR_DEV_HANDLE_INFO  devHndlInfo[MAX_RAIDMAP_PHYSICAL_DEVICES];
	struct MR_LD_SPAN_MAP      ldSpanMap[1];
};

struct IO_REQUEST_INFO {
	u64 ldStartBlock;
	u32 numBlocks;
	u16 ldTgtId;
	u8 isRead;
	__le16 devHandle;
	u64 pdBlock;
	u8 fpOkForIo;
	u8 IoforUnevenSpan;
	u8 start_span;
	u8 do_fp_rlbypass;
	u64 start_row;
	u8  span_arm;	/* span[7:5], arm[4:0] */
	u8  pd_after_lb;
};

struct MR_LD_TARGET_SYNC {
	u8  targetId;
	u8  reserved;
	__le16 seqNum;
};

#define IEEE_SGE_FLAGS_ADDR_MASK            (0x03)
#define IEEE_SGE_FLAGS_SYSTEM_ADDR          (0x00)
#define IEEE_SGE_FLAGS_IOCDDR_ADDR          (0x01)
#define IEEE_SGE_FLAGS_IOCPLB_ADDR          (0x02)
#define IEEE_SGE_FLAGS_IOCPLBNTA_ADDR       (0x03)
#define IEEE_SGE_FLAGS_CHAIN_ELEMENT        (0x80)
#define IEEE_SGE_FLAGS_END_OF_LIST          (0x40)

struct megasas_register_set;
struct megasas_instance;

union desc_word {
	u64 word;
	struct {
		u32 low;
		u32 high;
	} u;
};

struct megasas_cmd_fusion {
	struct MPI2_RAID_SCSI_IO_REQUEST	*io_request;
	dma_addr_t			io_request_phys_addr;

	union MPI2_SGE_IO_UNION	*sg_frame;
	dma_addr_t		sg_frame_phys_addr;

	u8 *sense;
	dma_addr_t sense_phys_addr;

	struct list_head list;
	struct scsi_cmnd *scmd;
	struct megasas_instance *instance;

	u8 retry_for_fw_reset;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION  *request_desc;

	/*
	 * Context for a MFI frame.
	 * Used to get the mfi cmd from list when a MFI cmd is completed
	 */
	u32 sync_cmd_idx;
	u32 index;
	u8 pd_r1_lb;
	struct completion done;
};

struct LD_LOAD_BALANCE_INFO {
	u8	loadBalanceFlag;
	u8	reserved1;
	atomic_t     scsi_pending_cmds[MAX_PHYSICAL_DEVICES];
	u64     last_accessed_block[MAX_PHYSICAL_DEVICES];
};

/* SPAN_SET is info caclulated from span info from Raid map per LD */
typedef struct _LD_SPAN_SET {
	u64  log_start_lba;
	u64  log_end_lba;
	u64  span_row_start;
	u64  span_row_end;
	u64  data_strip_start;
	u64  data_strip_end;
	u64  data_row_start;
	u64  data_row_end;
	u8   strip_offset[MAX_SPAN_DEPTH];
	u32    span_row_data_width;
	u32    diff;
	u32    reserved[2];
} LD_SPAN_SET, *PLD_SPAN_SET;

typedef struct LOG_BLOCK_SPAN_INFO {
	LD_SPAN_SET  span_set[MAX_SPAN_DEPTH];
} LD_SPAN_INFO, *PLD_SPAN_INFO;

struct MR_FW_RAID_MAP_ALL {
	struct MR_FW_RAID_MAP raidMap;
	struct MR_LD_SPAN_MAP ldSpanMap[MAX_LOGICAL_DRIVES - 1];
} __attribute__ ((packed));

struct MR_DRV_RAID_MAP {
	/* total size of this structure, including this field.
	 * This feild will be manupulated by driver for ext raid map,
	 * else pick the value from firmware raid map.
	 */
	__le32                 totalSize;

	union {
	struct {
		__le32         maxLd;
		__le32         maxSpanDepth;
		__le32         maxRowSize;
		__le32         maxPdCount;
		__le32         maxArrays;
	} validationInfo;
	__le32             version[5];
	};

	/* timeout value used by driver in FP IOs*/
	u8                  fpPdIoTimeoutSec;
	u8                  reserved2[7];

	__le16                 ldCount;
	__le16                 arCount;
	__le16                 spanCount;
	__le16                 reserve3;

	struct MR_DEV_HANDLE_INFO  devHndlInfo[MAX_RAIDMAP_PHYSICAL_DEVICES];
	u8                  ldTgtIdToLd[MAX_LOGICAL_DRIVES_EXT];
	struct MR_ARRAY_INFO       arMapInfo[MAX_API_ARRAYS_EXT];
	struct MR_LD_SPAN_MAP      ldSpanMap[1];

};

/* Driver raid map size is same as raid map ext
 * MR_DRV_RAID_MAP_ALL is created to sync with old raid.
 * And it is mainly for code re-use purpose.
 */
struct MR_DRV_RAID_MAP_ALL {

	struct MR_DRV_RAID_MAP raidMap;
	struct MR_LD_SPAN_MAP      ldSpanMap[MAX_LOGICAL_DRIVES_EXT - 1];
} __packed;



struct MR_FW_RAID_MAP_EXT {
	/* Not usred in new map */
	u32                 reserved;

	union {
	struct {
		u32         maxLd;
		u32         maxSpanDepth;
		u32         maxRowSize;
		u32         maxPdCount;
		u32         maxArrays;
	} validationInfo;
	u32             version[5];
	};

	u8                  fpPdIoTimeoutSec;
	u8                  reserved2[7];

	__le16                 ldCount;
	__le16                 arCount;
	__le16                 spanCount;
	__le16                 reserve3;

	struct MR_DEV_HANDLE_INFO  devHndlInfo[MAX_RAIDMAP_PHYSICAL_DEVICES];
	u8                  ldTgtIdToLd[MAX_LOGICAL_DRIVES_EXT];
	struct MR_ARRAY_INFO       arMapInfo[MAX_API_ARRAYS_EXT];
	struct MR_LD_SPAN_MAP      ldSpanMap[MAX_LOGICAL_DRIVES_EXT];
};

/*
 *  * define MR_PD_CFG_SEQ structure for system PDs
 *   */
struct MR_PD_CFG_SEQ {
	u16 seqNum;
	u16 devHandle;
	struct {
#if   defined(__BIG_ENDIAN_BITFIELD)
		u8     reserved:7;
		u8     tmCapable:1;
#else
		u8     tmCapable:1;
		u8     reserved:7;
#endif
	} capability;
	u8  reserved[3];
} __packed;

struct MR_PD_CFG_SEQ_NUM_SYNC {
	__le32 size;
	__le32 count;
	struct MR_PD_CFG_SEQ seq[1];
} __packed;

struct MPI2_IOC_INIT_RDPQ_ARRAY_ENTRY {
	u64 RDPQBaseAddress;
	u32 Reserved1;
	u32 Reserved2;
};

struct fusion_context {
	struct megasas_cmd_fusion **cmd_list;
	dma_addr_t req_frames_desc_phys;
	u8 *req_frames_desc;

	struct dma_pool *io_request_frames_pool;
	dma_addr_t io_request_frames_phys;
	u8 *io_request_frames;

	struct dma_pool *sg_dma_pool;
	struct dma_pool *sense_dma_pool;

	dma_addr_t reply_frames_desc_phys[MAX_MSIX_QUEUES_FUSION];
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_frames_desc[MAX_MSIX_QUEUES_FUSION];
	struct dma_pool *reply_frames_desc_pool;

	u16 last_reply_idx[MAX_MSIX_QUEUES_FUSION];

	u32 reply_q_depth;
	u32 request_alloc_sz;
	u32 reply_alloc_sz;
	u32 io_frames_alloc_sz;

	struct MPI2_IOC_INIT_RDPQ_ARRAY_ENTRY *rdpq_virt;
	dma_addr_t rdpq_phys;
	u16	max_sge_in_main_msg;
	u16	max_sge_in_chain;

	u8	chain_offset_io_request;
	u8	chain_offset_mfi_pthru;

	struct MR_FW_RAID_MAP_ALL *ld_map[2];
	dma_addr_t ld_map_phys[2];

	/*Non dma-able memory. Driver local copy.*/
	struct MR_DRV_RAID_MAP_ALL *ld_drv_map[2];

	u32 max_map_sz;
	u32 current_map_sz;
	u32 drv_map_sz;
	u32 drv_map_pages;
	struct MR_PD_CFG_SEQ_NUM_SYNC	*pd_seq_sync[JBOD_MAPS_COUNT];
	dma_addr_t pd_seq_phys[JBOD_MAPS_COUNT];
	u8 fast_path_io;
	struct LD_LOAD_BALANCE_INFO load_balance_info[MAX_LOGICAL_DRIVES_EXT];
	LD_SPAN_INFO log_to_span[MAX_LOGICAL_DRIVES_EXT];
	u8 adapter_type;
};

union desc_value {
	__le64 word;
	struct {
		__le32 low;
		__le32 high;
	} u;
};

void megasas_free_cmds_fusion(struct megasas_instance *instance);
int megasas_ioc_init_fusion(struct megasas_instance *instance);
u8 megasas_get_map_info(struct megasas_instance *instance);
int megasas_sync_map_info(struct megasas_instance *instance);
void megasas_release_fusion(struct megasas_instance *instance);
void megasas_reset_reply_desc(struct megasas_instance *instance);
int megasas_check_mpio_paths(struct megasas_instance *instance,
			      struct scsi_cmnd *scmd);
void megasas_fusion_ocr_wq(struct work_struct *work);

#endif /* _MEGARAID_SAS_FUSION_H_ */

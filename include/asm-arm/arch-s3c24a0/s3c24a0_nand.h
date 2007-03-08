/*
 * s3c24a0_nand.h
 *
 * s3c24a0 NAND specific definiton
 *
 * $Id: s3c24a0_nand.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * Copyright (C) SAMSUNG MOBILE
 */

#ifndef _S3C24A0_NAND_H_
#define _S3C24A0_NAND_H_

#define bNAND_CTL(Nb)	__REG(0x40c00000 + (Nb))

#define NFCONF			bNAND_CTL(0x00)
#define NFCONT			bNAND_CTL(0x04)
#define NFCMMD			bNAND_CTL(0x08)
#define NFADDR			bNAND_CTL(0x0c)
#define NFDATA			bNAND_CTL(0x10)
#define NFMECCDATA0		bNAND_CTL(0x14)
#define NFMECCDATA1		bNAND_CTL(0x18)
#define NFMECCDATA2		bNAND_CTL(0x1c)
#define NFMECCDATA3		bNAND_CTL(0x20)
#define NFSECCDATA0		bNAND_CTL(0x24)
#define NFSECCDATA1		bNAND_CTL(0x28)
#define NFSTAT			bNAND_CTL(0x2c)
#define NFESTAT0		bNAND_CTL(0x30)
#define NFESTAT1		bNAND_CTL(0x34)
#define NFMECC0			bNAND_CTL(0x38)
#define NFMECC1			bNAND_CTL(0x3c)
#define NFSECC			bNAND_CTL(0x40)
#define NFSBLK			bNAND_CTL(0x44)
#define NFEBLK			bNAND_CTL(0x48)


/*
 * NFCONF
 */
#define fNFCONF_AdvanceFlash	Fld(1,22)
#define fNFCONF_TCEH			Fld(6,16)
#define fNFCONF_TACLS			Fld(3,12)
#define fNFCONF_TWRPH0			Fld(3,8)
#define fNFCONF_X16Device		Fld(1,7)
#define fNFCONF_TWRPH1			Fld(3,4)
#define fNFCONF_Hardware_nCE	Fld(1,3)
#define fNFCONF_BusWidth		Fld(1,2)
#define fNFCONF_PageSize		Fld(1,1)
#define fNFCONF_AddressCycle	Fld(1,0)

#define m1NFCONF_AdvanceFlash	FMsk(fNFCONF_AdvanceFlash)
#define m1NFCONF_TCEH			FMsk(fNFCONF_TCEH)
#define m1NFCONF_TACLS			FMsk(fNFCONF_TACLS)
#define m1NFCONF_TWRPH0			FMsk(fNFCONF_TWRPH0)
#define m1NFCONF_X16Device		FMsk(fNFCONF_X16Device)
#define m1NFCONF_TWRPH1			FMsk(fNFCONF_TWRPH1)
#define m1NFCONF_Hardware_nCE	FMsk(fNFCONF_Hardware_nCE)
#define m1NFCONF_BusWidth		FMsk(fNFCONF_BusWidth)
#define m1NFCONF_PageSize		FMsk(fNFCONF_PageSize)
#define m1NFCONF_AddressCycle	FMsk(fNFCONF_AddressCycle)

#define m0NFCONF_AdvanceFlash		(~m1NFCONF_AdvanceFlash)
#define m0NFCONF_TCEH				(~m1NFCONF_TCEH)
#define m0NFCONF_TACLS				(~m1NFCONF_TACLS)
#define m0NFCONF_TWRPH0				(~m1NFCONF_TWRPH0)
#define m0NFCONF_X16Device			(~m1NFCONF_X16Device)
#define m0NFCONF_TWRPH1				(~m1NFCONF_TWRPH1)
#define m0NFCONF_Hardware_nCE		(~m1NFCONF_Hardware_nCE)
#define m0NFCONF_BusWidth			(~m1NFCONF_BusWidth)
#define m0NFCONF_PageSize			(~m1NFCONF_PageSize)
#define m0NFCONF_AddressCycle		(~m1NFCONF_AddressCycle)

#define sNFCONF_TCEH(f_)			(FInsrt(f_,fNFCONF_TCEH)			& m1NFCONF_TCEH)
#define sNFCONF_TACLS(f_)			(FInsrt(f_,fNFCONF_TACLS)			& m1NFCONF_TACLS)
#define sNFCONF_TWRPH0(f_)			(FInsrt(f_,fNFCONF_TWRPH0)			& m1NFCONF_TWRPH0)
#define sNFCONF_TWRPH1(f_)			(FInsrt(f_,fNFCONF_TWRPH1)			& m1NFCONF_TWRPH1)
#define sNFCONF_Hardware_nCE(f_)	(FInsrt(f_,fNFCONF_Hardware_nCE)	& m1NFCONF_Hardware_nCE)


/*
 * NFCONT
 */
#define fNFCONT_LdStrAddr			Fld(12,16)
#define fNFCONT_EnbIllegalAccINT	Fld(1,15)
#define fNFCONT_EnbLoadINT			Fld(1,14)
#define fNFCONT_EnbStoreINT			Fld(1,13)
#define fNFCONT_EnbRnBINT			Fld(1,12)
#define fNFCONT_RnB_TransMode		Fld(1,11)
#define fNFCONT_SpareECCLock		Fld(1,10)
#define fNFCONT_MainECCLock			Fld(1,9)
#define fNFCONT_InitECC				Fld(1,8)
#define fNFCONT_Reg_nCE				Fld(1,7)
#define fNFCONT_LoadPageSize		Fld(3,4)
#define fNFCONT_Lock_tight			Fld(1,3)
#define fNFCONT_Lock				Fld(1,2)
#define fNFCONT_Mode				Fld(2,0)

#define m1NFCONT_LdStrAddr			FMsk(fNFCONT_LdStrAddr)
#define m1NFCONT_EnbIllegalAccINT	FMsk(fNFCONT_EnbIllegalAccINT)
#define m1NFCONT_EnbLoadINT			FMsk(fNFCONT_EnbLoadINT)
#define m1NFCONT_EnbStoreINT		FMsk(fNFCONT_EnbStoreINT)
#define m1NFCONT_EnbRnBINT			FMsk(fNFCONT_EnbRnBINT)
#define m1NFCONT_RnB_TransMode		FMsk(fNFCONT_RnB_TransMode)
#define m1NFCONT_SpareECCLock		FMsk(fNFCONT_SpareECCLock)
#define m1NFCONT_MainECCLock		FMsk(fNFCONT_MainECCLock)
#define m1NFCONT_InitECC			FMsk(fNFCONT_InitECC)
#define m1NFCONT_Reg_nCE			FMsk(fNFCONT_Reg_nCE)
#define m1NFCONT_LoadPageSize		FMsk(fNFCONT_LoadPageSize)
#define m1NFCONT_Lock_tight			FMsk(fNFCONT_Lock_tight)
#define m1NFCONT_Lock				FMsk(fNFCONT_Lock)
#define m1NFCONT_Mode				FMsk(fNFCONT_Mode)

#define m0NFCONT_LdStrAddr			(~m1NFCONT_LdStrAddr)
#define m0NFCONT_EnbIllegalAccINT	(~m1NFCONT_EnbIllegalAccINT)
#define m0NFCONT_EnbLoadINT			(~m1NFCONT_EnbLoadINT)
#define m0NFCONT_EnbStoreINT		(~m1NFCONT_EnbStoreINT)
#define m0NFCONT_EnbRnBINT			(~m1NFCONT_EnbRnBINT)
#define m0NFCONT_RnB_TransMode		(~m1NFCONT_RnB_TransMode)
#define m0NFCONT_SpareECCLock		(~m1NFCONT_SpareECCLock)
#define m0NFCONT_MainECCLock		(~m1NFCONT_MainECCLock)
#define m0NFCONT_InitECC			(~m1NFCONT_InitECC)
#define m0NFCONT_Reg_nCE			(~m1NFCONT_Reg_nCE)
#define m0NFCONT_LoadPageSize		(~m1NFCONT_LoadPageSize)
#define m0NFCONT_Lock_tight			(~m1NFCONT_Lock_tight)
#define m0NFCONT_Lock				(~m1NFCONT_Lock)
#define m0NFCONT_Mode				(~m1NFCONT_Mode)

#define sNFCONT_LdStrAddr(f_)			(FInsrt(f_,fNFCONT_LdStrAddr)			& m1NFCONT_LdStrAddr)
#define sNFCONT_EnbIllegalAccINT(f_)	(FInsrt(f_,fNFCONT_EnbIllegalAccINT)	& m1NFCONT_EnbIllegalAccINT)
#define sNFCONT_EnbLoadINT(f_)			(FInsrt(f_,fNFCONT_EnbLoadINT)			& m1NFCONT_EnbLoadINT)
#define sNFCONT_EnbStoreINT(f_)			(FInsrt(f_,fNFCONT_EnbStoreINT)			& m1NFCONT_EnbStoreINT)
#define sNFCONT_EnbRnBINT(f_)			(FInsrt(f_,fNFCONT_EnbRnBINT)			& m1NFCONT_EnbRnBINT)
#define sNFCONT_RnB_TransMode(f_)		(FInsrt(f_,fNFCONT_RnB_TransMode)		& m1NFCONT_RnB_TransMode)
#define sNFCONT_SpareECCLock(f_)		(FInsrt(f_,fNFCONT_SpareECCLock)		& m1NFCONT_SpareECCLock)
#define sNFCONT_MainECCLock(f_)			(FInsrt(f_,fNFCONT_MainECCLock)			& m1NFCONT_MainECCLock)
#define sNFCONT_InitECC(f_)				(FInsrt(f_,fNFCONT_InitECC)				& m1NFCONT_InitECC)
#define sNFCONT_Reg_nCE(f_)				(FInsrt(f_,fNFCONT_Reg_nCE)				& m1NFCONT_Reg_nCE)
#define sNFCONT_LoadPageSize(f_)		(FInsrt(f_,fNFCONT_LoadPageSize)		& m1NFCONT_LoadPageSize)
#define sNFCONT_Lock_tight(f_)			(FInsrt(f_,fNFCONT_Lock_tight)			& m1NFCONT_Lock_tight)
#define sNFCONT_Lock(f_)				(FInsrt(f_,fNFCONT_Lock)				& m1NFCONT_Lock)
#define sNFCONT_Mode(f_)				(FInsrt(f_,fNFCONT_Mode)				& m1NFCONT_Mode)


/*
 * NFCMMD
 */
#define fNFCMMD_NFCMMD1		Fld(8,8)
#define fNFCMMD_NFCMMD0		Fld(8,0)

#define m1NFCMMD_NFCMMD1	FMsk(fNFCMMD_NFCMMD1)
#define m1NFCMMD_NFCMMD0	FMsk(fNFCMMD_NFCMMD0)

#define m0NFCMMD_NFCMMD1	(~m1NFCMMD_NFCMMD1)
#define m0NFCMMD_NFCMMD0	(~m1NFCMMD_NFCMMD0)

#define sNFCMMD_NFCMMD1(f_)	(FInsrt(f_,fNFCMMD_NFCMMD1)	& m1NFCMMD_NFCMMD1)
#define sNFCMMD_NFCMMD0(f_)	(FInsrt(f_,fNFCMMD_NFCMMD0)	& m1NFCMMD_NFCMMD0)


/*
 * NFADDR
 */
#define fNFADDR_NFADDR3		Fld(8,24)
#define fNFADDR_NFADDR2		Fld(8,16)
#define fNFADDR_NFADDR1		Fld(8,8)
#define fNFADDR_NFADDR0		Fld(8,0)

#define m1NFADDR_NFADDR3	FMsk(fNFADDR_NFADDR3)
#define m1NFADDR_NFADDR2	FMsk(fNFADDR_NFADDR2)
#define m1NFADDR_NFADDR1	FMsk(fNFADDR_NFADDR1)
#define m1NFADDR_NFADDR0	FMsk(fNFADDR_NFADDR0)

#define m0NFADDR_NFADDR3	(~m1NFADDR_NFADDR3)
#define m0NFADDR_NFADDR2	(~m1NFADDR_NFADDR2)
#define m0NFADDR_NFADDR1	(~m1NFADDR_NFADDR1)
#define m0NFADDR_NFADDR0	(~m1NFADDR_NFADDR0)

#define sNFADDR_NFADDR3(f_)	(FInsrt(f_,fNFADDR_NFADDR3)	& m1NFADDR_NFADDR3)
#define sNFADDR_NFADDR2(f_)	(FInsrt(f_,fNFADDR_NFADDR2)	& m1NFADDR_NFADDR2)
#define sNFADDR_NFADDR1(f_)	(FInsrt(f_,fNFADDR_NFADDR1)	& m1NFADDR_NFADDR1)
#define sNFADDR_NFADDR0(f_)	(FInsrt(f_,fNFADDR_NFADDR0)	& m1NFADDR_NFADDR0)


/*
 * NFDATA
 */
#define fNFDATA_NFDATA1		Fld(8,8)
#define fNFDATA_NFDATA0		Fld(8,0)

#define m1NFDATA_NFDATA1	FMsk(fNFDATA_NFDATA1)
#define m1NFDATA_NFDATA0	FMsk(fNFDATA_NFDATA0)

#define m0NFDATA_NFDATA1	(~m1NFDATA_NFDATA1)
#define m0NFDATA_NFDATA0	(~m1NFDATA_NFDATA0)

#define sNFDATA_NFDATA1(f_)	(FInsrt(f_,fNFDATA_NFDATA1)	& m1NFDATA_NFDATA1)
#define sNFDATA_NFDATA0(f_)	(FInsrt(f_,fNFDATA_NFDATA0)	& m1NFDATA_NFDATA0)


/*
 * NFMECCDATA0
 */
#define fNFMECCDATA0_ECCData0_1		Fld(8,8)
#define fNFMECCDATA0_ECCData0_0		Fld(8,0)

#define m1NFMECCDATA0_ECCData0_1	FMsk(fNFMECCDATA0_ECCData0_1)
#define m1NFMECCDATA0_ECCData0_0	FMsk(fNFMECCDATA0_ECCData0_0)

#define m0NFMECCDATA0_ECCData0_1	(~m1NFMECCDATA0_ECCData0_1)
#define m0NFMECCDATA0_ECCData0_0	(~m1NFMECCDATA0_ECCData0_0)

#define sNFMECCDATA0_ECCData0_1(f_)	(FInsrt(f_,fNFMECCDATA0_ECCData0_1)	& m1NFMECCDATA0_ECCData0_1)
#define sNFMECCDATA0_ECCData0_0(f_)	(FInsrt(f_,fNFMECCDATA0_ECCData0_0)	& m1NFMECCDATA0_ECCData0_0)

/*
 * NFMECCDATA1
 */
#define fNFMECCDATA1_ECCData1_1		Fld(8,8)
#define fNFMECCDATA1_ECCData1_0		Fld(8,0)

#define m1NFMECCDATA1_ECCData1_1	FMsk(fNFMECCDATA1_ECCData1_1)
#define m1NFMECCDATA1_ECCData1_0	FMsk(fNFMECCDATA1_ECCData1_0)

#define m0NFMECCDATA1_ECCData1_1	(~m1NFMECCDATA1_ECCData1_1)
#define m0NFMECCDATA1_ECCData1_0	(~m1NFMECCDATA1_ECCData1_0)

#define sNFMECCDATA1_ECCData1_1(f_)	(FInsrt(f_,fNFMECCDATA1_ECCData1_1)	& m1NFMECCDATA1_ECCData1_1)
#define sNFMECCDATA1_ECCData1_0(f_)	(FInsrt(f_,fNFMECCDATA1_ECCData1_0)	& m1NFMECCDATA1_ECCData1_0)

/*
 * NFMECCDATA2
 */
#define fNFMECCDATA2_ECCData2_1		Fld(8,8)
#define fNFMECCDATA2_ECCData2_0		Fld(8,0)

#define m1NFMECCDATA2_ECCData2_1	FMsk(fNFMECCDATA2_ECCData2_1)
#define m1NFMECCDATA2_ECCData2_0	FMsk(fNFMECCDATA2_ECCData2_0)

#define m0NFMECCDATA2_ECCData2_1	(~m1NFMECCDATA2_ECCData2_1)
#define m0NFMECCDATA2_ECCData2_0	(~m1NFMECCDATA2_ECCData2_0)

#define sNFMECCDATA2_ECCData2_1(f_)	(FInsrt(f_,fNFMECCDATA2_ECCData2_1)	& m1NFMECCDATA2_ECCData2_1)
#define sNFMECCDATA2_ECCData2_0(f_)	(FInsrt(f_,fNFMECCDATA2_ECCData2_0)	& m1NFMECCDATA2_ECCData2_0)

/*
 * NFMECCDATA3
 */
#define fNFMECCDATA3_ECCData3_1		Fld(8,8)
#define fNFMECCDATA3_ECCData3_0		Fld(8,0)

#define m1NFMECCDATA3_ECCData3_1	FMsk(fNFMECCDATA3_ECCData3_1)
#define m1NFMECCDATA3_ECCData3_0	FMsk(fNFMECCDATA3_ECCData3_0)

#define m0NFMECCDATA3_ECCData3_1	(~m1NFMECCDATA3_ECCData3_1)
#define m0NFMECCDATA3_ECCData3_0	(~m1NFMECCDATA3_ECCData3_0)

#define sNFMECCDATA3_ECCData3_1(f_)	(FInsrt(f_,fNFMECCDATA3_ECCData3_1)	& m1NFMECCDATA3_ECCData3_1)
#define sNFMECCDATA3_ECCData3_0(f_)	(FInsrt(f_,fNFMECCDATA3_ECCData3_0)	& m1NFMECCDATA3_ECCData3_0)


/*
 * NFSECCDATA0
 */
#define fNFSECCDATA0_ECCData0_1		Fld(8,8)
#define fNFSECCDATA0_ECCData0_0		Fld(8,0)

#define m1NFSECCDATA0_ECCData0_1	FMsk(fNFSECCDATA0_ECCData0_1)
#define m1NFSECCDATA0_ECCData0_0	FMsk(fNFSECCDATA0_ECCData0_0)

#define m0NFSECCDATA0_ECCData0_1	(~m1NFSECCDATA0_ECCData0_1)
#define m0NFSECCDATA0_ECCData0_0	(~m1NFSECCDATA0_ECCData0_0)

#define sNFSECCDATA0_ECCData0_1(f_)	(FInsrt(f_,fNFSECCDATA0_ECCData0_1)	& m1NFSECCDATA0_ECCData0_1)
#define sNFSECCDATA0_ECCData0_0(f_)	(FInsrt(f_,fNFSECCDATA0_ECCData0_0)	& m1NFSECCDATA0_ECCData0_0)

/*
 * NFSECCDATA1
 */
#define fNFSECCDATA1_ECCData1_1		Fld(8,8)
#define fNFSECCDATA1_ECCData1_0		Fld(8,0)

#define m1NFSECCDATA1_ECCData1_1	FMsk(fNFSECCDATA1_ECCData1_1)
#define m1NFSECCDATA1_ECCData1_0	FMsk(fNFSECCDATA1_ECCData1_0)

#define m0NFSECCDATA1_ECCData1_1	(~m1NFSECCDATA1_ECCData1_1)
#define m0NFSECCDATA1_ECCData1_0	(~m1NFSECCDATA1_ECCData1_0)

#define sNFSECCDATA1_ECCData1_1(f_)	(FInsrt(f_,fNFSECCDATA1_ECCData1_1)	& m1NFSECCDATA1_ECCData1_1)
#define sNFSECCDATA1_ECCData1_0(f_)	(FInsrt(f_,fNFSECCDATA1_ECCData1_0)	& m1NFSECCDATA1_ECCData1_0)


/*
 * NFSTAT
 */
#define fNFSTAT_IllegalAccess		Fld(1,16)
#define fNFSTAT_AutoLoadDone		Fld(1,15)
#define fNFSTAT_AutoStoreDone		Fld(1,14)
#define fNFSTAT_RnB_TransDetect		Fld(1,13)
#define fNFSTAT_Flash_nCE			Fld(1,12)
#define fNFSTAT_Flash_RnB1			Fld(1,11)
#define fNFSTAT_Flash_RnB0			Fld(1,10)
#define fNFSTAT_STON_A2				Fld(10,0)

#define m1NFSTAT_IllegalAccess		FMsk(fNFSTAT_IllegalAccess)
#define m1NFSTAT_AutoLoadDone		FMsk(fNFSTAT_AutoLoadDone)
#define m1NFSTAT_AutoStoreDone		FMsk(fNFSTAT_AutoStoreDone)
#define m1NFSTAT_RnB_TransDetect	FMsk(fNFSTAT_RnB_TransDetect)
#define m1NFSTAT_Flash_nCE			FMsk(fNFSTAT_Flash_nCE)
#define m1NFSTAT_Flash_RnB1			FMsk(fNFSTAT_Flash_RnB1)
#define m1NFSTAT_Flash_RnB0			FMsk(fNFSTAT_Flash_RnB0)
#define m1NFSTAT_STON_A2			FMsk(fNFSTAT_STON_A2)

#define m0NFSTAT_IllegalAccess		(~m1NFSTAT_IllegalAccess)
#define m0NFSTAT_AutoLoadDone		(~m1NFSTAT_AutoLoadDone)
#define m0NFSTAT_AutoStoreDone		(~m1NFSTAT_AutoStoreDone)
#define m0NFSTAT_RnB_TransDetect	(~m1NFSTAT_RnB_TransDetect)
#define m0NFSTAT_Flash_nCE			(~m1NFSTAT_Flash_nCE)
#define m0NFSTAT_Flash_RnB1			(~m1NFSTAT_Flash_RnB1)
#define m0NFSTAT_Flash_RnB0			(~m1NFSTAT_Flash_RnB0)
#define m0NFSTAT_STON_A2			(~m1NFSTAT_STON_A2)

#define sNFSTAT_IllegalAccess(f_)	(FInsrt(f_,fNFSTAT_IllegalAccess)	& m1NFSTAT_IllegalAccess)
#define sNFSTAT_AutoLoadDone(f_)	(FInsrt(f_,fNFSTAT_AutoLoadDone)	& m1NFSTAT_AutoLoadDone)
#define sNFSTAT_AutoStoreDone(f_)	(FInsrt(f_,fNFSTAT_AutoStoreDone)	& m1NFSTAT_AutoStoreDone)
#define sNFSTAT_RnB_TransDetect(f_)	(FInsrt(f_,fNFSTAT_RnB_TransDetect)	& m1NFSTAT_RnB_TransDetect)


/*
 * NFESTAT0
 */
#define fNFESTAT0_SErrorDataNo		Fld(4,21)
#define fNFESTAT0_SErrorBitNo		Fld(3,18)
#define fNFESTAT0_MErrorDataNo		Fld(11,7)
#define fNFESTAT0_MErrorBitNo		Fld(3,4)
#define fNFESTAT0_SpareError		Fld(2,2)
#define fNFESTAT0_MainError			Fld(2,0)

#define m1NFESTAT0_SErrorDataNo		FMsk(fNFESTAT0_SErrorDataNo)
#define m1NFESTAT0_SErrorBitNo		FMsk(fNFESTAT0_SErrorBitNo)
#define m1NFESTAT0_MErrorDataNo		FMsk(fNFESTAT0_MErrorDataNo)
#define m1NFESTAT0_MErrorBitNo		FMsk(fNFESTAT0_MErrorBitNo)
#define m1NFESTAT0_SpareError		FMsk(fNFESTAT0_SpareError)
#define m1NFESTAT0_MainError		FMsk(fNFESTAT0_MainError)

#define m0NFESTAT0_SErrorDataNo		(~m1NFESTAT0_SErrorDataNo)
#define m0NFESTAT0_SErrorBitNo		(~m1NFESTAT0_SErrorBitNo)
#define m0NFESTAT0_MErrorDataNo		(~m1NFESTAT0_MErrorDataNo)
#define m0NFESTAT0_MErrorBitNo		(~m1NFESTAT0_MErrorBitNo)
#define m0NFESTAT0_SpareError		(~m1NFESTAT0_SpareError)
#define m0NFESTAT0_MainError		(~m1NFESTAT0_MainError)

#define sNFESTAT0_SErrorDataNo(f_)	(FInsrt(f_,fNFESTAT0_SErrorDataNo)	& m1NFESTAT0_SErrorDataNo)
#define sNFESTAT0_SErrorBitNo(f_)	(FInsrt(f_,fNFESTAT0_SErrorBitNo)	& m1NFESTAT0_SErrorBitNo)
#define sNFESTAT0_MErrorDataNo(f_)	(FInsrt(f_,fNFESTAT0_MErrorDataNo)	& m1NFESTAT0_MErrorDataNo)
#define sNFESTAT0_MErrorBitNo(f_)	(FInsrt(f_,fNFESTAT0_MErrorBitNo)	& m1NFESTAT0_MErrorBitNo)
#define sNFESTAT0_SpareError(f_)	(FInsrt(f_,fNFESTAT0_SpareError)	& m1NFESTAT0_SpareError)
#define sNFESTAT0_MainError(f_)		(FInsrt(f_,fNFESTAT0_MainError)		& m1NFESTAT0_MainError)

/*
 * NFESTAT1
 */
#define fNFESTAT1_SErrorDataNo		Fld(4,21)
#define fNFESTAT1_SErrorBitNo		Fld(3,18)
#define fNFESTAT1_MErrorDataNo		Fld(11,7)
#define fNFESTAT1_MErrorBitNo		Fld(3,4)
#define fNFESTAT1_SpareError		Fld(2,2)
#define fNFESTAT1_MainError			Fld(2,0)

#define m1NFESTAT1_SErrorDataNo		FMsk(fNFESTAT1_SErrorDataNo)
#define m1NFESTAT1_SErrorBitNo		FMsk(fNFESTAT1_SErrorBitNo)
#define m1NFESTAT1_MErrorDataNo		FMsk(fNFESTAT1_MErrorDataNo)
#define m1NFESTAT1_MErrorBitNo		FMsk(fNFESTAT1_MErrorBitNo)
#define m1NFESTAT1_SpareError		FMsk(fNFESTAT1_SpareError)
#define m1NFESTAT1_MainError		FMsk(fNFESTAT1_MainError)

#define m0NFESTAT1_SErrorDataNo		(~m1NFESTAT1_SErrorDataNo)
#define m0NFESTAT1_SErrorBitNo		(~m1NFESTAT1_SErrorBitNo)
#define m0NFESTAT1_MErrorDataNo		(~m1NFESTAT1_MErrorDataNo)
#define m0NFESTAT1_MErrorBitNo		(~m1NFESTAT1_MErrorBitNo)
#define m0NFESTAT1_SpareError		(~m1NFESTAT1_SpareError)
#define m0NFESTAT1_MainError		(~m1NFESTAT1_MainError)

#define sNFESTAT1_SErrorDataNo(f_)	(FInsrt(f_,fNFESTAT1_SErrorDataNo)	& m1NFESTAT1_SErrorDataNo)
#define sNFESTAT1_SErrorBitNo(f_)	(FInsrt(f_,fNFESTAT1_SErrorBitNo)	& m1NFESTAT1_SErrorBitNo)
#define sNFESTAT1_MErrorDataNo(f_)	(FInsrt(f_,fNFESTAT1_MErrorDataNo)	& m1NFESTAT1_MErrorDataNo)
#define sNFESTAT1_MErrorBitNo(f_)	(FInsrt(f_,fNFESTAT1_MErrorBitNo)	& m1NFESTAT1_MErrorBitNo)
#define sNFESTAT1_SpareError(f_)	(FInsrt(f_,fNFESTAT1_SpareError)	& m1NFESTAT1_SpareError)
#define sNFESTAT1_MainError(f_)		(FInsrt(f_,fNFESTAT1_MainError)		& m1NFESTAT1_MainError)


/*
 * NFMECC0
 */
#define fNFMECC0_MECC0_3	Fld(8,24)
#define fNFMECC0_MECC0_2	Fld(8,16)
#define fNFMECC0_MECC0_1	Fld(8,8)
#define fNFMECC0_MECC0_0	Fld(8,0)

#define m1NFMECC0_MECC0_3	FMsk(fNFMECC0_MECC0_3)
#define m1NFMECC0_MECC0_2	FMsk(fNFMECC0_MECC0_2)
#define m1NFMECC0_MECC0_1	FMsk(fNFMECC0_MECC0_1)
#define m1NFMECC0_MECC0_0	FMsk(fNFMECC0_MECC0_0)

#define m0NFMECC0_MECC0_3	(~m1NFMECC0_MECC0_3)
#define m0NFMECC0_MECC0_2	(~m1NFMECC0_MECC0_2)
#define m0NFMECC0_MECC0_1	(~m1NFMECC0_MECC0_1)
#define m0NFMECC0_MECC0_0	(~m1NFMECC0_MECC0_0)

/*
 * NFMECC1
 */
#define fNFMECC1_MECC1_3	Fld(8,24)
#define fNFMECC1_MECC1_2	Fld(8,16)
#define fNFMECC1_MECC1_1	Fld(8,8)
#define fNFMECC1_MECC1_0	Fld(8,0)

#define m1NFMECC1_MECC1_3	FMsk(fNFMECC1_MECC1_3)
#define m1NFMECC1_MECC1_2	FMsk(fNFMECC1_MECC1_2)
#define m1NFMECC1_MECC1_1	FMsk(fNFMECC1_MECC1_1)
#define m1NFMECC1_MECC1_0	FMsk(fNFMECC1_MECC1_0)

#define m0NFMECC1_MECC1_3	(~m1NFMECC1_MECC1_3)
#define m0NFMECC1_MECC1_2	(~m1NFMECC1_MECC1_2)
#define m0NFMECC1_MECC1_1	(~m1NFMECC1_MECC1_1)
#define m0NFMECC1_MECC1_0	(~m1NFMECC1_MECC1_0)


/*
 * NFSECC
 */
#define fNFSECC_SECC1_1		Fld(8,24)
#define fNFSECC_SECC1_0		Fld(8,16)
#define fNFSECC_SECC0_1		Fld(8,8)
#define fNFSECC_SECC0_0		Fld(8,0)

#define m1NFSECC_SECC1_1	FMsk(fNFSECC_SECC1_1)
#define m1NFSECC_SECC1_0	FMsk(fNFSECC_SECC1_0)
#define m1NFSECC_SECC0_1	FMsk(fNFSECC_SECC0_1)
#define m1NFSECC_SECC0_0	FMsk(fNFSECC_SECC0_0)

#define m0NFSECC_SECC1_1	(~m1NFSECC_SECC1_1)
#define m0NFSECC_SECC1_0	(~m1NFSECC_SECC1_0)
#define m0NFSECC_SECC0_1	(~m1NFSECC_SECC0_1)
#define m0NFSECC_SECC0_0	(~m1NFSECC_SECC0_0)


/*
 * NFSBLK
 */
#define fNFSBLK_SBLK_ADDR2	Fld(8,16)
#define fNFSBLK_SBLK_ADDR1	Fld(8,8)
#define fNFSBLK_SBLK_ADDR0	Fld(8,0)

#define m1NFSBLK_SBLK_ADDR2	FMsk(fNFSBLK_SBLK_ADDR2)
#define m1NFSBLK_SBLK_ADDR1	FMsk(fNFSBLK_SBLK_ADDR1)
#define m1NFSBLK_SBLK_ADDR0	FMsk(fNFSBLK_SBLK_ADDR0)

#define m0NFSBLK_SBLK_ADDR2	(~m1NFSBLK_SBLK_ADDR2)
#define m0NFSBLK_SBLK_ADDR1	(~m1NFSBLK_SBLK_ADDR1)
#define m0NFSBLK_SBLK_ADDR0	(~m1NFSBLK_SBLK_ADDR0)

#define sNFSBLK_SBLK_ADDR2(f_)	(FInsrt(f_,fNFSBLK_SBLK_ADDR2)	& m1NFSBLK_SBLK_ADDR2)
#define sNFSBLK_SBLK_ADDR1(f_)	(FInsrt(f_,fNFSBLK_SBLK_ADDR1)	& m1NFSBLK_SBLK_ADDR1)
#define sNFSBLK_SBLK_ADDR0(f_)	(FInsrt(f_,fNFSBLK_SBLK_ADDR0)	& m1NFSBLK_SBLK_ADDR0)

/*
 * NFEBLK
 */
#define fNFEBLK_EBLK_ADDR2	Fld(8,16)
#define fNFEBLK_EBLK_ADDR1	Fld(8,8)
#define fNFEBLK_EBLK_ADDR0	Fld(8,0)

#define m1NFEBLK_EBLK_ADDR2	FMsk(fNFEBLK_EBLK_ADDR2)
#define m1NFEBLK_EBLK_ADDR1	FMsk(fNFEBLK_EBLK_ADDR1)
#define m1NFEBLK_EBLK_ADDR0	FMsk(fNFEBLK_EBLK_ADDR0)

#define m0NFEBLK_EBLK_ADDR2	(~m1NFEBLK_EBLK_ADDR2)
#define m0NFEBLK_EBLK_ADDR1	(~m1NFEBLK_EBLK_ADDR1)
#define m0NFEBLK_EBLK_ADDR0	(~m1NFEBLK_EBLK_ADDR0)

#define sNFEBLK_EBLK_ADDR2(f_)	(FInsrt(f_,fNFEBLK_EBLK_ADDR2)	& m1NFEBLK_EBLK_ADDR2)
#define sNFEBLK_EBLK_ADDR1(f_)	(FInsrt(f_,fNFEBLK_EBLK_ADDR1)	& m1NFEBLK_EBLK_ADDR1)
#define sNFEBLK_EBLK_ADDR0(f_)	(FInsrt(f_,fNFEBLK_EBLK_ADDR0)	& m1NFEBLK_EBLK_ADDR0)

#endif /* _S3C24A0_NAND_H_ */

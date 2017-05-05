/******************************************************************************
*
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xv_hdmirx.h
*
* This is the main header file for Xilinx HDMI RX core. HDMI RX core is used
* for extracting the video and audio streams from HDMI stream. It consists of
* - Receiver core
* - AXI4-Stream to Video Bridge
* - Video Timing Controller and
* - High-bandwidth Digital Content Protection (HDCP) (Optional)
* - Data Recovery Unit (DRU) (Optional).
*
* Receiver core performs following operations:
* - Aligns incoming data stream to the word boundary and removes inter channel
* skew.
* - Unscrambles the data if data rates above the 3.4 Gps. Otherwise bypasses
* the Scrambler.
* - Splits the data stream into video and packet data streams.
* - Optional data streams decrypt by an external HDCP module.
* - Decodes TMDS data into video data.
* - Converts the pixel data from the link domain into the video domain.
*
* AXI Video Bridge converts the captured native video to AXI stream and outputs
* the video data through the AXI video interface.
*
* Video Timing Controller (VTC) measures the video timing.
*
* Data Recovery Unit (DRU) to recover the data from the HDMI stream if incoming
* HDMI stream is too slow for the transceiver.
*
* <b>Core Features </b>
*
* For a full description of HDMI RX features, please see the hardware
* specification.
*
* <b>Software Initialization & Configuration</b>
*
* The application needs to do following steps in order for preparing the
* HDMI RX core to be ready.
*
* - Call XV_HdmiRx_LookupConfig using a device ID to find the core configuration.
* - Call XV_HdmiRx_CfgInitialize to initialize the device and the driver
* instance associated with it.
*
* <b>Interrupts </b>
*
* This driver provides interrupt handlers
* - XV_HdmiRx_IntrHandler, for handling the interrupts from the HDMI RX core
* peripherals.
*
* Application developer needs to register interrupt handler with the processor,
* within their examples. Whenever processor calls registered application's
* interrupt handler associated with interrupt id, application's interrupt
* handler needs to call appropriate peripheral interrupt handler reading
* peripheral's Status register.

* This driver provides XV_HdmiRx_SetCallback API to register functions with HDMI
* RX core instance.
*
* <b> Virtual Memory </b>
*
* This driver supports Virtual Memory. The RTOS is responsible for calculating
* the correct device base address in Virtual Memory space.
*
* <b> Threads </b>
*
* This driver is not thread safe. Any needs for threads or thread mutual
* exclusion must be satisfied by the layer above this driver.
*
* <b> Asserts </b>
*
* Asserts are used within all Xilinx drivers to enforce constraints on argument
* values. Asserts can be turned off on a system-wide basis by defining, at
* compile time, the NDEBUG identifier. By default, asserts are turned on and it
* is recommended that users leave asserts on during development.
*
* <b> Building the driver </b>
*
* The HDMI RX driver is composed of several source files. This allows the user
* to build and link only those parts of the driver that are necessary.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  gm, mg 10/07/15 Initial release.
* 1.01  yh     14/01/16 Added Marco for AxisEnable PIO
* 1.02  yh     15/01/16 Added 3D Video support
* 1.03  MG     18/02/16 Added link error callback.
* 1.04  MG     08/03/16 Added RefClk to structure XV_HdmiRx_Stream
* 1.05  MG     13/05/16 Added XV_HdmiRx_DdcHdcp22Mode and XV_HdmiRx_DdcHdcp14
*                       Mode macros
* 1.06  MG     27/05/16 Added VTD timebase macro
* 1.07  YH     25/07/16 Used UINTPTR instead of u32 for BaseAddress
*                          XV_HdmiRx_Config
*                          XV_HdmiRx_CfgInitialize
* 1.08  YH     14/11/16 Added XV_HdmiRx_Bridge_yuv420 & XV_HdmiRx_Bridge_pixel
*                          mode macros
* 1.09  MMO    02/03/17 Added Sync Loss and IsMode Handler for HDCP
*                          compliance test.
* </pre>
*
******************************************************************************/
#ifndef XV_HDMIRX_H_
#define XV_HDMIRX_H_		/**< Prevent circular inclusions
				  *  by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xv_hdmirx_hw.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xdebug.h"
#include "xvidc.h"

#include "xv_hdmirx_vsif.h"
/************************** Constant Definitions *****************************/

/** @name Handler Types
* @{
*/
/**
* These constants specify different types of handler and used to differentiate
* interrupt requests from peripheral.
*/
typedef enum {
	XV_HDMIRX_HANDLER_CONNECT = 1,	/**< A connect event interrupt type */
	XV_HDMIRX_HANDLER_AUX,			/**< Interrupt type for AUX peripheral */
	XV_HDMIRX_HANDLER_AUD,			/**< Interrupt type for AUD peripheral */
	XV_HDMIRX_HANDLER_LNKSTA,		/**< Interrupt type for LNKSTA peripheral */
	XV_HDMIRX_HANDLER_DDC,			/**< Interrupt type for DDC	peripheral */
	XV_HDMIRX_HANDLER_STREAM_DOWN,	/**< Interrupt type for stream down */
	XV_HDMIRX_HANDLER_STREAM_INIT,	/**< Interrupt type for stream init */
	XV_HDMIRX_HANDLER_STREAM_UP,	/**< Interrupt type for stream up */
	XV_HDMIRX_HANDLER_HDCP,			/**< Interrupt type for hdcp */
	XV_HDMIRX_HANDLER_LINK_ERROR,		/**< Interrupt type for link error */
	XV_HDMIRX_HANDLER_SYNC_LOSS,       /**< Interrupt type for sync loss */
	XV_HDMIRX_HANDLER_MODE             /**< Interrupt type for mode */
} XV_HdmiRx_HandlerType;
/*@}*/

/** @name HDMI RX stream status
* @{
*/
typedef enum {
	XV_HDMIRX_STATE_STREAM_DOWN,			/**< Stream down */
	XV_HDMIRX_STATE_STREAM_IDLE,			/**< Stream idle */
	XV_HDMIRX_STATE_STREAM_INIT,			/**< Stream init */
	XV_HDMIRX_STATE_STREAM_ARM,				/**< Stream arm */
	XV_HDMIRX_STATE_STREAM_LOCK,			/**< Stream lock */
	XV_HDMIRX_STATE_STREAM_RDY,				/**< Stream ready */
	XV_HDMIRX_STATE_STREAM_UP				/**< Stream up */
} XV_HdmiRx_State;

/**************************** Type Definitions *******************************/

/**
* This typedef contains Video identification information in tabular form.
*/
typedef struct {
	XVidC_VideoMode VmId;	/**< Video mode/Resolution ID */
	u8 Vic;			/**< Video Identification code */
} XV_HdmiRx_VicTable;

/**
* This typedef contains configuration information for the HDMI RX core.
* Each HDMI RX device should have a configuration structure associated.
*/
typedef struct {
	u16 DeviceId;		/**< DeviceId is the unique ID of the HDMI RX core */
	UINTPTR BaseAddress;	/**< BaseAddress is the physical base address
						* of the core's registers */
} XV_HdmiRx_Config;

/**
* This typedef contains HDMI RX audio stream specific data structure.
*/
typedef struct {
	u8	Active;			/**< Active flag. This flag is set when an acitve audio
						* stream was detected */
	u8 	Channels;		/**< Channels */
} XV_HdmiRx_AudioStream;

/**
* This typedef contains HDMI RX stream specific data structure.
*/
typedef struct {
	XVidC_VideoStream 	Video;			/**< Video stream for HDMI RX */
	XV_HdmiRx_AudioStream	Audio;		/**< Audio stream */
	u8 	Vic;							/**< Video Identification code flag */
	u8 	IsHdmi;							/**< HDMI flag. 1 - HDMI Stream, 0 - DVI Stream */
	u32 PixelClk;						/**< Pixel Clock */
	u32 RefClk;							/**< Reference Clock */
	u8 	IsScrambled; 					/**< Scrambler flag 1 - scrambled data ,
										*	0 - non scrambled data */
	XV_HdmiRx_State	State;				/**< State */
	u8 	IsConnected;					/**< Connected flag. This flag is set when
										* the cable is connected */
	u8 GetVideoPropertiesTries;			/** This value is used  in the GetVideoProperties API*/
} XV_HdmiRx_Stream;


/**
* This typedef contains Auxiliary header information for infoframe.
*/
typedef union {
	u32 Data;	/**< AUX header data field */
	u8 Byte[4];	/**< AUX header byte field */
} XV_HdmiRx_AuxHeader;

/**
* This typedef contains Auxiliary data information for infoframe.
*/
typedef union {
	u32 Data[8];	/**< AUX data field */
	u8 Byte[32];	/**< AUX data byte field */
} XV_HdmiRx_AuxData;

/**
* This typedef holds HDMI RX Auxiliary peripheral specific data structure.
*/
typedef struct {
	XV_HdmiRx_AuxHeader Header;	/**< AUX header field */
	XV_HdmiRx_AuxData Data;		/**< AUX data field */
} XV_HdmiRx_Aux;

/**
* Callback type for interrupt.
*
* @param	CallbackRef is a callback reference passed in by the upper
*		layer when setting the callback functions, and passed back to
*		the upper layer when the callback is invoked.
*
* @return	None.
*
* @note		None.
*
*/
typedef void (*XV_HdmiRx_Callback)(void *CallbackRef);
typedef void (*XV_HdmiRx_HdcpCallback)(void *CallbackRef, int Data);

/**
* The XHdmiRx driver instance data. An instance must be allocated for each
* HDMI RX core in use.
*/
typedef struct {
	XV_HdmiRx_Config Config;				/**< Hardware Configuration */
	u32 IsReady;							/**< Core and the driver instance are initialized */

	/*Callbacks */
	XV_HdmiRx_Callback ConnectCallback;		/**< Callback for connect event interrupt */
	void *ConnectRef;						/**< To be passed to the connect interrupt callback */
	u32 IsConnectCallbackSet;				/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback AuxCallback;			/**< Callback for AUX event interrupt */
	void *AuxRef;							/**< To be passed to the AUX interrupt callback */
	u32 IsAuxCallbackSet;					/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback AudCallback;			/**< Callback for AUD event interrupt */
	void *AudRef;							/**< To be passed to the Audio interrupt callback */
	u32 IsAudCallbackSet;					/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback LnkStaCallback;		/**< Callback for LNKSTA event interrupt */
	void *LnkStaRef;						/**< To be passed to the LNKSTA interrupt callback */
	u32 IsLnkStaCallbackSet;				/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback DdcCallback;			/**< Callback for PDDC interrupt */
	void *DdcRef;							/**< To be passed to the DDC interrupt callback */
	u32 IsDdcCallbackSet;					/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback StreamDownCallback;	/**< Callback for stream down callback */
	void *StreamDownRef;					/**< To be passed to the stream down callback */
	u32	IsStreamDownCallbackSet;			/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback StreamInitCallback;	/**< Callback for stream init callback */
	void *StreamInitRef;					/**< To be passed to the stream start callback */
	u32 IsStreamInitCallbackSet;			/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback StreamUpCallback;		/**< Callback for stream up callback */
	void *StreamUpRef;						/**< To be passed to the stream up callback */
	u32 IsStreamUpCallbackSet;				/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_HdcpCallback HdcpCallback;	/**< Callback for hdcp callback */
	void *HdcpRef;							/**< To be passed to the hdcp callback */
	u32	IsHdcpCallbackSet;					/**< Set flag. This flag is set to true when the callback has been registered */
	XV_HdmiRx_Callback LinkErrorCallback;	/**< Callback for link error callback */
	void *LinkErrorRef;						/**< To be passed to the link error callback */
	u32 IsLinkErrorCallbackSet;				/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback SyncLossCallback;		/**< Callback for sync loss callback */
	void *SyncLossRef;						/**< To be passed to the link error callback */
	u32 IsSyncLossCallbackSet;				/**< Set flag. This flag is set to true when the callback has been registered */

	XV_HdmiRx_Callback ModeCallback;			/**< Callback for sync loss callback */
	void *ModeRef;							/**< To be passed to the link error callback */
	u32 IsModeCallbackSet;					/**< Set flag. This flag is set to true when the callback has been registered */

	/* HDMI RX stream */
	XV_HdmiRx_Stream Stream;				/**< HDMI RX stream information */

	/* Aux peripheral specific */
	XV_HdmiRx_Aux Aux;					/**< AUX peripheral information */

	/* Audio peripheral specific */
	u32 AudCts;							/**< Audio CTS */
	u32 AudN;							/**< Audio N element */
	u32 AudFormat;						/**< Audio Format */
} XV_HdmiRx;

/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
/**
*
* This macro reads the RX version
*
* @param  InstancePtr is a pointer to the XHdmi_RX core instance.
*
* @return RX version.
*
* *note	C-style signature:
*		u32 XV_HdmiRx_GetVersion(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_GetVersion(InstancePtr) \
  XV_HdmiRx_ReadReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_VER_VERSION_OFFSET))

/*****************************************************************************/
/**
*
* This macro asserts or clears the HDMI RX reset.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
* @param	Reset specifies TRUE/FALSE value to either assert or
*		release HDMI RX reset.
*
* @return	None.
*
* @note		The reset output of the PIO is inverted. When the system is
*		in reset, the PIO output is cleared and this will reset the
*		HDMI RX. Therefore, clearing the PIO reset output will assert
*		the HDMI link and video reset.
*		C-style signature:
*		void XV_HdmiRx_Reset(XV_HdmiRx *InstancePtr, u8 Reset)
*
******************************************************************************/
#define XV_HdmiRx_Reset(InstancePtr, Reset) \
{ \
	if (Reset) { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_CLR_OFFSET), (XV_HDMIRX_PIO_OUT_RESET_MASK)); \
	} \
	else { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_SET_OFFSET), (XV_HDMIRX_PIO_OUT_RESET_MASK)); \
	} \
}

/*****************************************************************************/
/**
*
* This macro asserts or clears the HDMI RX link enable.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
* @param	SetClr specifies TRUE/FALSE value to either assert or
*		release HDMI RX link enable.
*
* @return	None.
*
* @note
*		C-style signature:
*		void XV_HdmiRx_Reset(XV_HdmiRx *InstancePtr, u8 SetClr)
*
******************************************************************************/
#define XV_HdmiRx_LinkEnable(InstancePtr, SetClr) \
{ \
	if (SetClr) { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_SET_OFFSET), (XV_HDMIRX_PIO_OUT_LNK_EN_MASK)); \
	} \
	else { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_CLR_OFFSET), (XV_HDMIRX_PIO_OUT_LNK_EN_MASK)); \
	} \
}

/*****************************************************************************/
/**
*
* This macro asserts or clears the HDMI RX video enable.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
* @param	SetClr specifies TRUE/FALSE value to either assert or
*		release HDMI RX video enable.
*
* @return	None.
*
* @note
*		C-style signature:
*		void XV_HdmiRx_Reset(XV_HdmiRx *InstancePtr, u8 SetClr)
*
******************************************************************************/
#define XV_HdmiRx_VideoEnable(InstancePtr, SetClr) \
{ \
	if (SetClr) { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_SET_OFFSET), (XV_HDMIRX_PIO_OUT_VID_EN_MASK)); \
	} \
	else { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_CLR_OFFSET), (XV_HDMIRX_PIO_OUT_VID_EN_MASK)); \
	} \
}

/*****************************************************************************/
/**
*
* This macro controls the HDMI RX Scrambler.
*
* @param	InstancePtr is a pointer to the XHdmi_Tx core instance.
* @param	SetClr specifies TRUE/FALSE value to either enable or disable the
*		scrambler.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_SetScrambler(XV_HdmiRx *InstancePtr, u8 SetClr)
*
******************************************************************************/
#define XV_HdmiRx_SetScrambler(InstancePtr, SetClr) \
{ \
	if (SetClr) { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_SET_OFFSET), (XV_HDMIRX_PIO_OUT_SCRM_MASK)); \
		(InstancePtr)->Stream.IsScrambled = (TRUE); \
	} \
	else { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_CLR_OFFSET), (XV_HDMIRX_PIO_OUT_SCRM_MASK)); \
		(InstancePtr)->Stream.IsScrambled = (FALSE); \
	} \
}

/*****************************************************************************/
/**
*
* This macro controls the YUV420 mode for video bridge.
*
* @param	InstancePtr is a pointer to the XHdmi_Rx core instance.
* @param	SetClr specifies TRUE/FALSE value to either enable or disable the
*		YUV 420 Support.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_Bridge_yuv420(XV_HdmiRx *InstancePtr, u8 SetClr)
*
******************************************************************************/
#define XV_HdmiRx_Bridge_yuv420(InstancePtr, SetClr) \
{ \
	if (SetClr) { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_SET_OFFSET), (XV_HDMIRX_PIO_OUT_BRIDGE_YUV420_MASK)); \
	} \
	else { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_CLR_OFFSET), (XV_HDMIRX_PIO_OUT_BRIDGE_YUV420_MASK)); \
	} \
}

/*****************************************************************************/
/**
*
* This macro controls the Pixel Drop mode for video bridge.
*
* @param	InstancePtr is a pointer to the XHdmi_Rx core instance.
* @param	SetClr specifies TRUE/FALSE value to either enable or disable the
*		Pixel Repitition.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_Bridge_pixel(XV_HdmiRx *InstancePtr, u8 SetClr)
*
******************************************************************************/
#define XV_HdmiRx_Bridge_pixel(InstancePtr, SetClr) \
{ \
	if (SetClr) { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_SET_OFFSET), (XV_HDMIRX_PIO_OUT_BRIDGE_PIXEL_MASK)); \
	} \
	else { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_CLR_OFFSET), (XV_HDMIRX_PIO_OUT_BRIDGE_PIXEL_MASK)); \
	} \
}

/*****************************************************************************/
/**
*
* This macro asserts or clears the AXIS enable output port.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
* @param	Reset specifies TRUE/FALSE value to either assert or
*		release HDMI RX reset.
*
* @return	None.
*
* @note		The reset output of the PIO is inverted. When the system is
*		in reset, the PIO output is cleared and this will reset the
*		HDMI RX. Therefore, clearing the PIO reset output will assert
*		the HDMI link and video reset.
*		C-style signature:
*		void XV_HdmiRx_AxisEnable(InstancePtr, Enable)
*
******************************************************************************/
#define XV_HdmiRx_AxisEnable(InstancePtr, Enable) \
{ \
	if (Enable) { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_SET_OFFSET), (XV_HDMIRX_PIO_OUT_AXIS_EN_MASK)); \
	} \
	else { \
		XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_OUT_CLR_OFFSET), (XV_HDMIRX_PIO_OUT_AXIS_EN_MASK)); \
	} \
}

/*****************************************************************************/
/**
*
* This macro enables the HDMI RX PIO peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_PioEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_PioEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_CTRL_SET_OFFSET), (XV_HDMIRX_PIO_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro disables the HDMI RX PIO peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_PioDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_PioDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_CTRL_CLR_OFFSET), (XV_HDMIRX_PIO_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro enables interrupts in the HDMI RX PIO peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_PioIntrEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_PioIntrEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_CTRL_SET_OFFSET), (XV_HDMIRX_PIO_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro disables interrupts in the HDMI RX PIO peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_PioIntrDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_PioIntrDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_PIO_CTRL_CLR_OFFSET), (XV_HDMIRX_PIO_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro enables the HDMI RX timer peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_TmrEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_TmrEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_TMR_CTRL_SET_OFFSET), (XV_HDMIRX_TMR_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro disables the HDMI RX timer peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_TmrDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_TmrDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_TMR_CTRL_CLR_OFFSET), (XV_HDMIRX_TMR_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro enables interrupts in the HDMI RX timer peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_TmrIntrEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_TmrIntrEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_TMR_CTRL_SET_OFFSET), (XV_HDMIRX_TMR_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro disables interrupt in the HDMI RX timer peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_TmrIntrDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_TmrIntrDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_TMR_CTRL_CLR_OFFSET), (XV_HDMIRX_TMR_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro starts the HDMI RX timer peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_TmrStart(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_TmrStart(InstancePtr, Value) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_TMR_CNT_OFFSET), (u32)(Value))

/*****************************************************************************/
/**
*
* This macro enables the HDMI RX Timing Detector peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_VtdEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_VtdEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_VTD_CTRL_SET_OFFSET), (XV_HDMIRX_VTD_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro disables the HDMI RX Timing Detector peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_VtdDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_VtdDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_VTD_CTRL_CLR_OFFSET), (XV_HDMIRX_VTD_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro enables interrupt in the HDMI RX Timing Detector peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_VtdIntrEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_VtdIntrEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_VTD_CTRL_SET_OFFSET), (XV_HDMIRX_VTD_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro disables interrupt in the HDMI RX Timing Detector peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_VtdIntrDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_VtdIntrDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_VTD_CTRL_CLR_OFFSET), (XV_HDMIRX_VTD_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro sets the timebase in the HDMI RX Timing Detector peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_VtdSetTimebase(XV_HdmiRx *InstancePtr, Value)
*
******************************************************************************/
#define XV_HdmiRx_VtdSetTimebase(InstancePtr, Value) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_VTD_CTRL_OFFSET), (u32)(Value << XV_HDMIRX_VTD_CTRL_TIMEBASE_SHIFT))


/*****************************************************************************/
/**
*
* This macro enables the HDMI RX Display Data Channel (DDC) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_SET_OFFSET), (XV_HDMIRX_DDC_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro enables the SCDC in the DDC peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcScdcEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcScdcEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_SET_OFFSET), (XV_HDMIRX_DDC_CTRL_SCDC_EN_MASK));

/*****************************************************************************/
/**
*
* This macro enables the HDCP in the DDC peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcHdcpEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcHdcpEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_SET_OFFSET), (XV_HDMIRX_DDC_CTRL_HDCP_EN_MASK));

#define XV_HdmiRx_DdcHdcpDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_CLR_OFFSET), (XV_HDMIRX_DDC_CTRL_HDCP_EN_MASK));


/*****************************************************************************/
/**
*
* This macro sets the DDC peripheral into HDCP 1.4 mode.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcHdcp14Mode(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcHdcp14Mode(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_CLR_OFFSET), (XV_HDMIRX_DDC_CTRL_HDCP_MODE_MASK));

/*****************************************************************************/
/**
*
* This macro sets the DDC peripheral into HDCP 2.2 mode.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcHdcp22Mode(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcHdcp22Mode(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_SET_OFFSET), (XV_HDMIRX_DDC_CTRL_HDCP_MODE_MASK));

/*****************************************************************************/
/**
*
* This macro disables the HDMI RX Display Data Channel (DDC) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_CLR_OFFSET), (XV_HDMIRX_DDC_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro enables interrupts in the HDMI RX Display Data Channel (DDC)
* peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcIntrEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcIntrEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_SET_OFFSET), (XV_HDMIRX_DDC_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro disables interrupts in the HDMI RX Display Data Channel (DDC)
* peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcIntrDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcIntrDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_CLR_OFFSET), (XV_HDMIRX_DDC_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro clears the SCDC registers in the DDC peripheral
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_DdcScdcClear(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcScdcClear(InstancePtr) \
{ \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_SET_OFFSET), (XV_HDMIRX_DDC_CTRL_SCDC_CLR_MASK)); \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_CLR_OFFSET), (XV_HDMIRX_DDC_CTRL_SCDC_CLR_MASK)); \
}

/*****************************************************************************/
/**
*
* This macro enables the HDMI RX Auxiliary (AUX) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_AuxEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_AuxEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_AUX_CTRL_SET_OFFSET), (XV_HDMIRX_AUX_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro disables the HDMI RX Auxiliary (AUX) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_AuxDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_AuxDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_AUX_CTRL_CLR_OFFSET), (XV_HDMIRX_AUX_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro enables interrupts in the HDMI RX Auxiliary (AUX) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_AuxIntrEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_AuxIntrEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_AUX_CTRL_SET_OFFSET), (XV_HDMIRX_AUX_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro disables interrupts in the HDMI RX Auxiliary (AUX) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_AuxIntrDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_AuxIntrDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_AUX_CTRL_CLR_OFFSET), (XV_HDMIRX_AUX_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro enables the HDMI RX Audio (AUD) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_AudioEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_AudioEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_AUD_CTRL_SET_OFFSET), (XV_HDMIRX_AUD_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro disables the HDMI RX Audio (AUD) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_AudioDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_AudioDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_AUD_CTRL_CLR_OFFSET), (XV_HDMIRX_AUD_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro enables interrupts in the HDMI RX Audio (AUD) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_AudioIntrEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_AudioIntrEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_AUD_CTRL_SET_OFFSET), (XV_HDMIRX_AUD_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro disables interrupts in the HDMI RX Audio (AUD) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_AudioIntrDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_AudioIntrDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_AUD_CTRL_CLR_OFFSET), (XV_HDMIRX_AUD_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro enables the HDMI RX Link Status (LNKSTA) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_LinkstaEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_LnkstaEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_LNKSTA_CTRL_SET_OFFSET), (XV_HDMIRX_LNKSTA_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro disables the HDMI RX Link Status (LNKSTA) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_LinkstaDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_LnkstaDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_LNKSTA_CTRL_CLR_OFFSET), (XV_HDMIRX_LNKSTA_CTRL_RUN_MASK))

/*****************************************************************************/
/**
*
* This macro enables interrupt in the HDMI RX Link Status (LNKSTA) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_LinkIntrEnable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_LinkIntrEnable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_LNKSTA_CTRL_SET_OFFSET), (XV_HDMIRX_LNKSTA_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro disable interrupt in the HDMI RX Link Status (LNKSTA) peripheral.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XV_HdmiRx_LinkIntrDisable(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_LinkIntrDisable(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_LNKSTA_CTRL_CLR_OFFSET), (XV_HDMIRX_LNKSTA_CTRL_IE_MASK))

/*****************************************************************************/
/**
*
* This macro returns true is the audio stream is active else false
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	TRUE if the audio stream is active, FALSE if it is not.
*
* @note		C-style signature:
*		u32 XV_HdmiRx_IsAudioActive(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_IsAudioActive(InstancePtr) \
	(InstancePtr)->Stream.Audio.Active

/*****************************************************************************/
/**
*
* This macro returns the number of active audio channels.
*
* @param	InstancePtr is a pointer to the XV_HdmiRx core instance.
*
* @return	Number of active audio channels.
*
* @note		C-style signature:
*		u32 XV_HdmiRx_GetAudioChannels(XV_HdmiRx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_GetAudioChannels(InstancePtr) \
	(InstancePtr)->Stream.Audio.Channels

/*****************************************************************************/
/**
*
* This macro clears the HDCP write message buffer in the DDC peripheral.
*
* @param	InstancePtr is a pointer to the XHdmi_Rx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XHdmiRx_DdcHdcpClearWriteMessageBuffer(XHdmi_Rx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcHdcpClearWriteMessageBuffer(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_SET_OFFSET), (XV_HDMIRX_DDC_CTRL_WMSG_CLR_MASK))

/*****************************************************************************/
/**
*
* This macro clears the HDCP read message buffer in the DDC peripheral.
*
* @param	InstancePtr is a pointer to the XHdmi_Rx core instance.
*
* @return	None.
*
* @note		C-style signature:
*		void XHdmiRx_DdcHdcpClearReadMessageBuffer(XHdmi_Rx *InstancePtr)
*
******************************************************************************/
#define XV_HdmiRx_DdcHdcpClearReadMessageBuffer(InstancePtr) \
	XV_HdmiRx_WriteReg((InstancePtr)->Config.BaseAddress, (XV_HDMIRX_DDC_CTRL_SET_OFFSET), (XV_HDMIRX_DDC_CTRL_RMSG_CLR_MASK))

/************************** Function Prototypes ******************************/

/* Initialization function in xv_hdmirx_sinit.c */
XV_HdmiRx_Config *XV_HdmiRx_LookupConfig(u16 DeviceId);

/* Initialization and control functions in xv_hdmirx.c */
int XV_HdmiRx_CfgInitialize(XV_HdmiRx *InstancePtr, XV_HdmiRx_Config *CfgPtr, UINTPTR EffectiveAddr);
void XV_HdmiRx_Clear(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_SetStream(XV_HdmiRx *InstancePtr, XVidC_PixelsPerClock Ppc, u32 Clock);
int XV_HdmiRx_IsStreamUp(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_IsStreamScrambled(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_IsStreamConnected(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_SetHpd(XV_HdmiRx *InstancePtr, u8 SetClr);
int XV_HdmiRx_SetPixelRate(XV_HdmiRx *InstancePtr);
void XV_HdmiRx_SetColorFormat(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_IsLinkStatusErrMax(XV_HdmiRx *InstancePtr);
void XV_HdmiRx_ClearLinkStatus(XV_HdmiRx *InstancePtr);
u32 XV_HdmiRx_GetLinkStatus(XV_HdmiRx *InstancePtr, u8 Type);
u32 XV_HdmiRx_GetAcrCts(XV_HdmiRx *InstancePtr);
u32 XV_HdmiRx_GetAcrN(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_DdcLoadEdid(XV_HdmiRx *InstancePtr, u8 *Data, u16 Length);
void XV_HdmiRx_DdcHdcpSetAddress(XV_HdmiRx *InstancePtr, u32 Addr);
void XV_HdmiRx_DdcHdcpWriteData(XV_HdmiRx *InstancePtr, u32 Data);
u32 XV_HdmiRx_DdcHdcpReadData(XV_HdmiRx *InstancePtr);
u16 XV_HdmiRx_DdcGetHdcpWriteMessageBufferWords(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_DdcIsHdcpWriteMessageBufferEmpty(XV_HdmiRx *InstancePtr);
u16 XV_HdmiRx_DdcGetHdcpReadMessageBufferWords(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_DdcIsHdcpReadMessageBufferEmpty(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_GetTmdsClockRatio(XV_HdmiRx *InstancePtr);
u8 XV_HdmiRx_GetAviVic(XV_HdmiRx *InstancePtr);
XVidC_ColorFormat XV_HdmiRx_GetAviColorSpace(XV_HdmiRx *InstancePtr);
XVidC_ColorDepth XV_HdmiRx_GetGcpColorDepth(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_GetVideoProperties(XV_HdmiRx *InstancePtr);
int XV_HdmiRx_GetVideoTiming(XV_HdmiRx *InstancePtr);
u32 XV_HdmiRx_Divide(u32 Dividend, u32 Divisor);

/* Log specific functions */
void XV_HdmiRx_DebugInfo(XV_HdmiRx *InstancePtr);

/* Self test function in xv_hdmirx_selftest.c */
int XV_HdmiRx_SelfTest(XV_HdmiRx *InstancePtr);

/* Interrupt related function in xv_hdmirx_intr.c */
void XV_HdmiRx_IntrHandler(void *InstancePtr);
int XV_HdmiRx_SetCallback(XV_HdmiRx *InstancePtr, u32 HandlerType, void *CallbackFunc, void *CallbackRef);

/* Vendor Specific Infomation related functions in xv_hdmirx_vsif.c */
int XV_HdmiRx_VSIF_ParsePacket(XV_HdmiRx_Aux *AuxPtr, XV_HdmiRx_VSIF  *VSIFPtr);
void XV_HdmiRx_VSIF_DisplayInfo(XV_HdmiRx_VSIF  *VSIFPtr);
char* XV_HdmiRx_VSIF_3DStructToString(XV_HdmiRx_3D_Struct_Field Item);
char* XV_HdmiRx_VSIF_3DSampMethodToString(XV_HdmiRx_3D_Sampling_Method Item);
char* XV_HdmiRx_VSIF_3DSampPosToString(XV_HdmiRx_3D_Sampling_Position Item);
/************************** Variable Declarations ****************************/
/************************** Variable Declarations ****************************/


#ifdef __cplusplus
}
#endif

#endif /* End of protection macro */

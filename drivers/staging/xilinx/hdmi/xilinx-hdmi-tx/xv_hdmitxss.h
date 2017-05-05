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
* @file xv_hdmitxss.h
*
* This is main header file of the Xilinx HDMI TX Subsystem driver
*
* <b>HDMI Transmitter Subsystem Overview</b>
*
* HDMI TX Subsystem is a collection of IP cores bounded together by software
* to provide an abstract view of the processing pipe. It hides all the
* complexities of programming the underlying cores from end user.
*
* <b>Subsystem Driver Features</b>
*
* HDMI Subsystem supports following features
*   - AXI Stream Input/Output interface
*   - 1, 2 or 4 pixel-wide video interface
*   - 8/10/12/16 bits per component
*   - RGB & YCbCr color space
*   - Up to 4k2k 60Hz resolution at both Input and Output interface
*   - Interlaced input support (1080i 50Hz/60Hz)

* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00         10/07/15 Initial release.
* 1.1   yh     15/01/16 Add 3D Support
* 1.2   yh     20/01/16 Added remapper support
* 1.3   yh     01/02/16 Added set_ppc api
* 1.4   MG     03/02/16 Added HDCP support
* 1.5   MG     09/03/16 Removed reduced blanking support and
*                       added XV_HdmiTxSS_SetHdmiMode and XV_HdmiTxSS_SetDviMode
* 1.6   MH     03/15/16 Added HDCP connect event
* 1.7   YH     17/03/16 Remove xintc.h as it is processor dependent
* 1.8   YH     18/03/16 Add XV_HdmiTxSs_SendGenericAuxInfoframe function
* 1.9   MH     23/06/16 Added HDCP repeater support.
* 1.10  YH     25/07/16 Used UINTPTR instead of u32 for BaseAddress
* 1.11  MH     08/08/16 Updates to optimize out HDCP when excluded.
* 1.12  YH     18/08/16 Combine Report function into one ReportInfo
*                       Add Event Log
* 1.13  YH     27/08/16 Remove unused function XV_HdmiTxSs_SetUserTimerHandler
* 1.14  YH     14/11/16 Added API to enable/disable YUV420/Pixel Repeat Mode
*                       for video bridge
* 1.15  YH     14/11/16 Remove Remapper APIs as remapper feature is moved to
*                       video bridge and controlled by HDMI core
* 1.16  mmo    03/01/17 Add compiler option(XV_HDMITXSS_LOG_ENABLE) to enable
*                            Log
*                       Re-order the enumation and data structure
* 1.17  mmo    02/03/17 Added XV_HdmiTxSs_ReadEdidSegment API for Multiple
*                             Segment Support and HDMI Compliance Test
* </pre>
*
******************************************************************************/

#ifndef HDMITXSS_H /**< prevent circular inclusions by using protection macros*/
#define HDMITXSS_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include "xstatus.h"
#include "xvidc.h"
#include "xvidc_edid.h"
#include "xv_hdmitx.h"
#include "xvtc.h"

#if !defined(XV_CONFIG_LOG_VHDMITXSS_DISABLE) && \
                                             !defined(XV_CONFIG_LOG_DISABLE_ALL)
#define XV_HDMITXSS_LOG_ENABLE
#endif

#if defined(XPAR_XHDCP_NUM_INSTANCES) || defined(XPAR_XHDCP22_TX_NUM_INSTANCES)
#define USE_HDCP_TX
#define XV_HDMITXSS_HDCP_KEYSEL 0x00u
#define XV_HDMITXSS_HDCP_MAX_QUEUE_SIZE 16
#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
#include "xtmrctr.h"
#include "xhdcp1x.h"
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
#include "xhdcp22_tx.h"
#endif



/****************************** Type Definitions ******************************/
/** @name Handler Types
* @{
*/
#ifdef XV_HDMITXSS_LOG_ENABLE
typedef enum {
	XV_HDMITXSS_LOG_EVT_NONE = 1,		  /**< Log event none. */
	XV_HDMITXSS_LOG_EVT_HDMITX_INIT,	  /**< Log event HDMITX Init. */
	XV_HDMITXSS_LOG_EVT_VTC_INIT,	      /**< Log event VTC Init. */
	XV_HDMITXSS_LOG_EVT_HDCPTIMER_INIT,	  /**< Log event HDCP Timer Init */
	XV_HDMITXSS_LOG_EVT_HDCP14_INIT,	  /**< Log event HDCP 14 Init. */
	XV_HDMITXSS_LOG_EVT_HDCP22_INIT,	  /**< Log event HDCP 22 Init. */
	XV_HDMITXSS_LOG_EVT_REMAP_HWRESET_INIT,	/**< Log event Remap reset Init. */
	XV_HDMITXSS_LOG_EVT_REMAP_INIT,		/**< Log event Remapper Init. */
	XV_HDMITXSS_LOG_EVT_START,	/**< Log event HDMITXSS Start. */
	XV_HDMITXSS_LOG_EVT_STOP,	/**< Log event HDMITXSS Stop. */
	XV_HDMITXSS_LOG_EVT_RESET,	/**< Log event HDMITXSS Reset. */
	XV_HDMITXSS_LOG_EVT_CONNECT, /**< Log event Cable connect. */
	XV_HDMITXSS_LOG_EVT_TOGGLE, /**< Log event HPD toggle. */
	XV_HDMITXSS_LOG_EVT_DISCONNECT,	/**< Log event Cable disconnect. */
	XV_HDMITXSS_LOG_EVT_STREAMUP,	/**< Log event Stream Up. */
	XV_HDMITXSS_LOG_EVT_STREAMDOWN,	/**< Log event Stream Down. */
	XV_HDMITXSS_LOG_EVT_STREAMSTART, /**< Log event Stream Start. */
	XV_HDMITXSS_LOG_EVT_SETAUDIOCHANNELS, /**< Log event Set Audio Channels. */
	XV_HDMITXSS_LOG_EVT_AUDIOMUTE,		/**< Log event Audio Mute */
	XV_HDMITXSS_LOG_EVT_AUDIOUNMUTE,	/**< Log event Audio Unmute. */
	XV_HDMITXSS_LOG_EVT_SETSTREAM,   /**< Log event HDMITXSS Setstream. */
	XV_HDMITXSS_LOG_EVT_HDCP14_AUTHREQ,   /**< Log event HDCP 1.4 AuthReq. */
	XV_HDMITXSS_LOG_EVT_HDCP22_AUTHREQ,   /**< Log event HDCP 2.2 AuthReq. */
	XV_HDMITXSS_LOG_EVT_DUMMY		/**< Dummy Event should be last */
} XV_HdmiTxSs_LogEvent;

/**
 * This typedef contains the logging mechanism for debug.
 */
typedef struct {
	u16 DataBuffer[256];		/**< Log buffer with event data. */
	u8 HeadIndex;			    /**< Index of the head entry of the
						             Event/DataBuffer. */
	u8 TailIndex;			    /**< Index of the tail entry of the
						             Event/DataBuffer. */
} XV_HdmiTxSs_Log;
#endif

/**
* These constants specify the HDCP protection schemes
*/
typedef enum
{
    XV_HDMITXSS_HDCP_NONE,   /**< No content protection */
    XV_HDMITXSS_HDCP_14,     /**< HDCP 1.4 */
    XV_HDMITXSS_HDCP_22      /**< HDCP 2.2 */
} XV_HdmiTxSs_HdcpProtocol;

#ifdef USE_HDCP_TX
/**
* These constants specify the HDCP key types
*/
typedef enum
{
    XV_HDMITXSS_KEY_HDCP22_LC128,   /**< HDCP 2.2 LC128 */
    XV_HDMITXSS_KEY_HDCP22_SRM,     /**< HDCP 2.2 SRM */
    XV_HDMITXSS_KEY_HDCP14,         /**< HDCP 1.4 Key */
    XV_HDMITXSS_KEY_HDCP14_SRM,     /**< HDCP 1.4 SRM */
    XV_HDMITXSS_KEY_INVALID         /**< Invalid Key */
} XV_HdmiTxSs_HdcpKeyType;

/**
* These constants specify HDCP repeater content stream management type
*/
typedef enum
{
    XV_HDMITXSS_HDCP_STREAMTYPE_0, /**< HDCP Stream Type 0 */
    XV_HDMITXSS_HDCP_STREAMTYPE_1  /**< HDCP Stream Type 1 */
} XV_HdmiTxSs_HdcpContentStreamType;

typedef enum
{
    XV_HDMITXSS_HDCP_NO_EVT,
    XV_HDMITXSS_HDCP_STREAMUP_EVT,
    XV_HDMITXSS_HDCP_STREAMDOWN_EVT,
    XV_HDMITXSS_HDCP_CONNECT_EVT,
    XV_HDMITXSS_HDCP_DISCONNECT_EVT,
    XV_HDMITXSS_HDCP_AUTHENTICATE_EVT,
    XV_HDMITXSS_HDCP_INVALID_EVT
} XV_HdmiTxSs_HdcpEvent;

/**
* These constants are used to identify fields inside the topology structure
*/
typedef enum {
    XV_HDMITXSS_HDCP_TOPOLOGY_DEPTH,
    XV_HDMITXSS_HDCP_TOPOLOGY_DEVICECNT,
    XV_HDMITXSS_HDCP_TOPOLOGY_MAXDEVSEXCEEDED,
    XV_HDMITXSS_HDCP_TOPOLOGY_MAXCASCADEEXCEEDED,
    XV_HDMITXSS_HDCP_TOPOLOGY_HDCP20REPEATERDOWNSTREAM,
    XV_HDMITXSS_HDCP_TOPOLOGY_HDCP1DEVICEDOWNSTREAM,
    XV_HDMITXSS_HDCP_TOPOLOGY_INVALID
} XV_HdmiTxSs_HdcpTopologyField;

typedef struct
{
    XV_HdmiTxSs_HdcpEvent   Queue[XV_HDMITXSS_HDCP_MAX_QUEUE_SIZE]; /**< Data */
    u8                      Tail;      /**< Tail pointer */
    u8                      Head;      /**< Head pointer */
} XV_HdmiTxSs_HdcpEventQueue;
#endif

/**
* These constants specify different types of handler and used to differentiate
* interrupt requests from peripheral.
*/
typedef enum {
    XV_HDMITXSS_HANDLER_CONNECT = 1,                       /**< Handler for
                                                            connect event */
    XV_HDMITXSS_HANDLER_TOGGLE,                            /**< Handler for
                                                            toggle event */
    XV_HDMITXSS_HANDLER_VS,                                /**< Handler for
                                                            vsync event */
    XV_HDMITXSS_HANDLER_STREAM_DOWN,                       /**< Handler for
                                                            stream down event */
    XV_HDMITXSS_HANDLER_STREAM_UP,                         /**< Handler for
                                                            stream up event */
    XV_HDMITXSS_HANDLER_HDCP_AUTHENTICATED,                /**< Handler for
                                                            HDCP authenticated
                                                            event */
    XV_HDMITXSS_HANDLER_HDCP_DOWNSTREAM_TOPOLOGY_AVAILABLE,/**< Handler for
                                                            HDCP downstream
                                                            topology available
                                                            event */
    XV_HDMITXSS_HANDLER_HDCP_UNAUTHENTICATED               /**< Handler for
                                                            HDCP unauthenticated
                                                            event */
} XV_HdmiTxSs_HandlerType;
/*@}*/

/**
 * Sub-Core Configuration Table
 */
typedef struct
{
  u16 IsPresent;  /**< Flag to indicate if sub-core is present in the design*/
  u16 DeviceId;   /**< Device ID of the sub-core */
  UINTPTR AbsAddr; /**< Sub-core Absolute Base Address */
}XV_HdmiTxSs_SubCore;

/**
 * Video Processing Subsystem configuration structure.
 * Each subsystem device should have a configuration structure associated
 * that defines the MAX supported sub-cores within subsystem
 */

typedef struct
{
    u16 DeviceId;                     /**< DeviceId is the unique ID  of the
                                           device */
    UINTPTR BaseAddress;              /**< BaseAddress is the physical base
                                           address of the subsystem address
                                           range */
    UINTPTR HighAddress;              /**< HighAddress is the physical MAX
                                           address of the subsystem address
                                           range */
    XVidC_PixelsPerClock Ppc;         /**< Supported Pixel per Clock */
    u8 MaxBitsPerPixel;               /**< Maximum  Supported Color Depth */
    u32 AxiLiteClkFreq;               /**< AXI Lite Clock Frequency in Hz */
    XV_HdmiTxSs_SubCore HdcpTimer;    /**< Sub-core instance configuration */
    XV_HdmiTxSs_SubCore Hdcp14;       /**< Sub-core instance configuration */
    XV_HdmiTxSs_SubCore Hdcp22;       /**< Sub-core instance configuration */
    XV_HdmiTxSs_SubCore HdmiTx;       /**< Sub-core instance configuration */
    XV_HdmiTxSs_SubCore Vtc;          /**< Sub-core instance configuration */
} XV_HdmiTxSs_Config;

/**
* Callback type for interrupt.
*
* @param  CallbackRef is a callback reference passed in by the upper
*   layer when setting the callback functions, and passed back to
*   the upper layer when the callback is invoked.
*
* @return None.
*
* @note   None.
*
*/
typedef void (*XV_HdmiTxSs_Callback)(void *CallbackRef);

/**
* The XVprocss driver instance data. The user is required to allocate a variable
* of this type for every XVprocss device in the system. A pointer to a variable
* of this type is then passed to the driver API functions.
*/
typedef struct
{
    XV_HdmiTxSs_Config Config;  /**< Hardware configuration */
    u32 IsReady;         /**< Device and the driver instance are initialized */

#ifdef XV_HDMITXSS_LOG_ENABLE
    XV_HdmiTxSs_Log Log;                /**< A log of events. */
#endif
#ifdef XPAR_XHDCP_NUM_INSTANCES
    XTmrCtr *HdcpTimerPtr;          /**< handle to sub-core driver instance */
    XHdcp1x *Hdcp14Ptr;             /**< handle to sub-core driver instance */
#endif
#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    XHdcp22_Tx  *Hdcp22Ptr;         /**< handle to sub-core driver instance */
#endif
    XV_HdmiTx *HdmiTxPtr;           /**< handle to sub-core driver instance */
    XVtc *VtcPtr;                   /**< handle to sub-core driver instance */

    /* Callbacks */
    XV_HdmiTxSs_Callback ConnectCallback; /**< Callback for connect event */
    void *ConnectRef;                     /**< To be passed to the connect
                                               callback */

    XV_HdmiTxSs_Callback ToggleCallback; /**< Callback for toggle event */
    void *ToggleRef;                     /**< To be passed to the toggle
                                              callback */

    XV_HdmiTxSs_Callback VsCallback; /**< Callback for Vsync event */
    void *VsRef;                   /**< To be passed to the Vsync callback */

    XV_HdmiTxSs_Callback StreamDownCallback; /**< Callback for stream down */
    void *StreamDownRef; /**< To be passed to the stream down callback */

    XV_HdmiTxSs_Callback StreamUpCallback; /**< Callback for stream up */
    void *StreamUpRef;  /**< To be passed to the stream up callback */

    /**< Scratch pad */
    u8 SamplingRate;              /**< HDMI TX Sampling rate */
    u8 IsStreamConnected;         /**< HDMI TX Stream Connected */
    u8 IsStreamUp;                /**< HDMI TX Stream Up */
    u8 IsStreamToggled;           /**< HDMI TX Stream Toggled */
    u8 AudioEnabled;              /**< HDMI TX Audio Enabled */
    u8 AudioMute;                 /**< HDMI TX Audio Mute */
    u8 AudioChannels;             /**< Number of Audio Channels */

    XV_HdmiTxSs_HdcpProtocol    HdcpProtocol;    /**< HDCP protect scheme */
#ifdef USE_HDCP_TX
    /**< HDCP specific */
    u32                         HdcpIsReady;     /**< HDCP ready flag */
    XV_HdmiTxSs_HdcpEventQueue  HdcpEventQueue;  /**< HDCP event queue */
#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
    u8                          *Hdcp22Lc128Ptr; /**< Pointer to HDCP 2.2
                                                      LC128 */
    u8                          *Hdcp22SrmPtr;   /**< Pointer to HDCP 2.2 SRM */
#endif
#ifdef XPAR_XHDCP_NUM_INSTANCES
    u8                          *Hdcp14KeyPtr;   /**< Pointer to HDCP 1.4 key */
    u8                          *Hdcp14SrmPtr;   /**< Pointer to HDCP 1.4 SRM */
#endif
#endif
} XV_HdmiTxSs;

/************************** Macros Definitions *******************************/
#ifdef USE_HDCP_TX
#define XV_HdmiTxSs_HdcpIsReady(InstancePtr) \
  (InstancePtr)->HdcpIsReady
#endif
/************************** Function Prototypes ******************************/
XV_HdmiTxSs_Config *XV_HdmiTxSs_LookupConfig(u32 DeviceId);
void XV_HdmiTxSS_SetHdmiMode(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSS_SetDviMode(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSS_HdmiTxIntrHandler(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_CfgInitialize(XV_HdmiTxSs *InstancePtr,
    XV_HdmiTxSs_Config *CfgPtr,
    UINTPTR EffectiveAddr);
void XV_HdmiTxSs_Start(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_Stop(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_Reset(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_SetCallback(XV_HdmiTxSs *InstancePtr,
    u32 HandlerType,
    void *CallbackFuncPtr,
    void *CallbackRef);
int XV_HdmiTxSs_ReadEdid(XV_HdmiTxSs *InstancePtr, u8 *BufferPtr);
int XV_HdmiTxSs_ReadEdidSegment(XV_HdmiTxSs *InstancePtr, u8 *Buffer, u8 segment);
void XV_HdmiTxSs_ShowEdid(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_StreamStart(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_SendAuxInfoframe(XV_HdmiTxSs *InstancePtr, void *AuxPtr);
void XV_HdmiTxSs_SendGenericAuxInfoframe(XV_HdmiTxSs *InstancePtr, void *AuxPtr);
void XV_HdmiTxSs_SetAudioChannels(XV_HdmiTxSs *InstancePtr, u8 AudioChannels);
void XV_HdmiTxSs_AudioMute(XV_HdmiTxSs *InstancePtr, u8 Enable);
u32 XV_HdmiTxSs_SetStream(XV_HdmiTxSs *InstancePtr,
    XVidC_VideoMode VideoMode,
    XVidC_ColorFormat ColorFormat,
    XVidC_ColorDepth Bpc,
    XVidC_3DInfo *Info3D);
XVidC_VideoStream *XV_HdmiTxSs_GetVideoStream(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_SetVideoStream(XV_HdmiTxSs *InstancePtr,
                                    XVidC_VideoStream VidStream);
void XV_HdmiTxSs_SetSamplingRate(XV_HdmiTxSs *InstancePtr, u8 SamplingRate);
void XV_HdmiTxSs_SetVideoIDCode(XV_HdmiTxSs *InstancePtr, u8 Vic);
void XV_HdmiTxSs_SetVideoStreamType(XV_HdmiTxSs *InstancePtr, u8 StreamType);
void XV_HdmiTxSs_SetVideoStreamScramblingFlag(XV_HdmiTxSs *InstancePtr,
                                                            u8 IsScrambled);
void XV_HdmiTxSs_SetTmdsClockRatio(XV_HdmiTxSs *InstancePtr, u8 Ratio);
u32 XV_HdmiTxSs_GetTmdsClockFreqHz(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_DetectHdmi20(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_RefClockChangeInit(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_ReportInfo(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_IsStreamUp(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_IsStreamConnected(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_IsStreamToggled(XV_HdmiTxSs *InstancePtr);
u8 XV_HdmiTxSs_IsSinkHdcp14Capable(XV_HdmiTxSs *InstancePtr);
u8 XV_HdmiTxSs_IsSinkHdcp22Capable(XV_HdmiTxSs *InstancePtr);

void XV_HdmiTxSs_SetDefaultPpc(XV_HdmiTxSs *InstancePtr, u8 Id);
void XV_HdmiTxSs_SetPpc(XV_HdmiTxSs *InstancePtr, u8 Id, u8 Ppc);

#ifdef XV_HDMITXSS_LOG_ENABLE
void XV_HdmiTxSs_LogReset(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_LogWrite(XV_HdmiTxSs *InstancePtr, XV_HdmiTxSs_LogEvent Evt, u8 Data);
u16 XV_HdmiTxSs_LogRead(XV_HdmiTxSs *InstancePtr);
#endif
void XV_HdmiTxSs_LogDisplay(XV_HdmiTxSs *InstancePtr);


#ifdef USE_HDCP_TX
void XV_HdmiTxSs_HdcpSetKey(XV_HdmiTxSs *InstancePtr, XV_HdmiTxSs_HdcpKeyType KeyType, u8 *KeyPtr);
int XV_HdmiTxSs_HdcpPoll(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpSetProtocol(XV_HdmiTxSs *InstancePtr, XV_HdmiTxSs_HdcpProtocol Protocol);
XV_HdmiTxSs_HdcpProtocol XV_HdmiTxSs_HdcpGetProtocol(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpClearEvents(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpPushEvent(XV_HdmiTxSs *InstancePtr, XV_HdmiTxSs_HdcpEvent Event);
int XV_HdmiTxSs_HdcpEnable(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpDisable(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpAuthRequest(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpEnableEncryption(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpDisableEncryption(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpEnableBlank(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpDisableBlank(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpIsEnabled(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpIsAuthenticated(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpIsEncrypted(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpIsInProgress(XV_HdmiTxSs *InstancePtr);

void XV_HdmiTxSs_HdcpInfo(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_HdcpSetInfoDetail(XV_HdmiTxSs *InstancePtr, u8 Verbose);

void *XV_HdmiTxSs_HdcpGetTopology(XV_HdmiTxSs *InstancePtr);
u8 *XV_HdmiTxSs_HdcpGetTopologyReceiverIdList(XV_HdmiTxSs *InstancePtr);
u32 XV_HdmiTxSs_HdcpGetTopologyField(XV_HdmiTxSs *InstancePtr, XV_HdmiTxSs_HdcpTopologyField Field);

void XV_HdmiTxSs_HdcpSetContentStreamType(XV_HdmiTxSs *InstancePtr,
       XV_HdmiTxSs_HdcpContentStreamType StreamType);
int XV_HdmiTxSs_HdcpIsRepeater(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpSetRepeater(XV_HdmiTxSs *InstancePtr, u8 Set);
int XV_HdmiTxSs_HdcpIsInComputations(XV_HdmiTxSs *InstancePtr);
int XV_HdmiTxSs_HdcpIsInWaitforready(XV_HdmiTxSs *InstancePtr);

#endif

#ifdef XPAR_XHDCP_NUM_INSTANCES
int XV_HdmiTxSs_HdcpTimerStart(void *InstancePtr, u16 TimeoutInMs);
int XV_HdmiTxSs_HdcpTimerStop(void *InstancePtr);
int XV_HdmiTxSs_HdcpTimerBusyDelay(void *InstancePtr, u16 DelayInMs);

void XV_HdmiTxSS_HdcpIntrHandler(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSS_HdcpTimerIntrHandler(XV_HdmiTxSs *InstancePtr);
void XV_HdmiTxSs_HdcpTimerCallback(void *CallBackRef, u8 TimerChannel);
#endif

#ifdef XPAR_XHDCP22_TX_NUM_INSTANCES
void XV_HdmiTxSS_Hdcp22TimerIntrHandler(XV_HdmiTxSs *InstancePtr);
#endif

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */

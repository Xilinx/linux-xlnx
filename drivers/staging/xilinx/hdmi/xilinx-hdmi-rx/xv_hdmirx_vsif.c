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
* @file xv_hdmirx_vsif.c
*
* Contains function definitions related to Vendor Specific InfoFrames used
* in HDMI. Please see xv_hdmirx_vsif.h for more details of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date        Changes
* ----- --- ----------   -----------------------------------------------
* X.XX  XX  YYYY/MM/DD   ...
* 1.00  RHe 2015/10/06   Initial release.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/
#include <linux/string.h>
#include "xv_hdmirx_vsif.h"
#include "xv_hdmirx.h"
#include "xil_printf.h"

/************************** Constant Definitions *****************************/

/***************** Macros (Inline Functions) Definitions *********************/

/**************************** Type Definitions *******************************/

/*************************** Function Prototypes *****************************/
static int XV_HdmiRx_VSIF_Extract3DInfo(u8 *VSIFRaw, XV_HdmiRx_3D_Info *InstancePtr);
static XV_HdmiRx_3D_Struct_Field XV_HdmiRx_VSIF_Conv3DInfoTo3DStruct(XVidC_3DInfo *InfoPtr);
static XV_HdmiRx_3D_Sampling_Method XV_HdmiRx_VSIF_Conv3DInfoTo3DSampMethod(XVidC_3DInfo *InfoPtr);
static XV_HdmiRx_3D_Sampling_Position XV_HdmiRx_VSIF_Conv3DInfoTo3DSampPos(XVidC_3DInfo *InfoPtr);
static XVidC_3DFormat XV_HdmiRx_VSIF_Conv3DStructTo3DFormat(XV_HdmiRx_3D_Struct_Field Value);
static XVidC_3DSamplingMethod XV_HdmiRx_VSIF_Conv3DSampMethodTo3DSampMethod(XV_HdmiRx_3D_Sampling_Method Value);
static XVidC_3DSamplingPosition XV_HdmiRx_VSIF_Conv3DSampPosTo3DSampPos(XV_HdmiRx_3D_Sampling_Position Value);

/************************** Variable Definitions *****************************/

/*************************** Function Definitions ****************************/

/*****************************************************************************/
/**
*
* This function extracts the 3D format from XVidC_3DInfo
* and returns a XV_HdmiRx_3D_Struct_Field type.
*
* @param InfoPtr is a pointer to the XVidC_3DInfo instance.
*
* @return The extracted value.
*
******************************************************************************/
static XV_HdmiRx_3D_Struct_Field XV_HdmiRx_VSIF_Conv3DInfoTo3DStruct(XVidC_3DInfo *InfoPtr)
{
    switch (InfoPtr->Format) {
        case XVIDC_3D_FRAME_PACKING :
            return XV_HDMIRX_3D_STRUCT_FRAME_PACKING;

        case XVIDC_3D_FIELD_ALTERNATIVE :
            return XV_HDMIRX_3D_STRUCT_FIELD_ALTERNATIVE;

        case XVIDC_3D_LINE_ALTERNATIVE :
            return XV_HDMIRX_3D_STRUCT_LINE_ALTERNATIVE;

        case XVIDC_3D_SIDE_BY_SIDE_FULL :
            return XV_HDMIRX_3D_STRUCT_SIDE_BY_SIDE_FULL;

        case XVIDC_3D_TOP_AND_BOTTOM_HALF :
            return XV_HDMIRX_3D_STRUCT_TOP_AND_BOTTOM;

        case XVIDC_3D_SIDE_BY_SIDE_HALF :
            return XV_HDMIRX_3D_STRUCT_SIDE_BY_SIDE_HALF;

        default :
            return XV_HDMIRX_3D_STRUCT_UNKNOWN;
    }
}

/*****************************************************************************/
/**
*
* This function extracts the sampling method info from XVidC_3DInfo
* and returns a XV_HdmiRx_3D_Sampling_Method type.
*
* @param InfoPtr is a pointer to the XVidC_3DInfo instance.
*
* @return The extracted value.
*
******************************************************************************/
static XV_HdmiRx_3D_Sampling_Method XV_HdmiRx_VSIF_Conv3DInfoTo3DSampMethod(XVidC_3DInfo *InfoPtr)
{
    switch (InfoPtr->Sampling.Method) {
        case XVIDC_3D_SAMPLING_HORIZONTAL :
            return XV_HDMIRX_3D_SAMPLING_HORIZONTAL;

        case XVIDC_3D_SAMPLING_QUINCUNX :
            return XV_HDMIRX_3D_SAMPLING_QUINCUNX;

        default :
            return XV_HDMIRX_3D_SAMPLING_UNKNOWN;
    }
}

/*****************************************************************************/
/**
*
* This function extracts the sampling position info from XVidC_3DInfo
* and returns a XV_HdmiRx_3D_Sampling_Position type.
*
* @param InfoPtr is a pointer to the XVidC_3DInfo instance.
*
* @return The extracted value.
*
******************************************************************************/
static XV_HdmiRx_3D_Sampling_Position XV_HdmiRx_VSIF_Conv3DInfoTo3DSampPos(XVidC_3DInfo *InfoPtr)
{
    switch (InfoPtr->Sampling.Position) {
        case XVIDC_3D_SAMPPOS_OLOR :
            return XV_HDMIRX_3D_SAMPPOS_OLOR;

        case XVIDC_3D_SAMPPOS_OLER :
            return XV_HDMIRX_3D_SAMPPOS_OLER;

        case XVIDC_3D_SAMPPOS_ELOR :
            return XV_HDMIRX_3D_SAMPPOS_ELOR;

        case XVIDC_3D_SAMPPOS_ELER :
            return XV_HDMIRX_3D_SAMPPOS_ELER;

        default :
            return XV_HDMIRX_3D_SAMPPOS_UNKNOWN;
    }
}

/*****************************************************************************/
/**
*
* This function converts a XV_HdmiRx_3D_Struct_Field type
* to a XVidC_3DFormat type.
*
* @param Value is the value to convert.
*
* @return The converted value.
*
******************************************************************************/
static XVidC_3DFormat XV_HdmiRx_VSIF_Conv3DStructTo3DFormat(XV_HdmiRx_3D_Struct_Field Value)
{
    switch(Value) {
        case XV_HDMIRX_3D_STRUCT_FRAME_PACKING :
            return XVIDC_3D_FRAME_PACKING;

        case XV_HDMIRX_3D_STRUCT_FIELD_ALTERNATIVE :
            return XVIDC_3D_FIELD_ALTERNATIVE;

        case XV_HDMIRX_3D_STRUCT_LINE_ALTERNATIVE :
            return XVIDC_3D_LINE_ALTERNATIVE;

        case XV_HDMIRX_3D_STRUCT_SIDE_BY_SIDE_FULL :
            return XVIDC_3D_SIDE_BY_SIDE_FULL;

        case XV_HDMIRX_3D_STRUCT_TOP_AND_BOTTOM :
            return XVIDC_3D_TOP_AND_BOTTOM_HALF;

        case XV_HDMIRX_3D_STRUCT_SIDE_BY_SIDE_HALF :
            return XVIDC_3D_SIDE_BY_SIDE_HALF;

        default :
            return XVIDC_3D_UNKNOWN;
    }
}

/*****************************************************************************/
/**
*
* This function converts a XV_HdmiRx_3D_Sampling_Method type
* to a XVidC_3DSamplingMethod type.
*
* @param Value is the value to convert.
*
* @return The converted value.
*
******************************************************************************/
static XVidC_3DSamplingMethod XV_HdmiRx_VSIF_Conv3DSampMethodTo3DSampMethod(XV_HdmiRx_3D_Sampling_Method Value)
{
    switch(Value) {
        case XV_HDMIRX_3D_SAMPLING_HORIZONTAL :
            return XVIDC_3D_SAMPLING_HORIZONTAL;
            break;

        case XV_HDMIRX_3D_SAMPLING_QUINCUNX :
            return XVIDC_3D_SAMPLING_QUINCUNX;
            break;

        default :
            return XVIDC_3D_SAMPLING_UNKNOWN;
            break;
    }
}

/*****************************************************************************/
/**
*
* This function converts a XV_HdmiRx_3D_Sampling_Position type
* to a XVidC_3DSamplingPosition type.
*
* @param Value is the value to convert.
*
* @return The converted value.
*
******************************************************************************/
static XVidC_3DSamplingPosition XV_HdmiRx_VSIF_Conv3DSampPosTo3DSampPos(XV_HdmiRx_3D_Sampling_Position Value)
{
    switch(Value) {
        case XV_HDMIRX_3D_SAMPPOS_OLOR :
            return XVIDC_3D_SAMPPOS_OLOR;
            break;

        case XV_HDMIRX_3D_SAMPPOS_OLER :
            return XVIDC_3D_SAMPPOS_OLER;
            break;

        case XV_HDMIRX_3D_SAMPPOS_ELOR :
            return XVIDC_3D_SAMPPOS_ELOR;
            break;

        case XV_HDMIRX_3D_SAMPPOS_ELER :
            return XVIDC_3D_SAMPPOS_ELER;
            break;

        default :
            return XVIDC_3D_SAMPPOS_UNKNOWN;
            break;
    }
}

/*****************************************************************************/
/**
*
* This function parses a Vendor Specific InfoFrame (VSIF).
*
* @param AuxPtr is a pointer to the XV_HdmiRx_Rx_Aux instance.
*
* @param VSIFPtr is a pointer to the XV_HdmiRx_VSIF instance.
*
* @return
*  - XST_SUCCESS if operation was successful
*  - XST_FAILURE if an error was detected during parsing
*
******************************************************************************/
int XV_HdmiRx_VSIF_ParsePacket(XV_HdmiRx_Aux *AuxPtr, XV_HdmiRx_VSIF  *VSIFPtr)
{
    u8 *pData;
    u32 temp;
    int Index;
    int Status = XST_FAILURE;

    /* Verify arguments */
    Xil_AssertNonvoid(AuxPtr != NULL);
    Xil_AssertNonvoid(VSIFPtr != NULL);

    pData = &AuxPtr->Data.Byte[0];

    /* Clear the instance */
    (void)memset((void *)VSIFPtr, 0, sizeof(XV_HdmiRx_VSIF));

    /* Set packet version */
    VSIFPtr->Version = AuxPtr->Header.Byte[1];

    /* IEEE Registration Identifier */
    for (Index = 0; Index < 3; Index++){
        temp = pData[Index+1];
        VSIFPtr->IEEE_ID |= (temp << (Index * 8));
    }

    /* HDMI Video Format */
    temp = (pData[4] & XV_HDMIRX_VSIF_VIDEO_FORMAT_MASK) >> XV_HDMIRX_VSIF_VIDEO_FORMAT_SHIFT;
    if (temp >= XV_HDMIRX_VSIF_VF_UNKNOWN) {
        VSIFPtr->Format = XV_HDMIRX_VSIF_VF_UNKNOWN;
    }
    else {
        VSIFPtr->Format = (XV_HdmiRx_VSIF_Video_Format)temp;
    }

    switch(VSIFPtr->Format) {
        /* HDMI VIC */
        case XV_HDMIRX_VSIF_VF_EXTRES :
            VSIFPtr->HDMI_VIC = pData[5];
            Status = XST_SUCCESS;
            break;

        /* 3D Information */
        case XV_HDMIRX_VSIF_VF_3D :
            Status = XV_HdmiRx_VSIF_Extract3DInfo(pData, &VSIFPtr->Info_3D);
            break;

        /* No additional information */
        case XV_HDMIRX_VSIF_VF_NOINFO :
            Status = XST_SUCCESS;
            break;

        default :

            break;
    }

    return Status;
}

/*****************************************************************************/
/**
*
* This function extracts the 3D information from the
* Vendor Specific InfoFrame (VSIF).
*
* @param VSIFRaw is a pointer to the VSIF payload.
*
* @param InstancePtr is a pointer to the XV_HdmiRx_3D_Info instance.
*
* @return
*  - XST_SUCCESS if operation was successful
*  - XST_FAILURE if an error was detected during parsing
*
******************************************************************************/
int XV_HdmiRx_VSIF_Extract3DInfo(u8 *VSIFRaw, XV_HdmiRx_3D_Info *InstancePtr)
{
    u8 *pData;
    u8 temp;

    /* Verify arguments */
    Xil_AssertNonvoid(VSIFRaw != NULL);
    Xil_AssertNonvoid(InstancePtr != NULL);

    /* 3D info starts at byte PB5 */
    pData = &VSIFRaw[5];

    /* Clear the instance */
    (void)memset((void *)InstancePtr, 0, sizeof(XV_HdmiRx_3D_Info));

    /* Set default values for the items that are not always set */
    InstancePtr->Stream.Sampling.Method = XV_HdmiRx_VSIF_Conv3DSampMethodTo3DSampMethod(XV_HDMIRX_3D_SAMPLING_UNKNOWN);
    InstancePtr->Stream.Sampling.Position = XV_HdmiRx_VSIF_Conv3DSampPosTo3DSampPos(XV_HDMIRX_3D_SAMPPOS_UNKNOWN);

    /* Detect 3D Metadata presence */
    if (*pData & XV_HDMIRX_3D_META_PRESENT_MASK)
        InstancePtr->MetaData.IsPresent = TRUE;
    else
        InstancePtr->MetaData.IsPresent = FALSE;

    /* Extract the 3D_Structure */
    temp = (*pData & XV_HDMIRX_3D_STRUCT_MASK) >> XV_HDMIRX_3D_STRUCT_SHIFT;
    if (temp >= XV_HDMIRX_3D_STRUCT_UNKNOWN || temp == 7) {
        InstancePtr->Stream.Format = XV_HdmiRx_VSIF_Conv3DStructTo3DFormat(XV_HDMIRX_3D_STRUCT_UNKNOWN);
    }
    else {
        InstancePtr->Stream.Format = XV_HdmiRx_VSIF_Conv3DStructTo3DFormat((XV_HdmiRx_3D_Struct_Field)temp);
    }

    /* Extract the 3D_Ext_Data */
    if (temp >= XV_HDMIRX_3D_STRUCT_SIDE_BY_SIDE_HALF) {
        /* Go to next byte */
        pData++;

        /* Extract the sampling method */
        temp = (*pData & XV_HDMIRX_3D_SAMP_METHOD_MASK) >> XV_HDMIRX_3D_SAMP_METHOD_SHIFT;
        if (temp >= XV_HDMIRX_3D_SAMPLING_UNKNOWN)
            InstancePtr->Stream.Sampling.Method = XV_HdmiRx_VSIF_Conv3DSampMethodTo3DSampMethod(XV_HDMIRX_3D_SAMPLING_UNKNOWN);
        else
            InstancePtr->Stream.Sampling.Method = XV_HdmiRx_VSIF_Conv3DSampMethodTo3DSampMethod((XV_HdmiRx_3D_Sampling_Method)temp);

        /* Extract the sampling position */
        temp = (*pData & XV_HDMIRX_3D_SAMP_POS_MASK) >> XV_HDMIRX_3D_SAMP_POS_SHIFT;
        if (temp >= XV_HDMIRX_3D_SAMPPOS_UNKNOWN)
            InstancePtr->Stream.Sampling.Position = XV_HdmiRx_VSIF_Conv3DSampPosTo3DSampPos(XV_HDMIRX_3D_SAMPPOS_UNKNOWN);
        else
            InstancePtr->Stream.Sampling.Position = XV_HdmiRx_VSIF_Conv3DSampPosTo3DSampPos((XV_HdmiRx_3D_Sampling_Position)temp);
    }

    /* Extract the 3D_Metadata */
    if (InstancePtr->MetaData.IsPresent) {
        /* Go to next byte */
        pData++;

        /* Extract the 3D Metadata type */
        temp = (*pData & XV_HDMIRX_3D_META_TYPE_MASK) >> XV_HDMIRX_3D_META_TYPE_SHIFT;
        if (temp >= XV_HDMIRX_3D_META_UNKNOWN)
            InstancePtr->MetaData.Type = XV_HDMIRX_3D_META_UNKNOWN;
        else
            InstancePtr->MetaData.Type = (XV_HdmiRx_3D_MetaData_Type)temp;

        /* Extract the 3D Metadata length */
        InstancePtr->MetaData.Length = (*pData & XV_HDMIRX_3D_META_LENGTH_MASK) >> XV_HDMIRX_3D_META_LENGTH_SHIFT;

        /* Extract the 3D Metadata */
        int i;
        for (i = 0; i<InstancePtr->MetaData.Length; i++){
            if (i < XV_HDMIRX_3D_META_MAX_SIZE)
                InstancePtr->MetaData.Data[i] = *(++pData);
            else
                return XST_FAILURE;
        }
    }

    return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function displays the contents of an XV_HdmiRx_VSIF instance.
*
* @param VSIFPtr is a pointer to the XV_HdmiRx_VSIF instance.
*
* @return None.
*
******************************************************************************/
void XV_HdmiRx_VSIF_DisplayInfo(XV_HdmiRx_VSIF  *VSIFPtr)
{
    switch (VSIFPtr->Format) {
        /* Extended resolution format present */
        case XV_HDMIRX_VSIF_VF_EXTRES :
            /* HDMI_VIC */
            xil_printf("HDMI_VIC : %d\n\r", VSIFPtr->HDMI_VIC);
            break;

        /* 3D format indication present */
        case XV_HDMIRX_VSIF_VF_3D :
            /* 3D_Structure */
            xil_printf("3D Format : %s\n\r", XV_HdmiRx_VSIF_3DStructToString(XV_HdmiRx_VSIF_Conv3DInfoTo3DStruct(&VSIFPtr->Info_3D.Stream)));


            /* 3D_Ext_Data */
            if (XV_HdmiRx_VSIF_Conv3DInfoTo3DStruct(&VSIFPtr->Info_3D.Stream) >= XV_HDMIRX_3D_STRUCT_SIDE_BY_SIDE_HALF) {
                xil_printf("Sampling Method : %s\n\r", XV_HdmiRx_VSIF_3DSampMethodToString(XV_HdmiRx_VSIF_Conv3DInfoTo3DSampMethod(&VSIFPtr->Info_3D.Stream)));
                xil_printf("Sampling Position : %s\n\r", XV_HdmiRx_VSIF_3DSampPosToString(XV_HdmiRx_VSIF_Conv3DInfoTo3DSampPos(&VSIFPtr->Info_3D.Stream)));
            }

            /* 3D Metadata */
            if (VSIFPtr->Info_3D.MetaData.IsPresent) {
                /* 3D_Metadata_type */


                /* 3D_Metadata */
            }

            break;

        default :
            break;
    }
}

/*****************************************************************************/
/**
*
* This function returns a string representation of the
* enumerated type XV_HdmiRx_3D_Struct_Field.
*
* @param Item specifies the value to convert.
*
* @return Pointer to the converted string.
*
******************************************************************************/
char* XV_HdmiRx_VSIF_3DStructToString(XV_HdmiRx_3D_Struct_Field Item)
{
    switch(Item) {
        case XV_HDMIRX_3D_STRUCT_FRAME_PACKING :
            return (char*) "Frame Packing";

        case XV_HDMIRX_3D_STRUCT_FIELD_ALTERNATIVE :
            return (char*) "Field Alternative";

        case XV_HDMIRX_3D_STRUCT_LINE_ALTERNATIVE :
            return (char*) "Line Alternative";

        case XV_HDMIRX_3D_STRUCT_SIDE_BY_SIDE_FULL :
            return (char*) "Side-by-Side(Full)";

        case XV_HDMIRX_3D_STRUCT_L_DEPTH :
            return (char*) "L + Depth";

        case XV_HDMIRX_3D_STRUCT_L_DEPTH_GRAPH_GDEPTH :
            return (char*) "L + Depth + Graphics + Graphics-depth";

        case XV_HDMIRX_3D_STRUCT_TOP_AND_BOTTOM :
            return (char*) "Top-and-Bottom";

        case XV_HDMIRX_3D_STRUCT_SIDE_BY_SIDE_HALF :
            return (char*) "Side-by-Side(Half)";

        default :
            return (char*) "Unknown";
    }
}

/*****************************************************************************/
/**
*
* This function returns a string representation of the
* enumerated type XV_HdmiRx_3D_Sampling_Method.
*
* @param Item specifies the value to convert.
*
* @return Pointer to the converted string.
*
******************************************************************************/
char* XV_HdmiRx_VSIF_3DSampMethodToString(XV_HdmiRx_3D_Sampling_Method Item)
{
    switch (Item) {
        case XV_HDMIRX_3D_SAMPLING_HORIZONTAL :
            return (char*) "Horizontal Sub-Sampling";

        case XV_HDMIRX_3D_SAMPLING_QUINCUNX :
            return (char*) "Quincunx Matrix";

        default :
            return (char*) "Unknown";
    }
}

/*****************************************************************************/
/**
*
* This function returns a string representation of the
* enumerated type XV_HdmiRx_3D_Sampling_Position.
*
* @param Item specifies the value to convert.
*
* @return Pointer to the converted string.
*
******************************************************************************/
char* XV_HdmiRx_VSIF_3DSampPosToString(XV_HdmiRx_3D_Sampling_Position Item)
{
    switch(Item) {
        case XV_HDMIRX_3D_SAMPPOS_OLOR :
            return (char*) "Odd/Left, Odd/Right";

        case XV_HDMIRX_3D_SAMPPOS_OLER :
            return (char*) "Odd/Left, Even/Right";

        case XV_HDMIRX_3D_SAMPPOS_ELOR :
            return (char*) "Even/Left, Odd/Right";

        case XV_HDMIRX_3D_SAMPPOS_ELER :
            return (char*) "Even/Left, Even/Right";

        default :
            return (char*) "Unknown";
    }
}

/****************************************************************************
 *
 * Copyright 2014 (c) Pointwise, Inc.
 * All rights reserved.
 *
 * This sample Pointwise plugin is not supported by Pointwise, Inc.
 * It is provided freely for demonstration purposes only.
 * SEE THE WARRANTY DISCLAIMER AT THE BOTTOM OF THIS FILE.
 *
 ***************************************************************************/

/****************************************************************************
 *
 * Pointwise Plugin utility functions
 *
 ***************************************************************************/

#ifndef _RTCAEPINITITEMS_H_
#define _RTCAEPINITITEMS_H_

// registered in ...\src\plugins\pluginRegistry.h
#define ID_CaeOpenFOAM     2

/*............................*/

{
    /*== CAEP_FORMATINFO FormatInfo */
    {   PWP_SITE_GROUPNAME,        /* const char *group */
        "OpenFOAM",                /* const char *name */
        MAKEGUID(ID_CaeOpenFOAM), /* PWP_UINT32 id */

        PWP_FILEDEST_FOLDER,  /* CAEP_ENUM_FILEDEST fileDest */

        PWP_FALSE,               /* PWP_BOOL allowedExportConditionsOnly */
        PWP_TRUE,               /* PWP_BOOL allowedVolumeConditions */

        PWP_TRUE,               /* PWP_BOOL allowedFileFormatASCII */
        PWP_FALSE,              /* PWP_BOOL allowedFileFormatBinary */
        PWP_FALSE,              /* PWP_BOOL allowedFileFormatUnformatted */

        PWP_FALSE,               /* PWP_BOOL allowedDataPrecisionSingle */
        PWP_TRUE,                /* PWP_BOOL allowedDataPrecisionDouble */

        PWP_FALSE,               /* PWP_BOOL allowedDimension2D */
        PWP_TRUE                 /* PWP_BOOL allowedDimension3D */
    },

    &pwpRtItem[1],  /* PWU_RTITEM* */

    /*== CAEP_BCINFO*    pBCInfo;    -- array of format BCs or NULL */
    /*== PWP_UINT32      BCCnt;      -- # format BCs */
    //                          See rtCaepSupportData.h
    ofoamBCInfo,                /* CAEP_BCINFO* */
    ARRAYSIZE(ofoamBCInfo),     /* PWP_UINT32 BCCnt */

    /*== CAEP_VCINFO*    pVCInfo;    -- array of format VCs or NULL */
    /*== PWP_UINT32      VCCnt;      -- # format VCs */
    //                          See rtCaepSupportData.h
    ofoamVCInfo,                /* CAEP_VCINFO* pVCInfo */
    ARRAYSIZE(ofoamVCInfo),     /* PWP_UINT32 VCCnt */

    /*== const char**    pFileExt;   -- array of valid file extensions */
    /*== PWP_UINT32      ExtCnt;     -- # valid file extensions */
    0,                          /* const char **pFileExt */
    0,                          /* PWP_UINT32 ExtCnt */

    /*== PWP_BOOL  elemType[PWGM_ELEMTYPE_SIZE]; -- un/supported elem */
    {   PWP_FALSE,             /* elemType[PWGM_ELEMTYPE_BAR] */
        PWP_TRUE,              /* elemType[PWGM_ELEMTYPE_HEX] */
        PWP_TRUE,              /* elemType[PWGM_ELEMTYPE_QUAD] */
        PWP_TRUE,              /* elemType[PWGM_ELEMTYPE_TRI] */
        PWP_TRUE,              /* elemType[PWGM_ELEMTYPE_TET] */
        PWP_TRUE,              /* elemType[PWGM_ELEMTYPE_WEDGE] */
        PWP_TRUE,              /* elemType[PWGM_ELEMTYPE_PYRAMID] */
        PWP_TRUE },            /* elemType[PWGM_ELEMTYPE_POINT] */

    0,  /* FILE *fp */

    /* PWU_UNFDATA UnfData */
    {   0,          /* PWP_UINT32 status */
        0,          /* FILE *fp */
        0,          /* sysFILEPOS fPos */
        PWP_FALSE,  /* PWP_BOOL hadError */
        PWP_FALSE,  /* PWP_BOOL inRec */
        0,          /* PWP_UINT32 recBytes */
        0,          /* PWP_UINT32 totRecBytes */
        0    },     /* PWP_UINT32 recCnt */

    0,  /* PWGM_HGRIDMODEL model */

    0,  /* const CAEP_WRITEINFO *pWriteInfo */

    0,   /* PWP_UINT32 progTotal */
    0,   /* PWP_UINT32 progComplete */
    {0}, /* clock_t clocks[] */
    0,   /* PWP_BOOL opAborted */
}
/*............................*/

#endif /* _RTCAEPINITITEMS_H_ */

/****************************************************************************
 *
 * DISCLAIMER:
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, POINTWISE DISCLAIMS
 * ALL WARRANTIES, EITHER EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED
 * TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, WITH REGARD TO THIS SCRIPT. TO THE MAXIMUM EXTENT PERMITTED
 * BY APPLICABLE LAW, IN NO EVENT SHALL POINTWISE BE LIABLE TO ANY PARTY
 * FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES
 * WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF
 * BUSINESS INFORMATION, OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE
 * USE OF OR INABILITY TO USE THIS SCRIPT EVEN IF POINTWISE HAS BEEN
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGES AND REGARDLESS OF THE
 * FAULT OR NEGLIGENCE OF POINTWISE.
 *
 ***************************************************************************/

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

#ifndef _RTCAEPSUPPORTDATA_H_
#define _RTCAEPSUPPORTDATA_H_

#include "vctypes.h"

/*------------------------------------*/
/* caeExp1 format item setup data */
/*------------------------------------*/
CAEP_BCINFO ofoamBCInfo[] = {
    { "patch",          100 },
    { "wall",           101 },
    { "symmetryPlane",  102 },
    { "empty",          103 },
    { "wedge",          104 },
    { "cyclic",         105 },
};

/*------------------------------------*/
CAEP_VCINFO ofoamVCInfo[] = {
    { "volumeToFace",                   VcFaces },   // All faces
    { "interiorToFace",                 VcIFaces },  // Only interior faces
    { "boundaryToFace",                 VcBFaces },  // Only boundary faces
    { "interiorToFace+boundaryToFace",  VcIBFaces }, // Interior and boundary faces separately

    { "volumeToCell",                               VcCells },        // All cells
    { "volumeToCell+volumeToFace",                  VcCellsFaces },   // All cells and faces
    { "volumeToCell+interiorToFace",                VcCellsIFaces },  // All cells and interior faces
    { "volumeToCell+boundaryToFace",                VcCellsBFaces },  // All cells and boundary faces
    { "volumeToCell+interiorToFace+boundaryToFace", VcCellsIBFaces }, // All cells and interior and boundary faces separately
};

#endif /* _RTCAEPSUPPORTDATA_H_ */

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

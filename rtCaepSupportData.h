/****************************************************************************
 *
 * (C) 2021 Cadence Design Systems, Inc. All rights reserved worldwide.
 *
 * This sample source code is not supported by Cadence Design Systems, Inc.
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
    { "faceSet",        106 },
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
 * This file is licensed under the Cadence Public License Version 1.0 (the
 * "License"), a copy of which is found in the included file named "LICENSE",
 * and is distributed "AS IS." TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE
 * LAW, CADENCE DISCLAIMS ALL WARRANTIES AND IN NO EVENT SHALL BE LIABLE TO
 * ANY PARTY FOR ANY DAMAGES ARISING OUT OF OR RELATING TO USE OF THIS FILE.
 * Please see the License for the full text of applicable terms.
 *
 ****************************************************************************/

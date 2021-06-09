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

#ifndef _VCTYPES_H_
#define _VCTYPES_H_

// VC is Unspecified
const PWP_UINT32 VcNone  = 0;

// Create single set/zone files for interior VC faces
const PWP_UINT32 VcIFaces  = 0x0001;

// Create single set/zone files for boundary VC faces
const PWP_UINT32 VcBFaces  = 0x0002;

// Create separate set/zone files for interior & boundary VC faces.
// This bit only has meaning when both VcIFaces and VcBFaces are enabled.
const PWP_UINT32 VcSplit   = 0x0004;

// Create single set/zone files for VC cells
const PWP_UINT32 VcCells   = 0x0008;

// Create single set/zone files for all VC faces together
const PWP_UINT32 VcFaces        = VcIFaces | VcBFaces;

// Create separate set/zone files for interior & boundary VC faces
const PWP_UINT32 VcIBFaces      = VcSplit | VcFaces;

// Create separate set/zone files for cells AND all VC faces together
const PWP_UINT32 VcCellsFaces   = VcCells | VcFaces;

// Create separate set/zone files for cells AND interior VC faces
const PWP_UINT32 VcCellsIFaces  = VcCells | VcIFaces;

// Create separate set/zone files for cells AND boundary VC faces
const PWP_UINT32 VcCellsBFaces  = VcCells | VcBFaces;

// Create separate set/zone files for cells AND interior & boundary VC faces
const PWP_UINT32 VcCellsIBFaces = VcCells | VcIBFaces;

#endif /* _VCTYPES_H_ */

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

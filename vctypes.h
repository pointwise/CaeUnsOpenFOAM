/****************************************************************************
 *
 * Copyright (c) 2012-2016 Pointwise, Inc.
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

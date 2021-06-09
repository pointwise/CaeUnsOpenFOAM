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
 * Pointwise OpenFOAM CAE Export Plugin
 *
 ***************************************************************************/

#include "apiCAEP.h"
#include "apiCAEPUtils.h"
#include "apiGridModel.h"
#include "apiPWP.h"
#include "runtimeWrite.h"
#include "pwpPlatform.h"
#include "vctypes.h"

#include <algorithm> // don't need this for C++11
#include <cmath>
#include <cstring>
#include <errno.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
//#include <utility> // std::swap<T>() moved here in C++11

#if defined(WINDOWS)
#   include <direct.h>
#   include <malloc.h>
    typedef int mode_t;
#else
#   include <stdlib.h>
#   include <sys/stat.h>
#   include <sys/types.h>
#endif /* WINDOWS */

#include <math.h>

// Disable warnings caused by the current usage of fgets, fscanf, etc.
#if defined(linux)
#pragma GCC diagnostic ignored "-Wunused-result"
#endif /* linux */


typedef std::set<std::string>               StringSet;
typedef std::vector<std::string>            StringVec;
typedef std::map<PWP_UINT32, PWP_UINT32>    UInt32UInt32Map;
typedef std::map<const char*, PWP_UINT32>   CharPtrUInt32Map;

enum Orientation {
    NegativeZ = -1,
    UnknownZ = 0,
    PositiveZ = 1
};


static const char *Unspecified = "Unspecified";
static const PWGM_CONDDATA UnspecifiedCond = {
    Unspecified,
    PWGM_UNSPECIFIED_COND_ID,
    Unspecified,
    PWGM_UNSPECIFIED_TYPE_ID
};

static const char *FaceExport       = "FaceExport";
static const char *CellExport       = "CellExport";
static const char *PointPrecision   = "PointPrecision";
static const char *Thickness        = "Thickness";
static const char *SideBCExport     = "SideBCExport";
enum SideBcMode {
    BcModeUnspecified,
    BcModeSingle,
    BcModeBaseTop,
    BcModeMultiple
};

static const PWP_REAL   ThicknessDef            = 0.0;
static const char *     ThicknessDefStr         = "0.0";
static const PWP_UINT   PointPrecisionDef       = 16;
static const char *     PointPrecisionDefStr    = "16";


/***************************************************************************
 * pwpCreateDir: create a fully-writable directory
 ***************************************************************************/
static int
pwpCreateDir(const char *dir, mode_t mode = 0777)
{
#if defined(WINDOWS)
    (void)mode; // silence unused arg warning
    return mkdir(dir);
#else
    return mkdir(dir, mode);
#endif /* WINDOWS */
}


/***************************************************************************
 * pwpDeleteDir: delete a directory
 ***************************************************************************/
static int
pwpDeleteDir(const char *dir)
{
    return rmdir(dir);
}


// return a sanitized file name
static const char *
safeFileName(const char *unsafeName, const char *suffix = "")
{
    const std::string safeChars("-_.");
    static std::string safeName;
    safeName = unsafeName;
    std::string::iterator it = safeName.begin();
    for (; it != safeName.end(); ++it) {
        // replace invalid characters in the file name with underscore
        if (!isalnum(*it) && (std::string::npos == safeChars.find(*it))) {
            *it = '_';
        }
    }
    safeName += suffix;
    return safeName.c_str();
}

// return a unique sanitized file name
static const char *
uniqueSafeFileName(const char *unsafeName, StringSet &usedNames,
    const char *suffix = "")
{
    static std::string safeName;
    std::string baseSafeName = safeName = safeFileName(unsafeName, suffix);
    int ndx = 0;
    char ndxStr[64];
    while (usedNames.end() != usedNames.find(safeName)) {
        safeName = baseSafeName;
        safeName += "-";
        sprintf(ndxStr, "%d", ++ndx);
        safeName += ndxStr;
    }
    usedNames.insert(safeName);
    return safeName.c_str();
}


static bool
getXYZ(PWGM_XYZVAL xyz[3], PWGM_HVERTEX vertex)
{
    return  PwVertXyzVal(vertex, PWGM_XYZ_X, &(xyz[0])) &&
            PwVertXyzVal(vertex, PWGM_XYZ_Y, &(xyz[1])) &&
            PwVertXyzVal(vertex, PWGM_XYZ_Z, &(xyz[2]));
}


static void
createVector(PWGM_XYZVAL vector[3], const PWGM_XYZVAL xyzStart[3], 
        const PWGM_XYZVAL xyzEnd[3])
{
    vector[0] = xyzEnd[0] - xyzStart[0];
    vector[1] = xyzEnd[1] - xyzStart[1];
    vector[2] = xyzEnd[2] - xyzStart[2];
}


static PWP_REAL
calcLength(const PWGM_XYZVAL xyzStart[3], const PWGM_XYZVAL xyzEnd[3])
{
    const PWP_REAL dx = xyzEnd[0] - xyzStart[0];
    const PWP_REAL dy = xyzEnd[1] - xyzStart[1];
    const PWP_REAL dz = xyzEnd[2] - xyzStart[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}


/*

From: http://www.openfoam.org/docs/user/mesh-description.php

5.1.1.2 Faces

A face is an ordered list of points, where a point is referred to by its label.
The ordering of point labels in a face is such that each two neighbouring
points are connected by an edge, i.e. you follow points as you travel around
the circumference of the face. Faces are compiled into a list and each face
is referred to by its label, representing its position in the list. The
direction of the face normal vector is defined by the right-hand rule,
i.e. looking towards a face, if the numbering of the points follows an
anti-clockwise path, the normal vector points towards you, as shown in
Figure 5.1.

      http://www.openfoam.org/docs/user/img/user428x.png

      Figure 5.1: Face area vector from point numbering on the face

There are two types of face:
Internal faces
    Those faces that connect two cells (and it can never be more than two).
    For each internal face, the ordering of the point labels is such that
    the face normal points into the cell with the larger label, i.e. for cells
    2 and 5, the normal points into 5; 
Boundary faces
    Those belonging to one cell since they coincide with the boundary of the
    domain. A boundary face is therefore addressed by one cell(only) and a
    boundary patch. The ordering of the point labels is such that the face
    normal points outside of the computational domain.

Faces are generally expected to be convex; at the very least the face centre
needs to be inside the face. Faces are allowed to be warped, i.e. not all
points of the face need to be coplanar. 

*/

class GridValidator {
public:

    /****************************************************************************
     * 
     * The getGridProperties(CAEP_RTITEM) function, used locally.
     * 
     * getGridProperties(CAEP_RTITEM) This function calculates the 
     * orientation of each domain based on the ordering of the points of the first 
     * element of the block. This is necessary to determine the proper order to output
     * the points for the triangles and quads as well as to decide the direction the z 
     * component will be incremented. If any domain is not oriented the same way as 
     * the first domain, a warning message is displayed to the user and the orientation
     * is assumed to be the direction of the first domain.
     * 
     ***************************************************************************/
    static void
    getGridProperties(PWGM_HGRIDMODEL model, bool &isZPlanar,
        PWGM_XYZVAL &planeZ, Orientation &orientation, bool &consistent)
    {
        PWGM_XYZVAL xyz0[3];
        PWGM_XYZVAL xyz1[3];
        PWGM_XYZVAL xyz2[3];
        PWGM_XYZVAL vector1[3];
        PWGM_XYZVAL vector2[3];
        PWGM_HVERTEX vertex;
        PWGM_HELEMENT element;
        PWGM_HBLOCK block;
        PWGM_ELEMDATA data = {PWGM_ELEMTYPE_SIZE};

        // Using the first block for the direction of extrusion regardless of 
        // the orientation of the other blocks.
        block = PwModEnumBlocks(model, 0);
        element = PwBlkEnumElements(block, 0);
        PwElemDataMod(element, &data);

        vertex = PwModEnumVertices(model, data.index[0]);
        getXYZ(xyz0, vertex);
        vertex = PwModEnumVertices(model, data.index[1]);
        getXYZ(xyz1, vertex);

        if (data.vertCnt == 4) {
            // Element is a quad, the vertex to be used is the vertex right
            // next to the 0th point i.e. point 3.
            vertex = PwModEnumVertices(model, data.index[3]);
            getXYZ(xyz2, vertex);
        } 
        else if (data.vertCnt == 3) {
            // Element is a tri, the vertex to be used is the only other vertex
            // remaining.
            vertex = PwModEnumVertices(model, data.index[2]);
            getXYZ(xyz2, vertex);
        }
    
        createVector(vector1, xyz0, xyz1);
        createVector(vector2, xyz0, xyz2);
        orientation = calcZOrientation(vector1, vector2);
        isConsistent(model, orientation, consistent);
        isPlanar(model, isZPlanar, planeZ);
    }


private:

    /**************************************************************************
    * The orientation of the block is determined by the value of the z comp
    * This is done using the formula:
    * <cx, cy, cz> = < ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx >
    * because the z component is the only required calculation, the third part 
    * of the formula is the only part being calculated.
    *************************************************************************/
    static Orientation 
    calcZOrientation(PWGM_XYZVAL vector1[3], PWGM_XYZVAL vector2[3])
    {
        const PWGM_XYZVAL orientationCheck = vector1[0] * vector2[1] -
            vector1[1] * vector2[0];
        return (orientationCheck > 0) ? PositiveZ : NegativeZ;
    }


    /**************************************************************************
    * This function verifies if the domains of each block is oriented the same
    * way. This is done by performing a vector cross product inside of each
    * block and comparing that with the orientation of the first block.
    **************************************************************************/
    static void
    isConsistent(PWGM_HGRIDMODEL model, const Orientation masterOrientation,
        bool &consistent)
    {
        PWP_UINT32 i;
        PWGM_XYZVAL xyz0[3], xyz1[3], xyz2[3];
        PWGM_XYZVAL vector1[3], vector2[3];
        PWP_UINT32 numBlocks = PwModBlockCount(model);
        PWGM_HVERTEX vertex;
        PWGM_HELEMENT element;
        PWGM_ELEMDATA data = {PWGM_ELEMTYPE_SIZE};
        PWGM_HBLOCK block;

        consistent = true;
        for (i=1; i < numBlocks; i++) {
            block = PwModEnumBlocks(model, i);
            element = PwBlkEnumElements(block, 0);
            PwElemDataMod(element, &data);

            vertex = PwModEnumVertices(model, data.index[0]);
            getXYZ(xyz0, vertex);
            vertex = PwModEnumVertices(model, data.index[1]);
            getXYZ(xyz1, vertex);

            if (data.vertCnt == 4) {
                vertex = PwModEnumVertices(model, data.index[3]);
                getXYZ(xyz2, vertex);
            }
            else if (data.vertCnt == 3) {
                vertex = PwModEnumVertices(model, data.index[2]);
                getXYZ(xyz2, vertex);
            }
            createVector(vector1, xyz0, xyz1);
            createVector(vector2, xyz0, xyz2);
            // Check to see if this block is oriented the same way as the first
            // block.
            if (masterOrientation != calcZOrientation(vector1, vector2)) {
                consistent = false;
                break;
            }
        }
    }
    

    /**************************************************************************
    * This function verifies if a grid is planar in the xy-plane. This is done
    * by checking the value of the first point with all other points and
    * comparing that value with the value of the grid tolerance.
    **************************************************************************/
    static void
    isPlanar(PWGM_HGRIDMODEL model, bool &isZPlanar, PWGM_XYZVAL &planeZ)
    {
        PWP_REAL gridPtTol;
        PwModGetAttributeREAL(model, "GridPointTol", &gridPtTol);
        PWP_UINT32 index = 0;
        PWGM_VERTDATA masterPt;
        if (!PwVertDataMod(PwModEnumVertices(model, index), &masterPt)) {
            // someting very bad just happened
            isZPlanar = false;
            planeZ = 0.0;
        }
        else {
            PWGM_VERTDATA vData;
            planeZ = masterPt.z;
            isZPlanar = true;
            while (PwVertDataMod(PwModEnumVertices(model, ++index), &vData)) {
                const PWGM_XYZVAL dz = planeZ - vData.z;
                if (fabs(dz) > gridPtTol) {
                    isZPlanar = false;
                    break;
                }
            }
        }
    }
};


/***************************************************************************
 * Base class FoamFile represents any output file for OpenFOAM.
 ***************************************************************************/
class FoamFile {

    enum { FldWd = 10 }; // num chars reserved for the numItems

public:
    // Constructor
    FoamFile(const char *cls, const char *object, const char *location = 0,
        const char *version = 0, const char *format = 0) :
            class_(cls ? cls : ""),
            object_(object ? object : ""),
            location_(location ? location : "constant/polyMesh"),
            version_(version ? version : "2.0"),
            format_(format ? format : "ascii"),
            fp_(0),
            pos_(),
            numItems_(0)
    {
    }

    // Destructor
    virtual ~FoamFile()
    {
        // ensure file closure
        close();
    }

    // set the class of file, stored in internal file header
    void setClass(const char *cls)
    {
        class_ = (cls ? cls : "");
    }

    // get the class of file
    const char * getClass() const
    {
        return class_.c_str();
    }

    // open the output file and write file header
    bool open(const char *object = 0)
    {
        close();
        numItems_ = 0;
        if (0 != object) {
            object_ = object;
        }
        if (!object_.empty()) {
            fp_ = pwpFileOpen(object_.c_str(), pwpWrite | pwpAscii);
        }
        if (fp_) {
            this->notifyOpen();
            writeFileHeader();
            pwpFileGetpos(fp_, &pos_);
            fprintf(fp_, "%*d\n", -FldWd, 0);
            fprintf(fp_, "(\n");
        }
        return 0 != fp_;
    }

    // close the file
    void close()
    {
        if (0 != fp_) {
            sysFILEPOS savePos;
            if (getSetFilePos(savePos, pos_)) {
                fprintf(fp_, "%*lu\n", -FldWd, (unsigned long)numItems_);
                pwpFileSetpos(fp_, &savePos);
            }
            this->notifyClosing();
            fputs(")\n", fp_);
            pwpFileClose(fp_);
            fp_ = 0;
        }
    }

    // increment the item counter
    PWP_UINT32 incrNumItems(PWP_UINT32 incr = 1)
    {
        return (numItems_ += incr);
    }

    // get the current number of items
    PWP_UINT32 getNumItems() const
    {
        return numItems_;
    }

    // return whether the file is open
    bool isOpen() const
    {
        return 0 != fp_;
    }

    // return the object (file) name
    const char *object() const
    {
        return object_.c_str();
    }

    // provide access to the underlying FILE pointer
    operator FILE*()
    {
        return fp_;
    }

private:
    // change file position to setPos, store old position in getPos
    bool getSetFilePos(sysFILEPOS &getPos, const sysFILEPOS &setPos)
    {
        return fp_ && !pwpFileGetpos(fp_, &getPos) &&
                !pwpFileSetpos(fp_, &setPos);
    }

    // callback for subclasses after the output file is opened successfully
    virtual void notifyOpen()
    {
        // do nothing by default
    }

    // callback for subclasses that the output file is about to be closed
    virtual void notifyClosing()
    {
        // do nothing by default
    }

    // write standard file header for all OpenFOAM files
    void writeFileHeader()
    {
        fprintf(fp_,     "FoamFile\n");
        fprintf(fp_,     "{\n");
        fprintf(fp_,     "    version     %s;\n", version_.c_str());
        fprintf(fp_,     "    format      %s;\n", format_.c_str());
        fprintf(fp_,     "    class       %s;\n", class_.c_str());
        fprintf(fp_,     "    location    \"%s\";\n", location_.c_str());
        fprintf(fp_,     "    object      %s;\n", object_.c_str());
        fputs("}\n", fp_);
        fputs("\n", fp_);
    }

private:
    std::string   class_;       // output file class name
    std::string   object_;      // output file name
    std::string   location_;    // ouput file location
    std::string   version_;     // output file version
    std::string   format_;      // output file format
    FILE        * fp_;          // underlying FILE
    sysFILEPOS    pos_;         // file position of item counter
    PWP_UINT32    numItems_;    // number of items written to the file
};


/***************************************************************************
 * Class FoamPointFile writes an OpenFOAM "points" file. The points file
 * contains all mesh global vertices.
 ***************************************************************************/
class FoamPointFile : public FoamFile {
public:
    // Default constructor, set class name and file name
    FoamPointFile(PWP_UINT prec) :
        FoamFile("vectorField", "points"),
        prec_(prec)
    {
    }

    // destructor
    virtual ~FoamPointFile()
    {
    }


    // write vertex to points file, one per line
    // (x y z)
    inline void
    writeVertex(const PWGM_VERTDATA &v)
    {
        const int p = (int)prec_;
        fprintf(*this, "(%.*g %.*g %.*g)\n", p, v.x, p, v.y, p, v.z);
        incrNumItems();
    }


    // write global vertex to points file
    inline void
    writeVertex(const PWGM_HVERTEX h)
    {
        PWGM_VERTDATA v;
        if (PwVertDataMod(h, &v)) {
            writeVertex(v);
        }
    }


    // write global vertex to points file
    void writeVertex(const PWGM_HVERTEX h, const PWGM_XYZVAL newZ)
    {
        PWGM_VERTDATA v;
        if (PwVertDataMod(h, &v)) {
            v.z = newZ;
            writeVertex(v);
        }
    }


private:

    PWP_UINT    prec_;
};


/***************************************************************************
 * Class FoamFacesFile write an OpenFOAM "faces" file. The faces file
 * contains cell face information as a list of global vertex indices.
 * A face is written as the number of vertices followed by the list of
 * vertices that comprise the face (quad, tri or bar).
 ***************************************************************************/
class FoamFacesFile : public FoamFile {
public:
    // Default constructor, set class and file name
    FoamFacesFile(bool is2D, PWP_UINT32 vertexCount) :
        FoamFile("faceList", "faces"),
        is2D_(is2D),
        vertexCount_(vertexCount)
    {
    }

    // destructor
    virtual ~FoamFacesFile()
    {
    }

    // write a cell face to the faces file
    void writeFace(PWGM_ELEMDATA &eData)
    {
        // The PW cell-face owner/bndry model has the face normals pointing
        // to the interior of the owner cell. Due to the way cells are
        // processed during streaming, the owner cell will always have a lower
        // cell id. The OpenFOAM spec requires an internal face normal to point
        // from the lower numbered cell to the higher numbered cell. Boundary
        // face normals must point outside the volume. Basically, the
        // exact opposite of PW.

        // Use a switch to avoid multiple fprintf() calls in a loop
        switch (eData.type) {
        case PWGM_ELEMTYPE_QUAD:
            fprintf(*this, "%lu(%lu %lu %lu %lu)\n",
                (unsigned long)eData.vertCnt, 
                (unsigned long)eData.index[3],
                (unsigned long)eData.index[2], 
                (unsigned long)eData.index[1],
                (unsigned long)eData.index[0]);
            incrNumItems();
            break;
        case PWGM_ELEMTYPE_TRI:
            fprintf(*this, "%lu(%lu %lu %lu)\n", 
                (unsigned long)eData.vertCnt,
                (unsigned long)eData.index[2], 
                (unsigned long)eData.index[1],
                (unsigned long)eData.index[0]);
            incrNumItems();
            break;
        case PWGM_ELEMTYPE_BAR:
            if (is2D_) {
                fprintf(*this, "%lu(%lu %lu %lu %lu)\n",
                    (unsigned long)eData.vertCnt + 2,
                    (unsigned long)eData.index[0],
                    (unsigned long)eData.index[1],
                    (unsigned long)eData.index[1] + vertexCount_,
                    (unsigned long)eData.index[0] + vertexCount_);
                incrNumItems();
            }
            else {
                fprintf(*this, "%lu(%lu %lu)\n", 
                    (unsigned long)eData.vertCnt,
                    (unsigned long)eData.index[1], 
                    (unsigned long)eData.index[0]);
                incrNumItems();
            }
            break;
        default:
            break;
        }
    }

private:
    PWP_UINT32  vertexCount_;     // Total number of vertices in file
    PWP_BOOL    is2D_;            // Is the file 2D?
};


/***************************************************************************
 * Base Class FoamAddressFile is used to write OpenFOAM cell topology
 * information.
 ***************************************************************************/
class FoamAddressFile : public FoamFile {

    enum { ItemsPerRow = 10 }; // max num addresses per line

public:
    // Constructor, set class type as "labelList"
    FoamAddressFile(const char *object, const char *location = 0) :
        FoamFile("labelList", object, location)
    {
    }

    // Destructor, forces end-of-line for partially filled rows
    virtual ~FoamAddressFile()
    {
        cleanup();
    }

    // write an address to the current row in the file, adding a row as needed
    void writeAddress(PWP_UINT32 addr)
    {
        const char *fmt = (needNewline() ? " %lu\n" : " %lu");
        fprintf(*this, fmt, (unsigned long)addr);
        incrNumItems();
    }

private:
    // break rows at ItemsPerRow items
    bool needNewline() const {
        return ((getNumItems() % ItemsPerRow) == (ItemsPerRow - 1));
    }

    // close partial row
    void cleanup()
    {
        if (isOpen()) {
            if (0 != getNumItems() % ItemsPerRow) {
                fputs("\n", *this);
            }
        }
    }

    // inherited callback to force end-of-row cleanup
    virtual void notifyClosing()
    {
        cleanup();
    }
};


/***************************************************************************
 * Class FoamOwnerFile writes an OpenFOAM "owner" file. This file
 * maps face index to owner cell index.
 ***************************************************************************/
class FoamOwnerFile : public FoamAddressFile {
public:
    // Default constructor, sets object to "owner"
    FoamOwnerFile() :
        FoamAddressFile("owner")
    {
    }

    // destructor
    virtual ~FoamOwnerFile()
    {
    }
};


/***************************************************************************
 * Class FoamNeighbourFile writes an OpenFOAM "neighbour" file. This file
 * maps face index to the neighbour cell index, if any. (Boundary faces do
 * not have neighbour cells.)
 ***************************************************************************/
class FoamNeighbourFile : public FoamAddressFile {
public:
    // Default constructor, sets object to "neighbour"
    FoamNeighbourFile() :
        FoamAddressFile("neighbour")
    {
    }

    // destructor
    virtual ~FoamNeighbourFile()
    {
    }
};


/***************************************************************************
 * Base class FoamSetFile writes an OpenFOAM "sets" file. A sets file
 * is a list of object (cell or face) indices.
 ***************************************************************************/
class FoamSetFile : public FoamAddressFile {
public:
    // Constructor, sets location to "sets" directory
    FoamSetFile(const char *cls) :
        FoamAddressFile("", "constant/polyMesh/sets")
    {
        setClass(cls);
    }

    // destructor
    virtual ~FoamSetFile()
    {
    }
};


/***************************************************************************
 * Class FoamCellSetFile writes an OpenFOAM "cellSet" file. A cellSet file
 * contains a list of cell indices for a given set.
 ***************************************************************************/
class FoamCellSetFile : public FoamSetFile {
public:
    // Default constructor sets class to "cellSet"
    FoamCellSetFile() :
        FoamSetFile("cellSet")
    {
    }

    // destructor
    virtual ~FoamCellSetFile()
    {
    }
};


/***************************************************************************
 * Class FoamFaceSetFile writes an OpenFOAM "faceSet" file. A faceSet file
 * contains a list of face indices for a given set.
 ***************************************************************************/
class FoamFaceSetFile : public FoamSetFile {
public:
    // Default constructor, sets class to "faceSet"
    FoamFaceSetFile() :
        FoamSetFile("faceSet")
    {
    }

    // destructor
    virtual ~FoamFaceSetFile()
    {
    }
};

typedef std::map<const char*, FoamCellSetFile*> VcNameToFile;
typedef std::map<FoamCellSetFile*, PWP_UINT32>  FileToCountMap;


/***************************************************************************
 * Base Class FoamZoneFile is used to write various OpenFOAM "zone" files.
 * A zone file contains sets of faces or cells with various attributes.
 * Zone files are used to defined volume and boundary conditions.
 ***************************************************************************/
class FoamZoneFile : public FoamFile {
public:
    // Constructor sets class to "regIOobject" and location to polyMesh
    // directory
    FoamZoneFile(const char *object) :
        FoamFile("regIOobject", object, "constant/polyMesh")
    {
    }

    // destructor
    virtual ~FoamZoneFile()
    {
    }

    // write the address section of the set file with the given name to the
    // zone file
    bool writeSet(const std::string &setName)
    {
        // Parsing logic assumes a set file of format:
        //
        // FoamFile
        // {
        //     version     2.0;
        //     format      ascii;
        //     location    "constant/polyMesh/sets";
        //     class       cellSet;
        //     object      blade;
        // }
        //
        // // * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //
        //
        // 4807  <-- start writing here
        // (
        //  9947  9948  9949  9950  9951  9952  9953  9954  9955  9956
        //  9957  9958  9959  9960  9961  9962  9963  9964  9965  9966
        //  ...
        // 14737 14738 14739 14740 14741 14742 14743 14744 14745 14746
        // 14747 14748 14749 14750 14751 14752 14753 )  <-- end writing here

        bool ret = false;
        if (0 != getNumItems()) {
            fprintf(*this, "\n");
        }

        fprintf(*this, "%s\n", setName.c_str());
        pwpFileWriteStr("{\n", *this);
        // allow subclass to write custom data before label list
        this->writeLabelListPrefix();
        std::string setFileName("sets/");
        setFileName += setName;
        FILE *setFile = pwpFileOpen(setFileName.c_str(), pwpRead | pwpAscii);
        unsigned long labelCnt = 0;

        if (0 != setFile) {
            ret = true;
            // search setFile until we find a line starting with a digit char
            char buf[1024];
            fgets(buf, sizeof(buf), setFile);
            while (!pwpFileEof(setFile)) {
                // sscanf handles possible leading/trailing whitespace
                if (1 == sscanf(buf, " %lu \n", &labelCnt)) {
                    break;
                }
                fgets(buf, sizeof(buf), setFile);
            }
            // write lines until we find one ending with a ')' char
            while (!pwpFileEof(setFile)) {
                fprintf(*this, "  %s", buf);
                if (0 != strrchr(buf, ')')) {
                    break; // last line written - stop
                }
                fgets(buf, sizeof(buf), setFile);
            }
            ret = !pwpFileEof(setFile);
            pwpFileClose(setFile);
        }
        // mark end of label list
        pwpFileWriteStr("  ;\n", *this);
        // allow subclass to write custom data after label list
        this->writeLabelListSuffix(labelCnt);
        // mark end of zone
        pwpFileWriteStr("}\n", *this);
        incrNumItems();
        return ret;
    }

private:
    // write zone label list to file
    virtual void writeLabelListPrefix()
    {
        // assumes object_ is of form "xxxxZones"
        // write as "xxxxZone" (singular)
        fprintf(*this, "  type %8.8s;\n", object());
        // write as "xxxx"
        fprintf(*this, "  %4.4sLabels List<label>\n", object());
    }

    // allow subclass to write information after label list
    virtual void writeLabelListSuffix(unsigned long labelCnt)
    {
        (void)labelCnt; // do nothing
    }
};


/***************************************************************************
 * Class FoamCellZoneFile is used to write an OpenFOAM "cellZones" file.
 * A cell zone contains cells with the same volume condition.
 ***************************************************************************/
class FoamCellZoneFile : public FoamZoneFile {
public:
    // Default constructor sets object to "cellZones"
    FoamCellZoneFile() :
        FoamZoneFile("cellZones")
    {
    }

    // destructor
    virtual ~FoamCellZoneFile()
    {
    }
};


/***************************************************************************
 * Class FoamFaceZoneFile is used to write an OpenFOAM "faceZones" file.
 * A face zone contains faces with the same boundary condition.
 ***************************************************************************/
class FoamFaceZoneFile : public FoamZoneFile {
public:
    // Default constructor sets object to "faceZones"
    FoamFaceZoneFile() :
        FoamZoneFile("faceZones")
    {
    }

    // destructor
    virtual ~FoamFaceZoneFile()
    {
    }

private:
    // callback from parent class after face zones have been written
    virtual void writeLabelListSuffix(unsigned long labelCnt)
    {
        // pointwise faces are never flipped
        fprintf(*this, "  flipMap List<bool> %lu{0};\n",
            (unsigned long)labelCnt);
    }
};


/***************************************************************************
 * Class BcStat represents a single face range with an OpenFOAM boundary
 * condition.
 ***************************************************************************/
class BcStat {
public:
    // Default constructor
    BcStat() :
        name_(),
        type_(),
        nFaces_(0),
        startFace_(0)
    {
    }

    // Copy constructor, needed to store in value array
    BcStat(const BcStat& rhs) :
        name_(rhs.name_),
        type_(rhs.type_),
        nFaces_(rhs.nFaces_),
        startFace_(rhs.startFace_)
    {
    }

    // Destructor
    ~BcStat()
    {
    }

    // Assignment operator
    BcStat& operator=(const BcStat& rhs)
    {
        name_ = rhs.name_;
        type_ = rhs.type_;
        nFaces_ = rhs.nFaces_;
        startFace_ = rhs.startFace_;
        return *this;
    }

    std::string name_;      // boundary condition name
    std::string type_;      // boundary condition type
    PWP_UINT32  nFaces_;    // number of faces in this range
    PWP_UINT32  startFace_; // first face number in this range
};

// Value array of BcStat
typedef std::vector<BcStat> BcStats;


/***************************************************************************
 * Class FoamBoundaryFile is used to write an OpenFOAM "boundary" file.
 * The boundary file contains the face range to which each boundary
 * condition is applied.
 ***************************************************************************/
class FoamBoundaryFile : public FoamFile {
public:
    // Default constructor sets class name and file name
    FoamBoundaryFile() :
        FoamFile("polyBoundaryMesh", "boundary")
    {
    }

    // destructor
    ~FoamBoundaryFile()
    {
    }

    /*
       bndry file contains entries of the form

        <bcName:string>
        {
            type <bcPhyType:string>;
            nFaces <numFacesInBc:integer>;
            startFace <firstBcFaceIndex:integer>;
        }
        ...repeat...
    */

    // write the boundary condition entries
    void writeBoundaries(const BcStats &bcStats)
    {
        BcStats::const_iterator it = bcStats.begin();
        for (; it != bcStats.end(); ++it) {
            fprintf(*this, "    %s\n", it->name_.c_str());
            fprintf(*this, "    {\n");
            fprintf(*this, "        type %s;\n", it->type_.c_str());
            fprintf(*this, "        nFaces %lu;\n", (unsigned long)it->nFaces_);
            fprintf(*this, "        startFace %lu;\n",
                (unsigned long)it->startFace_);
            fprintf(*this, "    }\n");
            incrNumItems();
        }
    }
};


/***************************************************************************
 * Helper class VcSetFiles is used to write OpenFOAM face and cell set files.
 ***************************************************************************/
class VcSetFiles {
public:
    // Default constructor
    VcSetFiles(const PWGM_CONDDATA &vc, StringSet &usedNames) :
        internalFaceSetFile_(0),
        boundaryFaceSetFile_(0),
        cellSetFile_(0)
    {
        const char *sfxIFaces = "-interiorFaces";
        const char *sfxBFaces = "-boundaryFaces";
        const char *sfxFaces = "-faces";
        // make "./sets" the current directory
        pwpCwdPush("sets");
        // allocate sets per vc.tid
        if (VcIBFaces == (VcIBFaces & vc.tid)) {
            // interior and boundary faces go to different set files
            internalFaceSetFile_ = new FoamFaceSetFile;
            internalFaceSetFile_->open(uniqueSafeFileName(vc.name, usedNames,
                sfxIFaces));
            boundaryFaceSetFile_ = new FoamFaceSetFile;
            boundaryFaceSetFile_->open(uniqueSafeFileName(vc.name, usedNames,
                sfxBFaces));
        }
        else if (VcFaces == (VcFaces & vc.tid)) {
            // interior and boundary faces go to same set file
            internalFaceSetFile_ = new FoamFaceSetFile;
            internalFaceSetFile_->open(uniqueSafeFileName(vc.name, usedNames,
                sfxFaces));
            boundaryFaceSetFile_ = internalFaceSetFile_;
        }
        else if (VcIFaces & vc.tid) {
            // interior face set only
            internalFaceSetFile_ = new FoamFaceSetFile;
            internalFaceSetFile_->open(uniqueSafeFileName(vc.name, usedNames,
                sfxIFaces));
        }
        else if (VcBFaces & vc.tid) {
            // boundary face set only
            boundaryFaceSetFile_ = new FoamFaceSetFile;
            boundaryFaceSetFile_->open(uniqueSafeFileName(vc.name, usedNames,
                sfxBFaces));
        }

        if (VcCells & vc.tid) {
            // build cell set
            cellSetFile_ = new FoamCellSetFile;
            cellSetFile_->open(uniqueSafeFileName(vc.name, usedNames,
                "-cells"));
        }
        pwpCwdPop();
    }

    // Destructor
    ~VcSetFiles()
    {
        if (internalFaceSetFile_ == boundaryFaceSetFile_) {
            boundaryFaceSetFile_ = 0; // don't delete twice
        }

        delete internalFaceSetFile_;
        internalFaceSetFile_ = 0;

        delete boundaryFaceSetFile_;
        boundaryFaceSetFile_ = 0;

        delete cellSetFile_;
        cellSetFile_ = 0;
    }

    // write a boundary, connection or interior face
    void addFace(PWGM_ENUM_FACETYPE type, PWP_UINT32 face)
    {
        switch (type) {
        case PWGM_FACETYPE_BOUNDARY:
        case PWGM_FACETYPE_CONNECTION:
            if (0 != boundaryFaceSetFile_) {
                boundaryFaceSetFile_->writeAddress(face);
            }
            break;
        case PWGM_FACETYPE_INTERIOR:
            if (0 != internalFaceSetFile_) {
                internalFaceSetFile_->writeAddress(face);
            }
            break;
        default:
            break;
        }
    }

    bool hasCellSetFile() const
    {
        return 0 != cellSetFile_;
    }

    // write a cell index
    void pushCell(PWP_UINT32 cell)
    {
        if (0 != cellSetFile_) {
            cellSetFile_->writeAddress(cell);
        }
    }

    // add face set file(s) contents to faceZones file
    void addFaceSetsToZonesFile(FoamFaceZoneFile &zoneFile)
    {
        if (0 != internalFaceSetFile_) {
            zoneFile.writeSet(internalFaceSetFile_->object());
        }
        if ((0 != boundaryFaceSetFile_) &&
                (internalFaceSetFile_ != boundaryFaceSetFile_)) {
            zoneFile.writeSet(boundaryFaceSetFile_->object());
        }
    }

    // add zone set file contents to cellZones file
    void addCellSetToZonesFile(FoamCellZoneFile &zoneFile)
    {
        if (0 != cellSetFile_) {
            zoneFile.writeSet(cellSetFile_->object());
        }
    }

    // close face set files
    void finalizeFaceSets()
    {
        if (0 != internalFaceSetFile_) {
            internalFaceSetFile_->close();
        }
        if ((0 != boundaryFaceSetFile_) &&
                (internalFaceSetFile_ != boundaryFaceSetFile_)) {
            boundaryFaceSetFile_->close();
        }
    }

    // delete set file with given name
    static void deleteSetFile(const char *name)
    {
        std::string setFileName("sets/");
        setFileName += name;
        pwpFileDelete(setFileName.c_str());
    }

    // delete face set file(s)
    void deleteFaceSetFiles()
    {
        finalizeFaceSets();
        if (0 != internalFaceSetFile_) {
            deleteSetFile(internalFaceSetFile_->object());
        }
        if ((0 != boundaryFaceSetFile_) &&
                (internalFaceSetFile_ != boundaryFaceSetFile_)) {
            deleteSetFile(boundaryFaceSetFile_->object());
        }
    }

    // close cell set file
    void finalizeCellSet()
    {
        if (0 != cellSetFile_) {
            cellSetFile_->close();
        }
    }

    // delete cell set file
    void deleteCellSetFiles()
    {
        finalizeCellSet();
        if (0 != cellSetFile_) {
            deleteSetFile(cellSetFile_->object());
        }
    }

private:
    // Hidden copy constructor
    VcSetFiles(const VcSetFiles & vcf);

    // Hidden assignment operator
    VcSetFiles & operator=(const VcSetFiles & rhs);

    FoamFaceSetFile *internalFaceSetFile_;  // interior face set file or null
    FoamFaceSetFile *boundaryFaceSetFile_;  // boundary face set file or null
    FoamCellSetFile *cellSetFile_;          // cell set file or null
};

// Domains are agglomerated by the core. Only need a simple, 1-to-1 mapping from
// the non-inflated domain's id to the face set file.
typedef std::map<PWP_UINT32, FoamFaceSetFile>   DomIdFaceSetFileMap;
typedef std::vector<VcSetFiles *>               VcSetFilesVec;
typedef std::vector<std::string *>              BcSetFileNames;


/***************************************************************************
 * Class OpenFoamPlugin is the main workhorse for this CAE plugin.
 ***************************************************************************/
class OpenFoamPlugin {
public:
    // Constructor
    OpenFoamPlugin(CAEP_RTITEM *pRti, PWGM_HGRIDMODEL model,
            const CAEP_WRITEINFO * pWriteInfo) :
        rti_(*pRti),
        model_(model),
        writeInfo_(*pWriteInfo),
        faces_(CAEPU_RT_DIM_2D(&rti_), PwModVertexCount(model_)),
        owner_(),
        neighbour_(),
        bcStats_(),
        usedFileNames_(),
        exportFaceSets_(false),
        exportFaceZones_(false),
        exportCellSets_(false),
        exportCellZones_(true),
        sideBcMode_(BcModeSingle),
        totElemCnt_(0),
        blkIdOffset_(),
        vcSetFiles_(),
        bcSetFiles_(),
        numFaces_(0),
        curInflId_(PWP_UINT32_MAX),
        nonInflBCSetFiles_(),
        orientation_(UnknownZ),
        planeZ_(0.0),
        totalEdgeLength_(0.0),
        doThicknessCalc_(false),
        thickness_(ThicknessDef),
        doFaceSets_(false),
        setsDirWasCreated_(false)
    {
        if (!PwModGetAttributeREAL(model_, Thickness, &thickness_)) {
            thickness_ = ThicknessDef;
        }
        doThicknessCalc_ = CAEPU_RT_DIM_2D(&rti_) && (0.0 == thickness_);
    }


    // destructor
    ~OpenFoamPlugin()
    {
        VcSetFilesVec::iterator it = vcSetFiles_.begin();
        for (; it != vcSetFiles_.end(); ++it) {
            delete (*it);
        }
        vcSetFiles_.clear();
    }


    // main entry point for CAE export
    PWP_BOOL run()
    {
        if (CAEPU_RT_DIM_2D(&rti_)) {
            PwModAppendEnumElementOrder(model_, PWGM_ELEMORDER_VC);
            bool isZPlanar;
            bool isConsistent;
            GridValidator::getGridProperties(model_, isZPlanar, planeZ_,
                orientation_, isConsistent);
            if (!isZPlanar) {
                caeuSendErrorMsg(&rti_, "The grid is not Z-planar.", 0);
                return PWP_FALSE;
            } else if (!isConsistent) {
                caeuSendErrorMsg(&rti_, "The grid has inconsistent normals.", 0);
                return PWP_FALSE;
            }
        }
        // None|SetsOnly|ZonesOnly|SetsAndZones
        //    0|       1|        2|           3

        PWP_UINT cellExport = 0;
        PwModGetAttributeUINT(model_, CellExport, &cellExport);
        exportCellSets_  = (0 != (cellExport & 1));
        exportCellZones_ = (0 != (cellExport & 2));

        PWP_UINT faceExport = 0;
        PwModGetAttributeUINT(model_, FaceExport, &faceExport);
        exportFaceSets_  = (0 != (faceExport & 1));
        exportFaceZones_ = (0 != (faceExport & 2));

        PWP_UINT sideBCExport = BcModeSingle;
        PwModGetAttributeUINT(model_, SideBCExport, &sideBCExport);
        sideBcMode_ = static_cast<SideBcMode>(sideBCExport);

        PWP_BOOL ret = PWP_FALSE;
        PWP_UINT32 majorSteps = 3 + (exportCellZones_ ? 1 : 0);

        if (!caeuProgressInit(&rti_, majorSteps)) {
        }
        else if (needSetsDir() && !createSetsDir()) {
            caeuSendErrorMsg(&rti_, "Could not create 'sets' directory.", 0);
        }
        else if (needSetsDir() && !prepareVcSetFiles()) {
            caeuSendErrorMsg(&rti_, "Could prepare VC set files.", 0);
        }
        else if (!processFaces()) {
            caeuSendErrorMsg(&rti_, "Could not write face files.", 0);
        }
        else if (!processPoints()) {
            caeuSendErrorMsg(&rti_, "Could not write points file.", 0);
        }
        else if (!processCells()) {
            caeuSendErrorMsg(&rti_, "Could not write cell sets.", 0);
        }
        else {
            ret = PWP_TRUE;
        }

        if (setsDirWasCreated_) {
            // Attempt to delete. Will fail if dir contains any files.
            pwpDeleteDir("sets");
        }

        caeuProgressEnd(&rti_, ret);
        return ret;
    }


private:

    // Accumulate boundary face group information. Data is written to
    // "boundary" file at end of export. This method assumes that the
    // faces are being streamed in boundary group order.
    void pushBcFace(const PWGM_FACESTREAM_DATA &data)
    {
        PWGM_CONDDATA condData;
        if (PwDomCondition(data.owner.domain, &condData)) {
            pushBcFace(condData, data.face);
        }
    }


    // Accumulate boundary face group information. Data is written to
    // "boundary" file at end of export. This method assumes that the
    // faces are being streamed in boundary group order.
    void pushBcFace(const PWGM_CONDDATA &condData, PWP_UINT32 faceId)
    {
            if ((0 == bcStats_.size()) ||
                    (0 != bcStats_.back().name_.compare(condData.name))) {
                // we are starting a new BC group
                BcStat stats;
                stats.name_ = condData.name;
                stats.type_ = condData.type;
                stats.nFaces_ = 1;
            stats.startFace_ = faceId;
                bcStats_.push_back(stats);
            }
            else {
                // same BC group, update face count
                ++bcStats_.back().nFaces_;
            }
        }


    // Return whether the "sets" directory is needed during this export
    bool needSetsDir() const {
        return exportCellSets_ || exportCellZones_ || exportFaceSets_ ||
            exportFaceZones_;
    }


    // Return whether any cell sets or face sets are being exported
    bool exportingAnySets() const {
        return exportCellSets_ || exportFaceSets_;
    }


    // Return whether face sets are needed for this export
    bool faceSetsNeeded() {
        return (exportFaceZones_ || exportFaceSets_) && !vcSetFiles_.empty();
    }


    // Return whether cell sets are needed for this export
    bool cellSetsNeeded() {
        return (exportCellSets_ || exportCellZones_) && !vcSetFiles_.empty();
    }


    // Obtain and write all the global vertices in the exported mesh system
    bool processPoints()
    {
        PWP_UINT prec;
        if (!PwModGetAttributeUINT(model_, PointPrecision, &prec)) {
            prec = PointPrecisionDef;
        }
        bool ret = false;
        const bool is2D = (0 != CAEPU_RT_DIM_2D(&rti_));
        const PWP_UINT32 numPts = PwModVertexCount(model_);
        FoamPointFile points(prec);
        if (is2D && (UnknownZ == orientation_)) {
            // not good
        }
        else if (progressBeginStep(numPts * (is2D ? 2 : 1)) && points.open()) {
            ret = true;
            for (PWP_UINT32 ii = 0; ii < numPts; ++ii) {
                points.writeVertex(PwModEnumVertices(model_, ii));
                if (!progressIncr()) {
                    ret = false;
                    break;
                }
            }
            if (ret && is2D) {
                // Create a second set of points for a single cell thick
                // extrusion. Thickened points are on the newZ plane.
                const PWGM_XYZVAL newZ = planeZ_ + (orientation_ * thickness_);
                for (PWP_UINT32 ii = 0; ii < numPts; ++ii) {
                    points.writeVertex(PwModEnumVertices(model_, ii), newZ);
                    if (!progressIncr()) {
                        ret = false;
                        break;
                    }
                }
            }
        }
        progressEndStep();
        return ret;
    }


    // Callback from plugin API when face streaming is about to begin
    static PWP_UINT32 streamBegin(PWGM_BEGINSTREAM_DATA *data)
    {
        if (0 == data->userData) {
            return PWP_FALSE;
        }
        OpenFoamPlugin &ofp = *((OpenFoamPlugin*)data->userData);
        ofp.numFaces_ = data->totalNumFaces;
        ofp.doFaceSets_ = ofp.faceSetsNeeded();
        ofp.totalEdgeLength_ = 0.0;

        // Open the faces, owner, and neighbour export files. They are all
        // written in parallel as faces stream into faceStreamCB().
        return ofp.progressBeginStep(data->totalNumFaces) &&
               ofp.faces_.open() && ofp.owner_.open() && ofp.neighbour_.open();
    }


    // Callback from plugin API to write a cell face
    static PWP_UINT32 streamFace(PWGM_FACESTREAM_DATA *data)
    {
        if (0 == data->userData) {
            return PWP_FALSE;
        }
        OpenFoamPlugin &ofp = *((OpenFoamPlugin*)data->userData);

        // export the nth face's connectivity
        ofp.faces_.writeFace(data->elemData);

        // export the cell id that owns the nth face
        ofp.owner_.writeAddress(data->owner.cellIndex);

        if (PWGM_FACETYPE_BOUNDARY == data->type) {
            // push face into boundary accumulator.
            ofp.pushBcFace(*data);
        }
        else { // PWGM_FACETYPE_INTERIOR or PWGM_FACETYPE_CONNECTION
            // export the cell id that is on the other side of the nth face
            // (the owner's neighbour).
            ofp.neighbour_.writeAddress(data->neighborCellIndex);
        }

        if ((ofp.exportFaceSets_ || ofp.exportFaceZones_) &&
            (PWGM_FACETYPE_CONNECTION == data->type) &&
            PWGM_HDOMAIN_ISVALID(data->owner.domain)) {
            // This face belongs to a non-inflatable face set.
            const PWP_UINT32 id = PWGM_HDOMAIN_ID(data->owner.domain);
            DomIdFaceSetFileMap &fsFiles = ofp.nonInflBCSetFiles_;
            FoamFaceSetFile *fsf = 0;
            DomIdFaceSetFileMap::iterator nit = fsFiles.find(id);
            if (fsFiles.end() != nit) {
                // face set file for id already exists - use it
                fsf = &(nit->second);
            }
            else if (!ofp.createSetsDir()) {
                fsf = 0; // BAD
            }
            else if (0 == pwpCwdPush("sets")) {
                // "./sets" is now the cwd. Create new face set file for id
                DomIdFaceSetFileMap::value_type val(id, FoamFaceSetFile());
                PWGM_CONDDATA condData;
                if (!PwDomCondition(data->owner.domain, &condData)) {
                    fsf = 0; // BAD
                }
                else if (fsFiles.end() == (nit = fsFiles.insert(val).first)) {
                    fsf = 0; // BAD
                }
                else if (!nit->second.open(uniqueSafeFileName(condData.name,
                    ofp.usedFileNames_))) {
                    fsf = 0; // BAD
                }
                else {
                    fsf = &(nit->second);
                }
                pwpCwdPop();
            }
            if (0 != fsf) {
                // add face to appropriate non-inflatable face set.
                fsf->writeAddress(data->face);
            }
            else {
                caeuSendErrorMsg(&ofp.rti_, "Could not create faceSet.", 0);
                return 0;
            }
        }

        if (ofp.doFaceSets_) {
            ofp.addFaceToSet(*data);
        }

        if (ofp.doThicknessCalc_) {
            // Compute the edge's length and add it to the total.
            PWGM_XYZVAL xyz0[3];
            PWGM_XYZVAL xyz1[3];
            if (getXYZ(xyz0, data->elemData.vert[0]) &&
                    getXYZ(xyz1, data->elemData.vert[1])) {
                ofp.totalEdgeLength_ += calcLength(xyz0, xyz1);
            }
        }

        return ofp.progressIncr();
    }


    void offsetVertices(PWP_UINT32 offset, PWGM_ELEMDATA &elemData)
    {
        elemData.index[0] += offset;
        elemData.index[1] += offset;
        elemData.index[2] += offset;
        switch (elemData.type){
        case PWGM_ELEMTYPE_QUAD:
            elemData.index[3] += offset;
            std::swap(elemData.index[0], elemData.index[3]);
            std::swap(elemData.index[1], elemData.index[2]);
            break;
        case PWGM_ELEMTYPE_TRI:
            std::swap(elemData.index[0], elemData.index[2]);
            break;
        default:
            break;
        }
    }


    void writeFaces()
    {
        PWP_UINT32 faceOffset = numFaces_;
        PWP_UINT32 vertOffset = 0;
        // write original tri/quads as boundary elements of the extruded grid
        writeFaces(faceOffset, vertOffset);
        // write offset tri/quads as boundary elements of the extruded grid
        faceOffset += PwModEnumElementCount(model_, 0);
        vertOffset += PwModVertexCount(model_);
        writeFaces(faceOffset, vertOffset);
    }


    void getElementCond(const PWP_UINT32 blkId, PWGM_CONDDATA &cond,
        const bool isOffset, PWP_UINT32 &prevBlkId)
    {
        static const char *EmptyType = "empty";
        static const PWP_UINT32 EmptyTid = 103;
        if (blkId != prevBlkId) {
            prevBlkId = blkId;
            PWGM_HBLOCK hBlk;
            PWGM_HBLOCK_SET(hBlk, model_, blkId);
            // Use the 2D block's VC as base for the extruded side BCs
            if (!PwBlkCondition(hBlk, &cond)) {
                cond = UnspecifiedCond;
            }
            switch (sideBcMode_) {
            case BcModeUnspecified:
                cond = UnspecifiedCond;
                break;
            case BcModeBaseTop:
                cond.name = (isOffset ? "Top" : "Base");
                cond.type = EmptyType;
                cond.tid = EmptyTid;
                break;
            case BcModeMultiple: {
                static std::string bcName;
                bcName = cond.name;
                bcName += (isOffset ? "-top" : "-base");
                // cond.name ptr only valid until next call to getElementCond()
                cond.name = bcName.c_str();
                cond.type = EmptyType;
                cond.tid = EmptyTid;
                break; }
            case BcModeSingle:
            default:
                cond.name = "BaseAndTop";
                cond.type = EmptyType;
                cond.tid = EmptyTid;
                break;
            }
        }
    }


    void writeFaces(const PWP_UINT32 faceOffset, const PWP_UINT32 vertOffset)
    {
        const bool isOffset = (0 < vertOffset);
        PWGM_CONDDATA bc = { 0 };
        PWP_UINT32 prevBlkId = PWP_UINT32_MAX;
        PWGM_ENUMELEMDATA eData = {};
        PWP_UINT32 index = 0;
        PWGM_HELEMENT hElem = PwModEnumElements(model_, index);
        while (PwElemDataModEnum(hElem, &eData)) {
            if (isOffset) {
                // This element is an offset of an original element
                offsetVertices(vertOffset, eData.elemData);
            }
            // Add the face
            faces_.writeFace(eData.elemData);
            // This 2D tri/quad element is extruded to a 3D element prism/hex
            // element with the same id as the 2D element. This cell id is the
            // face's owner.
            owner_.writeAddress(PWGM_HELEMENT_ID(hElem));
            // getElementCond() will update bc when blkId changes
            const PWP_UINT32 blkId = PWGM_HELEMENT_PID(eData.hBlkElement);
            getElementCond(blkId, bc, isOffset, prevBlkId);
            // The face id follows cell id with an offset
            const PWP_UINT32 faceId = PWGM_HELEMENT_ID(hElem) + faceOffset;
            pushBcFace(bc, faceId);
            if (doFaceSets_) {
                // Add this boundary element (tri/quad) to the face set of the
                // volume it touches.
                addBndryFaceToSet(blkId, faceId);
            }
            hElem = PwModEnumElements(model_, ++index);
        }
    }


    // Callback from plugin API when face streaming has completed
    static PWP_UINT32 streamEnd(PWGM_ENDSTREAM_DATA *data)
    {
        if (0 == data->userData) {
            return PWP_FALSE;
        }
        OpenFoamPlugin &ofp = *((OpenFoamPlugin*)data->userData);
        DomIdFaceSetFileMap::iterator nit;
        nit = ofp.nonInflBCSetFiles_.begin();
        for (; nit != ofp.nonInflBCSetFiles_.end(); ++nit) {
            nit->second.close();
        }
        if (CAEPU_RT_DIM_2D(&ofp.rti_)) {
            ofp.writeFaces();
        }
        FoamBoundaryFile boundary;
        if (boundary.open()) {
            // Flush the accumulated BC information to the boundary file.
            boundary.writeBoundaries(ofp.bcStats_);
        }
        if (ofp.doThicknessCalc_ && (0 < ofp.numFaces_)) {
            // Set thickness_ to the 2D grid's average edge length. Remember,
            // for 2D grids, ofp.numFaces_ is the number of 2D cell edges that
            // were streamed.
            ofp.thickness_ = ofp.totalEdgeLength_ / ofp.numFaces_;
            std::ostringstream oss;
            oss << "2D Thickness set to " << ofp.thickness_;
            // Let user know!
            caeuSendInfoMsg(&ofp.rti_, oss.str().c_str(), 0);
        }
        return ofp.progressEndStep();
    }


    // create the sets directory
    bool createSetsDir()
    {
        bool ret = true;
        if (0 == pwpCreateDir("sets")) {
            // A new dir was created - all is OK. Only set setsDirWasCreated_ to
            // true if dir was actually created so that createSetsDir() can be
            // called multiple times.
            setsDirWasCreated_ = true;
        }
        else if (EEXIST == errno) {
            // If it already existed, all is OK
            ret = true;
        }
        else {
            // Could not create dir
            ret = false;
        }
        // returns true if sets dir is available
        return ret;
    }


    // process the cell faces using the face streaming plugin API
    bool processFaces()
    {
        // stream the faces
        bool ret = (0 != PwModStreamFaces(
            model_,                        // the API mesh model handle
            PWGM_FACEORDER_BCGROUPSLAST,   // face order, BC faces last
            streamBegin,                   // callback for start of streaming
            streamFace,                    // callback for new face
            streamEnd,                     // callback at end of streaming
            (void *)this));                // user data, passed to stream calls

        // write face sets accumulated during streaming
        finalizeFaceSets();

        // construct and write face zones
        if (ret && exportFaceZones_) {
            writeFaceZonesFile();
        }

        // clean up set files based on export option
        if (!exportFaceSets_) {
            // dont need face set files anymore, delete them
            VcSetFilesVec::iterator it = vcSetFiles_.begin();
            for (; it != vcSetFiles_.end(); ++it) {
                (*it)->deleteFaceSetFiles();
            }
            DomIdFaceSetFileMap::const_iterator nit;
            nit = nonInflBCSetFiles_.begin();
            for (; nit != nonInflBCSetFiles_.end(); ++nit) {
                VcSetFiles::deleteSetFile(nit->second.object());
            }
        }
        return ret;
    }


    // process the cell sets
    bool processCells()
    {
        bool ret = true;
        if (!cellSetsNeeded()) {
            // do nothing
        }
        else if (exportCellZones_) {
            // need cell set files to build the cellZones file
            ret = writeCellSetFiles();
            if (ret) {
                writeCellZonesFile();
                if (!exportCellSets_) {
                    // dont need cell set file anymore, delete them
                    VcSetFilesVec::iterator it = vcSetFiles_.begin();
                    for (; it != vcSetFiles_.end(); ++it) {
                        (*it)->deleteCellSetFiles();
                    }
                }
            }
        }
        else if (exportCellSets_) {
            ret = writeCellSetFiles();
        }
        return ret;
    }


    // write cell set files
    bool writeCellSetFiles()
    {
        bool ret = false;
        if (!progressBeginStep(totElemCnt_)) {
            // aborted
        }
        else if (0 != pwpCwdPush("sets")) { // make "./sets" the cwd
        }
        else if (vcSetFiles_.empty()) {
            ret = true; // no VCs assigned?
        }
        else {
            ret = true;
            PWP_UINT32 curBlkId = PWP_UINT32_MAX;
            VcSetFiles *vcFiles = 0;
            PWP_UINT32 cellId = 0;
            PWGM_HELEMENT elem = PwModEnumElements(model_, cellId);
            PWGM_ENUMELEMDATA elemData;

            // loop over all cells in the global mesh model
            while (PwElemDataModEnum(elem, &elemData)) {
                PWP_UINT32 blkId = PWGM_HELEMENT_PID(elemData.hBlkElement);
                // when block ID changes, switch current VC file
                if (curBlkId != blkId) {
                    curBlkId = blkId;
                    PWP_UINT32 offset = 0;
                    if (blkIdOffset_.end() != blkIdOffset_.find(blkId)) {
                        // blkId found in the offset map.
                        offset = blkIdOffset_[blkId];
                        vcFiles = vcSetFiles_.at(offset);
                    }
                    else {
                        // blkId NOT found in the offset map. It probably has
                        // the Unspecified VC applied to it.
                        vcFiles = 0;
                    }
                    // If the block's vcFiles ptr was NOT found above or block
                    // does not want a cell set file written, skip all cells in
                    // this block!
                    if (!(vcFiles && vcFiles->hasCellSetFile())) {
                        PWGM_HBLOCK block = PwModEnumBlocks(model_, blkId);
                        // skip past all cells in this block. -1 because of
                        // pre incr on cellId below.
                        cellId += PwBlkElementCount(block, 0) - 1;
                        vcFiles = 0;
                    }
                }

                // add the cell to the current VC file
                if (vcFiles) {
                    vcFiles->pushCell(cellId);
                    if (!progressIncr()) {
                        ret = false;
                        break;
                    }
                }
                elem = PwModEnumElements(model_, ++cellId);
            }
            pwpCwdPop();
        }
        progressEndStep();
        return ret;
    }


    // write the cell zones file
    void writeCellZonesFile()
    {
        finalizeCellSets();
        FoamCellZoneFile cellZones;
        if (!progressBeginStep((PWP_UINT32)vcSetFiles_.size())) {
            // aborted
        }
        else if (cellZones.open()) {
            VcSetFilesVec::iterator it = vcSetFiles_.begin();
            for (; it != vcSetFiles_.end(); ++it) {
                (*it)->addCellSetToZonesFile(cellZones);
                if (!progressIncr()) {
                    break;
                }
            }
        }
        progressEndStep();
    }


    // build VC sets
    bool prepareVcSetFiles()
    {
        // worst case scenario is numBlocks == numUniqueVCs
        vcSetFiles_.reserve(PwModBlockCount(model_));

        // For each unique VC name:
        //  Create a VcSetFiles object.
        //  Make a blkIdOffset_ mapping.
        //  Keep a tally of the number of cells.
        //  Track the max index value.
        PWP_UINT32 blkId = 0;
        CharPtrUInt32Map::iterator iter;
        PWGM_CONDDATA vc;
        PWP_UINT32 offset = 0;
        // Becasue blocks are not agglomerated, there is a many-to-one
        // relationship between blocks and VC set files (multiple blocks can map
        // to one VC set file). Use vcNameOffset to maintain the 1-to-1
        // VC-to-vcSetFile mapping.
        CharPtrUInt32Map vcNameOffset; // vc name to a vcSetFiles_ index
        PWGM_HBLOCK block = PwModEnumBlocks(model_, blkId);
        while (PwBlkCondition(block, &vc)) {
            // Check if vc mapping exists
            iter = vcNameOffset.find(vc.name);
            if (vcNameOffset.end() == iter) {
                // first time for this VC name - allocate a new file
                offset = (PWP_UINT32)vcSetFiles_.size();
                vcNameOffset[vc.name] = offset;
                VcSetFiles *vcset = new VcSetFiles(vc, usedFileNames_);
                vcSetFiles_.push_back(vcset);
            }
            else {
                // VC already mapped - use existing file
                offset = iter->second;
            }
            blkIdOffset_[blkId] = offset;
            totElemCnt_ += PwBlkElementCount(block, 0);
            block = PwModEnumBlocks(model_, ++blkId); // next block
        }
        return true;
    }


    // Change face type from connection to interior when owner and neighbor
    // cells (which come from different blocks) but have the same volume
    // condition. When VC block agglomeration is supported, this method won't
    // be needed.
    PWGM_ENUM_FACETYPE adjustFaceType(const PWGM_FACESTREAM_DATA &data)
    {
        PWGM_ENUM_FACETYPE ret = data.type;
        PWGM_ENUMELEMDATA eData;
        if ((PWGM_FACETYPE_CONNECTION == ret) && PwElemDataModEnum(
              PwModEnumElements(data.model, data.neighborCellIndex), &eData)) {
            // get blk id of neighbor cell
            PWP_UINT32 ownerBlkId = PWGM_HBLOCK_ID(data.owner.block);
            PWP_UINT32 neighborBlkId = PWGM_HELEMENT_PID(eData.hBlkElement);
            PWGM_CONDDATA vcOwner;
            PWGM_CONDDATA vcNeighbor;
            PWGM_HBLOCK blkOwner = PwModEnumBlocks(model_, ownerBlkId);
            PWGM_HBLOCK blkNeighbor = PwModEnumBlocks(model_, neighborBlkId);
            if (PwBlkCondition(blkOwner, &vcOwner) &&
                    PwBlkCondition(blkNeighbor, &vcNeighbor) &&
                    (vcOwner.name == vcNeighbor.name)) {
                ret = PWGM_FACETYPE_INTERIOR;
            }
        }
        return ret;
    }


    // store a cell face during face streaming
    void addFaceToSet(const PWGM_FACESTREAM_DATA &data)
    {
        PWGM_ENUM_FACETYPE faceType = adjustFaceType(data);
        addFaceToSet(PWGM_HBLOCK_ID(data.owner.block), faceType, data.face);
        // A connection face has different VCs on either side.
        // Must also push face to neighbor's VcSetFiles
        PWGM_ENUMELEMDATA eData;
        if ((PWGM_FACETYPE_CONNECTION == faceType) && PwElemDataModEnum(
              PwModEnumElements(data.model, data.neighborCellIndex), &eData)) {
            PWP_UINT32 neighborBlkId = PWGM_HELEMENT_PID(eData.hBlkElement);
            PWP_UINT32 neighborOffset = blkIdOffset_[neighborBlkId];
            VcSetFiles *neighborVcFiles = vcSetFiles_.at(neighborOffset);
            neighborVcFiles->addFace(faceType, data.face);
        }
    }


    void addFaceToSet(const PWP_UINT32 blkId, PWGM_ENUM_FACETYPE faceType, 
        PWP_UINT32 face)
    {
        PWP_UINT32 offset = blkIdOffset_[blkId];
        VcSetFiles *vcFiles = vcSetFiles_.at(offset);
        vcFiles->addFace(faceType, face);
    }


    void addBndryFaceToSet(const PWP_UINT32 blkId, PWP_UINT32 face)
    {
        addFaceToSet(blkId, PWGM_FACETYPE_BOUNDARY, face);
    }


    // write the face zones to the face sets files
    void writeFaceZonesFile()
    {
        finalizeFaceSets();
        const PWP_UINT32 stepCnt = (PWP_UINT32)(vcSetFiles_.size() +
            nonInflBCSetFiles_.size());
        FoamFaceZoneFile faceZones;
        if (progressBeginStep(stepCnt) && faceZones.open()) {
            VcSetFilesVec::iterator it = vcSetFiles_.begin();
            for (; it != vcSetFiles_.end(); ++it) {
                (*it)->addFaceSetsToZonesFile(faceZones);
                if (!progressIncr()) {
                    break;
                }
            }
            DomIdFaceSetFileMap::const_iterator nit;
            nit = nonInflBCSetFiles_.begin();
            for (; nit != nonInflBCSetFiles_.end(); ++nit) {
                faceZones.writeSet(nit->second.object());
                if (!progressIncr()) {
                    break;
                }
            }
        }
        progressEndStep();
    }


    // close the face sets files
    void finalizeFaceSets()
    {
        VcSetFilesVec::iterator it = vcSetFiles_.begin();
        for (; it != vcSetFiles_.end(); ++it) {
            (*it)->finalizeFaceSets();
        }
    }


    // close the cell sets file
    void finalizeCellSets()
    {
        VcSetFilesVec::iterator it = vcSetFiles_.begin();
        for (; it != vcSetFiles_.end(); ++it) {
            (*it)->finalizeCellSet();
        }
    }


    // plugin API progress, initialize sequence of steps
    bool progressBeginStep(PWP_UINT32 steps)
    {
        return 0 != caeuProgressBeginStep(&rti_, steps);
    }


    // plugin API progress, advance to next progress step
    bool progressIncr()
    {
        return 0 != caeuProgressIncr(&rti_);
    }


    // plugin API progress, finish sequence of steps
    bool progressEndStep()
    {
        return 0 != caeuProgressEndStep(&rti_);
    }


    // hidden assignment operator
    OpenFoamPlugin& operator=(OpenFoamPlugin &)
    {
        return *this;
    }


    static bool isCellVc(const PWGM_CONDDATA &vc)
    {
        return 0 != (vc.tid & VcCells);
    }


    static bool isFaceVc(const PWGM_CONDDATA &vc)
    {
        return 0 != (vc.tid & VcFaces);
    }


    static bool isUnspecifiedVc(const PWGM_CONDDATA &vc)
    {
        return 0 == vc.tid;
    }


private:

    CAEP_RTITEM          &rti_;              // ref to runtimeWrite *pRti
    PWGM_HGRIDMODEL      model_;             // same as runtimeWrite model
    const CAEP_WRITEINFO &writeInfo_;        // ref to runtimeWrite *pWriteInfo
    FoamFacesFile        faces_;             // The mesh "faces" file
    FoamOwnerFile        owner_;             // The mesh cell "owner" file
    FoamNeighbourFile    neighbour_;         // The mesh cell "neighbour" file
    BcStats              bcStats_;           // cached BC stat data
    StringSet            usedFileNames_;     // set of used VC set file names
    bool                 exportFaceSets_;    // true if exporting face sets
    bool                 exportFaceZones_;   // true if exporting face zones
    bool                 exportCellSets_;    // true if exporting cell sets
    bool                 exportCellZones_;   // true if exporting cell zones
    SideBcMode           sideBcMode_;        // side BC export setting
    PWP_UINT32           totElemCnt_;        // total # of cells in all blocks
    UInt32UInt32Map      blkIdOffset_;       // blkId to a vcSetFiles_ index
    VcSetFilesVec        vcSetFiles_;        // vc file
    BcSetFileNames       bcSetFiles_;        // bc face set file names
    PWP_UINT32           numFaces_;          // Number of faces for 2D export
    PWP_UINT32           curInflId_;         // current non-inflated dom id
    DomIdFaceSetFileMap  nonInflBCSetFiles_; // the non-inflated face set files
    Orientation          orientation_;       // 2D offset orientation
    PWGM_XYZVAL          planeZ_;            // The 2D grid's Z-plane location
    PWP_REAL             totalEdgeLength_;   // Sum of 2D edge lengths
    bool                 doThicknessCalc_;   // true if 2D and thickness == 0
    PWP_REAL             thickness_;         // The 2D extrusion thickness
    bool                 doFaceSets_;        // true if writing face sets
    bool                 setsDirWasCreated_; // set true if dir was created
};


//***************************************************************************
//***************************************************************************
//***************************************************************************
//***************************************************************************
//***************************************************************************
//***************************************************************************


/***************************************************************************
 * runtimeWrite is the plugin API entry point for CAE export
 ***************************************************************************/
PWP_BOOL
runtimeWrite(CAEP_RTITEM *pRti, PWGM_HGRIDMODEL model,
    const CAEP_WRITEINFO * pWriteInfo)
{
    OpenFoamPlugin ofp(pRti, model, pWriteInfo);
    return ofp.run();
}


/***************************************************************************
 * runtimeCreate is the plugin API entry point for plugin initialization,
 * called when the plugin is loaded
 ***************************************************************************/
PWP_BOOL
runtimeCreate(CAEP_RTITEM * /* pRti */)
{
    PWP_BOOL ret = PWP_TRUE;

    // The non-inflated BC types
    const char * const ShadowTypes = "faceSet";
    ret = ret && caeuAssignInfoValue("ShadowBcTypes", ShadowTypes, true);

    // None|SetsOnly|ZonesOnly|SetsAndZones
    //    0|       1|        2|           3
    //
    // This enum forms a bit field where:
    //
    //   SetsAndZones = SetsOnly | ZonesOnly
    const char *SetZoneEnum = "None|Sets|Zones|SetsAndZones";
    ret = ret &&
          caeuPublishValueDefinition(CellExport, PWP_VALTYPE_ENUM,
            "SetsAndZones", "RW", "Controls the export of cell sets and zones",
            SetZoneEnum);

    ret = ret &&
          caeuPublishValueDefinition(FaceExport, PWP_VALTYPE_ENUM,
            "SetsAndZones", "RW", "Controls the export of face sets and zones",
            SetZoneEnum);

    // Let user control the decimal precision.
    ret = ret &&
        caeuPublishValueDefinition(PointPrecision, PWP_VALTYPE_INT,
            PointPrecisionDefStr, "RW",
            "Controls the export of face sets and zones", "4 16");

    // Let user control the 2D grid thickening offset
    ret = ret &&
        caeuPublishValueDefinition(Thickness, PWP_VALTYPE_REAL,
            ThicknessDefStr, "RW", "Offset distance for 2D export", "0.0 +Inf");

    // Let user control the 2D BC assignments
    const char *SideBCExportEnum = "Unspecified|Single|BaseTop|Multiple";
    ret = ret &&
          caeuPublishValueDefinition(SideBCExport, PWP_VALTYPE_ENUM,
            "Single", "RW", "Controls how BCs are assigned to the top and "
            "base boundaries for 2D export.", SideBCExportEnum);

    return ret;
}


/***************************************************************************
 * runtimeDestroy is the plugin API entry point for plugin destruction, called
 * when the plugin is unloaded
 ***************************************************************************/
PWP_VOID
runtimeDestroy(CAEP_RTITEM * /* pRti */)
{
}

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

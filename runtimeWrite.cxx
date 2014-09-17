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

#include <cstring>
#include <errno.h>
#include <set>
#include <string>
#include <map>
#include <vector>


#if defined(WINDOWS)
#   include <direct.h>
#   include <malloc.h>
    typedef int mode_t;
#else
#   include <stdlib.h>
#   include <sys/stat.h>
#   include <sys/types.h>
#endif /* WINDOWS */

// Disable warnings caused by the current usage of fgets, fscanf, etc.
#if defined(linux)
#pragma GCC diagnostic ignored "-Wunused-result"
#endif /* linux */


typedef std::set<std::string>               StringSet;
typedef std::vector<std::string>            StringVec;
typedef std::map<PWP_UINT32, PWP_UINT32>    UInt32UInt32Map;
typedef std::map<const char*, PWP_UINT32>   CharPtrUInt32Map;


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
        sysFILEPOS savePos;
        if (0 != fp_) {
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
    FoamPointFile() :
        FoamFile("vectorField", "points")
    {
    }

    // destructor
    virtual ~FoamPointFile()
    {
    }

    // write vertex to points file, one per line
    // (x y z)
    void writeVertex(const PWGM_VERTDATA &v)
    {
        fprintf(*this, "(%.12g %.12g %.12g)\n", v.x, v.y, v.z);
        incrNumItems();
    }

    // write global vertex to points file
    void writeVertex(const PWGM_HVERTEX h)
    {
        PWGM_VERTDATA v;
        if (PwVertDataMod(h, &v)) {
            writeVertex(v);
        }
    }
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
    FoamFacesFile() :
        FoamFile("faceList", "faces")
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
                (unsigned long)eData.vertCnt, (unsigned long)eData.index[3],
                (unsigned long)eData.index[2], (unsigned long)eData.index[1],
                (unsigned long)eData.index[0]);
            incrNumItems();
            break;
        case PWGM_ELEMTYPE_TRI:
            fprintf(*this, "%lu(%lu %lu %lu)\n", (unsigned long)eData.vertCnt,
                (unsigned long)eData.index[2], (unsigned long)eData.index[1],
                (unsigned long)eData.index[0]);
            incrNumItems();
            break;
        case PWGM_ELEMTYPE_BAR:
            fprintf(*this, "%lu(%lu %lu)\n", (unsigned long)eData.vertCnt,
                (unsigned long)eData.index[1], (unsigned long)eData.index[0]);
            incrNumItems();
            break;
        }
    }
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
            fprintf(*this, "        nFaces %d;\n", (int)it->nFaces_);
            fprintf(*this, "        startFace %d;\n", (int)it->startFace_);
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
    void pushFace(PWGM_ENUM_FACETYPE type, PWP_UINT32 face)
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
    void deleteFaceSets()
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
    void deleteCellSets()
    {
        finalizeCellSet();
        if (0 != cellSetFile_) {
            deleteSetFile(cellSetFile_->object());
        }
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

private:
    // Hidden copy constructor
    VcSetFiles(const VcSetFiles & vcf);

    // Hidden assignment operator
    VcSetFiles & operator=(const VcSetFiles & rhs);

    FoamFaceSetFile *internalFaceSetFile_;  // interior face set file or null
    FoamFaceSetFile *boundaryFaceSetFile_;  // boundary face set file or null
    FoamCellSetFile *cellSetFile_;          // cell set file or null
};

typedef std::vector<VcSetFiles *> VcSetFilesVec;


static const char *FaceExport   = "FaceExport";
static const char *CellExport   = "CellExport";


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

        faces_(),
        owner_(),
        neighbour_(),
        bcStats_(),
        usedFileNames_(),
        exportFaceSets_(false),
        exportFaceZones_(false),
        exportCellSets_(false),
        exportCellZones_(true),

        totElemCnt_(0),

        blkIdOffset_(),
        vcNameOffset_(),
        vcSetFiles_()
    {
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

        PWP_BOOL ret = PWP_FALSE;
        bool wasCreated = false;
        PWP_UINT32 majorSteps = 3 + (exportCellZones_ ? 1 : 0);

        if (!caeuProgressInit(&rti_, majorSteps)) {
        }
        else if (needSetsDir() && !createSetsDir(wasCreated)) {
            caeuSendErrorMsg(&rti_, "Could not create 'sets' directory!", 0);
        }
        else if (needSetsDir() && !prepareVcSetFiles()) {
            caeuSendErrorMsg(&rti_, "Could prepare VC set files!", 0);
        }
        else if (!processPoints()) {
            caeuSendErrorMsg(&rti_, "Could not write points!", 0);
        }
        else if (!processFaces()) {
            caeuSendErrorMsg(&rti_, "Could not write face files!", 0);
        }
        else if (!processCells()) {
            caeuSendErrorMsg(&rti_, "Could not write cell sets!", 0);
        }
        else {
            ret = PWP_TRUE;
        }

        if (wasCreated && !exportingAnySets()) {
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
            if ((0 == bcStats_.size()) ||
                    (0 != bcStats_.back().name_.compare(condData.name))) {
                // we are starting a new BC group
                BcStat stats;
                stats.name_ = condData.name;
                stats.type_ = condData.type;
                stats.nFaces_ = 1;
                stats.startFace_ = data.face;
                bcStats_.push_back(stats);
            }
            else {
                // same BC group, update face count
                ++bcStats_.back().nFaces_;
            }
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
        bool ret = false;
        PWP_UINT32 numPts = PwModVertexCount(model_);
        FoamPointFile points;
        if (progressBeginStep(numPts) && points.open()) {
            ret = true;
            for (PWP_UINT32 ii = 0; ii < numPts; ++ii) {
                points.writeVertex(PwModEnumVertices(model_, ii));
                if (!progressIncr()) {
                    ret = false;
                    break;
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

        if (ofp.faceSetsNeeded()) {
            ofp.pushFace(*data);
        }

        return ofp.progressIncr();
    }

    // Callback from plugin API when face streaming has completed
    static PWP_UINT32 streamEnd(PWGM_ENDSTREAM_DATA *data)
    {
        if (0 == data->userData) {
            return PWP_FALSE;
        }
        OpenFoamPlugin &ofp = *((OpenFoamPlugin*)data->userData);
        FoamBoundaryFile boundary;
        if (boundary.open()) {
            // Flush the accumulated BC information to the boundary file.
            boundary.writeBoundaries(ofp.bcStats_);
        }
        return ofp.progressEndStep();
    }

    // create the sets directory
    bool createSetsDir(bool &wasCreated)
    {
        bool ret = true;
        if (0 != pwpCreateDir("sets")) {
            wasCreated = false;
            ret = (EEXIST == errno);
        }
        else {
            wasCreated = true;
        }
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
                (*it)->deleteFaceSets();
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
                        (*it)->deleteCellSets();
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
        //  Make a vcNameOffset_ mapping.
        //  Keep a tally of the number of cells.
        //  Track the max index value.
        PWP_UINT32 blkId = 0;
        CharPtrUInt32Map::iterator iter;
        PWGM_CONDDATA vc;
        PWP_UINT32 offset = 0;
        PWGM_HBLOCK block = PwModEnumBlocks(model_, blkId);
        while (PwBlkCondition(block, &vc)) {
            if (!isUnspecifiedVc(vc)) {
                // Check if vc mapping exists
                iter = vcNameOffset_.find(vc.name);
                if (vcNameOffset_.end() == iter) {
                    // first time for this VC name - allocate a new file
                    offset = (PWP_UINT32)vcSetFiles_.size();
                    vcNameOffset_[vc.name] = offset;
                    VcSetFiles *vcset = new VcSetFiles(vc, usedFileNames_);
                    vcSetFiles_.push_back(vcset);
                }
                else {
                    // VC already mapped - use existing file
                    offset = iter->second;
                }
                blkIdOffset_[blkId] = offset;
                totElemCnt_ += PwBlkElementCount(block, 0);
            }
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
    void pushFace(const PWGM_FACESTREAM_DATA &data)
    {
        PWGM_ENUM_FACETYPE faceType = adjustFaceType(data);
        PWP_UINT32 blkId = PWGM_HBLOCK_ID(data.owner.block);
        PWP_UINT32 offset = blkIdOffset_[blkId];
        VcSetFiles *vcFiles = vcSetFiles_.at(offset);
        vcFiles->pushFace(faceType, data.face);

        // A connection face has different VCs on either side.
        // Must also push face to neighbor's VcSetFiles
        PWGM_ENUMELEMDATA eData;
        if ((PWGM_FACETYPE_CONNECTION == faceType) && PwElemDataModEnum(
              PwModEnumElements(data.model, data.neighborCellIndex), &eData)) {
            PWP_UINT32 neighborBlkId = PWGM_HELEMENT_PID(eData.hBlkElement);
            PWP_UINT32 neighborOffset = blkIdOffset_[neighborBlkId];
            VcSetFiles *neighborVcFiles = vcSetFiles_.at(neighborOffset);
            neighborVcFiles->pushFace(faceType, data.face);
        }
    }

    // write the face zones to the face sets files
    void writeFaceZonesFile()
    {
        finalizeFaceSets();
        FoamFaceZoneFile faceZones;
        if (!progressBeginStep((PWP_UINT32)vcSetFiles_.size())) {
            // aborted
        }
        else if (faceZones.open()) {
            VcSetFilesVec::iterator it = vcSetFiles_.begin();
            for (; it != vcSetFiles_.end(); ++it) {
                (*it)->addFaceSetsToZonesFile(faceZones);
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
    CAEP_RTITEM             &rti_;            // ref to runtimeWrite *pRti
    PWGM_HGRIDMODEL         model_;           // same as runtimeWrite model
    const CAEP_WRITEINFO    &writeInfo_;      // ref to runtimeWrite *pWriteInfo
    FoamFacesFile           faces_;           // The mesh "faces" file
    FoamOwnerFile           owner_;           // The mesh cell "owner" file
    FoamNeighbourFile       neighbour_;       // The mesh cell "neighbour" file
    BcStats                 bcStats_;         // cached BC stat data
    StringSet               usedFileNames_;   // set of used VC set file names
    bool                    exportFaceSets_;  // true if exporting face sets
    bool                    exportFaceZones_; // true if exporting face zones
    bool                    exportCellSets_;  // true if exporting cell sets
    bool                    exportCellZones_; // true if exporting cell zones
    PWP_UINT32              totElemCnt_;      // total # of cells in all blocks
    UInt32UInt32Map         blkIdOffset_;     // blkId to a vcSetFiles_ index
    CharPtrUInt32Map        vcNameOffset_;    // vc name to a vcSetFiles_ index
    VcSetFilesVec           vcSetFiles_;      // vc file
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

    return ret;
}


/***************************************************************************
 * runtimeCreate is the plugin API entry point for plugin destruction, called
 * when the plugin is unloaded
 ***************************************************************************/
PWP_VOID
runtimeDestroy(CAEP_RTITEM * /* pRti */)
{
}

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

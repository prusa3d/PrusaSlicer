#include "OCCTWrapper.hpp"

#include "occtwrapper_export.h"

#include <cassert>

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

#include "STEPCAFControl_Reader.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XCAFApp_Application.hxx"
#include "TopoDS_Builder.hxx"
#include "TopoDS.hxx"
#include "TDataStd_Name.hxx"
#include "BRepBuilderAPI_Transform.hxx"
#include "TopExp_Explorer.hxx"
#include "BRep_Tool.hxx"
#include "admesh/stl.h"
#include "libslic3r/Point.hpp"

const double STEP_TRANS_CHORD_ERROR = 0.005;
const double STEP_TRANS_ANGLE_RES = 1;

// const int LOAD_STEP_STAGE_READ_FILE          = 0;
// const int LOAD_STEP_STAGE_GET_SOLID          = 1;
// const int LOAD_STEP_STAGE_GET_MESH           = 2;

namespace Slic3r {

struct NamedSolid {
    NamedSolid(const TopoDS_Shape& s,
               const std::string& n) : solid{s}, name{n} {}
    const TopoDS_Shape solid;
    const std::string  name;
};

static void getNamedSolids(const TopLoc_Location& location, const Handle(XCAFDoc_ShapeTool) shapeTool,
                           const TDF_Label label, std::vector<NamedSolid>& namedSolids)
{
    TDF_Label referredLabel{label};
    if (shapeTool->IsReference(label))
        shapeTool->GetReferredShape(label, referredLabel);

    std::string name;
    Handle(TDataStd_Name) shapeName;
    if (referredLabel.FindAttribute(TDataStd_Name::GetID(), shapeName))
        name = TCollection_AsciiString(shapeName->Get()).ToCString();

    TopLoc_Location localLocation = location * shapeTool->GetLocation(label);
    TDF_LabelSequence components;
    if (shapeTool->GetComponents(referredLabel, components)) {
        for (Standard_Integer compIndex = 1; compIndex <= components.Length(); ++compIndex) {
            getNamedSolids(localLocation, shapeTool, components.Value(compIndex), namedSolids);
        }
    } else {
        TopoDS_Shape shape;
        shapeTool->GetShape(referredLabel, shape);
        TopAbs_ShapeEnum shape_type = shape.ShapeType();
        BRepBuilderAPI_Transform transform(shape, localLocation, Standard_True);
        switch (shape_type) {
        case TopAbs_COMPOUND:
            namedSolids.emplace_back(TopoDS::Compound(transform.Shape()), name);
            break;
        case TopAbs_COMPSOLID:
            namedSolids.emplace_back(TopoDS::CompSolid(transform.Shape()), name);
            break;
        case TopAbs_SOLID:
            namedSolids.emplace_back(TopoDS::Solid(transform.Shape()), name);
            break;
        default:
            break;
        }
    }
}

extern "C" OCCTWRAPPER_EXPORT bool load_step_internal(const char *path, OCCTResult* res /*BBS:, ImportStepProgressFn proFn*/)
{
try {
    //bool cb_cancel = false;
    //if (proFn) {
    //    proFn(LOAD_STEP_STAGE_READ_FILE, 0, 1, cb_cancel);
    //    if (cb_cancel)
    //        return false;
    //}
    

    std::vector<NamedSolid> namedSolids;
    Handle(TDocStd_Document) document;
    Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
    application->NewDocument(path, document);
    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    //BBS: Todo, read file is slow which cause the progress_bar no update and gui no response
    IFSelect_ReturnStatus stat = reader.ReadFile(path);
    if (stat != IFSelect_RetDone || !reader.Transfer(document)) {
        application->Close(document);
        res->error_str = std::string{"Could not read '"} + path + "'";
        return false;
    }
    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
    TDF_LabelSequence topLevelShapes;
    shapeTool->GetFreeShapes(topLevelShapes);

    Standard_Integer topShapeLength = topLevelShapes.Length() + 1;
    for (Standard_Integer iLabel = 1; iLabel < topShapeLength; ++iLabel) {
        //if (proFn) {
        //    proFn(LOAD_STEP_STAGE_GET_SOLID, iLabel, topShapeLength, cb_cancel);
        //    if (cb_cancel) {
        //        shapeTool.reset(nullptr);
        //        application->Close(document);
        //        return false;
        //    }
        //}
        getNamedSolids(TopLoc_Location{}, shapeTool, topLevelShapes.Value(iLabel), namedSolids);
    }

    

    // Now the object name. Set it to filename without suffix.
    // This will later be changed if only one volume is loaded.
    const char *last_slash = strrchr(path, DIR_SEPARATOR);
    std::string obj_name((last_slash == nullptr) ? path : last_slash + 1);
    res->object_name = obj_name;

    for (const NamedSolid &namedSolid : namedSolids) {
        BRepMesh_IncrementalMesh mesh(namedSolid.solid, STEP_TRANS_CHORD_ERROR, false, STEP_TRANS_ANGLE_RES, true);
        res->volumes.emplace_back();

        std::vector<Vec3f>      vertices;
        std::vector<stl_facet> &facets = res->volumes.back().facets;
        for (TopExp_Explorer anExpSF(namedSolid.solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
            const int aNodeOffset = int(vertices.size());
            const TopoDS_Shape& aFace = anExpSF.Current();
            TopLoc_Location aLoc;
            Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(aFace), aLoc);
            if (aTriangulation.IsNull())
                continue;

            // First copy vertices (will create duplicates).
            gp_Trsf aTrsf = aLoc.Transformation();
            for (Standard_Integer aNodeIter = 1; aNodeIter <= aTriangulation->NbNodes(); ++aNodeIter) {
                gp_Pnt aPnt = aTriangulation->Node(aNodeIter);
                aPnt.Transform(aTrsf);
                vertices.emplace_back(std::move(Vec3f(float(aPnt.X()), float(aPnt.Y()), float(aPnt.Z()))));
            }

            // Now copy the facets.
            const TopAbs_Orientation anOrientation = anExpSF.Current().Orientation();
            for (Standard_Integer aTriIter = 1; aTriIter <= aTriangulation->NbTriangles(); ++aTriIter) {
                const int aTriangleOffet = int(facets.size());
                Poly_Triangle aTri = aTriangulation->Triangle(aTriIter);

                Standard_Integer anId[3];
                aTri.Get(anId[0], anId[1], anId[2]);
                if (anOrientation == TopAbs_REVERSED) {
                    std::swap(anId[1], anId[2]);
                }

                stl_facet facet;
                facet.vertex[0] = vertices[anId[0] + aNodeOffset - 1];
                facet.vertex[1] = vertices[anId[1] + aNodeOffset - 1];
                facet.vertex[2] = vertices[anId[2] + aNodeOffset - 1];
                facet.normal    = (facet.vertex[1] - facet.vertex[0]).cross(facet.vertex[2] - facet.vertex[1]).normalized();
                facet.extra[0]  = 0;
                facet.extra[1]  = 0;
                facets.emplace_back(std::move(facet));
            }
        }

        res->volumes.back().volume_name = namedSolid.name;

        if (vertices.empty())
            res->volumes.pop_back();        
    }

    shapeTool.reset(nullptr);
    application->Close(document);

    if (res->volumes.empty())
        return false;
} catch (const std::exception& ex) {
    res->error_str = ex.what();
    return false;
} catch (...) {
    res->error_str = "An exception was thrown in load_step_internal.";
    return false;
}
    
    return true;
}

}; // namespace Slic3r

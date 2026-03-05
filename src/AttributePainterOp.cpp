#include "AttributePainterOp.h"
#include "ViewportBrushKnob.h"

// Nuke NDK
#include <DDImage/GeoInfo.h>
#include <DDImage/PolyMesh.h>
#include <DDImage/Attribute.h>
#include <DDImage/Scene.h>

// USD
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>

#include <cstring>
#include <algorithm>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace AP {

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Static data
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

const char* const AttributePainterOp::kFalloffNames[] = {
    "Smooth", "Linear", "Constant", "Gaussian", nullptr
};
const char* const AttributePainterOp::kBlendNames[] = {
    "Replace", "Add", "Subtract", "Multiply", "Smooth", nullptr
};

const DD::Image::Op::Description AttributePainterOp::description(
    "AttributePainter",
    AttributePainterOp::Build
);

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Constructor / Destructor
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

AttributePainterOp::AttributePainterOp(Node* node)
    : GeoOp(node)
    , sampler_ (std::make_unique<MeshSampler>())
    , writer_  (std::make_unique<USDColorWriter>())
{}

AttributePainterOp::~AttributePainterOp() = default;

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Help text
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

const char* AttributePainterOp::node_help() const {
    return
        "AttributePainter\n\n"
        "Interactive vertex colour painter for USD geometry in Nuke 17.\n"
        "Replicates Houdini's Attribute Paint SOP.\n\n"
        "Usage:\n"
        "  â€¢ Connect a USD node to the input.\n"
        "  â€¢ Set the USD Prim Path to the mesh you want to paint.\n"
        "  â€¢ Press P or click in the 3D viewer to begin painting.\n"
        "  â€¢ Ctrl+Scroll to resize the brush.\n"
        "  â€¢ Ctrl+Z / Ctrl+Shift+Z for undo/redo.\n\n"
        "Painted data is written as a 'vertex' interpolated primvar\n"
        "(default: 'displayColor') on the USD prim.\n";
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Knobs
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AttributePainterOp::knobs(DD::Image::Knob_Callback f) {
    // Passthrough GeoOp input knobs
    GeoOp::knobs(f);

    // â”€â”€ USD target â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    DD::Image::Divider(f, "USD Target");
    DD::Image::String_knob(f, &k_primPath_, "prim_path", "Prim Path");
    DD::Image::Tooltip(f, "SdfPath of the UsdGeomMesh to paint (e.g. /World/Body)");
    DD::Image::String_knob(f, &k_primvarName_, "primvar_name", "Primvar Name");
    DD::Image::Tooltip(f, "Name of the color primvar to write. Default: displayColor");

    // â”€â”€ Brush â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    DD::Image::Divider(f, "Brush");
    DD::Image::Bool_knob(f, &k_paintEnabled_, "paint_enabled", "Enable Paint");
    DD::Image::Bool_knob(f, &k_showBrush_,   "show_brush",    "Show Brush");
    DD::Image::Float_knob(f, &k_radius_,    "radius",    "Radius");
    DD::Image::SetRange(f, 0.001, 10.0);
    DD::Image::Float_knob(f, &k_strength_,  "strength",  "Strength");
    DD::Image::SetRange(f, 0.0, 1.0);
    DD::Image::Float_knob(f, &k_hardness_,  "hardness",  "Hardness");
    DD::Image::SetRange(f, 0.0, 1.0);
    DD::Image::Enumeration_knob(f, &k_falloff_, kFalloffNames, "falloff", "Falloff");
    DD::Image::Enumeration_knob(f, &k_blend_,   kBlendNames,   "blend",   "Blend Mode");
    DD::Image::Color_knob(f, k_color_, "paint_color", "Color");

    // â”€â”€ Misc â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    DD::Image::Divider(f, "");
    DD::Image::Bool_knob(f, &k_flipNormals_, "flip_normals", "Flip Normals");

    // â”€â”€ Viewport brush knob (invisible â€” only for 3D handle callbacks) â”€â”€â”€â”€â”€â”€
    // We use a custom knob so Nuke's handle system calls our draw/mouse methods.
    // Must be created LAST so it can reference the Op pointer.
    CustomKnob1(ViewportBrushKnob, f, this, "brush_handle");
}

int AttributePainterOp::knob_changed(DD::Image::Knob* k) {
    // Geometry-invalidating changes
    if (k->is("prim_path") || k->is("primvar_name")) {
        geometryDirty_.store(true);
        return 1;
    }
    // Brush state syncs
    syncBrushStateToKnobs();
    return GeoOp::knob_changed(k);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Brush state sync
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AttributePainterOp::syncBrushStateToKnobs() {
    if (!brushKnob_) return;

    BrushState bs;
    bs.radius   = k_radius_;
    bs.strength = k_strength_;
    bs.hardness = k_hardness_;
    bs.color    = { k_color_[0], k_color_[1], k_color_[2] };
    bs.falloff  = static_cast<FalloffMode>(k_falloff_);
    bs.blend    = static_cast<BlendMode>(k_blend_);
    bs.active   = k_paintEnabled_;

    brushKnob_->setBrushState(bs);
    brushKnob_->setEnabled(k_paintEnabled_ && k_showBrush_);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  USD stage extraction
//  In Nuke 17, the USD stage is accessible via the input GeoOp's attachment.
//  Nuke internally attaches a UsdStageData object to each GeoObject that came
//  from a UsdNode. We cast through the attachment type map to retrieve it.
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

bool AttributePainterOp::extractStageFromInput(DD::Image::GeometryList& geoList) {
    // Walk all geo objects in the list looking for a USD stage attachment
    for (unsigned obj = 0; obj < geoList.size(); ++obj) {
        const DD::Image::GeoInfo& info = geoList[obj];

        // Nuke 17 attaches the USD stage as a named group attribute.
        // The attribute name used internally by the UsdReader node is
        // "usdStage" and has type AT_POINTER (void*).
        const DD::Image::AttribContext* ctx =
            info.get_typed_group_attribcontext(DD::Image::Group_Object, "usdStage",
                                           DD::Image::POINTER_ATTRIB);
        if (ctx && ctx->attribute) {
            // Retrieve the raw void* and reinterpret as UsdStageRefPtr*
            const DD::Image::Attribute* attr = &(*ctx->attribute);
            if (attr->size() > 0) {
                auto* stagePtr = reinterpret_cast<UsdStageRefPtr*>(
                    attr->pointer(0));
                if (stagePtr && *stagePtr) {
                    stage_ = *stagePtr;
                    return true;
                }
            }
        }
    }
    return false;
}


// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Rebuild geometry (call when prim path or topology changes)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

bool AttributePainterOp::rebuildGeometry() {
    if (!stage_) return false;

    targetPath_ = SdfPath(k_primPath_);
    UsdPrim prim = stage_->GetPrimAtPath(targetPath_);
    if (!prim || !prim.IsA<UsdGeomMesh>()) return false;

    UsdGeomMesh mesh(prim);

    // â”€â”€ Points â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    VtArray<GfVec3f> vtPoints;
    mesh.GetPointsAttr().Get(&vtPoints);

    // Apply local-to-world xform
    GfMatrix4d xform = UsdGeomXformCache().GetLocalToWorldTransform(prim);
    std::vector<Vec3f> worldPts;
    worldPts.reserve(vtPoints.size());
    for (auto& p : vtPoints) {
        GfVec3d pw = xform.Transform(GfVec3d(p[0], p[1], p[2]));
        worldPts.push_back({ (float)pw[0], (float)pw[1], (float)pw[2] });
    }

    // â”€â”€ Topology â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    VtArray<int> vtCounts, vtIndices;
    mesh.GetFaceVertexCountsAttr() .Get(&vtCounts);
    mesh.GetFaceVertexIndicesAttr().Get(&vtIndices);

    std::vector<int> faceCounts (vtCounts.begin(),  vtCounts.end());
    std::vector<int> faceIndices(vtIndices.begin(), vtIndices.end());

    // â”€â”€ Rebuild sampler â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    sampler_->rebuild(worldPts, faceCounts, faceIndices);
    if (brushKnob_) brushKnob_->setMeshSampler(sampler_.get());
    // Load existing colours from USD
    writer_ = std::make_unique<USDColorWriter>(k_primvarName_);
    auto existing = writer_->read(mesh, worldPts.size());
    if (!existing.empty())
        sampler_->initColors(existing);

    geometryDirty_.store(false);
    return true;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  geometry_engine â€” called by Nuke to cook this node
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AttributePainterOp::_validate(bool for_real) {
    GeoOp::_validate(for_real);
}

void AttributePainterOp::geometry_engine(DD::Image::Scene& scene,
                                          DD::Image::GeometryList& out) {
    DD::Image::GeoOp* geoInput = dynamic_cast<DD::Image::GeoOp*>(input(0));
    if (!geoInput) return;
    geoInput->get_geometry(scene, out);
    if (!geometryDirty_.load()) return;
    std::vector<Vec3f> worldPts;
    std::vector<int> faceCounts, faceIndices;
    for (unsigned int g = 0; g < out.size(); ++g) {
        const DD::Image::GeoInfo& geo = out[g];
        const DD::Image::PointList* pts = geo.point_list();
        if (!pts || pts->size() == 0) continue;
        unsigned int npts = (unsigned int)pts->size();
        unsigned int baseIdx = (unsigned int)worldPts.size();
        for (unsigned int i = 0; i < npts; ++i) {
            const DD::Image::Vector3& p = (*pts)[i];
            worldPts.push_back({p.x, p.y, p.z});
        }
        for (unsigned int f = 0; f < geo.primitives(); ++f) {
            const DD::Image::Primitive* prim = geo.primitive(f);
            if (!prim) continue;
            unsigned int vcount = prim->vertices();
            faceCounts.push_back((int)vcount);
            for (unsigned int v = 0; v < vcount; ++v) {
                unsigned int vi = prim->vertex(v);
                if (vi >= npts) vi = 0;
                faceIndices.push_back((int)(baseIdx + vi));
            }
        }
    }
    if (!worldPts.empty() && !faceCounts.empty() && sampler_) {
        sampler_->rebuild(worldPts, faceCounts, faceIndices);
    }
    geometryDirty_.store(false);
}
void AttributePainterOp::build_handles(DD::Image::ViewerContext* ctx) {
    GeoOp::build_handles(ctx);
    if (!brushKnob_) {
        DD::Image::Knob* k = knob("brush_handle");
        brushKnob_ = dynamic_cast<ViewportBrushKnob*>(k);
        if (brushKnob_) {
            brushKnob_->setMeshSampler(sampler_.get());
            brushKnob_->setPaintCallback(
                [this](const Vec3f& pos, const Vec3f& normal, bool first) {
                    onPaintTick(pos, normal, first);
                });
            brushKnob_->setStrokeEndCallback([this]() { onStrokeEnd(); });
        }
    }
    syncBrushStateToKnobs();
    if (brushKnob_) brushKnob_->draw_handle(ctx);
}

void AttributePainterOp::draw_handle(DD::Image::ViewerContext* ctx) {
    if (brushKnob_) brushKnob_->draw_handle(ctx);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  onPaintTick â€” called each time the mouse moves while painting
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AttributePainterOp::onPaintTick(const Vec3f& pos,
                                      const Vec3f& normal,
                                      bool firstTick) {
    if (!sampler_ || !sampler_->isValid()) return;

    BrushState bs = brushKnob_->brushState();
    bs.center = pos;
    bs.normal = normal;

    // Query vertices in radius
    std::vector<std::pair<uint32_t,float>> nearby;
    sampler_->verticesInRadius(pos, bs.radius, nearby);
    if (nearby.empty()) return;

    // Snapshot before-state on first tick of this stroke
    if (firstTick) {
        strokeBefore_.clear();
        strokeCurrent_.clear();
        for (auto& [idx, dsq] : nearby) {
            strokeBefore_.push_back({ idx, sampler_->getColor(idx) });
        }
    }

    // Apply paint
    for (auto& [idx, dsq] : nearby) {
            float d = std::sqrt(dsq);
            float w = BrushSystem::weight(bs, d);
            if (w <= 0.f) continue;

            Color3f src = sampler_->getColor(idx);
            Color3f dst = BrushSystem::blend(bs, src, w);
            dst = BrushSystem::saturate(dst);
            sampler_->setColor(idx, dst);
            writer_->stage(idx, dst);
    }

    // Commit to USD every tick for live feedback
    commitToUSD();
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  onStrokeEnd â€” finalize undo record and flush USD
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AttributePainterOp::onStrokeEnd() {
    if (!sampler_) return;

    // Collect after-state
    std::vector<VertexColor> after;
    after.reserve(strokeBefore_.size());
    for (auto& bv : strokeBefore_)
        after.push_back({ bv.index, sampler_->getColor(bv.index) });

    undoStack_.beginStroke(strokeBefore_);
    undoStack_.endStroke(after);
    strokeBefore_.clear();

    // Tell Nuke the node is dirty so the viewer refreshes
    invalidate();
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  commitToUSD â€” flush staged colour edits to the live USD stage
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AttributePainterOp::commitToUSD() {
    if (!stage_) return;

    UsdPrim prim = stage_->GetPrimAtPath(targetPath_);
    if (!prim || !prim.IsA<UsdGeomMesh>()) return;

    UsdGeomMesh mesh(prim);
    auto& colors = const_cast<std::vector<Color3f>&>(sampler_->colors());
    writer_->commit(mesh, colors);
    writer_->clearStaged();
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  applyVertexColors â€” used by undo/redo
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AttributePainterOp::applyVertexColors(const std::vector<VertexColor>& vcs) {
    if (!sampler_) return;
    for (auto& vc : vcs) {
        sampler_->setColor(vc.index, vc.color);
        writer_->stage(vc.index, vc.color);
    }
    commitToUSD();
    invalidate();
}

} // namespace AP


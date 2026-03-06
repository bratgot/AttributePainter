#include "AttributePainterOp.h"
#include "ViewportBrushKnob.h"

#include <DDImage/GeoInfo.h>
#include <DDImage/PolyMesh.h>
#include <DDImage/Primitive.h>
#include <DDImage/Attribute.h>
#include <DDImage/Scene.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>

#include <cstring>
#include <cstdio>
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace AP {

const char* const AttributePainterOp::kFalloffNames[] = {
    "Smooth", "Linear", "Constant", "Gaussian", nullptr
};
const char* const AttributePainterOp::kBlendNames[] = {
    "Replace", "Add", "Subtract", "Multiply", "Smooth", "Erase", nullptr
};

const DD::Image::Op::Description AttributePainterOp::description(
    "AttributePainter",
    AttributePainterOp::Build
);

AttributePainterOp::AttributePainterOp(Node* node)
    : GeoOp(node)
    , sampler_ (std::make_unique<MeshSampler>())
    , writer_  (std::make_unique<USDColorWriter>())
{}

AttributePainterOp::~AttributePainterOp() = default;

const char* AttributePainterOp::node_help() const {
    return
        "AttributePainter v1.0.1\n\n"
        "Interactive vertex colour painter for geometry in Nuke's 3D viewer.\n\n"
        "Controls:\n"
        "  LMB drag           - paint\n"
        "  Shift + LMB drag   - resize brush\n"
        "  Erase via dropdown - select Erase in Blend Mode\n";
}

void AttributePainterOp::knobs(DD::Image::Knob_Callback f) {
    GeoOp::knobs(f);

    DD::Image::Text_knob(f, "v1.0.1");
    DD::Image::Divider(f, "USD Target");
    DD::Image::String_knob(f, &k_primPath_, "prim_path", "Prim Path");
    DD::Image::String_knob(f, &k_primvarName_, "primvar_name", "Primvar Name");

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

    DD::Image::Divider(f, "");
    DD::Image::Bool_knob(f, &k_flipNormals_, "flip_normals", "Flip Normals");
    DD::Image::Bool_knob(f, &k_debug_, "debug", "Debug");
    DD::Image::SetFlags(f, DD::Image::Knob::STARTLINE);

    CustomKnob1(ViewportBrushKnob, f, this, "brush_handle");
}

int AttributePainterOp::knob_changed(DD::Image::Knob* k) {
    if (k->is("prim_path") || k->is("primvar_name")) {
        geometryDirty_.store(true);
        return 1;
    }
    syncBrushStateToKnobs();
    return GeoOp::knob_changed(k);
}

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
    brushKnob_->setDebug(k_debug_);
}

bool AttributePainterOp::extractStageFromInput(DD::Image::GeometryList& geoList) {
    for (unsigned obj = 0; obj < geoList.size(); ++obj) {
        const DD::Image::GeoInfo& info = geoList[obj];
        const DD::Image::AttribContext* ctx =
            info.get_typed_group_attribcontext(DD::Image::Group_Object, "usdStage",
                                           DD::Image::POINTER_ATTRIB);
        if (ctx && ctx->attribute) {
            const DD::Image::Attribute* attr = &(*ctx->attribute);
            if (attr->size() > 0) {
                auto* stagePtr = reinterpret_cast<UsdStageRefPtr*>(attr->pointer(0));
                if (stagePtr && *stagePtr) { stage_ = *stagePtr; return true; }
            }
        }
    }
    return false;
}

bool AttributePainterOp::rebuildGeometry() {
    if (!stage_) return false;
    targetPath_ = SdfPath(k_primPath_);
    UsdPrim prim = stage_->GetPrimAtPath(targetPath_);
    if (!prim || !prim.IsA<UsdGeomMesh>()) return false;
    UsdGeomMesh mesh(prim);
    VtArray<GfVec3f> vtPoints; mesh.GetPointsAttr().Get(&vtPoints);
    GfMatrix4d xform = UsdGeomXformCache().GetLocalToWorldTransform(prim);
    std::vector<Vec3f> wpts; wpts.reserve(vtPoints.size());
    for (auto& p : vtPoints) {
        GfVec3d pw = xform.Transform(GfVec3d(p[0],p[1],p[2]));
        wpts.push_back({(float)pw[0],(float)pw[1],(float)pw[2]});
    }
    VtArray<int> vc, vi;
    mesh.GetFaceVertexCountsAttr().Get(&vc);
    mesh.GetFaceVertexIndicesAttr().Get(&vi);
    std::vector<int> fc(vc.begin(),vc.end()), fi(vi.begin(),vi.end());
    sampler_->rebuild(wpts, fc, fi);
    if (brushKnob_) brushKnob_->setMeshSampler(sampler_.get());
    writer_ = std::make_unique<USDColorWriter>(k_primvarName_);
    auto existing = writer_->read(mesh, wpts.size());
    if (!existing.empty()) sampler_->initColors(existing);
    geometryDirty_.store(false);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Face extraction helper
// ─────────────────────────────────────────────────────────────────────────────

static void extractFacesFromPrimitive(const DD::Image::Primitive* prim,
                                       unsigned int npts, unsigned int baseIdx,
                                       std::vector<int>& faceCounts,
                                       std::vector<int>& faceIndices,
                                       int& extractedFaces) {
    if (!prim) return;
    unsigned int numFaces = 0;
    try { numFaces = prim->faces(); } catch (...) { return; }

    if (numFaces > 1) {
        unsigned int vertexOffset = 0;
        for (unsigned int f = 0; f < numFaces; ++f) {
            unsigned int fvCount = 0;
            try { fvCount = prim->face_vertices(f); } catch (...) { continue; }
            if (fvCount < 3 || fvCount > 100) { vertexOffset += fvCount; continue; }
            faceCounts.push_back((int)fvCount);
            for (unsigned int v = 0; v < fvCount; ++v) {
                unsigned int pi = 0;
                try { pi = prim->vertex(vertexOffset + v); } catch (...) { pi = 0; }
                if (pi >= npts) pi = 0;
                faceIndices.push_back((int)(baseIdx + pi));
            }
            vertexOffset += fvCount;
            ++extractedFaces;
        }
    } else {
        unsigned int vcount = 0;
        try { vcount = prim->vertices(); } catch (...) { return; }
        if (vcount < 3 || vcount > 10000) return;
        faceCounts.push_back((int)vcount);
        for (unsigned int v = 0; v < vcount; ++v) {
            unsigned int vi = 0;
            try { vi = prim->vertex(v); } catch (...) { vi = 0; }
            if (vi >= npts) vi = 0;
            faceIndices.push_back((int)(baseIdx + vi));
        }
        ++extractedFaces;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  geometry_engine
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::_validate(bool for_real) {
    GeoOp::_validate(for_real);
}

void AttributePainterOp::geometry_engine(DD::Image::Scene& scene,
                                          DD::Image::GeometryList& out) {
    DD::Image::GeoOp* geoInput = dynamic_cast<DD::Image::GeoOp*>(input(0));
    if (!geoInput) {
        if (sampler_ && sampler_->isValid()) {
            sampler_ = std::make_unique<MeshSampler>();
            if (brushKnob_) brushKnob_->setMeshSampler(sampler_.get());
        }
        return;
    }
    geoInput->get_geometry(scene, out);
    extractStageFromInput(out);

    std::vector<Vec3f> worldPts;
    std::vector<int> faceCounts, faceIndices;

    for (unsigned int g = 0; g < out.size(); ++g) {
        const DD::Image::GeoInfo& geo = out[g];
        const DD::Image::PointList* pts = geo.point_list();
        if (!pts || pts->size() == 0) continue;
        unsigned int npts = (unsigned int)pts->size();
        unsigned int baseIdx = (unsigned int)worldPts.size();
        const DD::Image::Matrix4& xform = geo.matrix;

        for (unsigned int i = 0; i < npts; ++i) {
            DD::Image::Vector3 local = (*pts)[i];
            DD::Image::Vector4 w4 = xform * DD::Image::Vector4(local.x, local.y, local.z, 1.0f);
            if (std::abs(w4.w) > 1e-8f)
                worldPts.push_back({(float)(w4.x/w4.w),(float)(w4.y/w4.w),(float)(w4.z/w4.w)});
            else
                worldPts.push_back({(float)w4.x,(float)w4.y,(float)w4.z});
        }

        unsigned int numPrims = 0;
        try { numPrims = geo.primitives(); } catch (...) { numPrims = 0; }

        int extractedFaces = 0;
        for (unsigned int f = 0; f < numPrims; ++f) {
            const DD::Image::Primitive* prim = nullptr;
            try { prim = geo.primitive(f); } catch (...) { continue; }
            extractFacesFromPrimitive(prim, npts, baseIdx,
                                       faceCounts, faceIndices, extractedFaces);
        }

        if (extractedFaces == 0 && npts >= 3 && (npts % 3 == 0)) {
            for (unsigned int i = 0; i < npts; i += 3) {
                faceCounts.push_back(3);
                faceIndices.push_back((int)(baseIdx + i));
                faceIndices.push_back((int)(baseIdx + i + 1));
                faceIndices.push_back((int)(baseIdx + i + 2));
            }
        }
    }

    if (!worldPts.empty() && !faceCounts.empty() && sampler_) {
        sampler_->rebuild(worldPts, faceCounts, faceIndices);
        totalPointCount_ = (unsigned)worldPts.size();
    } else if (sampler_ && sampler_->isValid() && worldPts.empty()) {
        sampler_ = std::make_unique<MeshSampler>();
        if (brushKnob_) brushKnob_->setMeshSampler(sampler_.get());
    }

    geometryDirty_.store(false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  build_handles / draw_handle
// ─────────────────────────────────────────────────────────────────────────────

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
            brushKnob_->setRadiusCallback(
                [this](float r) { onRadiusChanged(r); });
        }
    }
    syncBrushStateToKnobs();
    add_draw_handle(ctx);
}

void AttributePainterOp::draw_handle(DD::Image::ViewerContext* ctx) {
    if (brushKnob_) brushKnob_->draw_handle(ctx);
}

void AttributePainterOp::onRadiusChanged(float newRadius) {
    k_radius_ = newRadius;
    DD::Image::Knob* k = knob("radius");
    if (k) k->set_value(newRadius);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Paint
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::onPaintTick(const Vec3f& pos,
                                      const Vec3f& normal,
                                      bool firstTick) {
    if (!sampler_ || !sampler_->isValid()) return;

    BrushState bs = brushKnob_->brushState();
    bs.center = pos;
    bs.normal = normal;

    std::vector<std::pair<uint32_t,float>> nearby;
    sampler_->verticesInRadius(pos, bs.radius, nearby);
    if (nearby.empty()) return;

    if (firstTick) {
        strokeBefore_.clear();
        strokeCurrent_.clear();
        strokeTouched_.clear();
    }

    for (auto& [idx, dsq] : nearby) {
        if (strokeTouched_.find(idx) == strokeTouched_.end()) {
            strokeBefore_.push_back({ idx, sampler_->getColor(idx) });
            strokeTouched_.insert(idx);
        }

        float d = std::sqrt(dsq);
        float w = BrushSystem::weight(bs, d);
        if (w <= 0.f) continue;

        Color3f src = sampler_->getColor(idx);
        Color3f dst = BrushSystem::blend(bs, src, w);
        dst = BrushSystem::saturate(dst);
        sampler_->setColor(idx, dst);

        if (writer_) writer_->stage(idx, dst);
    }

    commitToUSD();
}

void AttributePainterOp::onStrokeEnd() {
    if (!sampler_) return;
    std::vector<VertexColor> after;
    after.reserve(strokeBefore_.size());
    for (auto& bv : strokeBefore_)
        after.push_back({ bv.index, sampler_->getColor(bv.index) });
    undoStack_.beginStroke(strokeBefore_);
    undoStack_.endStroke(after);
    strokeBefore_.clear();
    strokeTouched_.clear();
}

void AttributePainterOp::commitToUSD() {
    if (!stage_ || !writer_) return;
    UsdPrim prim = stage_->GetPrimAtPath(targetPath_);
    if (!prim || !prim.IsA<UsdGeomMesh>()) return;
    UsdGeomMesh mesh(prim);
    auto& colors = const_cast<std::vector<Color3f>&>(sampler_->colors());
    writer_->commit(mesh, colors);
    writer_->clearStaged();
}

void AttributePainterOp::applyVertexColors(const std::vector<VertexColor>& vcs) {
    if (!sampler_) return;
    for (auto& vc : vcs) {
        sampler_->setColor(vc.index, vc.color);
        if (writer_) writer_->stage(vc.index, vc.color);
    }
    commitToUSD();
}

} // namespace AP

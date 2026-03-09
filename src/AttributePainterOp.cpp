#include "AttributePainterOp.h"
#include "ViewportBrushKnob.h"
#include "BrushSystem.h"

// Nuke NDK
#include <DDImage/Knobs.h>
#include <DDImage/ViewerContext.h>
#include <DDImage/gl.h>

// USG
#include <usg/geom/Stage.h>
#include <usg/geom/MeshPrim.h>
#include <usg/geom/XformCache.h>
#include <usg/api.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace AP {

// ─────────────────────────────────────────────────────────────────────────────
//  Static data
// ─────────────────────────────────────────────────────────────────────────────

const char* const AttributePainterOp::kFalloffNames[] = {
    "Smooth", "Linear", "Constant", "Gaussian", nullptr
};
const char* const AttributePainterOp::kBlendNames[] = {
    "Replace", "Add", "Subtract", "Multiply", "Smooth", nullptr
};

const DD::Image::GeomOp::Description AttributePainterOp::description(
    "AttributePainter",
    AttributePainterOp::Build
);

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

AttributePainterOp::AttributePainterOp(Node* node)
    : DD::Image::GeomOp(node, BuildEngine<Engine>())
    , sampler_(std::make_unique<MeshSampler>())
    , writer_ (std::make_unique<USDColorWriter>())
{}

AttributePainterOp::~AttributePainterOp() = default;

// ─────────────────────────────────────────────────────────────────────────────
//  Help
// ─────────────────────────────────────────────────────────────────────────────

const char* AttributePainterOp::node_help() const {
    return
        "AttributePainter\n\n"
        "Interactive vertex colour painter for USD geometry in Nuke 17.\n"
        "Connect a GeoImport/USD node, set prim path, paint in 3D viewer.\n\n"
        "  • LMB drag to paint\n"
        "  • Ctrl+Scroll to resize brush\n"
        "  • Ctrl+Z / Ctrl+Shift+Z to undo/redo\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Knobs
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::knobs(DD::Image::Knob_Callback f) {
    DD::Image::GeomOp::knobs(f);

    DD::Image::Text_knob(f, "v1.0.6");
    { std::ofstream _f("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
      _f << "knobs() called\n"; }

    DD::Image::Divider(f, "USD Target");
    DD::Image::String_knob(f, &k_primPath_,    "prim_path",    "Prim Path");
    DD::Image::String_knob(f, &k_primvarName_, "primvar_name", "Primvar Name");

    DD::Image::Divider(f, "Brush");
    DD::Image::Bool_knob (f, &k_paintEnabled_, "paint_enabled", "Enable Paint");
    DD::Image::Bool_knob (f, &k_showBrush_,   "show_brush",    "Show Brush");
    DD::Image::Float_knob(f, &k_radius_,       "radius",        "Radius");
    DD::Image::SetRange(f, 0.001, 10.0);
    DD::Image::Float_knob(f, &k_strength_,     "strength",      "Strength");
    DD::Image::SetRange(f, 0.0, 1.0);
    DD::Image::Float_knob(f, &k_hardness_,     "hardness",      "Hardness");
    DD::Image::SetRange(f, 0.0, 1.0);
    DD::Image::Enumeration_knob(f, &k_falloff_, kFalloffNames, "falloff", "Falloff");
    DD::Image::Enumeration_knob(f, &k_blend_,   kBlendNames,   "blend",   "Blend Mode");
    DD::Image::Color_knob(f, k_color_, "paint_color", "Color");

    DD::Image::Divider(f, "");
    DD::Image::Bool_knob(f, &k_flipNormals_, "flip_normals", "Flip Normals");

    CustomKnob1(ViewportBrushKnob, f, this, "brush_handle");
}

int AttributePainterOp::knob_changed(DD::Image::Knob* k) {
    if (k->is("prim_path") || k->is("primvar_name")) {
        geometryDirty_.store(true);
        return 1;
    }
    syncBrushStateToKnobs();
    return DD::Image::GeomOp::knob_changed(k);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Brush state sync
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
//  rebuildFromStage — fully usg-native, no pxr
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::rebuildGeometry() {
    DD::Image::GeomOp* gIn = input0();
    { std::ofstream _f("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
      _f << "rebuildGeometry: gIn=" << (gIn!=nullptr) << "\n"; }
    if (gIn) {
        usg::StageRef stageRef;
        usg::ArgSet   args;
        gIn->buildGeometryStage(stageRef, args);
        usgStage_ = stageRef;
        { std::ofstream _f2("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
          _f2 << "rebuildGeometry: stage valid=" << (bool)usgStage_ << "\n"; }
    }
    rebuildFromStage();
    syncBrushStateToKnobs();
}

bool AttributePainterOp::rebuildFromStage() {
    if (!usgStage_) return false;

    // Get mesh prim from usg stage
    usg::MeshPrim mesh = usg::MeshPrim::getInStage(usgStage_, usg::Path(k_primPath_));
    if (!mesh.isValid()) return false;

    // Points
    usg::Vec3fArray pts = mesh.getPoints();
    if (pts.empty()) return false;

    // World transform
    usg::XformCache xc(fdk::defaultTimeValue());
    fdk::Mat4d xform = xc.getLocalToWorldTransform(static_cast<usg::Prim>(mesh));

    std::vector<Vec3f> worldPts;
    worldPts.reserve(pts.size());
    for (auto& p : pts) {
        fdk::Vec3d pw = xform.transform(fdk::Vec3d(p[0], p[1], p[2]));
        worldPts.push_back({ (float)pw.x, (float)pw.y, (float)pw.z });
    }

    // Topology
    usg::IntArray counts  = mesh.getFaceVertexCounts();
    usg::IntArray indices = mesh.getFaceVertexIndices();

    sampler_->rebuild(worldPts,
                      std::vector<int>(counts.begin(),  counts.end()),
                      std::vector<int>(indices.begin(), indices.end()));
    if (brushKnob_) brushKnob_->setMeshSampler(sampler_.get());

    // Existing colors
    writer_ = std::make_unique<USDColorWriter>(k_primvarName_);
    auto existing = writer_->read(mesh, worldPts.size());
    if (!existing.empty()) sampler_->initColors(existing);

    geometryDirty_.store(false);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  build_handles
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::build_handles(DD::Image::ViewerContext* ctx) {
    DD::Image::GeomOp::build_handles(ctx);

    // Lazy-init brush knob
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
            brushKnob_->setRebuildCallback([this]() {
                DD::Image::GeomOp* gIn = input0();
                if (gIn) {
                    usg::StageRef stageRef;
                    usg::ArgSet   args;
                    gIn->buildGeometryStage(stageRef, args);
                    usgStage_ = stageRef;
                }
                rebuildFromStage();
                syncBrushStateToKnobs();
            });
        }
    }

    // Rebuild mesh when dirty
    if (geometryDirty_.load()) {
        DD::Image::GeomOp* gIn = input0();
        if (gIn) {
            usg::StageRef stageRef;
            usg::ArgSet   args;
            gIn->buildGeometryStage(stageRef, args);
            usgStage_ = stageRef;
        }
        rebuildFromStage();
    }

    syncBrushStateToKnobs();
    if (brushKnob_) brushKnob_->draw_handle(ctx);
}

void AttributePainterOp::draw_handle(DD::Image::ViewerContext* ctx) {
    if (brushKnob_) brushKnob_->draw_handle(ctx);
}

// ─────────────────────────────────────────────────────────────────────────────
//  onPaintTick
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
        for (auto& [idx, dsq] : nearby)
            strokeBefore_.push_back({ idx, sampler_->getColor(idx) });
    }

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

    commitToUSD();
}

// ─────────────────────────────────────────────────────────────────────────────
//  onStrokeEnd
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::onStrokeEnd() {
    if (!sampler_) return;
    std::vector<VertexColor> after;
    after.reserve(strokeBefore_.size());
    for (auto& bv : strokeBefore_)
        after.push_back({ bv.index, sampler_->getColor(bv.index) });
    undoStack_.beginStroke(strokeBefore_);
    undoStack_.endStroke(after);
    strokeBefore_.clear();
    invalidate();
}

// ─────────────────────────────────────────────────────────────────────────────
//  commitToUSD
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::commitToUSD() {
    if (!usgStage_) return;
    usg::MeshPrim mesh = usg::MeshPrim::getInStage(usgStage_, usg::Path(k_primPath_));
    if (!mesh.isValid()) return;
    auto& colors = const_cast<std::vector<Color3f>&>(sampler_->colors());
    writer_->commit(mesh, colors);
    writer_->clearStaged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  applyVertexColors (undo/redo)
// ─────────────────────────────────────────────────────────────────────────────

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

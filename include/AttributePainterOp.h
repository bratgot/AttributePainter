#pragma once

// ── Windows header guard (must precede DDImage and GL includes) ───────────────
#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// ── Nuke NDK ──────────────────────────────────────────────────────────────────
#include <DDImage/GeoOp.h>
#include <DDImage/GeomOp.h>
#include <DDImage/Knobs.h>
#include <DDImage/Enumeration_KnobI.h>
#include <DDImage/Scene.h>
#include <DDImage/GeometryList.h>
#include <DDImage/ViewerContext.h>

// USD
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>

#include "Types.h"
#include "BrushSystem.h"
#include "MeshSampler.h"
#include "USDColorWriter.h"
#include "UndoStack.h"

#include <memory>
#include <mutex>
#include <atomic>
#include <string>

namespace AP {

class ViewportBrushKnob;

// ─────────────────────────────────────────────────────────────────────────────
//  AttributePainterOp
//
//  GeoOp subclass that:
//    1. Reads a USD stage from its input (UsdNode or LookFileBake etc.)
//    2. Builds a MeshSampler from the target prim's points/faces
//    3. Adds a ViewportBrushKnob to paint vertex colors interactively
//    4. Writes back displayColor primvar on the USD prim at each stroke commit
//    5. Supports full undo/redo via UndoStack
//
//  Nuke 17 USD pipeline note:
//    Geometry flows through the node graph as UsdStageRefPtr payloads.
//    We fish the stage out of the input GeoOp's GeometryList by casting to
//    the internal UsdStageData attachment (see extractStageFromInput()).
// ─────────────────────────────────────────────────────────────────────────────
class AttributePainterOp : public DD::Image::GeoOp {
public:
    static const DD::Image::Op::Description description;

    explicit AttributePainterOp(Node* node);
    ~AttributePainterOp() override;

    // ── Op interface ─────────────────────────────────────────────────────────
    const char* Class()     const override { return "AttributePainter"; }
    const char* node_help() const override;
    void        knobs(DD::Image::Knob_Callback f) override;
    int         knob_changed(DD::Image::Knob* k) override;

    // GeoOp interface
    void geometry_engine(DD::Image::Scene& scene,
                         DD::Image::GeometryList& out) override;
    void _validate(bool for_real) override;
        void build_handles   (DD::Image::ViewerContext* ctx) override;
    void draw_handle     (DD::Image::ViewerContext* ctx) override;

    // Called from the knob callbacks
    void onPaintTick  (const Vec3f& pos, const Vec3f& normal, bool firstTick);
    void onStrokeEnd  ();

    // Static factory
    bool test_input(int input, DD::Image::Op* op) const override {
        return dynamic_cast<DD::Image::GeoOp*>(op) != nullptr
            || dynamic_cast<DD::Image::GeomOp*>(op) != nullptr;
    }
    static DD::Image::Op* Build(Node* node) {
        return new AttributePainterOp(node);
    }

private:
    // ── Knob storage (mirrored from Nuke knob system) ─────────────────────
    float    k_radius_    = 0.05f;
    float    k_strength_  = 1.0f;
    float    k_hardness_  = 0.5f;
    float    k_color_[3]  = {1.f, 0.f, 0.f};
    int      k_falloff_   = 0;
    int      k_blend_     = 0;
    bool     k_paintEnabled_ = true;
    bool     k_showBrush_    = true;
    std::string k_primPath_ = "/";      // USD prim path to paint on
    std::string k_primvarName_ = "displayColor";
    bool     k_flipNormals_  = false;

    // ── Internal state ────────────────────────────────────────────────────
    std::unique_ptr<MeshSampler>    sampler_;
    std::unique_ptr<USDColorWriter> writer_;
    UndoStack                       undoStack_;

    // The USD stage we're modifying (owned by the input UsdNode)
    PXR_NS::UsdStageRefPtr             stage_;
    PXR_NS::SdfPath                    targetPath_;

    // Knob pointer (owned by the Op's knob list)
    ViewportBrushKnob*              brushKnob_ = nullptr;

    // Dirty flags
    std::atomic<bool>               geometryDirty_{true};
    std::atomic<bool>               colorsDirty_  {false};

    // Stroke accumulator: collects vertex changes within one stroke
    std::vector<VertexColor>        strokeBefore_;
    std::vector<VertexColor>        strokeCurrent_;

    mutable std::mutex              dataMutex_; // protects sampler_ colors

    // ── Helpers ───────────────────────────────────────────────────────────
    bool  rebuildGeometry();
    bool  extractStageFromInput(DD::Image::GeometryList& geoList);
    void  syncBrushStateToKnobs();
    void  applyVertexColors(const std::vector<VertexColor>& vcs);
    void  commitToUSD();

    static const char* const kFalloffNames[];
    static const char* const kBlendNames[];
};

} // namespace AP

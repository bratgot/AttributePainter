#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include <DDImage/GeoOp.h>
#include <DDImage/GeomOp.h>
#include <DDImage/Knobs.h>
#include <DDImage/Enumeration_KnobI.h>
#include <DDImage/Scene.h>
#include <DDImage/GeometryList.h>
#include <DDImage/ViewerContext.h>

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
#include <unordered_set>

namespace AP {

class ViewportBrushKnob;

class AttributePainterOp : public DD::Image::GeoOp {
public:
    static const DD::Image::Op::Description description;

    explicit AttributePainterOp(Node* node);
    ~AttributePainterOp() override;

    const char* Class()     const override { return "AttributePainter"; }
    const char* node_help() const override;
    void        knobs(DD::Image::Knob_Callback f) override;
    int         knob_changed(DD::Image::Knob* k) override;

    void geometry_engine(DD::Image::Scene& scene,
                         DD::Image::GeometryList& out);
    void _validate(bool for_real) override;
    void build_handles(DD::Image::ViewerContext* ctx) override;
    void draw_handle  (DD::Image::ViewerContext* ctx) override;

    void onPaintTick(const Vec3f& pos, const Vec3f& normal, bool firstTick);
    void onStrokeEnd();
    void onRadiusChanged(float newRadius);

    bool test_input(int input, DD::Image::Op* op) const override {
        return dynamic_cast<DD::Image::GeoOp*>(op) != nullptr
            || dynamic_cast<DD::Image::GeomOp*>(op) != nullptr;
    }
    static DD::Image::Op* Build(Node* node) {
        return new AttributePainterOp(node);
    }

private:
    float    k_radius_    = 0.05f;
    float    k_strength_  = 1.0f;
    float    k_hardness_  = 0.5f;
    float    k_color_[3]  = {1.f, 0.f, 0.f};
    int      k_falloff_   = 0;
    int      k_blend_     = 0;
    bool     k_paintEnabled_ = true;
    bool     k_showBrush_    = true;
    bool     k_debug_        = false;
    std::string k_primPath_ = "/";
    std::string k_primvarName_ = "displayColor";
    bool     k_flipNormals_  = false;

    std::unique_ptr<MeshSampler>    sampler_;
    std::unique_ptr<USDColorWriter> writer_;
    UndoStack                       undoStack_;

    PXR_NS::UsdStageRefPtr          stage_;
    PXR_NS::SdfPath                 targetPath_;

    ViewportBrushKnob*              brushKnob_ = nullptr;

    std::atomic<bool>               geometryDirty_{true};
    std::atomic<bool>               colorsDirty_  {false};

    std::vector<VertexColor>        strokeBefore_;
    std::vector<VertexColor>        strokeCurrent_;
    std::unordered_set<uint32_t>    strokeTouched_;

    mutable std::mutex              dataMutex_;
    unsigned                        totalPointCount_ = 0;

    bool  rebuildGeometry();
    bool  extractStageFromInput(DD::Image::GeometryList& geoList);
    void  syncBrushStateToKnobs();
    void  applyVertexColors(const std::vector<VertexColor>& vcs);
    void  commitToUSD();

    static const char* const kFalloffNames[];
    static const char* const kBlendNames[];
};

} // namespace AP

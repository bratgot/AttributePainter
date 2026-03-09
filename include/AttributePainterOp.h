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

// Nuke NDK - GeomOp (new geometry system, connects to ScanlineRender2)
#include <DDImage/GeomOp.h>
#include <DDImage/Knobs.h>
#include <DDImage/ViewerContext.h>

// USG
#include <usg/geom/Stage.h>
#include <usg/geom/MeshPrim.h>
#include <usg/engine/GeomSceneContext.h>

#include "Types.h"
#include "MeshSampler.h"
#include "USDColorWriter.h"
#include "UndoStack.h"

#include <atomic>
#include <fstream>
#include <memory>
#include <vector>
#include <string>

namespace AP {

class ViewportBrushKnob;

class AttributePainterOp : public DD::Image::GeomOp
{
public:
    // ── Inner Engine ───────────────────────────────────────────────────────
    class Engine : public DD::Image::GeomOpEngine
    {
    public:
        Engine(DD::Image::GeomOpNode* parent) : GeomOpEngine(parent) {}
        void processScenegraph(usg::GeomSceneContext& context) override {
            GeomOpEngine::processScenegraph(context);
        }
    };

    // ── Description ───────────────────────────────────────────────────────
    static const DD::Image::GeomOp::Description description;
    static DD::Image::Op* Build(Node* node) { return new AttributePainterOp(node); }

    const char* Class()     const override { return description.name; }
    const char* node_help() const override;

    explicit AttributePainterOp(Node* node);
    ~AttributePainterOp() override;

    void knobs(DD::Image::Knob_Callback f) override;
    int  knob_changed(DD::Image::Knob* k) override;

    DD::Image::Op::HandlesMode doAnyHandles(DD::Image::ViewerContext* ctx) override {
        if (ctx->transform_mode() != DD::Image::VIEWER_2D) return eHandlesCooked;
        return DD::Image::GeomOp::doAnyHandles(ctx);
    }
    void build_handles(DD::Image::ViewerContext* ctx) override;
    void rebuildGeometry();  // called by ViewportBrushKnob when sampler invalid
    MeshSampler* getSampler() { return sampler_.get(); }
    void draw_handle  (DD::Image::ViewerContext* ctx) override;

protected:

    void onPaintTick (const Vec3f& pos, const Vec3f& normal, bool firstTick);
    void onStrokeEnd ();
    void applyVertexColors(const std::vector<VertexColor>& vcs);

    static const char* const kFalloffNames[];
    static const char* const kBlendNames[];

private:
    // Knob storage
    const char* k_primPath_    = "/World/Mesh";
    const char* k_primvarName_ = "displayColor";
    bool        k_paintEnabled_= true;
    bool        k_showBrush_   = true;
    float       k_radius_      = 0.05f;
    float       k_strength_    = 1.0f;
    float       k_hardness_    = 0.5f;
    int         k_falloff_     = 0;
    int         k_blend_       = 0;
    float       k_color_[3]    = {1.f, 1.f, 1.f};
    bool        k_flipNormals_ = false;

    // Stage
    usg::StageRef  usgStage_;

    // Painting
    std::unique_ptr<MeshSampler>    sampler_;
    std::unique_ptr<USDColorWriter> writer_;
    UndoStack                       undoStack_;
    ViewportBrushKnob*              brushKnob_  = nullptr;
    std::atomic<bool>               geometryDirty_{true};
    std::vector<VertexColor>        strokeBefore_;

    void syncBrushStateToKnobs();
    void commitToUSD();
    bool rebuildFromStage();
};

} // namespace AP

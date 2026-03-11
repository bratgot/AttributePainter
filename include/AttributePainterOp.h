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
        AttributePainterOp* op_ = nullptr;
        Engine(DD::Image::GeomOpNode* parent) : GeomOpEngine(parent) {}
        void processScenegraph(usg::GeomSceneContext& context) override {
            fprintf(stderr, "=== processScenegraph called ===\n");
            GeomOpEngine::processScenegraph(context);
            if (!op_) op_ = dynamic_cast<AttributePainterOp*>(firstOp());
            if (!op_ || !op_->sampler_ || !op_->sampler_->isValid()) return;
            // Use context stage, fall back to op stage
            const usg::StageRef& ctxStage = context.stage();
            usg::MeshPrim mesh = usg::MeshPrim::getInStage(ctxStage, usg::Path(op_->k_primPath_));
            if (!mesh.isValid() && op_->usgStage_)
                mesh = usg::MeshPrim::getInStage(op_->usgStage_, usg::Path(op_->k_primPath_));
            { std::ofstream _f("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
              _f << "processScenegraph: ctxStage=" << (bool)ctxStage << " meshValid=" << mesh.isValid() << " colors=" << op_->sampler_->colors().size() << "\n"; }
            if (!mesh.isValid()) return;
            { std::ofstream _f("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
              const auto& cc = op_->sampler_->colors();
              if (!cc.empty()) _f << "  color[0]=" << cc[0].r << "," << cc[0].g << "," << cc[0].b << "\n"; }
            const auto& colors = op_->sampler_->colors();
            usg::Vec3fArray vtColors(colors.size());
            for (size_t i = 0; i < colors.size(); ++i)
                vtColors[i] = fdk::Vec3f(colors[i].r, colors[i].g, colors[i].b);
            // Set vertex interpolation via PrimvarsAPI
            usg::PrimvarsAPI pvAPI(static_cast<usg::Prim>(mesh));
            usg::Primvar pv = pvAPI.createPrimvar(
                usg::Token("displayColor"),
                usg::Value::Type::Color3fArray,
                usg::GeomTokens.vertex);
            if (pv.isValid()) {
                pv.attribute().setValue(vtColors);
            } else {
                mesh.setDisplayColor(vtColors);
            }
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
    void autoDetectPrimPath();
    MeshSampler* getSampler() { return sampler_.get(); }
    void draw_handle  (DD::Image::ViewerContext* ctx) override;

protected:

    void onPaintTick (const Vec3f& pos, const Vec3f& normal, bool firstTick);
    void onStrokeEnd ();
    void applyVertexColors(const std::vector<VertexColor>& vcs);

    static const char* const kFalloffNames[];
    static const char* const kBlendNames[];

public:
    // Knob storage
    std::string k_primPath_    = "/World/Mesh";
    std::string k_primvarName_ = "displayColor";
    bool        k_paintEnabled_= true;
    bool        k_showBrush_   = true;
    float       k_radius_      = 0.05f;
    float       k_strength_    = 1.0f;
    float       k_hardness_    = 0.5f;
    int         k_falloff_     = 0;
    int         k_blend_       = 0;
    float       k_color_[3]    = {1.f, 1.f, 1.f};
    bool        k_flipNormals_ = false;
    const char* k_notes_       = "";

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

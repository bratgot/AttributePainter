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
        usg::StageRef cachedContextStage_;  // Cached for direct writes during painting

        Engine(DD::Image::GeomOpNode* parent) : GeomOpEngine(parent) {}
        void processScenegraph(usg::GeomSceneContext& context) override {
            GeomOpEngine::processScenegraph(context);
            if (!op_) op_ = dynamic_cast<AttributePainterOp*>(firstOp());
            if (!op_) return;

            // Cache the context stage and engine pointer for direct writes during painting
            cachedContextStage_ = context.stage();
            op_->engine_ = this;

            // When disabled, simply don't write colours — context stage passes through clean
            if (op_->node_disabled()) {
                return;
            }

            if (!op_->sampler_ || !op_->sampler_->isValid()) return;

            writeColorsToContextStage();
        }

        // Write current sampler colors to the cached context stage.
        // Called from processScenegraph AND from onPaintTick (live painting).
        bool writeColorsToContextStage() {
            if (!op_ || !op_->sampler_) return false;
            return writeColors(op_->sampler_->colors());
        }

        // Write a specific color array to the cached context stage.
        // Used to restore original colors when the node is disabled.
        bool writeColors(const std::vector<Color3f>& colors) {
            if (!cachedContextStage_ || !op_) return false;

            usg::MeshPrim mesh = usg::MeshPrim::getInStage(
                cachedContextStage_, usg::Path(op_->k_primPath_));
            if (!mesh.isValid() && op_->usgStage_)
                mesh = usg::MeshPrim::getInStage(
                    op_->usgStage_, usg::Path(op_->k_primPath_));
            if (!mesh.isValid()) return false;

            if (colors.empty()) {
                // Write empty array to clear the primvar display
                mesh.setDisplayColor(usg::Vec3fArray());
                return true;
            }

            usg::Vec3fArray vtColors(colors.size());
            for (size_t i = 0; i < colors.size(); ++i)
                vtColors[i] = fdk::Vec3f(colors[i].r, colors[i].g, colors[i].b);

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
            return true;
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
    void append(DD::Image::Hash& hash) override {
        DD::Image::GeomOp::append(hash);
        hash.append(paintVersion_.load());
    }

    DD::Image::Op::HandlesMode doAnyHandles(DD::Image::ViewerContext* ctx) override {
        if (ctx->transform_mode() != DD::Image::VIEWER_2D) return eHandlesCooked;
        return DD::Image::GeomOp::doAnyHandles(ctx);
    }
    void build_handles(DD::Image::ViewerContext* ctx) override;
    void rebuildGeometry();  // called by ViewportBrushKnob when sampler invalid
    void autoDetectPrimPath();
    MeshSampler* getSampler() { return sampler_.get(); }
    void draw_handle  (DD::Image::ViewerContext* ctx) override;

    // Check if the input is actually producing geometry (not disabled/disconnected)
    bool isInputActive() {
        DD::Image::GeomOp* gIn = input0();
        if (!gIn) return false;
        usg::StageRef testStage;
        usg::ArgSet testArgs;
        gIn->buildGeometryStage(testStage, testArgs);
        if (!testStage) return false;
        usg::MeshPrim mesh = usg::MeshPrim::getInStage(testStage, usg::Path(k_primPath_));
        return mesh.isValid();
    }

protected:

    void onPaintTick (const Vec3f& pos, const Vec3f& normal, bool firstTick);
    void onStrokeEnd ();
    void applyVertexColors(const std::vector<VertexColor>& vcs);

    static const char* const kFalloffNames[];
    static const char* const kBlendNames[];
    static const char* const kSaveFormatNames[];
    void saveColors();
    void loadColors();
    void saveUSD(const std::string& path);
    void saveJSON(const std::string& path);
    void loadUSD(const std::string& path);
    void loadJSON(const std::string& path);

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
    bool        k_showVertices_= false;
    int         k_saveFormat_  = 1;
    bool        k_autoSave_    = false;
    const char* k_notes_       = "";

    // Stage
    usg::StageRef  usgStage_;

    // Painting
    std::unique_ptr<MeshSampler>    sampler_;
    std::unique_ptr<USDColorWriter> writer_;
    UndoStack                       undoStack_;
    ViewportBrushKnob*              brushKnob_  = nullptr;
    Engine*                         engine_     = nullptr;  // Cached by processScenegraph
    std::atomic<bool>               geometryDirty_{true};
    std::atomic<uint32_t>           paintVersion_{0};
    std::vector<VertexColor>        strokeBefore_;
    std::vector<Color3f>            originalColors_;  // Pre-paint state to restore on disable
    bool                            hadOriginalColors_ = false;
    bool                            wasDisabled_ = false;  // Track disable state transitions
    DD::Image::Hash                 lastInputHash_;  // Detect input changes (transform, topology)

    void syncBrushStateToKnobs();
    void commitToUSD();
    void pushColorsToHydra();  // Write sampler colors directly to cached context stage
    void restoreOriginalColors(); // Write original colors back to context stage (on disable)
    bool rebuildFromStage();
};

} // namespace AP

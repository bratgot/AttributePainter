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

#include <DDImage/Knob.h>
#include <DDImage/Knobs.h>
#include <DDImage/ViewerContext.h>
#include <DDImage/OutputContext.h>
#include <DDImage/GeoOp.h>

#include <GL/glew.h>

#include "Types.h"
#include "BrushSystem.h"
#include "MeshSampler.h"
#include "UndoStack.h"
#include "USDColorWriter.h"

#include <functional>
#include <memory>

namespace AP {

class AttributePainterOp;

class ViewportBrushKnob : public DD::Image::Knob {
public:
    using PaintCallback     = std::function<void(const Vec3f& hitPos,
                                                  const Vec3f& hitNormal,
                                                  bool  isFirstTick)>;
    using StrokeEndCallback = std::function<void()>;
    using RadiusCallback    = std::function<void(float newRadius)>;
    using RebuildCallback   = std::function<void()>;

    ViewportBrushKnob(DD::Image::Knob_Closure* kc, AttributePainterOp* op, const char* name);
    ~ViewportBrushKnob() override = default;

    const char* Class() const override { return "ViewportBrushKnob"; }
    bool not_default() const override { return false; }
    void to_script(std::ostream&, const DD::Image::OutputContext*, bool) const override {}
    bool from_script(const char*) override { return false; }

    bool build_handle(DD::Image::ViewerContext* ctx) override {
        return true;
    }
    void draw_handle(DD::Image::ViewerContext* ctx) override;

    void setBrushState(const BrushState& bs) { brushState_ = bs; }
    const BrushState& brushState() const { return brushState_; }

    void setPaintCallback(PaintCallback cb)         { onPaint_     = std::move(cb); }
    void setStrokeEndCallback(StrokeEndCallback cb) { onStrokeEnd_ = std::move(cb); }
    void setRadiusCallback(RadiusCallback cb)       { onRadius_    = std::move(cb); }
    void setRebuildCallback(RebuildCallback cb)      { onRebuild_   = std::move(cb); }

    void setMeshSampler(MeshSampler* ms)      { sampler_ = ms; }
    void setShowVertices(bool s) { showVertices_ = s; }
    void setEnabled(bool e)                    { enabled_ = e; }
    void setDebug(bool d)                      { debug_ = d; }

private:
    AttributePainterOp* op_      = nullptr;
    MeshSampler*        sampler_ = nullptr;
    BrushState          brushState_;
    bool                showVertices_= false;
    bool                enabled_     = true;
    bool                painting_    = false;
    bool                firstTick_   = true;
    bool                debug_       = false;
    bool                inputActive_ = true;  // False when input geo not producing geometry

    PaintCallback       onPaint_;
    StrokeEndCallback   onStrokeEnd_;
    RadiusCallback      onRadius_;
    RebuildCallback     onRebuild_;

    float               mouseGLX_ = 0.f;
    float               mouseGLY_ = 0.f;
    float               nukeMouseX_ = 0.f;
    float               nukeMouseY_ = 0.f;

    HitResult           lastHit_;
    bool                hitValid_ = false;

    Vec3f               smoothedNormal_ = {0.f, 1.f, 0.f};
    bool                hasSmoothedNormal_ = false;

    // Shift+drag brush resize state
    bool                resizing_      = false;
    float               resizeStartX_  = 0.f;
    float               resizeStartRadius_ = 0.f;
    float               resizeLockScreenX_ = 0.f;  // Screen position where resize started
    float               resizeLockScreenY_ = 0.f;
    Vec3f               resizeLockWorldPos_ = {};   // World position locked during resize

    double              cachedMV_[16];
    double              cachedProj_[16];
    double              prevMV_[16];        // Previous frame's modelview for camera-change detection
    bool                prevMVValid_ = false;
    GLint               cachedVP_[4] = {};
    bool                matricesCached_ = false;

    Ray                 debugRay_ = {{0,0,0},{0,0,-1}};
    int                 debugFrame_ = 0;

    void cacheGLMatrices();
    void updateMouseFromWin32();
    void updateHit();
    Ray  buildRay(float glX, float glY) const;
    void drawBrushCircle() const;
    void drawDebugOverlay() const;
    void drawPaintedVertices() const;

    bool projectToScreen(const Vec3f& world, float& screenX, float& screenY) const;
};

} // namespace AP

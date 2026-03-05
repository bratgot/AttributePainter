#pragma once

// â”€â”€ Windows headers MUST come before any GL includes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Including GL/glew.h or windows.h in the wrong order causes redefinition errors.
#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX          // prevent windows.h polluting std::min/max
#  endif
#  include <windows.h>
#endif

// â”€â”€ Nuke NDK â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#include <DDImage/Knob.h>
#include <DDImage/Knobs.h>
#include <DDImage/ViewerContext.h>
#include <DDImage/OutputContext.h>
#include <DDImage/GeoOp.h>

// â”€â”€ GL â€” Nuke ships GLEW; must be included after windows.h â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#include <GL/glew.h>

#include "Types.h"
#include "BrushSystem.h"
#include "MeshSampler.h"
#include "UndoStack.h"
#include "USDColorWriter.h"

#include <functional>
#include <memory>

namespace AP {

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  ViewportBrushKnob
//  Custom DDImage::Knob that:
//   â€¢ Draws the 3D brush circle overlay in the viewer
//   â€¢ Receives mouse events (hover, LMB drag = paint, RMB = pick)
//   â€¢ Calls back into AttributePainterOp to apply paint
//
    //  Nuke's handle system:  draw_handle() for rendering, hit() for picking,
//  and knob_changed() plumbing for undo integration.
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

class AttributePainterOp; // forward

class ViewportBrushKnob : public DD::Image::Knob {
public:
    // Callback invoked when the user paints a tick (moved mouse while LMB held)
    using PaintCallback = std::function<void(const Vec3f& hitPos,
                                              const Vec3f& hitNormal,
                                              bool  isFirstTick)>;
    // Callback when stroke ends (LMB release)
    using StrokeEndCallback = std::function<void()>;
    ViewportBrushKnob(DD::Image::Knob_Closure* kc, AttributePainterOp* op, const char* name);

    ~ViewportBrushKnob() override = default;

    // Knob interface
    const char* Class() const override { return "ViewportBrushKnob"; }
    bool not_default() const override { return false; }
    void to_script(std::ostream&, const DD::Image::OutputContext*, bool) const override {}
    bool from_script(const char*) override { return false; }

    // Called every viewer redraw â€” draw the brush circle
    bool build_handle(DD::Image::ViewerContext* ctx) override;
    void draw_handle(DD::Image::ViewerContext* ctx) override;

    // Setters called from the Op when knob values change
    void setBrushState(const BrushState& bs) { brushState_ = bs; }
    const BrushState& brushState() const { return brushState_; }

    void setPaintCallback(PaintCallback cb)     { onPaint_     = std::move(cb); }
    void setStrokeEndCallback(StrokeEndCallback cb) { onStrokeEnd_ = std::move(cb); }

    void setMeshSampler(MeshSampler* ms) { sampler_ = ms; }
    void setEnabled(bool e) { enabled_ = e; }

private:
    AttributePainterOp* op_     = nullptr;
    MeshSampler*        sampler_= nullptr;
    BrushState          brushState_;
    bool                enabled_    = true;
    bool                painting_   = false; // LMB held
    bool                firstTick_  = true;

    PaintCallback       onPaint_;
    StrokeEndCallback   onStrokeEnd_;

    // Last known screen-space mouse position (for hover ray)
    float               mouseX_    = 0.f;
    float               mouseY_    = 0.f;

    // Last hit for overlay drawing
    HitResult           lastHit_;
    bool                hitValid_   = false;

    // Helpers
    Ray       buildRay(DD::Image::ViewerContext* ctx, float sx, float sy) const;
    void      updateHit(DD::Image::ViewerContext* ctx);
    void handleMouseMove(DD::Image::ViewerContext* ctx);
    void handleMouseDown(DD::Image::ViewerContext* ctx);
    void handleMouseDrag(DD::Image::ViewerContext* ctx);
    void handleMouseUp(DD::Image::ViewerContext* ctx);
    void handleMouseScroll(DD::Image::ViewerContext* ctx);
    void      drawBrushCircle(DD::Image::ViewerContext* ctx) const;

    // Draw a filled circle on the mesh surface using geo-projected points
    void      drawCircleOnSurface(const Vec3f& center,
                                   const Vec3f& normal,
                                   float radius) const;
};

} // namespace AP



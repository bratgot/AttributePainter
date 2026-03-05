#include "ViewportBrushKnob.h"
#include "AttributePainterOp.h"
#include <fdk/math/Mat4.h>   // fdk::Mat4d for camera worldTransform()
#include <DDImage/AxisOp.h>
#include <DDImage/ViewerContext.h>
#include <DDImage/Matrix4.h>
#include <DDImage/Vector3.h>
#include <DDImage/gl.h>

#include <cmath>
#include <algorithm>

// ── Portable PI ───────────────────────────────────────────────────────────────
#ifndef AP_PIf
static constexpr float AP_PIf = 3.14159265358979323846f;
#endif

namespace AP {

// Number of segments for the brush circle overlay
static constexpr int BRUSH_CIRCLE_SEGS = 64;

ViewportBrushKnob::ViewportBrushKnob(DD::Image::Knob_Closure* kc, AttributePainterOp* op, const char* name)
    : DD::Image::Knob(kc, name), op_(op) {}

// ─────────────────────────────────────────────────────────────────────────────
//  Ray building from screen coordinates using Nuke's ViewerContext
// ─────────────────────────────────────────────────────────────────────────────

Ray ViewportBrushKnob::buildRay(DD::Image::ViewerContext* ctx,
                                 float sx, float sy) const {
    // Viewport dimensions
    int vpW = ctx->viewport().w();
    int vpH = ctx->viewport().h();

    // Normalised Device Coordinates [-1,1]
    float ndcX = (2.f * sx / float(vpW)) - 1.f;
    float ndcY = 1.f - (2.f * sy / float(vpH)); // flip Y

    // Fetch projection + modelview from ctx
    DD::Image::Matrix4 proj     = ctx->proj_matrix();
    DD::Image::Matrix4 modelview = DD::Image::Matrix4::identity();
    if (ctx->camera()) {
        const fdk::Mat4d& wt = ctx->camera()->worldTransform();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                modelview[r][c] = (float)wt[r][c];
    }
    DD::Image::Matrix4 invPV    = (proj * modelview).inverse();

    // Near point (z=-1 in NDC)
    DD::Image::Vector4 nearPt = invPV * DD::Image::Vector4(ndcX, ndcY, -1.f, 1.f);
    // Far point  (z=+1 in NDC)
    DD::Image::Vector4 farPt  = invPV * DD::Image::Vector4(ndcX, ndcY,  1.f, 1.f);

    DD::Image::Vector3 near3(nearPt.x/nearPt.w, nearPt.y/nearPt.w, nearPt.z/nearPt.w);
    DD::Image::Vector3 far3 ( farPt.x/ farPt.w,  farPt.y/ farPt.w,  farPt.z/ farPt.w);
    DD::Image::Vector3 dir  = far3 - near3;
    float len = dir.length();
    if (len > 1e-8f) dir /= len;

    return Ray{ {near3.x, near3.y, near3.z}, {dir.x, dir.y, dir.z} };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Update hover hit point
// ─────────────────────────────────────────────────────────────────────────────

void ViewportBrushKnob::updateHit(DD::Image::ViewerContext* ctx) {
    hitValid_ = false;
    if (!sampler_ || !sampler_->isValid()) return;

    Ray ray = buildRay(ctx, mouseX_, mouseY_);
    lastHit_ = sampler_->intersect(ray);
    hitValid_ = lastHit_.hit;

    // Keep brush center updated so the op can draw it
    if (hitValid_) brushState_.center = lastHit_.position;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mouse events
// ─────────────────────────────────────────────────────────────────────────────

void ViewportBrushKnob::handleMouseMove(DD::Image::ViewerContext* ctx) {
    if (!enabled_) return;
    mouseX_ = (float)ctx->mouse_x();
    mouseY_ = (float)ctx->mouse_y();
    updateHit(ctx);
}

void ViewportBrushKnob::handleMouseDown(DD::Image::ViewerContext* ctx) {
    if (!enabled_) return;
    if (ctx->button() != DD::Image::LeftButton) return;

    mouseX_ = (float)ctx->mouse_x();
    mouseY_ = (float)ctx->mouse_y();
    updateHit(ctx);

    if (hitValid_) {
        painting_  = true;
        firstTick_ = true;
        if (onPaint_) onPaint_(lastHit_.position, lastHit_.normal, true);
        firstTick_ = false;
    }
}

void ViewportBrushKnob::handleMouseDrag(DD::Image::ViewerContext* ctx) {
    if (!enabled_ || !painting_) return;

    mouseX_ = (float)ctx->mouse_x();
    mouseY_ = (float)ctx->mouse_y();
    updateHit(ctx);

    if (hitValid_ && onPaint_)
        onPaint_(lastHit_.position, lastHit_.normal, false);
}

void ViewportBrushKnob::handleMouseUp(DD::Image::ViewerContext* ctx) {
    if (!painting_) return;
    painting_ = false;
    if (onStrokeEnd_) onStrokeEnd_();
}

void ViewportBrushKnob::handleMouseScroll(DD::Image::ViewerContext* ctx) {
    if (!enabled_) return;
    // Ctrl+scroll → resize brush
    if (ctx->state() & DD::Image::CTRL) {
        float delta = (ctx->wheel_dy() > 0) ? 1.1f : (1.f/1.1f);
        brushState_.radius = std::clamp(brushState_.radius * delta, 0.001f, 100.0f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  GL Overlay — build_handle + draw_handle
//
//  IMPORTANT: For Nuke to route mouse events (PUSH, DRAG, RELEASE) to a
//  custom knob, the knob must register a pickable handle region during the
//  draw pass using begin_handle / end_handle (or make_handle).
//
//  Without this registration, all mouse events go to Nuke's default viewer
//  navigation (tumble/pan/zoom) and the brush never receives input.
//
//  The flow:
//    1. build_handle() returns true → Nuke knows to call draw_handle()
//    2. During DRAW_LINES/DRAW_OPAQUE passes, we call begin_handle() to
//       register our pick region, draw our overlay, then end_handle()
//    3. When the user clicks/drags over our registered region, Nuke routes
//       PUSH/DRAG/RELEASE events to our draw_handle()
// ─────────────────────────────────────────────────────────────────────────────

bool ViewportBrushKnob::build_handle(DD::Image::ViewerContext* ctx) {
    // Return true so Nuke registers draw_handle as a callback.
    // This is the registration phase — no GL calls here.
    return enabled_;
}

void ViewportBrushKnob::draw_handle(DD::Image::ViewerContext* ctx) {
    using namespace DD::Image;
    const ViewerEvent ev = ctx->event();

    // ── Drawing passes ──────────────────────────────────────────────────────
    // During draw passes, register a pickable handle region so that Nuke
    // routes subsequent mouse events to us instead of the viewer navigation.
    if (ev == DRAW_OPAQUE || ev == DRAW_LINES || ev == DRAW_OVERLAY) {
        // Register pick region on ALL draw passes so Nuke routes mouse
        // events to us, but only render the overlay during DRAW_OVERLAY.
        // DRAW_OVERLAY runs after all opaque geometry is rendered, so
        // with depth testing disabled the brush always draws on top.
        begin_handle(Knob::ANYWHERE, ctx, nullptr, 0, 0, 0);

        if (ev == DRAW_OVERLAY && hitValid_)
            drawBrushCircle(ctx);

        end_handle(ctx);
        return;
    }

    // ── Mouse event dispatch ────────────────────────────────────────────────
    // These events only arrive if we successfully registered a pick region
    // above during a prior draw pass.
    if (ev == MOVE || ev == HOVER_MOVE) { handleMouseMove(ctx);   return; }
    if (ev == PUSH)                     { handleMouseDown(ctx);   return; }
    if (ev == DRAG)                     { handleMouseDrag(ctx);   return; }
    if (ev == RELEASE)                  { handleMouseUp(ctx);     return; }
}

void ViewportBrushKnob::drawBrushCircle(DD::Image::ViewerContext* ctx) const {
    const Vec3f& c = brushState_.center;
    const Vec3f  n = lastHit_.normal;

    // Build an orthonormal basis on the surface (tangent, bitangent)
    Vec3f up = { 0.f, 1.f, 0.f };
    if (std::abs(n.dot(up)) > 0.99f) up = {1.f, 0.f, 0.f};

    // Gram-Schmidt
    float d = n.dot(up);
    Vec3f t = { up.x - n.x*d, up.y - n.y*d, up.z - n.z*d };
    float tlen = std::sqrt(t.lengthSq());
    if (tlen < 1e-8f) return;
    t = t * (1.f / tlen);

    // Bitangent = n × t
    Vec3f b = { n.y*t.z - n.z*t.y,
                n.z*t.x - n.x*t.z,
                n.x*t.y - n.y*t.x };

    float r = brushState_.radius;

    // ── Outer ring ──────────────────────────────────────────────────────────
    glPushAttrib(GL_LINE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT);
    glDisable(GL_DEPTH_TEST);   // always draw on top of mesh geometry
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);
    glColor4f(1.f, 1.f, 1.f, 0.9f);

    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < BRUSH_CIRCLE_SEGS; ++i) {
        float theta = (float)i / (float)BRUSH_CIRCLE_SEGS * 2.f * AP_PIf;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        Vec3f pt = {
            c.x + (t.x * cosT + b.x * sinT) * r,
            c.y + (t.y * cosT + b.y * sinT) * r,
            c.z + (t.z * cosT + b.z * sinT) * r,
        };
        // Offset slightly along normal to avoid z-fighting
        const float BIAS = 0.0005f;
        glVertex3f(pt.x + n.x*BIAS, pt.y + n.y*BIAS, pt.z + n.z*BIAS);
    }
    glEnd();

    // ── Inner ring (hardness) ────────────────────────────────────────────────
    float innerR = r * brushState_.hardness;
    glColor4f(1.f, 1.f, 0.f, 0.5f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < BRUSH_CIRCLE_SEGS; ++i) {
        float theta = (float)i / (float)BRUSH_CIRCLE_SEGS * 2.f * AP_PIf;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        Vec3f pt = {
            c.x + (t.x * cosT + b.x * sinT) * innerR,
            c.y + (t.y * cosT + b.y * sinT) * innerR,
            c.z + (t.z * cosT + b.z * sinT) * innerR,
        };
        const float BIAS = 0.0005f;
        glVertex3f(pt.x + n.x*BIAS, pt.y + n.y*BIAS, pt.z + n.z*BIAS);
    }
    glEnd();

    // ── Normal tick ──────────────────────────────────────────────────────────
    glColor4f(0.3f, 0.8f, 1.f, 0.8f);
    glBegin(GL_LINES);
        glVertex3f(c.x, c.y, c.z);
        glVertex3f(c.x + n.x*r*0.5f,
                   c.y + n.y*r*0.5f,
                   c.z + n.z*r*0.5f);
    glEnd();

    glPopAttrib();
}

} // namespace AP

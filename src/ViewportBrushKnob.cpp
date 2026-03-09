#include "ViewportBrushKnob.h"
#include <fstream>
#include "AttributePainterOp.h"
#include <DDImage/ViewerContext.h>
#include <DDImage/Vector3.h>
#include <DDImage/gl.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

#ifndef AP_PIf
static constexpr float AP_PIf = 3.14159265358979323846f;
#endif

namespace AP {

static constexpr int BRUSH_CIRCLE_SEGS = 64;

// ── Raw 4x4 column-major double matrix helpers ──────────────────────────────

static void mat4Mul(const double A[16], const double B[16], double out[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c*4+r] = A[0*4+r]*B[c*4+0] + A[1*4+r]*B[c*4+1]
                       + A[2*4+r]*B[c*4+2] + A[3*4+r]*B[c*4+3];
}

static bool mat4Inv(const double m[16], double inv[16]) {
    double t[16];
    t[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
           + m[9]*m[7]*m[14]  + m[13]*m[6]*m[11]  - m[13]*m[7]*m[10];
    t[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14]  + m[8]*m[6]*m[15]
           - m[8]*m[7]*m[14]  - m[12]*m[6]*m[11]  + m[12]*m[7]*m[10];
    t[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13]  - m[8]*m[5]*m[15]
           + m[8]*m[7]*m[13]  + m[12]*m[5]*m[11]  - m[12]*m[7]*m[9];
    t[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13]  + m[8]*m[5]*m[14]
           - m[8]*m[6]*m[13]  - m[12]*m[5]*m[10]  + m[12]*m[6]*m[9];
    t[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14]  + m[9]*m[2]*m[15]
           - m[9]*m[3]*m[14]  - m[13]*m[2]*m[11]  + m[13]*m[3]*m[10];
    t[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14]  - m[8]*m[2]*m[15]
           + m[8]*m[3]*m[14]  + m[12]*m[2]*m[11]  - m[12]*m[3]*m[10];
    t[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13]  + m[8]*m[1]*m[15]
           - m[8]*m[3]*m[13]  - m[12]*m[1]*m[11]  + m[12]*m[3]*m[9];
    t[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13]  - m[8]*m[1]*m[14]
           + m[8]*m[2]*m[13]  + m[12]*m[1]*m[10]  - m[12]*m[2]*m[9];
    t[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]   - m[5]*m[2]*m[15]
           + m[5]*m[3]*m[14]  + m[13]*m[2]*m[7]   - m[13]*m[3]*m[6];
    t[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]   + m[4]*m[2]*m[15]
           - m[4]*m[3]*m[14]  - m[12]*m[2]*m[7]   + m[12]*m[3]*m[6];
    t[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]   - m[4]*m[1]*m[15]
           + m[4]*m[3]*m[13]  + m[12]*m[1]*m[7]   - m[12]*m[3]*m[5];
    t[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]   + m[4]*m[1]*m[14]
           - m[4]*m[2]*m[13]  - m[12]*m[1]*m[6]   + m[12]*m[2]*m[5];
    t[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]   + m[5]*m[2]*m[11]
           - m[5]*m[3]*m[10]  - m[9]*m[2]*m[7]    + m[9]*m[3]*m[6];
    t[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]   - m[4]*m[2]*m[11]
           + m[4]*m[3]*m[10]  + m[8]*m[2]*m[7]    - m[8]*m[3]*m[6];
    t[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]    + m[4]*m[1]*m[11]
           - m[4]*m[3]*m[9]   - m[8]*m[1]*m[7]    + m[8]*m[3]*m[5];
    t[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]    - m[4]*m[1]*m[10]
           + m[4]*m[2]*m[9]   + m[8]*m[1]*m[6]    - m[8]*m[2]*m[5];
    double det = m[0]*t[0] + m[1]*t[4] + m[2]*t[8] + m[3]*t[12];
    if (std::abs(det) < 1e-20) return false;
    double id = 1.0/det;
    for (int i=0;i<16;++i) inv[i]=t[i]*id;
    return true;
}

static void mat4MulVec4(const double M[16], const double v[4], double out[4]) {
    for (int r=0;r<4;++r)
        out[r]=M[0*4+r]*v[0]+M[1*4+r]*v[1]+M[2*4+r]*v[2]+M[3*4+r]*v[3];
}

static bool glProject(const double obj[3], const double mv[16],
                       const double proj[16], const int vp[4],
                       double& winX, double& winY) {
    double mvp[16]; mat4Mul(proj,mv,mvp);
    double v4[4]={obj[0],obj[1],obj[2],1.0}, clip[4];
    mat4MulVec4(mvp,v4,clip);
    if (std::abs(clip[3])<1e-12) return false;
    winX=vp[0]+vp[2]*(clip[0]/clip[3]+1.0)*0.5;
    winY=vp[1]+vp[3]*(clip[1]/clip[3]+1.0)*0.5;
    return true;
}

static bool glUnproject(double winX, double winY, double winZ,
                         const double mv[16], const double proj[16],
                         const int vp[4],
                         double& objX, double& objY, double& objZ) {
    double mvp[16],inv[16];
    mat4Mul(proj,mv,mvp);
    if (!mat4Inv(mvp,inv)) return false;
    double ndc[4]={2.0*(winX-vp[0])/vp[2]-1.0,
                   2.0*(winY-vp[1])/vp[3]-1.0,
                   2.0*winZ-1.0, 1.0};
    double w[4]; mat4MulVec4(inv,ndc,w);
    if (std::abs(w[3])<1e-12) return false;
    objX=w[0]/w[3]; objY=w[1]/w[3]; objZ=w[2]/w[3];
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

ViewportBrushKnob::ViewportBrushKnob(DD::Image::Knob_Closure* kc,
                                       AttributePainterOp* op,
                                       const char* name)
    : DD::Image::Knob(kc, name), op_(op)
{
    memset(cachedMV_,0,sizeof(cachedMV_));
    memset(cachedProj_,0,sizeof(cachedProj_));
}

void ViewportBrushKnob::cacheGLMatrices() {
    glGetDoublev(GL_MODELVIEW_MATRIX,  cachedMV_);
    glGetDoublev(GL_PROJECTION_MATRIX, cachedProj_);
    glGetIntegerv(GL_VIEWPORT,         cachedVP_);
    matricesCached_ = true;
}

bool ViewportBrushKnob::projectToScreen(const Vec3f& world,
                                          float& sx, float& sy) const {
    if (!matricesCached_) return false;
    double obj[3]={world.x,world.y,world.z}, wx,wy;
    if (!glProject(obj,cachedMV_,cachedProj_,cachedVP_,wx,wy)) return false;
    sx=(float)wx; sy=(float)wy;
    return true;
}

void ViewportBrushKnob::updateMouseFromWin32() {
#if defined(_WIN32) || defined(_WIN64)
    HDC hdc=wglGetCurrentDC(); HWND hwnd=WindowFromDC(hdc);
    if (!hwnd) return;
    POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
    RECT rc; GetClientRect(hwnd, &rc);
    mouseGLX_=(float)pt.x;
    mouseGLY_=(float)(rc.bottom-rc.top-1-pt.y);
#endif
}

Ray ViewportBrushKnob::buildRay(float glX, float glY) const {
    if (!matricesCached_) return Ray{{0,0,0},{0,0,-1}};
    double nx,ny,nz,fx,fy,fz;
    if (!glUnproject(glX,glY,0.0,cachedMV_,cachedProj_,cachedVP_,nx,ny,nz))
        return Ray{{0,0,0},{0,0,-1}};
    if (!glUnproject(glX,glY,1.0,cachedMV_,cachedProj_,cachedVP_,fx,fy,fz))
        return Ray{{0,0,0},{0,0,-1}};
    float dx=(float)(fx-nx),dy=(float)(fy-ny),dz=(float)(fz-nz);
    float len=std::sqrt(dx*dx+dy*dy+dz*dz);
    if (len>1e-8f){dx/=len;dy/=len;dz/=len;}
    return Ray{{(float)nx,(float)ny,(float)nz},{dx,dy,dz}};
}

void ViewportBrushKnob::updateHit() {
    hitValid_ = false;
    { std::ofstream _f("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
      _f << "  drawBrush: sampler=" << (sampler_!=nullptr) << " valid=" << (sampler_&&sampler_->isValid()) << " matrices=" << matricesCached_ << "\n"; }
    if (!sampler_ || !sampler_->isValid()) {
        if (op_) {
            { std::ofstream _dbg("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
              _dbg << "  calling rebuildGeometry op=" << (void*)op_ << "\n"; }
            op_->rebuildGeometry();
            sampler_ = op_->getSampler();
            { std::ofstream _dbg2("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
              _dbg2 << "  after rebuild sampler=" << (sampler_!=nullptr) << " valid=" << (sampler_&&sampler_->isValid()) << "\n"; }
        }
        if (!sampler_ || !sampler_->isValid()) return;
    }
    if (!matricesCached_) return;
    debugRay_ = buildRay(mouseGLX_, mouseGLY_);
    lastHit_ = sampler_->intersect(debugRay_);
    hitValid_ = lastHit_.hit;
    if (hitValid_) {
        brushState_.center = lastHit_.position;
        Vec3f rawN = lastHit_.normal;
        if (!hasSmoothedNormal_) {
            smoothedNormal_ = rawN;
            hasSmoothedNormal_ = true;
        } else {
            const float s=0.3f;
            smoothedNormal_.x+=(rawN.x-smoothedNormal_.x)*s;
            smoothedNormal_.y+=(rawN.y-smoothedNormal_.y)*s;
            smoothedNormal_.z+=(rawN.z-smoothedNormal_.z)*s;
            float len=std::sqrt(smoothedNormal_.lengthSq());
            if (len>1e-8f) smoothedNormal_=smoothedNormal_*(1.f/len);
        }
        brushState_.normal = smoothedNormal_;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  draw_handle
//
//  Input mapping:
//    LMB drag              → paint with current blend mode
//    Shift + LMB drag      → resize brush (horizontal delta)
//    Erase via dropdown    → select "Erase" in Blend Mode knob
//
//  Note: Ctrl+LMB is NOT used — it conflicts with Nuke's viewport controls.
// ─────────────────────────────────────────────────────────────────────────────

static bool _vbk_cb(DD::Image::ViewerContext* ctx, DD::Image::Knob* k, int) {
    static_cast<ViewportBrushKnob*>(k)->draw_handle(ctx); return true; }

void ViewportBrushKnob::draw_handle(DD::Image::ViewerContext* ctx) {
    { std::ofstream _f("C:/dev/AttributePainter/handle_debug.txt", std::ios::app);
      _f << "draw_handle event=" << ctx->event() << " enabled=" << enabled_ << "\n"; }
    using namespace DD::Image;
    const ViewerEvent ev = ctx->event();

    // Register for ANYWHERE events (mouse move, click etc)
    if (ev == DRAW_OPAQUE || ev == PUSH || ev == DRAG) {
        begin_handle(Knob::ANYWHERE, ctx, _vbk_cb, 0, 0, 0, 0);
        end_handle(ctx);
    }

    static bool printed = false;
    if (!printed) {
        fprintf(stderr, "=== AttributePainter v1.0.7 ===\n");
        printed = true;
    }

    if (ev == DRAW_OPAQUE) {
        cacheGLMatrices();
        updateMouseFromWin32();
        nukeMouseX_ = (float)ctx->mouse_x();
        nukeMouseY_ = (float)ctx->mouse_y();
        updateHit();
        ++debugFrame_;

        bool lmbDown   = false;
        bool shiftHeld = false;
#if defined(_WIN32) || defined(_WIN64)
        lmbDown   = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        shiftHeld = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
#endif

        // ── Shift+LMB: brush resize ─────────────────────────────────────
        // Don't return early — let DRAW_OVERLAY still run so the brush
        // circle keeps drawing under the cursor during resize.
        if (shiftHeld && lmbDown && !painting_) {
            if (!resizing_) {
                resizing_ = true;
                resizeStartX_ = mouseGLX_;
                resizeStartRadius_ = brushState_.radius;
            }
            float delta = (mouseGLX_ - resizeStartX_) * 0.002f;
            float newRadius = std::max(0.001f, resizeStartRadius_ + delta);
            brushState_.radius = newRadius;
            if (onRadius_) onRadius_(newRadius);
        }
        else if (resizing_ && !lmbDown) {
            resizing_ = false;
        }

        // ── Paint (only when NOT resizing) ───────────────────────────────
        if (!resizing_ && enabled_ && hitValid_) {
            if (lmbDown && !painting_) {
                painting_  = true;
                firstTick_ = true;
                if (onPaint_)
                    onPaint_(lastHit_.position, lastHit_.normal, true);
                firstTick_ = false;
            }
            else if (lmbDown && painting_) {
                if (onPaint_)
                    onPaint_(lastHit_.position, lastHit_.normal, false);
            }
        }

        if (!lmbDown && painting_) {
            painting_ = false;
            if (onStrokeEnd_) onStrokeEnd_();
        }

        redraw();
        return;
    }

    if (ev == DRAW_OVERLAY) {
        if (debug_)
            drawDebugOverlay();

        drawPaintedVertices();

        if (hitValid_ && enabled_)
            drawBrushCircle();
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw painted vertex colors as GL points
// ─────────────────────────────────────────────────────────────────────────────

void ViewportBrushKnob::drawPaintedVertices() const {
    if (!sampler_ || sampler_->pointCount() == 0) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glPointSize(6.0f);
    glBegin(GL_POINTS);
    size_t n = std::min(sampler_->pointCount(), (size_t)100000);
    for (size_t i = 0; i < n; ++i) {
        Color3f c = sampler_->getColor((uint32_t)i);
        if (c.r > 0.01f || c.g > 0.01f || c.b > 0.01f) {
            Vec3f p = sampler_->getPoint((uint32_t)i);
            glColor4f(c.r, c.g, c.b, 1.0f);
            glVertex3f(p.x, p.y, p.z);
        }
    }
    glEnd();
    glPopAttrib();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Debug overlay
// ─────────────────────────────────────────────────────────────────────────────

void ViewportBrushKnob::drawDebugOverlay() const {
    if (!matricesCached_) return;

    Vec3f cen=sampler_->centroid();
    Vec3f mn=sampler_->bboxMin(), mx=sampler_->bboxMax();

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // GREEN AABB
    glColor4f(0,1,0,0.6f); glLineWidth(1.0f);
    glBegin(GL_LINES);
    glVertex3f(mn.x,mn.y,mn.z); glVertex3f(mx.x,mn.y,mn.z);
    glVertex3f(mn.x,mx.y,mn.z); glVertex3f(mx.x,mx.y,mn.z);
    glVertex3f(mn.x,mn.y,mx.z); glVertex3f(mx.x,mn.y,mx.z);
    glVertex3f(mn.x,mx.y,mx.z); glVertex3f(mx.x,mx.y,mx.z);
    glVertex3f(mn.x,mn.y,mn.z); glVertex3f(mn.x,mx.y,mn.z);
    glVertex3f(mx.x,mn.y,mn.z); glVertex3f(mx.x,mx.y,mn.z);
    glVertex3f(mn.x,mn.y,mx.z); glVertex3f(mn.x,mx.y,mx.z);
    glVertex3f(mx.x,mn.y,mx.z); glVertex3f(mx.x,mx.y,mx.z);
    glVertex3f(mn.x,mn.y,mn.z); glVertex3f(mn.x,mn.y,mx.z);
    glVertex3f(mx.x,mn.y,mn.z); glVertex3f(mx.x,mn.y,mx.z);
    glVertex3f(mn.x,mx.y,mn.z); glVertex3f(mn.x,mx.y,mx.z);
    glVertex3f(mx.x,mx.y,mn.z); glVertex3f(mx.x,mx.y,mx.z);
    glEnd();

    float sz=std::max({mx.x-mn.x,mx.y-mn.y,mx.z-mn.z})*0.05f;
    glColor4f(0,1,0,1); glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex3f(cen.x-sz,cen.y,cen.z); glVertex3f(cen.x+sz,cen.y,cen.z);
    glVertex3f(cen.x,cen.y-sz,cen.z); glVertex3f(cen.x,cen.y+sz,cen.z);
    glVertex3f(cen.x,cen.y,cen.z-sz); glVertex3f(cen.x,cen.y,cen.z+sz);
    glEnd();

    {
        Vec3f ro=debugRay_.origin, rd=debugRay_.dir;
        float rl=std::max({mx.x-mn.x,mx.y-mn.y,mx.z-mn.z})*3.f;
        Vec3f fp=ro+rd*rl;
        glColor4f(1,0,0,0.8f); glLineWidth(2.0f);
        glBegin(GL_LINES); glVertex3f(ro.x,ro.y,ro.z); glVertex3f(fp.x,fp.y,fp.z); glEnd();
    }

    if (hitValid_) {
        Vec3f hp=lastHit_.position;
        glColor4f(1,1,0,1); glLineWidth(3.0f);
        glBegin(GL_LINES);
        glVertex3f(hp.x-sz,hp.y,hp.z); glVertex3f(hp.x+sz,hp.y,hp.z);
        glVertex3f(hp.x,hp.y-sz,hp.z); glVertex3f(hp.x,hp.y+sz,hp.z);
        glVertex3f(hp.x,hp.y,hp.z-sz); glVertex3f(hp.x,hp.y,hp.z+sz);
        glEnd();
    }

    {
        float vpX=(float)cachedVP_[0],vpY=(float)cachedVP_[1];
        float vpW=(float)cachedVP_[2],vpH=(float)cachedVP_[3];
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(vpX,vpX+vpW,vpY,vpY+vpH,-1,1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        float csx,csy;
        if (projectToScreen(cen,csx,csy)) {
            glColor4f(0,1,1,1); glPointSize(12.f);
            glBegin(GL_POINTS); glVertex2f(csx,csy); glEnd();
        }
        glColor4f(1,0,1,0.9f); glPointSize(12.f);
        glBegin(GL_POINTS); glVertex2f(mouseGLX_,mouseGLY_); glEnd();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }
    glPopAttrib();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Brush circle
// ─────────────────────────────────────────────────────────────────────────────

void ViewportBrushKnob::drawBrushCircle() const {
    const Vec3f& c = brushState_.center;
    const Vec3f  n = smoothedNormal_;

    Vec3f up={0.f,1.f,0.f};
    if (std::abs(n.dot(up))>0.99f) up={1.f,0.f,0.f};
    float d=n.dot(up);
    Vec3f t={up.x-n.x*d, up.y-n.y*d, up.z-n.z*d};
    float tlen=std::sqrt(t.lengthSq());
    if (tlen<1e-8f) return;
    t=t*(1.f/tlen);
    Vec3f b={n.y*t.z-n.z*t.y, n.z*t.x-n.x*t.z, n.x*t.y-n.y*t.x};

    float r = brushState_.radius;

    glPushAttrib(GL_LINE_BIT|GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT|GL_ENABLE_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Outer ring — cyan during resize, white otherwise
    if (resizing_)
        glColor4f(0.f, 1.f, 1.f, 0.9f);
    else
        glColor4f(1.f, 1.f, 1.f, 0.9f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    for (int i=0;i<BRUSH_CIRCLE_SEGS;++i) {
        float theta=(float)i/(float)BRUSH_CIRCLE_SEGS*2.f*AP_PIf;
        float cosT=std::cos(theta), sinT=std::sin(theta);
        Vec3f pt={c.x+(t.x*cosT+b.x*sinT)*r,
                  c.y+(t.y*cosT+b.y*sinT)*r,
                  c.z+(t.z*cosT+b.z*sinT)*r};
        const float BIAS=0.001f;
        glVertex3f(pt.x+n.x*BIAS, pt.y+n.y*BIAS, pt.z+n.z*BIAS);
    }
    glEnd();

    // Inner ring (hardness)
    float innerR=r*brushState_.hardness;
    glColor4f(1.f,1.f,0.f,0.5f); glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    for (int i=0;i<BRUSH_CIRCLE_SEGS;++i) {
        float theta=(float)i/(float)BRUSH_CIRCLE_SEGS*2.f*AP_PIf;
        float cosT=std::cos(theta), sinT=std::sin(theta);
        Vec3f pt={c.x+(t.x*cosT+b.x*sinT)*innerR,
                  c.y+(t.y*cosT+b.y*sinT)*innerR,
                  c.z+(t.z*cosT+b.z*sinT)*innerR};
        const float BIAS=0.001f;
        glVertex3f(pt.x+n.x*BIAS, pt.y+n.y*BIAS, pt.z+n.z*BIAS);
    }
    glEnd();

    // Normal tick
    glColor4f(0.3f,0.8f,1.f,0.8f);
    glBegin(GL_LINES);
    glVertex3f(c.x,c.y,c.z);
    glVertex3f(c.x+n.x*r*0.5f, c.y+n.y*r*0.5f, c.z+n.z*r*0.5f);
    glEnd();

    glPopAttrib();
}

} // namespace AP

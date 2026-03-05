#pragma once
#include "Types.h"
#include <cmath>

namespace AP {

// ─────────────────────────────────────────────────────────────────────────────
//  BrushSystem
//  Stateless math helpers + a BrushState struct that the knob owns.
//  All weight computation is branchless / SIMD-friendly.
// ─────────────────────────────────────────────────────────────────────────────

struct BrushState {
    float      radius    = 0.05f;  // world-space radius
    float      strength  = 1.0f;  // [0,1]
    float      hardness  = 0.5f;  // [0,1] — controls falloff width
    Color3f    color     = {1.f, 0.f, 0.f};
    FalloffMode falloff  = FalloffMode::Smooth;
    BlendMode   blend    = BlendMode::Replace;
    bool        active   = false;
    Vec3f       center   = {};    // world-space brush centre (last hit point)
    Vec3f       normal   = {};    // surface normal at hit
};

class BrushSystem {
public:
    // Compute normalised weight [0,1] for a point at world-space distance d
    // from the brush centre, given the current brush state.
    static float weight(const BrushState& b, float d) noexcept {
        if (d >= b.radius) return 0.f;
        const float t = 1.f - (d / b.radius);   // 1 at centre, 0 at edge
        // hardness controls where the falloff begins:
        //   hardness=1 → constant (no falloff)
        //   hardness=0 → full cosine taper
        const float inner = b.hardness;
        float w;
        if (t >= inner) {
            w = 1.f;
        } else {
            const float u = (inner > 1e-6f) ? (t / inner) : t;
            w = falloffCurve(b.falloff, u);
        }
        return w * b.strength;
    }

    // Blend current vertex colour toward brush colour using the given blend mode
    static Color3f blend(const BrushState& b, const Color3f& src, float w) noexcept {
        switch (b.blend) {
            case BlendMode::Replace:
                return lerp(src, b.color, w);
            case BlendMode::Add:
                return {src.r + b.color.r * w,
                        src.g + b.color.g * w,
                        src.b + b.color.b * w};
            case BlendMode::Subtract:
                return {src.r - b.color.r * w,
                        src.g - b.color.g * w,
                        src.b - b.color.b * w};
            case BlendMode::Multiply:
                return lerp(src, {src.r * b.color.r,
                                  src.g * b.color.g,
                                  src.b * b.color.b}, w);
            case BlendMode::Smooth: {
                // push src toward brush colour by w
                Color3f target = lerp(src, b.color, 0.5f);
                return lerp(src, target, w);
            }
        }
        return src;
    }

    // Clamp colour channels to [0,1]
    static Color3f saturate(Color3f c) noexcept {
        return { clamp01(c.r), clamp01(c.g), clamp01(c.b) };
    }

private:
    static float falloffCurve(FalloffMode m, float u) noexcept {
        switch (m) {
            case FalloffMode::Linear:   return u;
            case FalloffMode::Constant: return 1.f;
            case FalloffMode::Gaussian: return std::exp(-4.f * (1.f - u) * (1.f - u));
            case FalloffMode::Smooth:
            default:
                return u * u * (3.f - 2.f * u); // smoothstep
        }
    }
    static float clamp01(float v) noexcept {
        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    }
    static Color3f lerp(const Color3f& a, const Color3f& b, float t) noexcept {
        float s = 1.f - t;
        return { a.r*s + b.r*t, a.g*s + b.g*t, a.b*s + b.b*t };
    }
};

} // namespace AP

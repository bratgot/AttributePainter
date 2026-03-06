#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <functional>

namespace AP {

struct Vec2f { float x, y; };
struct Vec3f {
    float x, y, z;
    Vec3f operator+(const Vec3f& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3f operator-(const Vec3f& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3f operator*(float s)         const { return {x*s,   y*s,   z*s};   }
    float dot(const Vec3f& o)        const { return x*o.x + y*o.y + z*o.z; }
    float lengthSq()                 const { return dot(*this); }
};
struct Vec4f { float x, y, z, w; };
struct Color3f { float r, g, b; };

struct VertexColor {
    uint32_t index;
    Color3f  color;
};

struct StrokeSnapshot {
    std::vector<VertexColor> before;
    std::vector<VertexColor> after;
};

enum class FalloffMode : int {
    Smooth    = 0,
    Linear    = 1,
    Constant  = 2,
    Gaussian  = 3,
};

enum class BlendMode : int {
    Replace  = 0,
    Add      = 1,
    Subtract = 2,
    Multiply = 3,
    Smooth   = 4,
    Erase    = 5,   // lerp toward black
};


struct Ray {
    Vec3f origin;
    Vec3f dir;
};

struct HitResult {
    bool  hit      = false;
    float t        = 1e30f;
    Vec3f position = {};
    Vec3f normal   = {};
    uint32_t faceIndex = UINT32_MAX;
};

} // namespace AP

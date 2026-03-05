# generate_stubs.ps1
# Creates minimal Nuke NDK and OpenUSD header/lib stubs so the CMake
# configure + compile step succeeds on Windows CI without a real SDK.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\generate_stubs.ps1
param(
    [string]$StubsRoot = (Join-Path $PSScriptRoot "..\stubs" | Resolve-Path -ErrorAction SilentlyContinue)
)

# Resolve relative paths even if stubs dir doesn't exist yet
if (-not $StubsRoot) {
    $StubsRoot = Join-Path (Split-Path $PSScriptRoot) "stubs"
}
$StubsRoot = [System.IO.Path]::GetFullPath($StubsRoot)

Write-Host "Generating stubs in: $StubsRoot"

# ── Helpers ────────────────────────────────────────────────────────────────────
function New-Header([string]$Path, [string]$Content) {
    $dir = Split-Path $Path
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    Set-Content -Path $Path -Value $Content -Encoding UTF8
}

function New-StubLib([string]$Dir, [string]$Name) {
    if (-not (Test-Path $Dir)) { New-Item -ItemType Directory -Path $Dir -Force | Out-Null }
    # Create a minimal valid COFF .lib (empty archive) using LIB.exe if available,
    # otherwise write a dummy file — CMake find_library only checks existence.
    $libPath = Join-Path $Dir "$Name.lib"
    if (Get-Command lib.exe -ErrorAction SilentlyContinue) {
        # Write a tiny dummy .obj then archive it
        $dummy = Join-Path $Dir "_dummy_$Name.obj"
        [System.IO.File]::WriteAllBytes($dummy, [byte[]]@())
        & lib.exe /nologo /out:$libPath $dummy 2>$null
        Remove-Item $dummy -ErrorAction SilentlyContinue
    } else {
        # Fallback: write empty file (enough for find_library)
        [System.IO.File]::WriteAllBytes($libPath, [byte[]]@())
    }
    # Also provide a .dll stub for IMPORTED_LOCATION
    $dllPath = Join-Path $Dir "$Name.dll"
    [System.IO.File]::WriteAllBytes($dllPath, [byte[]]@())
}

$Nuke = Join-Path $StubsRoot "nuke"
$Usd  = Join-Path $StubsRoot "usd"

# ── Nuke NDK header stubs ──────────────────────────────────────────────────────
Write-Host "  -> Nuke NDK headers"

New-Header "$Nuke\include\DDImage\Op.h" @'
#pragma once
#include <string>
namespace DD { namespace Image {
  struct Node {};
  class Op {
  public:
    struct Description {
      Description(const char*, const char*, Op*(*)(Node*)) {}
    };
    explicit Op(Node*) {}
    virtual ~Op() = default;
    virtual const char* Class()     const { return "Op"; }
    virtual const char* node_help() const { return ""; }
    void invalidate() {}
  };
}} // namespace
'@

New-Header "$Nuke\include\DDImage\GeoOp.h" @'
#pragma once
#include "Op.h"
namespace DD { namespace Image {
  class Scene {};
  class GeometryList {
  public:
    unsigned size() const { return 0; }
  };
  class GeoOp : public Op {
  public:
    explicit GeoOp(Node* n) : Op(n) {}
    virtual void geometry_engine(Scene&, GeometryList&) {}
    virtual void build_handles(struct ViewerContext*) {}
    virtual void draw_handle(struct ViewerContext*) {}
    GeoOp& input0() { static GeoOp g(nullptr); return g; }
    Op* input(int) { return nullptr; }
    void get(Scene&, GeometryList&) {}
    void add_draw_handle(struct ViewerContext*) {}
  };
}} // namespace
'@

New-Header "$Nuke\include\DDImage\Knob.h" @'
#pragma once
#include <ostream>
namespace DD { namespace Image {
  struct OutputContext {};
  struct Knob_Closure {};
  class Knob {
  public:
    Knob(Knob_Closure*, const char*) {}
    virtual ~Knob() = default;
    virtual const char* Class()      const { return "Knob"; }
    virtual bool not_default()       const { return false; }
    virtual void to_script(std::ostream&, const OutputContext*, bool) const {}
    virtual bool from_script(const char*) { return false; }
    virtual void draw_handle(struct ViewerContext*) {}
    virtual int  knob_mouse_down  (struct ViewerContext*) { return 0; }
    virtual int  knob_mouse_drag  (struct ViewerContext*) { return 0; }
    virtual int  knob_mouse_up    (struct ViewerContext*) { return 0; }
    virtual int  knob_mouse_move  (struct ViewerContext*) { return 0; }
    virtual int  knob_mouse_scroll(struct ViewerContext*) { return 0; }
    bool is(const char*) const { return false; }
    unsigned size() const { return 0; }
    void* pointer(int) const { return nullptr; }
  };
}} // namespace
'@

New-Header "$Nuke\include\DDImage\Knobs.h" @'
#pragma once
#include "Knob.h"
namespace DD { namespace Image {
  using Knob_Callback = void(*)(Knob_Closure*, ...);
  inline void Divider   (Knob_Callback, const char*) {}
  inline void Bool_knob (Knob_Callback, bool*, const char*, const char*) {}
  inline void Float_knob(Knob_Callback, float*, const char*, const char*) {}
  inline void Color_knob(Knob_Callback, float*, const char*, const char*) {}
  inline void String_knob(Knob_Callback, char*, int, const char*, const char*) {}
  inline void SetRange(Knob_Callback, double, double) {}
  inline void Tooltip (Knob_Callback, const char*) {}
  template<typename T, typename... A> inline void CustomKnob1(Knob_Callback, A&&...) {}
  template<typename... A>             inline void Enumeration_knob(A&&...) {}
}} // namespace
'@

New-Header "$Nuke\include\DDImage\ViewerContext.h" @'
#pragma once
namespace DD { namespace Image {
  class ViewerContext {
  public:
    enum { kDraw3DPass=1, kMouseLeft=0, kControlKey=1 };
    struct Rect { int w() const { return 1920; } int h() const { return 1080; } };
    Rect viewport()     const { return {}; }
    struct Mat4 {
      Mat4 operator*(const Mat4&) const { return {}; }
      Mat4 inverse()              const { return {}; }
    };
    Mat4 proj()         const { return {}; }
    Mat4 modelview()    const { return {}; }
    int  button()       const { return 0; }
    int  state()        const { return 0; }
    int  mouse_x()      const { return 0; }
    int  mouse_y()      const { return 0; }
    int  scroll_delta() const { return 0; }
    void request_draw()       {}
    int  draw_pass()    const { return kDraw3DPass; }
    float pressure()    const { return 1.f; }
  };
}} // namespace
'@

New-Header "$Nuke\include\DDImage\Matrix4.h" @'
#pragma once
namespace DD { namespace Image {
  struct Vector4 {
    float x=0,y=0,z=0,w=1;
    Vector4(float x=0,float y=0,float z=0,float w=1):x(x),y(y),z(z),w(w){}
  };
  struct Vector3 {
    float x=0,y=0,z=0;
    Vector3(float x=0,float y=0,float z=0):x(x),y(y),z(z){}
    Vector3 operator-(const Vector3& o) const { return {x-o.x,y-o.y,z-o.z}; }
    Vector3& operator/=(float) { return *this; }
    float length() const { return 1.f; }
  };
  struct Matrix4 {
    Matrix4 operator*(const Matrix4&) const { return {}; }
    Matrix4 inverse()                 const { return {}; }
    Vector4 operator*(const Vector4&) const { return {}; }
  };
}} // namespace
'@

New-Header "$Nuke\include\DDImage\GeoInfo.h" @'
#pragma once
#include "Knob.h"
#define POINTER_ATTRIB 0
#define GROUP_OBJECT   0
namespace DD { namespace Image {
  struct AttribContext { Knob* attribute = nullptr; };
  struct GeoInfo {
    const AttribContext* get_typed_attrib_context(const char*, int, int) const { return nullptr; }
  };
}} // namespace
'@

New-Header "$Nuke\include\DDImage\gl.h"             "#pragma once"
New-Header "$Nuke\include\DDImage\Scene.h"           "#pragma once"
New-Header "$Nuke\include\DDImage\PolyMesh.h"        "#pragma once"
New-Header "$Nuke\include\DDImage\Attribute.h"       "#pragma once"
New-Header "$Nuke\include\DDImage\Enumeration_KnobI.h" "#pragma once"
New-Header "$Nuke\include\DDImage\GeometryList.h"    "#pragma once`n#include `"GeoInfo.h`""
New-Header "$Nuke\include\DDImage\Vector3.h"         "#pragma once`n#include `"Matrix4.h`""
New-Header "$Nuke\include\GL\glew.h" @'
#pragma once
// Stub GL for CI builds
#ifndef GL_LINES
#define GL_LINES            0x0001
#define GL_LINE_LOOP        0x0002
#define GL_LINE_BIT         0x00000004
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LEQUAL           515
inline void glBegin(unsigned){}
inline void glEnd(){}
inline void glVertex3f(float,float,float){}
inline void glColor4f(float,float,float,float){}
inline void glLineWidth(float){}
inline void glDepthFunc(unsigned){}
inline void glPushAttrib(unsigned){}
inline void glPopAttrib(){}
#endif
'@

Write-Host "  -> Nuke NDK stub libs"
New-StubLib "$Nuke" "DDImage"
New-StubLib "$Nuke" "RIKsupport"

# ── USD header stubs ───────────────────────────────────────────────────────────
Write-Host "  -> USD headers"

New-Header "$Usd\include\pxr\pxr.h" @'
#pragma once
#define PXR_NAMESPACE_USING_DIRECTIVE using namespace pxr;
'@

New-Header "$Usd\include\pxr\base\tf\token.h" @'
#pragma once
#include <string>
namespace pxr {
  class TfToken {
  public:
    TfToken() = default;
    explicit TfToken(const std::string& s) : s_(s) {}
    explicit TfToken(const char* s) : s_(s) {}
    const std::string& GetString() const { return s_; }
  private: std::string s_;
  };
  namespace UsdGeomTokens { inline TfToken vertex{"vertex"}; }
}
'@

New-Header "$Usd\include\pxr\base\vt\array.h" @'
#pragma once
#include <vector>
namespace pxr {
  template<typename T>
  struct VtArray : std::vector<T> {
    using std::vector<T>::vector;
    VtArray() = default;
    explicit VtArray(size_t n) : std::vector<T>(n) {}
  };
}
'@

New-Header "$Usd\include\pxr\base\gf\vec3f.h" @'
#pragma once
namespace pxr {
  struct GfVec3f {
    float data[3]={};
    GfVec3f()=default;
    GfVec3f(float x,float y,float z):data{x,y,z}{}
    float& operator[](int i){ return data[i]; }
    float  operator[](int i) const { return data[i]; }
  };
}
'@

New-Header "$Usd\include\pxr\base\gf\vec3d.h" @'
#pragma once
namespace pxr {
  struct GfVec3d {
    double data[3]={};
    GfVec3d(double x,double y,double z):data{x,y,z}{}
    double operator[](int i) const { return data[i]; }
  };
}
'@

New-Header "$Usd\include\pxr\base\gf\matrix4d.h" @'
#pragma once
#include "vec3d.h"
namespace pxr {
  struct GfMatrix4d {
    GfVec3d Transform(const GfVec3d& v) const { return v; }
  };
}
'@

New-Header "$Usd\include\pxr\usd\sdf\path.h" @'
#pragma once
#include <string>
namespace pxr {
  struct SdfPath {
    SdfPath()=default;
    explicit SdfPath(const std::string& s):s_(s){}
    const std::string& GetString() const { return s_; }
  private: std::string s_;
  };
  namespace SdfValueTypeNames { struct _T{}; inline _T Color3fArray; }
}
'@

New-Header "$Usd\include\pxr\usd\usd\stage.h" @'
#pragma once
#include "../sdf/path.h"
#include <memory>
namespace pxr {
  class UsdPrim {
  public:
    bool IsValid() const { return false; }
    operator bool() const { return false; }
    template<typename T> bool IsA() const { return false; }
  };
  class UsdStage {
  public:
    UsdPrim GetPrimAtPath(const SdfPath&) const { return {}; }
  };
  using UsdStageRefPtr = std::shared_ptr<UsdStage>;
}
'@

New-Header "$Usd\include\pxr\usd\usd\prim.h"  "#pragma once`n#include `"../usd/stage.h`""

New-Header "$Usd\include\pxr\usd\usdGeom\mesh.h" @'
#pragma once
#include "../usd/prim.h"
namespace pxr {
  struct UsdAttribute {
    template<typename T> bool Get(T*) const { return false; }
  };
  class UsdGeomMesh {
  public:
    explicit UsdGeomMesh(const UsdPrim&) {}
    UsdAttribute GetPointsAttr()            const { return {}; }
    UsdAttribute GetFaceVertexCountsAttr()  const { return {}; }
    UsdAttribute GetFaceVertexIndicesAttr() const { return {}; }
  };
}
'@

New-Header "$Usd\include\pxr\usd\usdGeom\primvarsAPI.h" @'
#pragma once
#include "mesh.h"
#include "../../base/tf/token.h"
#include "../../base/vt/array.h"
namespace pxr {
  struct UsdGeomPrimvar {
    bool IsDefined() const { return false; }
    operator bool()  const { return false; }
    template<typename T> bool Get(T*) const { return false; }
    template<typename T> bool Set(const T&) const { return false; }
    bool GetIndices(VtArray<int>*) const { return false; }
  };
  class UsdGeomPrimvarsAPI {
  public:
    explicit UsdGeomPrimvarsAPI(const UsdGeomMesh&) {}
    UsdGeomPrimvar GetPrimvar(const TfToken&) const { return {}; }
    UsdGeomPrimvar FindOrCreatePrimvar(const TfToken&, ...) const { return {}; }
  };
}
'@

New-Header "$Usd\include\pxr\usd\usdGeom\primvar.h" "#pragma once`n#include `"primvarsAPI.h`""

New-Header "$Usd\include\pxr\usd\usdGeom\xformCache.h" @'
#pragma once
#include "../usd/prim.h"
#include "../../base/gf/matrix4d.h"
namespace pxr {
  class UsdGeomXformCache {
  public:
    GfMatrix4d GetLocalToWorldTransform(const UsdPrim&) { return {}; }
  };
}
'@

Write-Host "  -> USD stub libs"
foreach ($lib in @("usd_usd","usd_sdf","usd_tf","usd_usdGeom","usd_vt")) {
    New-StubLib "$Usd\lib" $lib
}

Write-Host ""
Write-Host "Stubs generated successfully in: $StubsRoot" -ForegroundColor Green

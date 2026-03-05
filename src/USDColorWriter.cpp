#include "USDColorWriter.h"
using namespace PXR_NS;
// Full USD headers here (only in this TU)
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace AP {

USDColorWriter::USDColorWriter(const std::string& primvarName)
    : primvarName_(primvarName) {}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Read
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

std::vector<Color3f> USDColorWriter::read(const UsdGeomMesh& mesh,
                                           size_t expectedCount) const {
    std::vector<Color3f> result;

    UsdGeomPrimvarsAPI pvAPI(mesh);
    UsdGeomPrimvar pv = pvAPI.GetPrimvar(TfToken(primvarName_));
    if (!pv || !pv.IsDefined()) return result;

    VtArray<GfVec3f> vtColors;
    if (!pv.Get(&vtColors)) return result;

    if (vtColors.size() != expectedCount) {
        // Size mismatch â€” might be faceVarying or indexed; ignore for now
        // TODO: handle indexed primvars by expanding with pv.GetIndices()
        return result;
    }

    result.resize(vtColors.size());
    for (size_t i = 0; i < vtColors.size(); ++i) {
        result[i] = { vtColors[i][0], vtColors[i][1], vtColors[i][2] };
    }
    return result;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Write (full buffer)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

bool USDColorWriter::write(UsdGeomMesh& mesh,
                            const std::vector<Color3f>& colors) const {
    if (colors.empty()) return false;

    UsdGeomPrimvarsAPI pvAPI(mesh);
    UsdGeomPrimvar pv = pvAPI.CreatePrimvar(
        TfToken(primvarName_),
        SdfValueTypeNames->Color3fArray,
        UsdGeomTokens->vertex); // per-point interpolation

    if (!pv) return false;

    VtArray<GfVec3f> vtColors(colors.size());
    for (size_t i = 0; i < colors.size(); ++i) {
        vtColors[i] = GfVec3f(colors[i].r, colors[i].g, colors[i].b);
    }

    return pv.Set(vtColors);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Stage + Commit (incremental â€” only write once per stroke tick)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void USDColorWriter::stage(uint32_t vertexIndex, Color3f color) {
    staged_.push_back({vertexIndex, color});
}

bool USDColorWriter::commit(UsdGeomMesh& mesh,
                             std::vector<Color3f>& workingColors) const {
    if (staged_.empty()) return true;

    // Apply staged edits to working color buffer
    for (auto& [idx, c] : staged_) {
        if (idx < workingColors.size())
            workingColors[idx] = c;
    }

    // Write the full buffer in one USD call (avoids repeated primvar authoring)
    return const_cast<USDColorWriter*>(this)->write(mesh, workingColors);
}

void USDColorWriter::clearStaged() {
    staged_.clear();
}

} // namespace AP


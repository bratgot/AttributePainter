#include "USDColorWriter.h"

#include <usg/base/Token.h>
#include <usg/base/Value.h>
#include <usg/geom/GeomTokens.h>

namespace AP {

USDColorWriter::USDColorWriter(const std::string& primvarName)
    : primvarName_(primvarName) {}

// ─────────────────────────────────────────────────────────────────────────────
//  Read
// ─────────────────────────────────────────────────────────────────────────────

std::vector<Color3f> USDColorWriter::read(const usg::MeshPrim& mesh,
                                           size_t               expectedCount) const {
    std::vector<Color3f> result;
    if (!mesh.isValid()) return result;

    if (isDisplayColor()) {
        // Fast path: built-in displayColor attribute on GprimPrim
        usg::Vec3fArray colors = mesh.getDisplayColor();
        if (colors.size() == expectedCount) {
            result.resize(colors.size());
            for (size_t i = 0; i < colors.size(); ++i)
                result[i] = { colors[i][0], colors[i][1], colors[i][2] };
        }
        return result;
    }

    // Custom primvar path
    usg::Primvar pv(static_cast<usg::Prim>(mesh), usg::Token(primvarName_.c_str()));
    if (!pv.isValid()) return result;

    usg::Vec3fArray flat;
    if (!pv.computeFlattened(flat)) return result;
    if (flat.size() != expectedCount) return result;

    result.resize(flat.size());
    for (size_t i = 0; i < flat.size(); ++i)
        result[i] = { flat[i][0], flat[i][1], flat[i][2] };
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Write
// ─────────────────────────────────────────────────────────────────────────────

bool USDColorWriter::write(usg::MeshPrim&               mesh,
                            const std::vector<Color3f>&  colors) const {
    if (!mesh.isValid() || colors.empty()) return false;

    // Build usg Vec3fArray
    usg::Vec3fArray vtColors(colors.size());
    for (size_t i = 0; i < colors.size(); ++i)
        vtColors[i] = fdk::Vec3f(colors[i].r, colors[i].g, colors[i].b);

    if (isDisplayColor()) {
        // Built-in GprimPrim method — sets primvars:displayColor with vertex interp
        mesh.setDisplayColor(vtColors);
        return true;
    }

    // Custom primvar
    usg::PrimvarsAPI pvAPI(static_cast<usg::Prim>(mesh));
    usg::Primvar pv = pvAPI.createPrimvar(
        usg::Token(primvarName_.c_str()),
        usg::Value::Type::Color3fArray,
        usg::GeomTokens.vertex);

    if (!pv.isValid()) return false;
    return pv.attribute().setValue(vtColors);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Incremental staging
// ─────────────────────────────────────────────────────────────────────────────

void USDColorWriter::stage(uint32_t vertexIndex, Color3f color) {
    staged_.push_back({ vertexIndex, color });
}

bool USDColorWriter::commit(usg::MeshPrim&        mesh,
                             std::vector<Color3f>& workingColors) {
    if (staged_.empty()) return true;
    for (auto& [idx, c] : staged_) {
        if (idx < workingColors.size())
            workingColors[idx] = c;
    }
    return write(mesh, workingColors);
}

void USDColorWriter::clearStaged() {
    staged_.clear();
}

} // namespace AP

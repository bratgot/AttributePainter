#pragma once

#include "Types.h"

// USG
#include <usg/geom/MeshPrim.h>
#include <usg/geom/GprimPrim.h>
#include <usg/geom/PrimvarsAPI.h>
#include <usg/geom/Primvar.h>

#include <vector>
#include <string>

namespace AP {

class USDColorWriter
{
public:
    explicit USDColorWriter(const std::string& primvarName = "displayColor");

    // ── Read existing primvar from a usg mesh ────────────────────────────────
    std::vector<Color3f> read(const usg::MeshPrim& mesh,
                               size_t               expectedCount) const;

    // ── Write full color buffer to usg mesh ──────────────────────────────────
    bool write(usg::MeshPrim&                mesh,
               const std::vector<Color3f>&  colors) const;

    // ── Incremental staging (write once per stroke tick) ─────────────────────
    void stage(uint32_t vertexIndex, Color3f color);
    bool commit(usg::MeshPrim&         mesh,
                std::vector<Color3f>&  workingColors);
    void clearStaged();

private:
    std::string              primvarName_;
    std::vector<VertexColor> staged_;

    bool isDisplayColor() const { return primvarName_ == "displayColor"; }
};

} // namespace AP

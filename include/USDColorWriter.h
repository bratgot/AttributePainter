#pragma once
#include "Types.h"
#include <string>
#include <vector>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

namespace AP {

class USDColorWriter {
public:
    explicit USDColorWriter(const std::string& primvarName = "displayColor");

    std::vector<Color3f> read(const PXR_NS::UsdGeomMesh& mesh,
                               size_t expectedCount) const;

    bool write(PXR_NS::UsdGeomMesh& mesh,
               const std::vector<Color3f>& colors) const;

    void stage(uint32_t vertexIndex, Color3f color);

    bool commit(PXR_NS::UsdGeomMesh& mesh,
                std::vector<Color3f>& workingColors) const;

    void clearStaged();

    const std::string& primvarName() const { return primvarName_; }

private:
    std::string primvarName_;
    std::vector<std::pair<uint32_t, Color3f>> staged_;
};

} // namespace AP
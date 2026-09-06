#include "r3d_gl.h"
#include "r3d_replacement.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if !defined(F15_ORIGINAL_ASSETS)
#define F15_ORIGINAL_ASSETS ""
#endif
#if !defined(F15_CONVERTED_ASSETS)
#define F15_CONVERTED_ASSETS ""
#endif

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void setReplacementRoot(const std::filesystem::path *root) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", root ? root->string().c_str() : "");
#else
    if (root) setenv("F15_REPLACEMENT_ROOT", root->string().c_str(), 1);
    else unsetenv("F15_REPLACEMENT_ROOT");
#endif
}

std::vector<unsigned char> readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

unsigned read16(const std::vector<unsigned char> &bytes, size_t offset) {
    require(offset + 2 <= bytes.size(), "truncated 3D3 word");
    return bytes[offset] | ((unsigned)bytes[offset + 1] << 8);
}

bool is3d3(const std::filesystem::path &path) {
    std::string extension = path.extension().string();
    for (char &c : extension) c = (char)std::toupper((unsigned char)c);
    return extension == ".3D3";
}

void compareMesh(const std::string &logicalName, int shapeId,
                 const unsigned char *legacy, size_t legacySize) {
    R3DLegacyShapeStats source = {};
    require(r3dgl_testLegacyShapeStats(legacy, legacySize, &source),
            "legacy shape decode failed: " + logicalName);
    R3DReplacementMesh *mesh =
        r3dReplacementMesh(logicalName.c_str(), shapeId);
    if (!source.renderable) {
        require(mesh == nullptr,
                "non-renderable shape unexpectedly has replacement: " +
                    logicalName + " shape " + std::to_string(shapeId));
        return;
    }
    require(mesh != nullptr,
            "missing replacement: " + logicalName +
                " shape " + std::to_string(shapeId));

    int previousSourceIndex[4] = {-1, -1, -1, -1};
    int triangles = 0;
    int lines = 0;
    int points = 0;
    int faceColors[256] = {};
    int lineColors[256] = {};
    int pointColors[256] = {};
    for (int i = 0; i < mesh->nPrims; ++i) {
        const R3DReplacementPrim &primitive = mesh->prims[i];
        require((primitive.sourceFlags & 1u) != 0u &&
                    primitive.sourceKind >= 1 &&
                    primitive.sourceKind <= 3 &&
                    primitive.sourceColor >= 0 &&
                    primitive.sourceColor < 256,
                "replacement lacks source proof metadata");
        require(primitive.sourceIndex >=
                    previousSourceIndex[primitive.sourceKind],
                "replacement source primitive order changed");
        previousSourceIndex[primitive.sourceKind] = primitive.sourceIndex;
        require((primitive.sourceKind == 1 && primitive.mode == 4) ||
                    (primitive.sourceKind == 2 && primitive.mode == 1) ||
                    (primitive.sourceKind == 3 && primitive.mode == 0),
                "replacement source kind and draw mode disagree");
        if (primitive.mode == 4) {
            triangles += primitive.nVerts / 3;
            faceColors[primitive.sourceColor] += primitive.nVerts / 3;
        } else if (primitive.mode == 1) {
            lines += primitive.nVerts / 2;
            lineColors[primitive.sourceColor] += primitive.nVerts / 2;
        } else {
            points += primitive.nVerts;
            pointColors[primitive.sourceColor] += primitive.nVerts;
        }
    }

    require(triangles == source.triangles,
            "replacement triangle count differs");
    require(lines >= source.minimumLines && lines <= source.maximumLines,
            "replacement line count is outside legacy coverage");
    require(points == source.points, "replacement point count differs");
    require(std::memcmp(faceColors, source.faceColors,
                        sizeof(faceColors)) == 0,
            "replacement face colors differ");
    require(std::memcmp(pointColors, source.pointColors,
                        sizeof(pointColors)) == 0,
            "replacement point colors differ");
    for (int color = 0; color < 256; ++color) {
        require(lineColors[color] >= source.minimumLineColors[color] &&
                    lineColors[color] <= source.maximumLineColors[color],
                "replacement line color coverage differs");
    }
}

} // namespace

int main() {
    const std::filesystem::path originalRoot = F15_ORIGINAL_ASSETS;
    const std::filesystem::path convertedRoot = F15_CONVERTED_ASSETS;
    setReplacementRoot(&convertedRoot);

    int containers = 0;
    int shapes = 0;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(originalRoot)) {
        if (!entry.is_regular_file() || !is3d3(entry.path())) continue;
        const std::vector<unsigned char> bytes = readFile(entry.path());
        if (bytes.size() < 8) continue;
        const unsigned shapeCount = read16(bytes, 2);
        const size_t tableStart = 4;
        const size_t modelSizePosition =
            tableStart + (size_t)shapeCount * 2u;
        if (modelSizePosition + 2 > bytes.size()) continue;
        const unsigned modelSize = read16(bytes, modelSizePosition);
        const size_t modelStart = modelSizePosition + 2;
        if (modelStart + modelSize > bytes.size()) continue;
        std::vector<unsigned> offsets;
        for (unsigned i = 0; i < shapeCount; ++i) {
            offsets.push_back(read16(bytes, tableStart + (size_t)i * 2u));
        }
        offsets.push_back(modelSize);
        const std::string logicalName =
            std::filesystem::relative(entry.path(), originalRoot).generic_string();
        for (unsigned shape = 0; shape < shapeCount; ++shape) {
            if (offsets[shape] >= offsets[shape + 1] ||
                offsets[shape + 1] > modelSize) {
                continue;
            }
            compareMesh(logicalName, (int)shape,
                        bytes.data() + modelStart + offsets[shape],
                        offsets[shape + 1] - offsets[shape]);
            ++shapes;
        }
        r3dReplacementShutdown();
        ++containers;
    }

    setReplacementRoot(nullptr);
    require(containers > 0 && shapes > 0, "no 3D3 shapes were compared");
    std::cout << "r3d_replacement_full_validation_tests compared "
              << shapes << " shape slots in " << containers
              << " containers\n";
    return 0;
}

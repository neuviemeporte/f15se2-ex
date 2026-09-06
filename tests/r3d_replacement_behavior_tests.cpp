#include "r3d_replacement.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void appendU32(std::vector<unsigned char> &bytes, unsigned value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back((unsigned char)(value >> shift));
    }
}

void appendF32(std::vector<unsigned char> &bytes, float value) {
    unsigned bits;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(bytes, bits);
}

void writeMesh(const std::filesystem::path &path, bool valid) {
    std::vector<unsigned char> bytes(40, 0);
    std::memcpy(bytes.data(), "F15GLM3", 7);
    appendU32(bytes, 1);
    appendU32(bytes, 4);
    appendU32(bytes, valid ? 3 : 2);
    appendU32(bytes, 1);
    appendU32(bytes, 0);
    appendU32(bytes, 7);
    appendU32(bytes, 1);
    appendF32(bytes, 1.0f);
    appendF32(bytes, 0.5f);
    appendF32(bytes, 0.25f);
    appendF32(bytes, 1.0f);
    for (int vertex = 0; vertex < (valid ? 3 : 2); ++vertex) {
        appendF32(bytes, (float)vertex);
        appendF32(bytes, 0.0f);
        appendF32(bytes, 0.0f);
    }
    std::ofstream out(path, std::ios::binary);
    out.write((const char *)bytes.data(), (std::streamsize)bytes.size());
}

void setRoot(const std::string &root) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", root.c_str());
#else
    if (root.empty()) unsetenv("F15_REPLACEMENT_ROOT");
    else setenv("F15_REPLACEMENT_ROOT", root.c_str(), 1);
#endif
}

void setTool(const std::string &tool) {
#if defined(_WIN32)
    _putenv_s("F15_ASSET_TOOL", tool.c_str());
#else
    if (tool.empty()) unsetenv("F15_ASSET_TOOL");
    else setenv("F15_ASSET_TOOL", tool.c_str(), 1);
#endif
}

} // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() / "f15se2-ex-r3d-tests";
    const auto models = root / "TEST";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(models);
    setRoot(root.string());

    writeMesh(models / "shape_005_Fighter.glmesh", true);
    R3DReplacementMesh *mesh = r3dReplacementMesh("test.3d3", 5);
    require(mesh && mesh->nPrims == 1, "standalone GLMESH loads by shape slot");
    require(mesh->prims[0].mode == 4 && mesh->prims[0].nVerts == 3,
            "triangle primitive is preserved");
    require(mesh->prims[0].rgba[1] == 0.5f
            && mesh->prims[0].xyz[3] == 1.0f,
            "material and vertex data are preserved");

    writeMesh(models / "shape_006_Broken.glmesh", false);
    require(!r3dReplacementMesh("TEST.3D3", 6),
            "invalid primitive cardinality falls back atomically");

#if !defined(_WIN32)
    const auto oversized = root / "oversized.glmesh";
    {
        std::ofstream output(oversized, std::ios::binary);
        std::vector<char> block(1024 * 1024, '\0');
        for (int i = 0; i < 17; ++i) {
            output.write(block.data(), (std::streamsize)block.size());
        }
    }
    std::ofstream(models / "shape_007_Oversized.glb", std::ios::binary)
        << "not a GLB";
    setTool("cat '" + oversized.string() + "' #");
    require(!r3dReplacementMesh("TEST.3D3", 7),
            "oversized converter output is drained and rejected");
    setTool("");
#endif

    r3dReplacementShutdown();
    setRoot("");
    std::filesystem::remove_all(root);
    std::cout << "r3d_replacement_behavior_tests passed\n";
    return 0;
}

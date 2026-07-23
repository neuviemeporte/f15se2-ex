#include "shared/asset_path.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void setReplacementRoot(const std::string &value) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", value.c_str());
#else
    if (value.empty()) unsetenv("F15_REPLACEMENT_ROOT");
    else setenv("F15_REPLACEMENT_ROOT", value.c_str(), 1);
#endif
}

} // namespace

int main() {
    const auto testRoot =
        std::filesystem::temp_directory_path() / "f15se2-ex-asset-path-tests";
    std::filesystem::remove_all(testRoot);
    std::filesystem::create_directories(testRoot / "Fonts");
    std::ofstream(testRoot / "WALL.PNG", std::ios::binary) << "png";
    std::ofstream(testRoot / "Fonts" / "FONT_1.BDF", std::ios::binary) << "bdf";

    char path[1024] = {};
    setReplacementRoot("");
    require(!findAssetReplacement("WALL.png", path, sizeof(path)),
            "lookup is disabled without an explicit replacement root");

    setReplacementRoot(testRoot.string());
    require(findAssetReplacement("wall.png", path, sizeof(path)),
            "lookup matches DOS-style uppercase names");
    require(std::filesystem::path(path).filename() == "WALL.PNG",
            "lookup returns the actual filesystem spelling");

    require(findAssetReplacement("fonts/font_1.bdf", path, sizeof(path)),
            "lookup matches every nested path component case-insensitively");
    require(!findAssetReplacement("../WALL.PNG", path, sizeof(path)),
            "lookup rejects parent traversal");
    require(!findAssetReplacement(testRoot.string().c_str(), path, sizeof(path)),
            "lookup rejects absolute paths");

    char shortPath[2] = {'x', '\0'};
    require(!findAssetReplacement("WALL.PNG", shortPath, sizeof(shortPath)),
            "lookup rejects an undersized output buffer");
    require(shortPath[0] == '\0', "failed lookup clears the output buffer");
    require(!findAssetReplacement("missing.png", path, sizeof(path)),
            "lookup rejects missing files");
    require(path[0] == '\0', "missing lookup clears the output buffer");

    setReplacementRoot("");
    std::filesystem::remove_all(testRoot);
    std::cout << "asset_path_behavior_tests passed\n";
    return 0;
}

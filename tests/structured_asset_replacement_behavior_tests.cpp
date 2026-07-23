#include "shared/common.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern bool setGamePath(const char *path);

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void setEnvironment(const char *name, const std::string &value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name);
    else setenv(name, value.c_str(), 1);
#endif
}

void writeFile(const std::filesystem::path &path, const std::string &contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
}

std::string readStream(SDL_IOStream *stream) {
    std::string result;
    char buffer[32];
    size_t count;
    while ((count = SDL_ReadIO(stream, buffer, sizeof(buffer))) != 0) {
        result.append(buffer, count);
    }
    return result;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root =
        fs::temp_directory_path() / "f15se2-structured-replacement-test";
    const fs::path game = root / "game";
    const fs::path replacements = root / "replacements";
    const fs::path tool = root / "asset-tool.py";

    fs::remove_all(root);
    fs::create_directories(game);
    writeFile(game / "LIBYA.WLD", "LEGACY_WLD");
    writeFile(game / "LB.3D3", "LEGACY_3D3");
    writeFile(replacements / "LIBYA" / "LIBYA.WLD.json",
              "{\"format\":\"WLD\"}\n");
    writeFile(replacements / "LIBYA" / "LB.3D3.json",
              "{\"format\":\"3D3\"}\n");
    writeFile(tool,
              "import sys\n"
              "sys.stdout.buffer.write(('BUILT_' + sys.argv[-1]).encode())\n");

    setEnvironment("F15_REPLACEMENT_ROOT", replacements.string());
    setEnvironment("F15_ASSET_TOOL", "python3 " + tool.string());
    require(setGamePath(game.string().c_str()), "game path is accepted");

    SDL_IOStream *stream = openFile("Libya.wld", 0);
    require(stream != nullptr, "WLD JSON replacement opens");
    require(readStream(stream) == "BUILT_WLD",
            "WLD replacement bytes come from the configured importer");
    fileClose(stream);

    stream = openFile("LB.3D3", 0);
    require(stream != nullptr, "legacy 3D3 fallback opens");
    require(readStream(stream) == "LEGACY_3D3",
            "metadata-only 3D3 JSON does not replace the table stream");
    fileClose(stream);

    writeFile(replacements / "LIBYA" / "LB.3D3.json",
              "{\"format\":\"3D3\",\"model_data\":[]}\n");
    stream = openFile("LB.3D3", 0);
    require(stream != nullptr, "full 3D3 JSON replacement opens");
    require(readStream(stream) == "BUILT_3D3",
            "full 3D3 JSON is rebuilt through the configured importer");
    fileClose(stream);

    setEnvironment("F15_ASSET_TOOL", "python3 -c \"import sys; sys.exit(2)\"");
    stream = openFile("Libya.wld", 0);
    require(stream != nullptr, "legacy WLD fallback opens after importer failure");
    require(readStream(stream) == "LEGACY_WLD",
            "importer failure transparently falls back to the legacy asset");
    fileClose(stream);

    setEnvironment(
        "F15_ASSET_TOOL",
        "python3 -c \"import sys; sys.stdout.buffer.write(b'x' * 1048577)\"");
    stream = openFile("Libya.wld", 0);
    require(stream != nullptr, "legacy WLD fallback opens after oversized output");
    require(readStream(stream) == "LEGACY_WLD",
            "oversized importer output is drained then rejected");
    fileClose(stream);

    setEnvironment("F15_REPLACEMENT_ROOT", "");
    setEnvironment("F15_ASSET_TOOL", "");
    fs::remove_all(root);
    std::cout << "structured_asset_replacement_behavior_tests passed\n";
    return 0;
}

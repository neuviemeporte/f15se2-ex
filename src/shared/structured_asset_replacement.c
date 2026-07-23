/*
 * structured_asset_replacement.c - Feed editable structured JSON assets through
 * the proven legacy loaders without teaching every loader about JSON.
 */
#include "structured_asset_replacement.h"

#include "asset_path.h"
#include "log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#if defined(_WIN32)
#define F15_POPEN(command, mode) _popen((command), (mode))
#define F15_PCLOSE(pipe) _pclose(pipe)
#define F15_PIPE_READ_MODE "rb"
#else
#define F15_POPEN(command, mode) popen((command), (mode))
#define F15_PCLOSE(pipe) pclose(pipe)
#define F15_PIPE_READ_MODE "r"
#endif

namespace {

constexpr size_t MAX_JSON_BYTES = 1024 * 1024;
constexpr size_t MAX_REBUILT_BYTES = 1024 * 1024;

/* Fold one ASCII byte to uppercase for format and extension matching. */
std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return value;
}

/* Quote one path safely for the explicitly configured external asset-tool command. */
std::string shellQuote(const std::string &value) {
#if defined(_WIN32)
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"') quoted += "\\\"";
        else quoted += c;
    }
    return quoted + "\"";
#else
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    return quoted + "'";
#endif
}

/* Return the converter format identifier for a supported structured legacy extension. */
const char *formatFor(const fs::path &legacy) {
    const std::string extension = upper(legacy.extension().string());
    if (extension == ".WLD") return "WLD";
    if (extension == ".3DT") return "3DT";
    if (extension == ".3DG") return "3DG";
    if (extension == ".3D3") return "3D3";
    return nullptr;
}

/* Resolve the replacement directory associated with a structured legacy asset. */
std::string replacementDirectory(const fs::path &legacy) {
    const std::string stem = upper(legacy.stem().string());
    if (stem == "LB") return "LIBYA";
    if (stem == "PG") return "GULF";
    return stem;
}

/* Find the canonical editable JSON replacement for a structured asset. */
bool findStructuredJson(const fs::path &legacy, char *path, size_t pathSize) {
    const std::string filename = upper(legacy.filename().string()) + ".json";
    const std::string directory = replacementDirectory(legacy);
    std::vector<fs::path> candidates;

    if (!legacy.parent_path().empty()) {
        candidates.push_back(legacy.parent_path() / directory / filename);
        candidates.push_back(legacy.parent_path() / filename);
    }
    candidates.push_back(fs::path(directory) / filename);
    candidates.push_back(filename);

    for (const fs::path &candidate : candidates) {
        if (findAssetReplacement(candidate.generic_string().c_str(),
                                 path, pathSize)) {
            return true;
        }
    }
    return false;
}

/* Check whether a constrained converter-generated JSON file contains a required property. */
bool fileContainsProperty(const char *path, const char *property) {
    FILE *file;
    std::vector<char> bytes;
    char chunk[4096];
    size_t count;

    if (!path || !property) return false;
    file = std::fopen(path, "rb");
    if (!file) return false;
    while ((count = std::fread(chunk, 1, sizeof(chunk), file)) != 0) {
        bytes.insert(bytes.end(), chunk, chunk + count);
        if (bytes.size() > MAX_JSON_BYTES) {
            std::fclose(file);
            return false;
        }
    }
    if (std::ferror(file)) {
        std::fclose(file);
        return false;
    }
    std::fclose(file);

    const std::string text(bytes.begin(), bytes.end());
    size_t position = 0;
    while ((position = text.find(property, position)) != std::string::npos) {
        size_t separator = position + std::strlen(property);
        while (separator < text.size() &&
               std::isspace((unsigned char)text[separator])) {
            ++separator;
        }
        if (separator < text.size() && text[separator] == ':') return true;
        ++position;
    }
    return false;
}

/* Build the default converter command path relative to the running executable. */
std::string defaultToolCommand(const fs::path &jsonPath) {
    const char *configured = std::getenv("F15_ASSET_TOOL");
    if (configured && configured[0]) return configured;

    std::vector<fs::path> candidates;
    fs::path cursor = jsonPath.parent_path();
    while (!cursor.empty()) {
        if (cursor.filename() == "converted_assets_all") {
            candidates.push_back(cursor.parent_path() / "tools/f15assets/cli.py");
            break;
        }
        const fs::path parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
    candidates.push_back("tools/f15assets/cli.py");
    candidates.push_back("../tools/f15assets/cli.py");
    for (const fs::path &candidate : candidates) {
        std::error_code error;
        if (fs::is_regular_file(candidate, error) && !error) {
            return "python3 " + shellQuote(candidate.string());
        }
    }
    return "python3 -m tools.f15assets.cli";
}

/* Expose rebuilt structured bytes as a rewindable temporary FILE stream. */
SDL_IOStream *streamFromBytes(const std::vector<unsigned char> &bytes) {
    SDL_IOStream *stream = SDL_IOFromDynamicMem();
    if (!stream) return nullptr;
    if (SDL_WriteIO(stream, bytes.data(), bytes.size()) != bytes.size() ||
        SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET) < 0) {
        SDL_CloseIO(stream);
        return nullptr;
    }
    return stream;
}

} // namespace

/* Open an editable structured replacement, rebuilding legacy bytes only for the existing loader. */
SDL_IOStream *openStructuredAssetReplacement(const char *legacyFilename) {
    fs::path legacy;
    const char *format;
    char jsonPath[512];
    std::string command;
    FILE *pipe;
    std::vector<unsigned char> rebuilt;
    unsigned char chunk[4096];
    size_t count;
    int status;
    bool readFailed;
    bool tooLarge = false;

    if (!legacyFilename || !legacyFilename[0]) return nullptr;
    legacy = fs::path(legacyFilename);
    format = formatFor(legacy);
    if (!format ||
/* Find the canonical editable JSON replacement for a structured asset. */
        !findStructuredJson(legacy, jsonPath, sizeof(jsonPath))) {
        return nullptr;
    }

    /* Default 3D3 sidecars are indexes for per-shape GLBs, not complete binary
     * replacements. Only an explicitly exported model_data field can replace
     * the legacy 3D3 table stream. */
    if (std::strcmp(format, "3D3") == 0 &&
/* Check whether a constrained converter-generated JSON file contains a required property. */
        !fileContainsProperty(jsonPath, "\"model_data\"")) {
        return nullptr;
    }

    command = defaultToolCommand(jsonPath) + " build-binary " +
              shellQuote(jsonPath) + " --format " + format;
    pipe = F15_POPEN(command.c_str(), F15_PIPE_READ_MODE);
    if (!pipe) {
        LogWarn(("asset replacement: cannot start JSON importer for %s; using legacy asset",
                 legacyFilename));
        return nullptr;
    }

    while ((count = std::fread(chunk, 1, sizeof(chunk), pipe)) != 0) {
        if (!tooLarge) {
            if (count > MAX_REBUILT_BYTES - rebuilt.size()) {
                /*
                 * Keep draining the child after crossing the limit. Waiting
                 * with unread pipe data can deadlock when the importer is
                 * blocked trying to finish its write.
                 */
                tooLarge = true;
                rebuilt.clear();
            } else {
                rebuilt.insert(rebuilt.end(), chunk, chunk + count);
            }
        }
    }
    readFailed = std::ferror(pipe) != 0;
    status = F15_PCLOSE(pipe);
    if (tooLarge) {
        LogWarn(("asset replacement: rebuilt %s exceeds size limit; using legacy asset",
                 legacyFilename));
        return nullptr;
    }
    if (readFailed) {
        LogWarn(("asset replacement: cannot read rebuilt %s; using legacy asset",
                 legacyFilename));
        return nullptr;
    }
    if (status != 0 || rebuilt.empty()) {
        LogWarn(("asset replacement: JSON importer failed for %s; using legacy asset",
                 legacyFilename));
        return nullptr;
    }

    SDL_IOStream *stream = streamFromBytes(rebuilt);
    if (!stream) {
        LogWarn(("asset replacement: cannot open rebuilt bytes for %s; using legacy asset",
                 legacyFilename));
        return nullptr;
    }
    LogInfo(("asset replacement: loaded %s from JSON %s (%zu bytes)",
             legacyFilename, jsonPath, rebuilt.size()));
    return stream;
}

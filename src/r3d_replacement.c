/*
 * r3d_replacement.c - Per-shape GLB discovery and GLMESH cache loading.
 *
 * GLB remains the editable source. The converter reduces it to a deliberately
 * small binary cache so the game does not need a general JSON/glTF stack.
 */

#include "r3d_replacement.h"

#include "log.h"

#include <SDL3/SDL.h>
#include <quickdigest5.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

typedef struct CachedMesh {
    std::string key;
    R3DReplacementMesh mesh;
    int loaded;
} CachedMesh;

static std::vector<CachedMesh *> g_cache;

static std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}

static fs::path findChildCaseInsensitive(const fs::path &parent,
                                         const std::string &name) {
    std::error_code error;
    if (!fs::is_directory(parent, error)) return {};
    const std::string wanted = lower(name);
    for (const fs::directory_entry &entry : fs::directory_iterator(parent, error)) {
        if (error) break;
        if (lower(entry.path().filename().string()) == wanted) {
            return entry.path();
        }
    }
    return {};
}

static fs::path replacementRoot(void) {
    const char *root = std::getenv("F15_REPLACEMENT_ROOT");
    if (!root || !root[0]) return {};
    std::error_code error;
    fs::path path(root);
    return fs::is_directory(path, error) ? path : fs::path();
}

static fs::path shapeDirectory(const char *container_name) {
    fs::path root = replacementRoot();
    if (root.empty() || !container_name || !container_name[0]) return {};
    std::string stem = fs::path(container_name).stem().string();
    const std::string normalized = lower(stem);

    /* The original runtime uses short theater filenames, while the converter
     * gives their replacement directories descriptive campaign names. */
    if (normalized == "lb") {
        stem = "LIBYA";
    } else if (normalized == "pg") {
        stem = "GULF";
    }

    fs::path directory = findChildCaseInsensitive(root, stem);
    if (!directory.empty() && fs::is_directory(directory)) return directory;
    return {};
}

static fs::path findShapeFile(const fs::path &directory, int shape_id,
                              const char *extension) {
    char exact[64];
    char prefix[64];
    std::vector<fs::path> matches;
    std::error_code error;
    std::snprintf(exact, sizeof(exact), "shape_%03d%s", shape_id, extension);
    std::snprintf(prefix, sizeof(prefix), "shape_%03d_", shape_id);
    const std::string exact_lower = lower(exact);
    const std::string prefix_lower = lower(prefix);
    const std::string extension_lower = lower(extension);

    if (!fs::is_directory(directory, error)) return {};
    for (const fs::directory_entry &entry :
         fs::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file()) continue;
        const std::string filename = lower(entry.path().filename().string());
        if (filename == exact_lower) return entry.path();
        if (filename.rfind(prefix_lower, 0) == 0
            && lower(entry.path().extension().string()) == extension_lower) {
            matches.push_back(entry.path());
        }
    }
    std::sort(matches.begin(), matches.end());
    if (matches.size() > 1) {
        LogWarn(("asset replacement: ambiguous shape %d (%zu matching %s files)",
                 shape_id, matches.size(), extension));
        return {};
    }
    return matches.empty() ? fs::path() : matches.front();
}

static uint32 readU32(const uint8 *bytes) {
    return (uint32)bytes[0] | ((uint32)bytes[1] << 8)
         | ((uint32)bytes[2] << 16) | ((uint32)bytes[3] << 24);
}

static int32 readS32(const uint8 *bytes) {
    return (int32)readU32(bytes);
}

static float readF32(const uint8 *bytes) {
    float value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

static void freeMesh(R3DReplacementMesh *mesh) {
    if (!mesh) return;
    for (int index = 0; index < mesh->nPrims; ++index) {
        SDL_free(mesh->prims[index].xyz);
    }
    SDL_free(mesh->prims);
    mesh->prims = NULL;
    mesh->nPrims = 0;
}

static int parseGlmesh(R3DReplacementMesh *mesh, const uint8 *data,
                       size_t size) {
    size_t position = 44;
    if (!mesh || !data || size < position
        || std::memcmp(data, "F15GLM3", 7) != 0) {
        return 0;
    }
    const uint32 primitive_count = readU32(data + 40);
    if (primitive_count == 0 || primitive_count > 4096) return 0;
    mesh->prims = (R3DReplacementPrim *)SDL_calloc(
        primitive_count, sizeof(*mesh->prims));
    if (!mesh->prims) return 0;
    mesh->nPrims = (int)primitive_count;

    for (uint32 index = 0; index < primitive_count; ++index) {
        R3DReplacementPrim *primitive = &mesh->prims[index];
        if (position > size || size - position < 40) goto fail;
        primitive->mode = (int)readU32(data + position);
        primitive->nVerts = (int)readU32(data + position + 4);
        primitive->sourceKind = (int)readU32(data + position + 8);
        primitive->sourceIndex = (int)readS32(data + position + 12);
        primitive->sourceColor = (int)readS32(data + position + 16);
        primitive->sourceFlags = readU32(data + position + 20);
        for (int channel = 0; channel < 4; ++channel) {
            primitive->rgba[channel] =
                readF32(data + position + 24 + channel * 4);
            if (!std::isfinite(primitive->rgba[channel])) goto fail;
        }
        position += 40;

        if (primitive->nVerts <= 0 || primitive->nVerts > 65536
            || (primitive->mode != 4 && primitive->mode != 1
                && primitive->mode != 0)
            || (primitive->mode == 4 && primitive->nVerts % 3 != 0)
            || (primitive->mode == 1 && primitive->nVerts % 2 != 0)) {
            goto fail;
        }
        const size_t float_count = (size_t)primitive->nVerts * 3;
        if (position > size || float_count > (size - position) / 4) goto fail;
        primitive->xyz =
            (float *)SDL_malloc(float_count * sizeof(float));
        if (!primitive->xyz) goto fail;
        for (size_t value_index = 0; value_index < float_count; ++value_index) {
            primitive->xyz[value_index] = readF32(data + position);
            if (!std::isfinite(primitive->xyz[value_index])) goto fail;
            position += 4;
        }
    }
    if (position != size) goto fail;
    return 1;

fail:
    freeMesh(mesh);
    return 0;
}

static std::vector<uint8> readBytes(const fs::path &path) {
    std::vector<uint8> bytes;
    std::error_code error;
    const uintmax_t length = fs::file_size(path, error);
    if (error || length == 0 || length > 16 * 1024 * 1024) return bytes;
    FILE *file = std::fopen(path.string().c_str(), "rb");
    if (!file) return bytes;
    bytes.resize((size_t)length);
    if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
        bytes.clear();
    }
    std::fclose(file);
    return bytes;
}

static int writeBytes(const fs::path &path, const std::vector<uint8> &bytes) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    FILE *file = std::fopen(path.string().c_str(), "wb");
    if (!file) return 0;
    const int written =
        std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && written;
}

static std::string shellQuote(const std::string &value) {
#if defined(_WIN32)
    std::string quoted = "\"";
    for (char c : value) quoted += c == '"' ? "\\\"" : std::string(1, c);
    return quoted + '"';
#else
    std::string quoted = "'";
    for (char c : value) quoted += c == '\'' ? "'\\''" : std::string(1, c);
    return quoted + '\'';
#endif
}

static std::string assetTool(void) {
    const char *configured = std::getenv("F15_ASSET_TOOL");
    if (configured && configured[0]) return configured;
    const fs::path candidates[] = {
        "tools/f15assets/cli.py",
        "../tools/f15assets/cli.py",
        "../../tools/f15assets/cli.py"
    };
    for (const fs::path &candidate : candidates) {
        if (fs::is_regular_file(candidate)) {
            return "python3 " + shellQuote(candidate.string());
        }
    }
    return {};
}

static std::vector<uint8> buildCache(const fs::path &glb_path) {
    std::vector<uint8> bytes;
    bool too_large = false;
    const std::string tool = assetTool();
    if (tool.empty()) return bytes;
    const std::string command =
        tool + " build-glmesh " + shellQuote(glb_path.string());
#if defined(_WIN32)
    FILE *pipe = _popen(command.c_str(), "rb");
#else
    FILE *pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) return bytes;
    uint8 chunk[4096];
    size_t count;
    while ((count = std::fread(chunk, 1, sizeof(chunk), pipe)) != 0) {
        if (!too_large && count > 16 * 1024 * 1024 - bytes.size()) {
            /*
             * Keep draining the child after crossing the limit. Calling pclose
             * with unread pipe data can deadlock while the converter is blocked
             * writing the remainder.
             */
            too_large = true;
            bytes.clear();
        } else if (!too_large) {
            bytes.insert(bytes.end(), chunk, chunk + count);
        }
    }
    const int read_failed = std::ferror(pipe) != 0;
#if defined(_WIN32)
    const int status = _pclose(pipe);
#else
    const int status = pclose(pipe);
#endif
    if (status != 0 || read_failed || too_large) bytes.clear();
    return bytes;
}

static int cacheMatchesGlb(const std::vector<uint8> &bytes,
                           const fs::path &glb) {
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "F15GLM3", 7) != 0) {
        return 0;
    }
    const std::string expected((const char *)bytes.data() + 8, 32);
    return expected == QuickDigest5::fileToHash(glb.string());
}

static int cacheIsUsable(const std::vector<uint8> &bytes,
                         const fs::path &glb) {
    R3DReplacementMesh parsed = {};
    const int usable =
        cacheMatchesGlb(bytes, glb) &&
        parseGlmesh(&parsed, bytes.data(), bytes.size());
    freeMesh(&parsed);
    return usable;
}

static R3DReplacementMesh *loadShape(const char *container_name,
                                     int shape_id,
                                     const std::string &request_key) {
    const fs::path directory = shapeDirectory(container_name);
    if (directory.empty()) return NULL;
    const fs::path glb = findShapeFile(directory, shape_id, ".glb");
    fs::path cache;
    if (!glb.empty()) {
        cache = glb.parent_path() / "cache" / glb.filename();
        cache.replace_extension(".glmesh");
    } else {
        cache = findShapeFile(directory, shape_id, ".glmesh");
        if (cache.empty()) {
            cache = findShapeFile(directory / "cache", shape_id, ".glmesh");
        }
    }
    if (cache.empty()) return NULL;
    std::vector<uint8> bytes;
    if (!glb.empty()) {
        bytes = readBytes(cache);
        if (!cacheIsUsable(bytes, glb)) {
            bytes = buildCache(glb);
            if (!cacheIsUsable(bytes, glb)) {
                LogWarn(("asset replacement: cannot build GLMESH for %s",
                         glb.string().c_str()));
                return NULL;
            }
            if (!writeBytes(cache, bytes)) {
                LogWarn(("asset replacement: cannot write model cache %s",
                         cache.string().c_str()));
            }
        }
    } else {
        bytes = readBytes(cache);
    }

    CachedMesh *entry = new CachedMesh{};
    entry->key = request_key;
    entry->loaded = parseGlmesh(&entry->mesh, bytes.data(), bytes.size());
    if (!entry->loaded) {
        LogWarn(("asset replacement: rejected model cache %s",
                 cache.string().c_str()));
        delete entry;
        return NULL;
    }
    g_cache.push_back(entry);
    LogInfo(("asset replacement: loaded shape %d for %s from %s",
             shape_id, container_name, cache.string().c_str()));
    return &entry->mesh;
}

R3DReplacementMesh *r3dReplacementMesh(const char *container_name,
                                       int shape_id) {
    if (!container_name || shape_id < 0 || shape_id > 999) return NULL;
    const std::string key =
        lower(fs::path(container_name).stem().string())
        + ":" + std::to_string(shape_id);
    for (CachedMesh *entry : g_cache) {
        if (entry->key == key) return entry->loaded ? &entry->mesh : NULL;
    }
    R3DReplacementMesh *mesh = loadShape(container_name, shape_id, key);
    if (mesh) return mesh;

    /* Negative entries prevent terrain objects without replacements from
     * rescanning the asset directory every rendered frame. */
    CachedMesh *missing = new CachedMesh{};
    missing->key = key;
    missing->loaded = 0;
    g_cache.push_back(missing);
    return NULL;
}

void r3dReplacementShutdown(void) {
    for (CachedMesh *entry : g_cache) {
        freeMesh(&entry->mesh);
        delete entry;
    }
    g_cache.clear();
}

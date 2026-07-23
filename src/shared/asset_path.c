/*
 * asset_path.c - Shared path lookup for optional modern asset packs.
 */

#include "asset_path.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static bool namesEqualIgnoringCase(const std::string &left, const std::string &right) {
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(),
            [](unsigned char a, unsigned char b) {
                return std::tolower(a) == std::tolower(b);
            });
}

static bool isSafeRelativePath(const fs::path &path) {
    if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
    for (const fs::path &component : path) {
        if (component == "..") return false;
    }
    return true;
}

static bool resolveComponent(const fs::path &directory, const fs::path &component,
                             fs::path *resolved) {
    std::error_code error{};
    const fs::path exact = directory / component;
    if (fs::exists(exact, error) && !error) {
        *resolved = exact;
        return true;
    }

    error.clear();
    for (fs::directory_iterator item(directory, error), end; !error && item != end;
         item.increment(error)) {
        if (namesEqualIgnoringCase(item->path().filename().string(), component.string())) {
            *resolved = item->path();
            return true;
        }
    }
    return false;
}

int findAssetReplacement(const char *relativePath, char *outPath, size_t outPathSize) {
    if (outPath && outPathSize) outPath[0] = '\0';
    if (!relativePath || !relativePath[0] || !outPath || !outPathSize) return 0;

    const char *rootValue = getenv("F15_REPLACEMENT_ROOT");
    if (!rootValue || !rootValue[0]) return 0;

    const fs::path relative{relativePath};
    if (!isSafeRelativePath(relative)) return 0;

    fs::path resolved{rootValue};
    std::error_code error{};
    if (!fs::is_directory(resolved, error) || error) return 0;

    for (const fs::path &component : relative) {
        if (component == ".") continue;
        fs::path next{};
        if (!resolveComponent(resolved, component, &next)) return 0;
        resolved = next;
    }

    error.clear();
    if (!fs::is_regular_file(resolved, error) || error) return 0;

    const std::string value = resolved.string();
    if (value.size() + 1 > outPathSize) return 0;
    memcpy(outPath, value.c_str(), value.size() + 1);
    return 1;
}

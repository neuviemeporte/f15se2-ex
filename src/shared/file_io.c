/*
 * file_io.c - shared file I/O, backed by SDL_IOStream.
 */

#include "inttype.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <string>
#include <filesystem>
#include <algorithm>
#include <vector>

using namespace std;
namespace fs = std::filesystem;
fs::path gamePath = ".";

/* Sets the directory to read game assets from, default is current dir */
bool setGamePath(const char* path) {
    if (!path) return true;
    gamePath = fs::path{path};
    if (!fs::is_directory(gamePath)) {
        const string msg = "Game asset directory does not exist: " + gamePath.string();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed",
            msg.c_str(), NULL);
        return false;
    }
    return true;
}

/* The game ships its assets with UPPERCASE 8.3 names (LABS.PIC), but the code
 * passes lowercase or mixed-case literals ("labs.pic", "HallFame"). DOS
 * filesystems were case-insensitive; a Linux filesystem is not.
 *
 * Resolve `filename` to the spelling that actually exists on disk: try the name
 * as given, then an all-uppercase form, then an all-lowercase form, and return
 * whichever exists. If none exists, return the original name. */
static string resolveCasePath(const char *filename, const bool require = false) {
    const fs::path filePath = gamePath / filename;
    if (fs::exists(filePath)) return filePath.string();

    string upperCase{filename};
    std::transform(upperCase.begin(), upperCase.end(), upperCase.begin(),
        [](unsigned char c){ return std::toupper(c); });
    const fs::path upperPath = gamePath / upperCase;
    if (fs::exists(upperPath)) return upperPath.string();

    string lowerCase{filename};
    std::transform(lowerCase.begin(), lowerCase.end(), lowerCase.begin(),
        [](unsigned char c){ return std::tolower(c); });
    const fs::path lowerPath = gamePath / lowerCase;
    if (fs::exists(lowerPath)) return lowerPath.string();

    return require ? "" : filePath.string();
}

/* Open an asset for reading. Returns the stream, or NULL on failure. */
SDL_IOStream *openFile(const char *filename, int mode) {
    (void)mode; /* the resident open service only distinguished read vs. write;
                 * every openFile caller in the game opens an asset for reading */
    return SDL_IOFromFile(resolveCasePath(filename).c_str(), "rb");
}

/* Create or truncate a file for writing. Returns the stream, or NULL on failure. */
SDL_IOStream *createFile(const char *filename, int attr) {
    (void)attr;
    return SDL_IOFromFile(resolveCasePath(filename).c_str(), "wb");
}

void fileClose(SDL_IOStream *io) {
    if (io) SDL_CloseIO(io);
}

/* fread/fwrite work-alikes over SDL_IOStream: transfer `count` items of `size`
 * bytes and return the number of whole items moved, matching the stdio
 * semantics the game's call sites were written against. */
size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *io) {
    if (!io || size == 0) return 0;
    return SDL_ReadIO(io, ptr, size * count) / size;
}

size_t fileWrite(const void *ptr, size_t size, size_t count, SDL_IOStream *io) {
    if (!io || size == 0) return 0;
    return SDL_WriteIO(io, ptr, size * count) / size;
}

/* Raw read into a host buffer. Shared with the PIC decoder (picimpl.c), which
 * streams the file 512 bytes at a time. Returns bytes read, or -1 on a null
 * stream. A negative count means "read the rest of the stream" (DOS passed
 * cx=0xFFFF; we resolve the exact remaining byte count so the request never
 * exceeds what the file holds, which SDL's backends handle inconsistently). */
int fileReadRaw(SDL_IOStream *io, void *dst, int count) {
    if (!io) return -1;
    if (count < 0) {
        const Sint64 size = SDL_GetIOSize(io);
        const Sint64 pos = SDL_TellIO(io);
        if (size < 0 || pos < 0 || pos >= size) return 0;
        count = (int)(size - pos);
    }
    return (int)SDL_ReadIO(io, dst, (size_t)count);
}

/* Print a '$'-terminated DOS string (INT 21h/09h) to the log. */
void dos_printstring(const char *str) {
    size_t len = 0;
    while (str[len] && str[len] != '$') len++;
    LogInfo(("%.*s", (int)len, str));
}

/* file_error.inc: Print error and exit */
void errorAndExit(const char *msg) {
    dos_printstring(msg);
    SDL_Quit();
    exit(1);
}

/* CRC-32 of a whole file, or false if it can't be read. */
static bool fileCrc32(const string &path, Uint32 *crc) {
    size_t size = 0;
    void *data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return false;
    *crc = SDL_crc32(0, data, size);
    SDL_free(data);
    return true;
}

/* The checksums identify the expected v451.03 files; they only need to tell
 * versions apart, not resist tampering. */
bool verifyGameAssets() {
    struct Asset {
        Uint32 crc;
        string filename;
    };
    const vector<Asset> assets = {
        { 0x5b2fc572u, "15flt.3d3" },
        { 0xa50de190u, "256left.pic" },
        { 0x841495b6u, "256pit.pic" },
        { 0x33e50d27u, "256rear.pic" },
        { 0xa39beb5du, "256right.pic" },
        { 0x27aed3cbu, "adv.pic" },
        { 0xe10c0debu, "armpiece.pic" },
        { 0x762acc2eu, "ce.3d3" },
        { 0xa6972872u, "ce.3dg" },
        { 0xee653c88u, "ce.3dt" },
        { 0x461674e6u, "ceurope.spr" },
        { 0x00f70fa0u, "ce.wld" },
        { 0x1b969861u, "cockpit.pic" },
        { 0x0c4fde5du, "dbicons.spr" },
        { 0x85991f0au, "death.pic" },
        { 0xa37f389du, "desk.pic" },
        { 0xd0352920u, "f15dgtl.bin" },
        { 0x288c54e4u, "f15.spr" },
        { 0xc514577du, "gulf.wld" },
        { 0x75550216u, "hiscore.pic" },
        { 0x87468c74u, "jp.3d3" },
        { 0xa93ccb53u, "jp.3dg" },
        { 0x239fb8d8u, "jp.3dt" },
        { 0xb3983810u, "jp.spr" },
        { 0xba3816a5u, "jp.wld" },
        { 0x68dea723u, "labs.pic" },
        { 0xf5953417u, "lb.3d3" },
        { 0x8285cd40u, "lb.3dg" },
        { 0x2288d8dcu, "lb.3dt" },
        { 0x79c31481u, "left.pic" },
        { 0xc328bfa9u, "libya.spr" },
        { 0x2a665c40u, "libya.wld" },
        { 0xc55bc1c2u, "me.3d3" },
        { 0x7bf4b034u, "me.3dg" },
        { 0x78115e59u, "me.3dt" },
        { 0x8908355au, "medal.pic" },
        { 0x4ac43598u, "me.spr" },
        { 0xa3a9725cu, "me.wld" },
        { 0x1016db22u, "nc.3d3" },
        { 0x1fe3b150u, "nc.3dg" },
        { 0xde682916u, "nc.3dt" },
        { 0xbb30da11u, "ncape.spr" },
        { 0x365a4956u, "nc.wld" },
        { 0x91480d33u, "persian.spr" },
        { 0x85a3fdcbu, "pg.3d3" },
        { 0x99e5227eu, "pg.3dg" },
        { 0xf792473bu, "pg.3dt" },
        { 0x564d4d74u, "photo.3d3" },
        { 0x7913a4e1u, "promo.pic" },
        { 0x51000abeu, "rear.pic" },
        { 0x47f25213u, "right.pic" },
        { 0x83e54c92u, "title16.pic" },
        { 0x030ac375u, "title640.pic" },
        { 0x4111f8a1u, "vn.3d3" },
        { 0xa4d9a08fu, "vn.3dg" },
        { 0x655d8a0au, "vn.3dt" },
        { 0x9876cd2eu, "vn.spr" },
        { 0x22994f63u, "vn.wld" },
        { 0x8035b7d3u, "wall.pic" },
    };
    for (const Asset &a : assets) {
        const string pathStr = resolveCasePath(a.filename.c_str(), true);
        Uint32 crc = 0;
        if (pathStr.empty()) {
            const string msg = "Could not find asset file " + a.filename
                + " in game directory: " + gamePath.string();
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed",
                msg.c_str(), NULL);
            return false;
        }
        if (!fileCrc32(pathStr, &crc) || crc != a.crc) {
            char expected[9];
            SDL_snprintf(expected, sizeof expected, "%08x", (unsigned)a.crc);
            const string msg = "Checksum mismatch for asset file " + pathStr
                + ": expected CRC-32 " + expected;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed",
                msg.c_str(), NULL);
            return false;
        }
    }
    return true;
}

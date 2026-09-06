/*
 * file_io.c - shared file I/O, backed by SDL_IOStream.
 */

#include "inttype.h"
#include "blackbox_io.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <quickdigest5.hpp>
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
    const string path = resolveCasePath(filename);
    return blackbox_openReadPath(filename, path.c_str());
}

/* Create or truncate a file for writing. Returns the stream, or NULL on failure. */
SDL_IOStream *createFile(const char *filename, int attr) {
    (void)attr;
    const string path = resolveCasePath(filename);
    return blackbox_openWritePath(filename, path.c_str());
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

bool verifyGameAssets() {
    struct Asset {
        string md5, filename;
    };
    const vector<Asset> assets = {
        { "3e468dbc9dd2c25a5343e384656d4b87", "15flt.3d3" },
        { "82b6b193954a0abba22f5f8267291d14", "1.pic" },
        { "8a8d0d29a6789de4971a5381cb89a60c", "256left.pic" },
        { "b6651ea956bec71890bf90de9a31fb1d", "256pit.pic" },
        { "4c4704170d85e18c842e18b1f438d266", "256rear.pic" },
        { "d18609791beb5a4b6bbc3c95a49979ee", "256right.pic" },
        { "a48a7ec1de1637da2d132a0fab3b4894", "2.pic" },
        { "e488a9127ba89f636fd0151da599ddb1", "3.pic" },
        { "d81029a10c5bcb0705ac98b6cc75621d", "4.pic" },
        { "fad492070c3afb3a11f32ad266df428d", "adv.pic" },
        { "f6b8b7b27b1de44282ca04ea6d369ea4", "armpiece.pic" },
        { "e480cd3510d5895f399e996725a58f6a", "ce.3d3" },
        { "b726a340b000005fdf501d61eeb57bcd", "ce.3dg" },
        { "b082fb4983b97804bff002e5824fbc90", "ce.3dt" },
        { "bacf2b850fcc6b592f92f046c89b15c8", "ceurope.spr" },
        { "df0d81f834eb7d8eaefb6b14b9728244", "ce.wld" },
        { "ff1bba14e570245e94e5f1a6186422a3", "cockpit.pic" },
        { "a490a0bf84b2c36dedab4fee0372bd09", "dbicons.spr" },
        { "6e26bcc7228da3563f17c1c919e14349", "death.pic" },
        { "07ae72e86ae2c38cb7293f86cb108e93", "desk.pic" },
        { "9c66ea28fec0daf5e3f11c28b9d7ffea", "f15dgtl.bin" },
        { "ac3d782b7a7c446dcf3036d532a3dbae", "f15.spr" },
        { "887f5679e3a645b2082503ff20fb1aa5", "gulf.wld" },
        { "537bbf274d79ebaa616f55917d14bf19", "hiscore.pic" },
        { "0dcf2b5bba17badda50bdf4273285e99", "jp.3d3" },
        { "e2201bf2eebd8f12859c41676070c4cd", "jp.3dg" },
        { "210b69b49ea60248ecf229e3a35c7832", "jp.3dt" },
        { "e2252dea49554e9d51482e6ba0a22a5b", "jp.spr" },
        { "c499fd1f955fde158159b770eabbc11e", "jp.wld" },
        { "157e724e8daa31336fe9f06255bd73c4", "labs.pic" },
        { "0b8866ceaad15bc03bd7897bd84c66d5", "lb.3d3" },
        { "d10c1d6cc9ade429e6dd81d9f2efc9de", "lb.3dg" },
        { "73b3bfa86cd57a507a2e1591db45ed76", "lb.3dt" },
        { "789455e28757793fec416cc444477063", "left.pic" },
        { "fb234541a8fd588684c80417eebbf729", "libya.spr" },
        { "34ac7cabf7c200aaffe72cf548978fa8", "libya.wld" },
        { "335931f93a4a5bb88129ea28ced2ae8c", "me.3d3" },
        { "ad0dbc926be9758b623995ee70bb777b", "me.3dg" },
        { "0e5a2dc77f195a8e51976575e7160282", "me.3dt" },
        { "3f75ad96c4d9c61842d58e087394d968", "medal.pic" },
        { "8079c4abfa88c04aad08e82908b7554e", "me.spr" },
        { "1e9407d174654d59a2bd34d5fe052caa", "me.wld" },
        { "f8ec10b3de5dfd555748497cc5f88f63", "nc.3d3" },
        { "2534b73170ae55e00b1ecfa706d5e04e", "nc.3dg" },
        { "ace87d2c23046f0fecf50c04e847ab3c", "nc.3dt" },
        { "a019f6eade84d4d6a2c8034850979c14", "ncape.spr" },
        { "4ecabfb63f451c989a51feca22048e19", "nc.wld" },
        { "fff6a0e3d2d16af739d9a36eb413339f", "persian.spr" },
        { "570679af0f0bb2d44eb16226d557bbed", "pg.3d3" },
        { "4cfaf482d83f9a90fe30734e7a701918", "pg.3dg" },
        { "46ced2bc0128d95674e10521fee61cd4", "pg.3dt" },
        { "5527467253f038e296fc2519606e38aa", "photo.3d3" },
        { "fa3efcb64547c7fc73bc165b4d06bbf2", "promo.pic" },
        { "0bbc6acfeaef8c7988cd5e75aaaf6320", "rear.pic" },
        { "94190aa3fe2f7de2395b15dab9c04a53", "right.pic" },
        { "bd7a9c10dcd06fec62af1071a678ad7f", "title16.pic" },
        { "14c7e302d9ba0b3567196f43b2a914a6", "title640.pic" },
        { "3e59272aa51365d4c5dc156844f3d4e8", "vn.3d3" },
        { "f54adf1df9788e90e3408d8609ecd40e", "vn.3dg" },
        { "31a214ca09ac9c5ea8ab61c2c2358928", "vn.3dt" },
        { "17e00b718ea797eff553ffca1d3ba2bb", "vn.spr" },
        { "ed64b9313161e9889454f33f6816ffc9", "vn.wld" },
        { "0839cb62142b5d3e5058b596ad36fb32", "wall.pic" },
    };
    for (const Asset &a : assets) {
        const string pathStr = resolveCasePath(a.filename.c_str(), true);
        if (pathStr.empty()) {
            const string msg = "Could not find asset file " + a.filename
                + " in game directory: " + gamePath.string();
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed",
                msg.c_str(), NULL);
            return false;
        }
        if (QuickDigest5::fileToHash(pathStr) != a.md5) {
            const string msg = "Checksum mismatch for asset file " + pathStr
                + ": expected " + a.md5;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed",
                msg.c_str(), NULL);
            return false;
        }
    }
    return true;
}

/* Keyboard profile parsing and storage, independent of the setup screen. */
#include "controls_mapping.h"
#include <fstream>

static const int keyboardProfileVersion = 1;
static const int supportedModifierBits = SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_SHIFT;

/* Keyboard mappings are global; raw joystick profiles remain per device. */
std::string controls_keyboardPath(void) {
    const char *overrideDir = SDL_getenv("F15_JOY_CONFIG_DIR");
    char *pref = overrideDir ? nullptr : SDL_GetPrefPath("f15se2-ex", "joystick");
    const std::string directory = overrideDir ? overrideDir : pref ? pref : "";
    SDL_free(pref);
    if (directory.empty() || !SDL_CreateDirectory(directory.c_str())) return {};
    return directory + "/keyboard.txt";
}

/* Parse atomically: invalid bindings never partially replace live controls. */
bool controls_loadKeyboard(const std::string &path) {
    std::ifstream file(path);
    std::string name;
    int version = 0;
    ControlBinding candidate[RAW_ACTION_COUNT] = {};
    if (!(file >> name >> version) || name != "F15_KEYBOARD" || version != keyboardProfileVersion) return false;
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) {
        int key = 0, mod = 0;
        if (!(file >> name >> key >> mod) || name != controls_action((RawAction)i).name ||
            key < SDL_SCANCODE_UNKNOWN || key >= SDL_SCANCODE_COUNT ||
            mod < 0 || (mod & ~supportedModifierBits) != 0) return false;
        const SDL_Scancode sc = (SDL_Scancode)key;
        const SDL_Keymod km = (SDL_Keymod)mod;
        candidate[i] = {sc, km};
    }
    if (file >> name || !file.eof()) return false;
    return controls_replaceKeyboard(candidate);
}

/* Replace the saved profile only after writing a complete temporary file. */
bool controls_saveKeyboard(const std::string &path) {
    if (path.empty()) return false;
    std::string data = "F15_KEYBOARD " + std::to_string(keyboardProfileVersion) + "\n";
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) {
        const RawAction action = (RawAction)i;
        const ControlBinding binding = controls_keyboardBinding(action);
        data += std::string(controls_action(action).name) + " " + std::to_string(binding.key) + " " + std::to_string(binding.mod) + "\n";
    }
    const std::string temp = path + "." + std::to_string(SDL_GetPerformanceCounter()) + ".tmp";
    const bool saved = SDL_SaveFile(temp.c_str(), data.data(), data.size()) && SDL_RenamePath(temp.c_str(), path.c_str());
    if (!saved) SDL_RemovePath(temp.c_str());
    return saved;
}

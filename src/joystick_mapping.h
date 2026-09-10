#ifndef F15_JOYSTICK_MAPPING_H
#define F15_JOYSTICK_MAPPING_H

#include "joystick.h"
#include <string>

/* Profile identity includes SDL GUID and control counts, not the device path. */
std::string joy_mappingPath(SDL_Joystick *joystick);
/* Invalid/incomplete profiles leave the caller's defaults unchanged. */
bool joy_loadMapping(const std::string &path, int count, int *buttons);
/* Replace a complete profile only after successfully writing its temporary file. */
bool joy_saveMapping(const std::string &path, int count, const int *buttons);

#endif

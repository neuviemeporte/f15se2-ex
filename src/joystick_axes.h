/* SDL exposes raw axis indices, but not their hardware meanings. Linux names
 * those axes in evdev/joydev metadata. Query metadata only: SDL still owns
 * calibration and all input events. Other platforms keep the explicit override. */
#ifndef F15_JOYSTICK_AXES_H
#define F15_JOYSTICK_AXES_H

#include <SDL3/SDL.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* joydev includes hats in its axis map; SDL removes them from analog indices.
 * evdev entries supplied below precede all hat codes, so the same rule applies. */
static inline int joy_linuxThrottleIndex(const unsigned char *codes, int count) {
    int axis = 0;
    for (int i = 0; i < count; ++i) {
        if (codes[i] >= ABS_HAT0X && codes[i] <= ABS_HAT3Y) continue;
        if (codes[i] == ABS_THROTTLE) return axis;
        ++axis;
    }
    return -1;
}
#endif

/* Read the same device SDL opened. Failure, virtual devices, and unnamed axes
 * deliberately return "unknown", never guessing that a third axis is thrust. */
static inline int joy_detectThrottleAxis(SDL_Joystick *joystick) {
#ifdef __linux__
    const char *path = SDL_GetJoystickPath(joystick);
    if (!path) return -1;
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    unsigned char codes[ABS_CNT] = {};
    unsigned char bits[(ABS_CNT + 7) / 8] = {};
    int count = 0;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) >= 0) {
        /* SDL's evdev backend enumerates supported codes in ascending order,
         * skipping codes whose EVIOCGABS query fails. ABS_THROTTLE is below
         * ABS_HAT0X, so hat classification cannot affect its axis index. */
        for (int code = 0; code <= ABS_THROTTLE; ++code) {
            struct input_absinfo info = {};
            if ((bits[code / 8] & (1u << (code % 8))) &&
                ioctl(fd, EVIOCGABS(code), &info) >= 0)
                codes[count++] = (unsigned char)code;
        }
    } else {
        unsigned char axes = 0;
        if (ioctl(fd, JSIOCGAXES, &axes) >= 0 && axes <= ABS_CNT &&
            ioctl(fd, JSIOCGAXMAP, codes) >= 0)
            count = axes;
    }
    close(fd);
    const int axis = joy_linuxThrottleIndex(codes, count);
    return axis < SDL_GetNumJoystickAxes(joystick) ? axis : -1;
#else
    (void)joystick;
    return -1;
#endif
}

#endif

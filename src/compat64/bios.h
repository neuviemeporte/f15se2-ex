#pragma once
// Minimal bios.h stub for 64-bit native builds

#ifndef _BIOS_H_COMPAT64
#define _BIOS_H_COMPAT64

#define _KEYBRD_READY 1
#define _KEYBRD_READ 0

/* Backed by the SDL keyboard layer in eginput.c (egame flight loop). */
int _bios_keybrd(int cmd);

#endif // _BIOS_H_COMPAT64

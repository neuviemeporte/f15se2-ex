#ifndef F15_SE2_EGTYPES
#define F15_SE2_EGTYPES
/* egame.exe compat macros + hardware/comm/overlay constants. */

#define far
#ifndef pascal
#define pascal
#endif

#define __int32 long
#define __int8 char
#define __cdecl
#define __far far

#define AIRCRAFT_MODELS_OFFSET 0xADD4
#define PORT_PIT_TIME0 0x40
#define PORT_PIT_CNTRL 0x43
#define COMM_GFXOVL_SEG 0x1a
#define COMM_SNDOVL_SEG 0x1c
#define COMM_WORLDBUF 0x7a
#define OFF_IACA_START 0x4f0
#define COMM_GAMEDATA_OFFSET 0x120e
#define IRQ_CBREAK 0x1b
#define GAMEDATA_DIFFICULTY 0x3e
#define GAMEDATA_UNK4 0x40
#define WAYPT_PRIMARY 1
#define WAYPT_SECONDARY 2
#define WAYPT_BASE 3
#define IRQ_VIDEO 0x10
#define OVL_HDR_CODESEG 0x18
#define OVL_HDR_FIRSTIDX 0x1c
#define UNIT_STATE_COUNT 0x64

#endif /* F15_SE2_EGTYPES */

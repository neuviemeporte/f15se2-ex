#include "dosfunc.h"
#include "inttype.h"
#include "log.h"
#include "memory.h"
#include "offsets.h"

#include <stdio.h>
#include <dos.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

typedef enum {
    DOSF_ALLOCMEM = 0x48,
    DOSF_FREEMEM = 0x49,
    DOSF_RESIZEMEM = 0x4a,
    DOSF_LOADPROG = 0x4b,
    DOSF_RETURNCODE = 0x4d,
    DOSF_SYSVARS = 0x52,
} DosFunctions;

typedef enum {
    DOSERR_NONE = 0x00,       // (0)   no error
    DOSERR_INVFUNC = 0x01,    // (1)   function number invalid
    DOSERR_FILENF = 0x02,     // (2)   file not found
    DOSERR_PATHNF = 0x03,     // (3)   path not found
    DOSERR_HANDLES = 0x04,    // (4)   too many open files (no handles available)
    DOSERR_ACCESS = 0x05,     // (5)   access denied
    DOSERR_BADHANDLE = 0x06,  // (6)   invalid handle
    DOSERR_MCBDEST = 0x07,    // (7)   memory control block destroyed
    DOSERR_NOMEM = 0x08,      // (8)   insufficient memory
    DOSERR_MCBINV = 0x09,     // (9)   memory block address invalid
    DOSERR_ENVINV = 0x0A,     // (10)  environment invalid (usually >32K in length)
    DOSERR_FMTINV = 0x0B,     // (11)  format invalid
    DOSERR_AXSCODEINV = 0x0C, // (12)  access code invalid
    DOSERR_DATAINV = 0x0D,    // (13)  data invalid
    DOSERR_RESERVED = 0x0E,   // (14)  reserved
    DOSERR_OFLOW = 0x0E,      // (14)  (PTS-DOS 6.51+, S/DOS 1.0+) fixup overflow
    DOSERR_DRIVEINV = 0x0F,   // (15)  invalid drive
    DOSERR_RMDIRCUR = 0x10,   // (16)  attempted to remove current directory
    DOSERR_NSAMEDEV = 0x11,   // (17)  not same device
    DOSERR_FILESNMORE = 0x12, // (18)  no more files
} DosError;

static union REGS rin, rout;
static struct SREGS sreg;

void *dos_alloc(const size_t paragraphs) {
    return malloc(paragraphs * 16);
}

// Format of EXEC parameter block for AL=00h,01h,04h:
// Offset Size Description (Table 01590)
// 00h WORD segment of environment to copy for child process (copy caller's environment if 0000h)
// 02h DWORD pointer to command tail to be copied into child's PSP
// 06h DWORD pointer to first FCB to be copied into child's PSP
// 0Ah DWORD pointer to second FCB to be copied into child's PSP
// 0Eh DWORD (AL=01h) will hold subprogram's initial SS:SP on return
// 12h DWORD (AL=01h) will hold entry point (CS:IP) on return
#pragma pack(1)
struct {
    uint16 envSegment;
    uint16 cmdlineOffset;
    uint16 cmdlineSegment;
    uint16 fcb1Offset;
    uint16 fcb1Segment;
    uint16 fcb2Offset;
    uint16 fcb2Segment;
    uint16 sp;
    uint16 ss;
    uint16 ip;
    uint16 cs;
} exeLoadParams;
#pragma pack()
STATIC_ASSERT(sizeof(exeLoadParams) == 22);

#pragma pack(1)
struct {
    uint16 segment;
    uint16 reloc;
} ovlLoadParams;
#pragma pack()
STATIC_ASSERT(sizeof(ovlLoadParams) == 4);

#define DOS_LOAD_EXEC 0
#define DOS_LOAD_NOEXEC 1
#define DOS_LOAD_OVL 3

static int loadprog(const char *file, const uint16 segment, const uint8 type, const char FAR *cmdline) {
    int err;
    rin.h.ah = DOSF_LOADPROG;
    rin.h.al = type;
    rin.x.dx = 0; // (unsigned int)file;
    switch (type) {
    case DOS_LOAD_EXEC:
    case DOS_LOAD_NOEXEC:
        exeLoadParams.envSegment = 0; // 0 - copy caller's environment
        exeLoadParams.cmdlineOffset = FP_OFF(cmdline);
        exeLoadParams.cmdlineSegment = FP_SEG(cmdline);
        // the two FCBs are located at offsets 0x5c and 0x6c in this program's PSP
        exeLoadParams.fcb1Offset = 0x5c;
        exeLoadParams.fcb1Segment = _psp;
        exeLoadParams.fcb2Offset = 0x6c;
        exeLoadParams.fcb2Segment = _psp;
        rin.x.bx = 0; // (unsigned int)&exeLoadParams;
        break;
    case DOS_LOAD_OVL:
        ovlLoadParams.segment = segment;
        ovlLoadParams.reloc = segment; // no idea, original does the same
        rin.x.bx = 0;                  // (unsigned int)&ovlLoadParams;
        break;
    default:
        LogError(("dos_loadprog(): unsupported load type: 0x%hx", type));
        return DOSERR_INVFUNC;
    }
    err = intdos(&rin, &rout);
    if (rout.x.cflag != 0) {
        LogError(("dos_loadprog: unable to load %s at 0x%x, error 0x%x", file, segment, err));
        return err;
    }
    return 0;
}

#pragma pack(1)
struct MCB {
    char type;
    uint16 pid;
    uint16 size;
    uint8 reserved[3];
    char desc[8];
};
#pragma pack()
STATIC_ASSERT(sizeof(struct MCB) == 16);

static uint8 FAR *dos_sysvars(void) {
    rin.h.ah = DOSF_SYSVARS;
    intdosx(&rin, &rout, &sreg);
    return (uint8 FAR *)MK_FP(sreg.es, rout.x.bx);
}

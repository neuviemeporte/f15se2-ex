#ifndef ASOUND_DOS_OPL_H
#define ASOUND_DOS_OPL_H

/* pc.h also declares kbhit(), which the game supplies through compat64. */
#include <inlines/pc.h>

/* AdLib-compatible OPL2 registers, also decoded by Sound Blaster cards. */
enum {
    ASND_OPL_ADDRESS_PORT = 0x388,
    ASND_OPL_DATA_PORT = 0x389,
    ASND_OPL_ADDRESS_DELAY_READS = 6,
    ASND_OPL_DATA_DELAY_READS = 35
};

static void asnd_writeDosOpl(unsigned char reg, unsigned char value) {
    outportb(ASND_OPL_ADDRESS_PORT, reg);
    /* Status reads supply the OPL2 bus delays without yielding halfway through
     * a register write: about 3.3 us after address, 23 us after data. */
    for (int i = 0; i < ASND_OPL_ADDRESS_DELAY_READS; ++i)
        inportb(ASND_OPL_ADDRESS_PORT);
    outportb(ASND_OPL_DATA_PORT, value);
    for (int i = 0; i < ASND_OPL_DATA_DELAY_READS; ++i)
        inportb(ASND_OPL_ADDRESS_PORT);
}

#endif

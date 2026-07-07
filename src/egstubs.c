#include "egtypes.h"
#include "egcode.h"
#include "egdata.h"
#include "inttype.h"
#include "struct.h"
#include "comm.h"
#include <stdio.h>
#include "dos_compat.h"

/* Joystick input lives in joystick.c (SDL gamepad/joystick). */
/* setInt9Handler/restoreInt9Handler (the keyboard ISR) live in eginput.c. */

/* --- functions declared in egcode.h --- */
int16 drawCenteredLabelBox(int16 panel, const char *text) { return 0; } // Real one is also a nop

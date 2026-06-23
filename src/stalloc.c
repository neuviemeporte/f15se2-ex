/* Memory allocation */
#include "dosfunc.h"
#include "stalloc.h"
#include "stcode.h"
#include "stdata.h"
#include "sttypes.h"
#include "shared/common.h"
#include "debug.h"

#include <dos.h>

void *allocBuffer(int size) {
    void *segment;
    TRACE(("allocBuffer(): Allocating buffer of size %u", size));
    if ((segment = dos_alloc(size)) == nullptr) {
        cleanup();
        dos_printstring("Insufficient system memory - AllocBuffer$");
        exit(0);
    }
    TRACE(("allocBuffer(): Allocated @ 0x%x", segment));
    return segment;
}

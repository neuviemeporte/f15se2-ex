/* Memory allocation */
#include "dosfunc.h"
#include "stalloc.h"
#include "stcode.h"
#include "stdata.h"
#include "sttypes.h"
#include "shared/common.h"
#include "log.h"

#include <dos.h>

void *allocBuffer(int16 size) {
    void *segment;
    if ((segment = dos_alloc(size)) == nullptr) {
        cleanup();
        dos_printstring("Insufficient system memory - AllocBuffer$");
        exit(0);
    }
    return segment;
}

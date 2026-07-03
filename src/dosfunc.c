#include "dosfunc.h"

#include <stdlib.h>

void *dos_alloc(const size_t paragraphs) {
    return malloc(paragraphs * 16);
}

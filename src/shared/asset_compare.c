/*
 * asset_compare.c - test-only replacement comparison switch.
 *
 * Old-vs-modern equivalence proof belongs in CTest/Python validation, not in a
 * normal game run. Keep the comparison helpers linkable for existing focused
 * tests, but never enable them from runtime environment variables.
 */

#include "asset_compare.h"
#include <stdlib.h>

int assetCompareEnabled(void) {
    return 0;
}

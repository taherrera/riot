/*
 * Copyright (C) 2015 Kaspar Schleiser <kaspar@schleiser.de>
 *               2017 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       xtimer_now64 continuity test application
 *
 * @author      Kaspar Schleiser <kaspar@schleiser.de>
 * @author      Sebastian Meiling <s@mlng.net>
 *
 * @}
 */

#include <stdio.h>
#include <stdint.h>

#include "xtimer.h"

#define ITERATIONS  (100000LU)
#define MAXDIFF     (1000U)

int main(void)
{
    uint32_t n = ITERATIONS;
    uint64_t diff_min = UINT64_MAX;
    uint64_t diff_max = 0;
    uint64_t diff_sum = 0;
    xtimer_ticks64_t before = xtimer_now64();
    puts("[START]");
    while(--n) {
        xtimer_ticks64_t now = xtimer_now64();
        xtimer_ticks64_t diff = xtimer_diff64(now, before);
        if (diff.ticks64 > diff_max) {
            diff_max = diff.ticks64;
        }
        if (diff.ticks64 < diff_min) {
            diff_min = diff.ticks64;
        }
        diff_sum += diff.ticks64;
        before = now;
    }
    printf("[RESULTS] min=%"PRIu64", avg=%"PRIu64", max=%"PRIu64"\n",
           diff_min, diff_sum/ITERATIONS, diff_max);
    if ((diff_max > MAXDIFF) || (diff_sum/ITERATIONS > MAXDIFF)) {
        puts("[FAILURE]");
        return 1;
    }
    puts("[SUCCESS]");
    return 0;
}

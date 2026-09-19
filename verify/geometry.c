/* CBMC harness: for every valid config with gp <= GP_MAX and mvp <= MVP_MAX
 * and every in-range value v, the bucket index is in range, the bucket
 * contains v, and consecutive buckets tile the value space with no gap. */
#include "../src/h2histogram.c"

#ifndef GP_MAX
#define GP_MAX 4
#endif
#ifndef MVP_MAX
#define MVP_MAX 12
#endif

uint32_t nondet_u32(void);
uint64_t nondet_u64(void);

void harness(void) {
    uint32_t gp = nondet_u32(), mvp = nondet_u32();
    __CPROVER_assume(gp <= GP_MAX && mvp <= MVP_MAX);
    h2_config c;
    if (h2_config_init(&c, gp, mvp) != H2_OK)
        return;

    uint64_t v = nondet_u64();
    __CPROVER_assume(v <= c.max);

    uint32_t i;
    __CPROVER_assert(h2_index(c, v, &i) == H2_OK, "index succeeds for in-range value");
    __CPROVER_assert(i < c.buckets, "index within bucket count");

    uint64_t lo, hi;
    __CPROVER_assert(h2_bounds(c, i, &lo, &hi) == H2_OK, "bounds succeed");
    __CPROVER_assert(lo <= v && v <= hi, "bucket contains the value");
    __CPROVER_assert(lo <= hi, "bucket is non-empty");

    if (i + 1 < c.buckets) {
        uint64_t lo2, hi2;
        __CPROVER_assert(h2_bounds(c, i + 1, &lo2, &hi2) == H2_OK, "next bounds succeed");
        __CPROVER_assert(lo2 == hi + 1, "consecutive buckets are contiguous");
    } else {
        __CPROVER_assert(hi == c.max, "last bucket ends at max");
    }
    if (i == 0)
        __CPROVER_assert(lo == 0, "first bucket starts at zero");

    /* An out-of-range value is rejected without touching the output. */
    if (c.max != UINT64_MAX) {
        uint32_t untouched = 0xdeadbeef;
        __CPROVER_assert(h2_index(c, c.max + 1, &untouched) == H2_RANGE, "out of range rejected");
        __CPROVER_assert(untouched == 0xdeadbeef, "output untouched on error");
    }
}

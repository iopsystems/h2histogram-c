/* CBMC harness: h2_downsample conserves mass in every phase. The source uses
 * gp=2, mvp=5 (16 buckets) and is downsampled to grouping power GP (0 or 1,
 * one run each; a symbolic target makes the result's allocation size symbolic
 * and the model intractable). Each coarse bucket must hold exactly the sum of
 * the fine buckets whose lower bound maps into it, H2_OVERFLOW must be reported
 * exactly when such a sum (or, for cumulative, the running prefix) is not
 * representable, and no partial result may escape.
 *
 * The source is built directly. Dense: all 16 counters arbitrary. Sparse and
 * cumulative: LEN entries (0..3, one run each) with arbitrary strictly
 * ascending indices and nonzero counts; cumulative prefixes are kept within
 * u64 as every valid cumulative object requires. The reference is computed in
 * the harness from index/bounds, which geometry.c proves separately. Run with
 * --no-malloc-may-fail. */
#include "../src/h2histogram.c"

#ifndef PHASE
#define PHASE H2_DENSE
#endif
#ifndef LEN
#define LEN 2
#endif
#ifndef GP
#define GP 1
#endif
#define FINE 16

uint64_t nondet_u64(void);
uint32_t nondet_u32(void);

void harness(void) {
    h2_config c, coarse;
    __CPROVER_assume(h2_config_init(&c, 2, 5) == H2_OK);
    __CPROVER_assert(c.buckets == FINE, "harness geometry");
    const uint32_t gp = GP;
    __CPROVER_assert(gp < c.grouping_power, "harness GP is a real downsample");
    __CPROVER_assume(h2_config_init(&coarse, gp, c.max_value_power) == H2_OK);

    h2_hist *h = NULL;
    uint64_t fine[FINE] = {0};
    if (PHASE == H2_DENSE) {
        __CPROVER_assume(allocate(c, H2_DENSE, FINE, &h) == H2_OK);
        for (size_t i = 0; i < FINE; i++)
            fine[i] = h->counts[i] = nondet_u64();
    } else {
        __CPROVER_assume(allocate(c, PHASE, LEN, &h) == H2_OK);
        uint64_t running = 0;
        for (size_t k = 0; k < LEN; k++) {
            uint32_t ix = nondet_u32();
            uint64_t v = nondet_u64();
            __CPROVER_assume(ix < FINE && (k == 0 || ix > h->indices[k - 1]) && v != 0);
            if (PHASE == H2_CUMULATIVE) {
                __CPROVER_assume(v <= UINT64_MAX - running);
                running += v;
            }
            h->indices[k] = ix;
            h->counts[k] = PHASE == H2_CUMULATIVE ? running : v;
            fine[ix] = v;
        }
    }

    uint64_t expect[FINE] = {0};
    _Bool overflow = 0;
    for (uint32_t i = 0; i < FINE; i++) {
        uint32_t j = index_unchecked(coarse, lower(c, i));
        __CPROVER_assert(j < coarse.buckets, "coarse index in range");
        if (fine[i] > UINT64_MAX - expect[j])
            overflow = 1; /* expect is irrelevant once a sum is unrepresentable */
        else
            expect[j] += fine[i];
    }
    if (PHASE == H2_CUMULATIVE)
        __CPROVER_assert(!overflow, "a valid cumulative source cannot overflow a coarse bucket");

    h2_hist *d = NULL;
    h2_status s = h2_downsample(h, gp, &d);
    __CPROVER_assert(s == (overflow ? H2_OVERFLOW : H2_OK), "overflow iff a coarse sum overflows");
    if (s != H2_OK) {
        __CPROVER_assert(d == NULL, "no partial result");
    } else {
        __CPROVER_assert(d->phase == PHASE, "phase preserved");
        __CPROVER_assert(d->config.grouping_power == gp &&
                             d->config.max_value_power == c.max_value_power,
                         "coarse geometry");
        uint64_t got[FINE] = {0};
        if (PHASE == H2_DENSE) {
            __CPROVER_assert(d->length == coarse.buckets, "dense length is the bucket count");
            for (size_t j = 0; j < coarse.buckets; j++)
                got[j] = d->counts[j];
        } else {
            __CPROVER_assert(d->length <= LEN && d->capacity == LEN, "never grows entries");
            for (size_t k = 0; k < d->length; k++) {
                __CPROVER_assert(d->indices[k] < coarse.buckets, "coarse entry in range");
                if (k)
                    __CPROVER_assert(d->indices[k] > d->indices[k - 1], "coarse indices ascend");
                uint64_t v = individual(d, k);
                __CPROVER_assert(v != 0, "coarse entries are nonzero");
                got[d->indices[k]] = v;
            }
        }
        for (size_t j = 0; j < FINE; j++)
            __CPROVER_assert(got[j] == expect[j], "coarse bucket is the exact sum");
    }
    h2_free(d);
    h2_free(h);
}

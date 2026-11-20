/* CBMC harness: sparse and cumulative h2_merge compute the exact per-bucket
 * sum of two valid snapshots, and fail without a partial result exactly when
 * that sum is not representable.
 *
 * The inputs are built directly: LA and LB entries (0..3 each, one run per
 * pair) with arbitrary strictly ascending in-range indices and arbitrary
 * nonzero counts, stored as counts (sparse) or checked prefixes (cumulative).
 * That covers disjoint, interleaved, overlapping and empty inputs with every
 * counter value. The expected result is computed in the harness on a dense
 * array, so no other library transform is trusted. Lengths are compile-time
 * constants because symbolic allocation sizes make the model intractable.
 * Run with --no-malloc-may-fail; allocation failure is covered by compact.c
 * and the allocation test suite.
 *
 * Sparse proofs use full 64-bit counts. Cumulative merge at full width is
 * SAT-hard (prefix sums plus the subtraction that recovers counts): two entries
 * per side did not finish in five minutes. Cumulative proofs therefore narrow
 * each count to BITS symbolic bits, run twice. With SHIFT=0 the values are
 * 1..255 and overflow is impossible, which proves exact sums for every
 * interleaving. With SHIFT=56 the values are multiples of 2^56, so sums carry
 * out of bit 63 and the same run proves both the overflow decision and exact
 * sums on the high bits. The merge code is width-agnostic, so both runs drive
 * every branch. */
#include "../src/h2histogram.c"

#ifndef PHASE
#define PHASE H2_SPARSE
#endif
#ifndef LA
#define LA 2
#endif
#ifndef LB
#define LB 2
#endif
#ifndef BITS
#define BITS 64 /* width of each symbolic count; see the header comment */
#endif
#ifndef SHIFT
#define SHIFT 0 /* left shift applied to each narrowed count */
#endif
#define BUCKETS 6

uint64_t nondet_u64(void);
uint32_t nondet_u32(void);

/* Fill a snapshot with a valid symbolic state and mirror it densely. */
static void populate(h2_hist *h, size_t n, uint64_t dense[BUCKETS], uint64_t *total) {
    uint64_t running = 0;
    for (size_t k = 0; k < n; k++) {
        uint32_t ix = nondet_u32();
        uint64_t v = nondet_u64();
        __CPROVER_assume(ix < BUCKETS && (k == 0 || ix > h->indices[k - 1]));
        if (BITS < 64) {
            __CPROVER_assume(v < (UINT64_C(1) << (BITS % 64)));
            v <<= SHIFT;
        }
        __CPROVER_assume(v != 0 && v <= UINT64_MAX - running); /* valid objects have a u64 total
                                                                  only in the cumulative phase,
                                                                  but bounding both keeps the
                                                                  reference arithmetic exact */
        running += v;
        h->indices[k] = ix;
        h->counts[k] = PHASE == H2_CUMULATIVE ? running : v;
        dense[ix] = v;
    }
    *total = running;
}

void harness(void) {
    h2_config c;
    __CPROVER_assume(h2_config_init(&c, 1, 3) == H2_OK);
    __CPROVER_assert(c.buckets == BUCKETS, "harness geometry");

    h2_hist *a = NULL, *b = NULL;
    __CPROVER_assume(allocate(c, PHASE, LA, &a) == H2_OK);
    __CPROVER_assume(allocate(c, PHASE, LB, &b) == H2_OK);
    uint64_t da[BUCKETS] = {0}, db[BUCKETS] = {0}, ta, tb;
    populate(a, LA, da, &ta);
    populate(b, LB, db, &tb);

    _Bool bucket_overflow = 0;
    for (size_t j = 0; j < BUCKETS; j++)
        if (db[j] > UINT64_MAX - da[j])
            bucket_overflow = 1;
    _Bool total_overflow = tb > UINT64_MAX - ta;
    /* Sparse merge checks each bucket; cumulative merge must also fit the prefix. */
    _Bool expect_overflow = PHASE == H2_CUMULATIVE ? total_overflow : bucket_overflow;
    __CPROVER_assert(!bucket_overflow || total_overflow,
                     "a bucket overflow implies total overflow");

    h2_hist *m = NULL;
    h2_status s = h2_merge(a, b, &m);
    __CPROVER_assert(s == (expect_overflow ? H2_OVERFLOW : H2_OK), "overflow iff unrepresentable");
    if (s != H2_OK) {
        __CPROVER_assert(m == NULL, "no partial result");
    } else {
        __CPROVER_assert(m->phase == PHASE, "phase preserved");
        __CPROVER_assert(m->length <= LA + LB && m->capacity == LA + LB, "length within capacity");
        uint64_t got[BUCKETS] = {0};
        for (size_t k = 0; k < m->length; k++) {
            __CPROVER_assert(m->indices[k] < BUCKETS, "merged index in range");
            if (k)
                __CPROVER_assert(m->indices[k] > m->indices[k - 1], "merged indices ascend");
            uint64_t v = individual(m, k);
            __CPROVER_assert(v != 0, "merged entries are nonzero");
            got[m->indices[k]] = v;
        }
        for (size_t j = 0; j < BUCKETS; j++)
            __CPROVER_assert(got[j] == da[j] + db[j], "merged bucket is the exact sum");
        if (PHASE == H2_CUMULATIVE && m->length)
            __CPROVER_assert(m->counts[m->length - 1] == ta + tb, "last prefix is the total");
    }

    /* Sources are untouched either way. */
    uint64_t check = 0;
    for (size_t k = 0; k < LA; k++) {
        uint64_t v = individual(a, k);
        __CPROVER_assert(v == da[a->indices[k]], "left source unchanged");
        check += v;
    }
    __CPROVER_assert(check == ta, "left total unchanged");

    h2_free(m);
    h2_free(a);
    h2_free(b);
}

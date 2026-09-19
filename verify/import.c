/* CBMC harness: h2_import accepts exactly the documented shapes. For every
 * sparse or cumulative input of up to 3 entries with arbitrary indices and
 * counts, import succeeds iff indices are in range and strictly ascending and,
 * for cumulative, the first prefix is positive and prefixes never decrease.
 * On success the stored entries are the input with zero counts (sparse) or
 * repeated prefixes (cumulative) removed, so dense round-trip and total agree
 * with the input. On failure the output pointer is untouched. Run with
 * --no-malloc-may-fail: allocation failure is covered by compact.c and the
 * allocation test suite, and would otherwise make success non-deterministic. */
#include "../src/h2histogram.c"

#ifndef PHASE
#define PHASE H2_SPARSE
#endif

uint64_t nondet_u64(void);
uint32_t nondet_u32(void);
size_t nondet_size(void);

void harness(void) {
    h2_config c;
    __CPROVER_assume(h2_config_init(&c, 1, 3) == H2_OK); /* 6 buckets */
    size_t n = nondet_size();
    __CPROVER_assume(n <= 3);
    uint32_t is[4];
    uint64_t ns[4];
    for (size_t i = 0; i < n; i++) {
        is[i] = nondet_u32();
        ns[i] = nondet_u64();
    }

    _Bool ok = 1;
    for (size_t i = 0; i < n; i++) {
        if (is[i] >= c.buckets)
            ok = 0;
        if (i && is[i] <= is[i - 1])
            ok = 0;
        if (PHASE == H2_CUMULATIVE && i && ns[i] < ns[i - 1])
            ok = 0;
    }
    if (PHASE == H2_CUMULATIVE && n && ns[0] == 0)
        ok = 0;

    h2_hist *h = NULL;
    h2_status s = h2_import(c, PHASE, is, ns, n, &h);
    __CPROVER_assert((s == H2_OK) == ok, "import succeeds iff shape is valid");
    if (s != H2_OK) {
        __CPROVER_assert(h == NULL, "output untouched on rejection");
        return;
    }

    /* Expected dense counts from the input, with individual counts derived
     * from prefixes for the cumulative phase. */
    uint64_t expect[8] = {0};
    for (size_t i = 0; i < n; i++)
        expect[is[i]] = PHASE == H2_CUMULATIVE ? ns[i] - (i ? ns[i - 1] : 0) : ns[i];

    __CPROVER_assert(h2_length(h) <= n, "normalization never adds entries");
    for (size_t k = 0; k < h2_length(h); k++) {
        __CPROVER_assert(individual(h, k) != 0, "stored entries are nonzero");
        if (k)
            __CPROVER_assert(h->indices[k] > h->indices[k - 1], "stored indices ascend");
    }

    if (PHASE == H2_CUMULATIVE) {
        uint64_t total;
        __CPROVER_assert(h2_total(h, &total) == H2_OK && total == (n ? ns[n - 1] : 0),
                         "cumulative total is the last prefix");
    }
    h2_hist *dense = NULL;
    __CPROVER_assert(h2_convert(h, H2_DENSE, &dense) == H2_OK, "dense round trip");
    for (size_t j = 0; j < c.buckets; j++)
        __CPROVER_assert(dense->counts[j] == expect[j], "dense counts match input");
    h2_free(dense);
    h2_free(h);
}

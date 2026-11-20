/* CBMC harness: h2_add is transactional. For any two dense histograms of a
 * small fixed geometry with arbitrary counters, either every counter is the
 * exact sum, or H2_OVERFLOW is returned and dst is byte-for-byte unchanged.
 * Also covers self-add (dst == src). */
#include "../src/h2histogram.c"

uint64_t nondet_u64(void);
_Bool nondet_bool(void);

void harness(void) {
    h2_config c;
    __CPROVER_assume(h2_config_init(&c, 1, 3) == H2_OK); /* 6 buckets */
    h2_hist *dst = NULL, *src = NULL;
    __CPROVER_assume(h2_create(c, &dst) == H2_OK);
    _Bool self = nondet_bool();
    if (self) {
        src = dst;
    } else {
        __CPROVER_assume(h2_create(c, &src) == H2_OK);
    }
    for (size_t i = 0; i < c.buckets; i++) {
        dst->counts[i] = nondet_u64();
        if (!self)
            src->counts[i] = nondet_u64();
    }
    uint64_t before_dst[8], before_src[8];
    for (size_t i = 0; i < c.buckets; i++) {
        before_dst[i] = dst->counts[i];
        before_src[i] = src->counts[i];
    }

    h2_status s = h2_add(dst, src);
    __CPROVER_assert(s == H2_OK || s == H2_OVERFLOW, "only OK or OVERFLOW");
    _Bool any_overflow = 0;
    for (size_t i = 0; i < c.buckets; i++)
        if (before_src[i] > UINT64_MAX - before_dst[i])
            any_overflow = 1;
    __CPROVER_assert((s == H2_OVERFLOW) == any_overflow, "overflow iff some bucket overflows");
    for (size_t i = 0; i < c.buckets; i++) {
        if (s == H2_OK)
            __CPROVER_assert(dst->counts[i] == before_dst[i] + before_src[i], "exact sum");
        else
            __CPROVER_assert(dst->counts[i] == before_dst[i], "dst unchanged on overflow");
        if (!self)
            __CPROVER_assert(src->counts[i] == before_src[i], "src never modified");
    }
    h2_free(dst);
    if (!self)
        h2_free(src);
}

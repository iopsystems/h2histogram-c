#include "h2histogram.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void __real_free(void *);
static size_t calls, live, fail_at;
void *__wrap_malloc(size_t n) {
    if (++calls == fail_at)
        return NULL;
    void *p = __real_malloc(n);
    if (p)
        live++;
    return p;
}

void *__wrap_calloc(size_t n, size_t s) {
    if (++calls == fail_at)
        return NULL;
    void *p = __real_calloc(n, s);
    if (p)
        live++;
    return p;
}

void __wrap_free(void *p) {
    if (p) {
        assert(live);
        live--;
    }
    __real_free(p);
}

static h2_status operation(unsigned op, h2_config c, const h2_hist *h, const h2_hist *s,
                           h2_hist **out) {
    uint32_t is[] = {0, 1};
    uint64_t ns[] = {3, 4};
    const h2_hist *hs[] = {h, h};
    switch (op) {
    case 0:
        return h2_create(c, out);
    case 1:
        return h2_import(c, H2_SPARSE, is, ns, 2, out);
    case 2:
        return h2_convert(h, H2_CUMULATIVE, out);
    case 3:
        return h2_merge(s, s, out);
    case 4:
        return h2_downsample(s, 0, out);
    default:
        return h2_sum(hs, 2, out);
    }
}

int main(void) {
    h2_config c;
    assert(h2_config_init(&c, 2, 8) == H2_OK);
    h2_hist *h = NULL, *s = NULL, *u = NULL, *copy = NULL;
    assert(h2_create(c, &h) == H2_OK);
    assert(h2_create(c, &copy) == H2_OK);
    assert(h2_record(h, 5, 5) == H2_OK);
    assert(h2_convert(h, H2_SPARSE, &s) == H2_OK);
    assert(h2_convert(h, H2_CUMULATIVE, &u) == H2_OK);
    size_t baseline = live;
    for (unsigned op = 0; op < 6; op++)
        for (size_t failure = 1; failure <= 4; failure++) {
            calls = 0;
            fail_at = failure;
            h2_hist *out = NULL;
            h2_status e = operation(op, c, h, s, &out);
            fail_at = 0;
            if (e == H2_NOMEM)
                assert(out == NULL);
            else {
                assert(e == H2_OK);
                h2_free(out);
            }
            assert(live == baseline);
        }
    const h2_hist *phases[] = {h, s, u};
    calls = 0;
    fail_at = 1;
    for (unsigned i = 0; i < 3; i++) {
        double ps[] = {1, 0, .5};
        h2_bucket bs[3];
        double mean;
        uint64_t total;
        assert(h2_percentiles(phases[i], ps, 3, bs) == H2_OK);
        assert(h2_percentile(phases[i], .99, bs) == H2_OK);
        assert(h2_mean(phases[i], &mean) == H2_OK);
        assert(h2_total(phases[i], &total) == H2_OK);
    }
    assert(h2_snapshot_into(h, copy) == H2_OK);
    assert(h2_add(copy, h) == H2_OK);
    assert(h2_drain_into(h, copy) == H2_OK);
    assert(h2_reset(copy) == H2_OK);
    assert(calls == 0);
    fail_at = 0;
    h2_free(h);
    h2_free(s);
    h2_free(u);
    h2_free(copy);
    assert(live == 0);
    puts("allocation failure and zero-allocation report tests passed");
    return 0;
}

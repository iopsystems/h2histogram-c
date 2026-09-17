#include "h2histogram.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define OK(x) assert((x) == H2_OK)
static void geometry(void) {
    FILE *f = fopen(H2_FIXTURE_PATH, "r");
    assert(f);
    char line[256];
    assert(fgets(line, sizeof line, f));
    while (fgets(line, sizeof line, f)) {
        unsigned gp, mp, idx, nb;
        uint64_t v, lo, hi;
        assert(sscanf(line, "%u,%u,%" SCNu64 ",%u,%" SCNu64 ",%" SCNu64 ",%u", &gp, &mp, &v, &idx,
                      &lo, &hi, &nb) == 7);
        h2_config c;
        uint32_t i;
        uint64_t a, b;
        OK(h2_config_init(&c, gp, mp));
        assert(c.buckets == nb);
        OK(h2_index(c, v, &i));
        assert(i == idx);
        OK(h2_bounds(c, i, &a, &b));
        assert(a == lo && b == hi);
    }
    fclose(f);
    h2_config c;
    assert(h2_config_init(&c, 31, 64) == H2_INVALID);
    assert(h2_config_init(&c, 7, 7) == H2_INVALID);
}

static void contracts(void) {
    h2_config c;
    OK(h2_config_init(&c, 2, 8));
    h2_hist *h = NULL, *copy = NULL, *s = NULL, *u = NULL, *d = NULL, *merged = NULL, *sum = NULL;
    OK(h2_create(c, &h));
    OK(h2_create(c, &copy));
    OK(h2_record(h, 1, 3));
    OK(h2_record(h, 100, 7));
    assert(h2_record(h, 256, 1) == H2_RANGE);
    OK(h2_snapshot_into(h, copy));
    assert(h2_drain_into(h, h) == H2_INVALID);
    OK(h2_convert(h, H2_SPARSE, &s));
    OK(h2_convert(s, H2_CUMULATIVE, &u));
    double ps[] = {1, 0, .5, .5};
    h2_bucket a[4], b[4];
    OK(h2_percentiles(h, ps, 4, a));
    OK(h2_percentiles(u, ps, 4, b));
    assert(memcmp(a, b, sizeof a) == 0);
    assert(a[0].count == 7 && a[1].count == 3);
    h2_bucket sentinel = {44, 55, 66};
    b[0] = sentinel;
    double bad[] = {0, NAN};
    assert(h2_percentiles(s, bad, 2, b) == H2_INVALID);
    assert(memcmp(b, &sentinel, sizeof sentinel) == 0);
    double m;
    OK(h2_mean(u, &m));
    assert(m > 69 && m < 75);
    OK(h2_downsample(u, 1, &d));
    uint64_t total;
    OK(h2_total(d, &total));
    assert(total == 10);
    OK(h2_merge(u, u, &merged));
    OK(h2_total(merged, &total));
    assert(total == 20);
    const h2_hist *inputs[] = {h, copy};
    OK(h2_sum(inputs, 2, &sum));
    OK(h2_total(sum, &total));
    assert(total == 20);
    OK(h2_drain_into(h, copy));
    OK(h2_total(h, &total));
    assert(total == 0);
    assert(h2_percentile(h, .5, &sentinel) == H2_EMPTY);
    assert(sentinel.count == 44);
    OK(h2_total(copy, &total));
    assert(total == 10);
    h2_reset(copy);
    OK(h2_total(s, &total));
    assert(total == 10);
    h2_free(h);
    h2_free(copy);
    h2_free(s);
    h2_free(u);
    h2_free(d);
    h2_free(merged);
    h2_free(sum);
}

static void overflow_import(void) {
    h2_config c;
    OK(h2_config_init(&c, 0, 64));
    h2_hist *h = NULL, *s = NULL, *u = NULL, *out = NULL;
    OK(h2_create(c, &h));
    OK(h2_record(h, 0, UINT64_MAX));
    OK(h2_record(h, 0, 1));
    uint64_t total;
    OK(h2_total(h, &total));
    assert(total == 0);
    OK(h2_record(h, 0, UINT64_MAX));
    OK(h2_record(h, 1, 1));
    total = 33;
    assert(h2_total(h, &total) == H2_OVERFLOW && total == 33);
    assert(h2_convert(h, H2_CUMULATIVE, &u) == H2_OVERFLOW && u == NULL);
    assert(h2_add(h, h) == H2_OVERFLOW);
    const uint64_t *counts = h2_counts(h);
    assert(counts[0] == UINT64_MAX && counts[1] == 1);
    uint32_t indices[] = {1, 0};
    uint64_t ns[] = {0, 1};
    assert(h2_import(c, H2_SPARSE, indices, ns, 2, &s) == H2_INVALID);
    indices[0] = 0;
    indices[1] = 1;
    ns[0] = UINT64_MAX - 1;
    ns[1] = 1;
    OK(h2_import(c, H2_SPARSE, indices, ns, 2, &s));
    OK(h2_convert(s, H2_CUMULATIVE, &u));
    h2_bucket b;
    OK(h2_percentile(u, 1, &b));
    assert(b.start == 1 && b.count == 1);
    OK(h2_percentile(s, 1, &b));
    assert(b.start == 1);
    OK(h2_percentile(u, 0, &b));
    assert(b.start == 0);
    assert(h2_merge(s, s, &out) == H2_OVERFLOW && out == NULL);
    h2_free(h);
    h2_free(s);
    h2_free(u);
}

static void initial_zero(void) {
    h2_config c;
    OK(h2_config_init(&c, 1, 8));
    uint32_t is[] = {1, 2};
    uint64_t ns[] = {0, 5};
    h2_hist *h = NULL;
    assert(h2_import(c, H2_CUMULATIVE, is, ns, 2, &h) == H2_INVALID && h == NULL);
}

static void decimal_percentiles(void) {
    h2_config c;
    OK(h2_config_init(&c, 1, 8));
    h2_hist *h = NULL;
    OK(h2_create(c, &h));
    OK(h2_record(h, 1, 1));
    OK(h2_record(h, 2, 9));
    h2_bucket b;
    OK(h2_percentile(h, .1, &b));
    assert(b.start == 1);
    h2_free(h);
}

static void phase_conservation(void) {
    for (uint32_t gp = 1; gp < 6; gp++) {
        h2_config c, inferred;
        OK(h2_config_init(&c, gp, 12));
        OK(h2_config_infer(&inferred, c.buckets, 12));
        assert(inferred.grouping_power == gp);
        h2_hist *h = NULL, *dense_down = NULL;
        OK(h2_create(c, &h));
        uint64_t expected = 0;
        for (uint64_t v = 0; v < 4096; v += 17) {
            uint64_t n = v % 13 + 1;
            OK(h2_record(h, v, n));
            expected += n;
        }
        OK(h2_downsample(h, gp - 1, &dense_down));
        for (h2_phase phase = H2_DENSE; phase <= H2_CUMULATIVE; phase++) {
            h2_hist *p = NULL, *down = NULL, *dense = NULL, *merged = NULL, *decum = NULL;
            OK(h2_convert(h, phase, &p));
            OK(h2_downsample(p, gp - 1, &down));
            OK(h2_convert(down, H2_DENSE, &dense));
            assert(h2_length(dense) == h2_length(dense_down));
            assert(memcmp(h2_counts(dense), h2_counts(dense_down),
                          h2_length(dense) * sizeof(uint64_t)) == 0);
            OK(h2_merge(p, p, &merged));
            uint64_t total;
            OK(h2_total(merged, &total));
            assert(total == expected * 2);
            OK(h2_convert(p, H2_SPARSE, &decum));
            OK(h2_total(decum, &total));
            assert(total == expected);
            for (unsigned k = 0; k <= 100; k++) {
                h2_bucket a, b;
                OK(h2_percentile(h, k / 100., &a));
                OK(h2_percentile(p, k / 100., &b));
                assert(memcmp(&a, &b, sizeof a) == 0);
            }
            h2_free(p);
            h2_free(down);
            h2_free(dense);
            h2_free(merged);
            h2_free(decum);
        }
        h2_free(h);
        h2_free(dense_down);
    }
}

static void invalid_and_transactional(void) {
    h2_config c, other;
    OK(h2_config_init(&c, 2, 8));
    OK(h2_config_init(&other, 1, 8));
    h2_hist *h = NULL, *mismatch = NULL, *out = NULL;
    OK(h2_create(c, &h));
    OK(h2_create(other, &mismatch));
    OK(h2_record(h, 0, 7));
    assert(h2_add(h, mismatch) == H2_MISMATCH);
    assert(h2_snapshot_into(h, mismatch) == H2_MISMATCH);
    assert(h2_drain_into(h, mismatch) == H2_MISMATCH);
    assert(h2_counts(h)[0] == 7);
    assert(h2_create(c, &h) == H2_INVALID);
    assert(h2_downsample(h, 2, &out) == H2_INVALID);
    assert(h2_sum(NULL, 0, &out) == H2_INVALID);
    const h2_hist *inputs[] = {h, mismatch};
    assert(h2_sum(inputs, 2, &out) == H2_MISMATCH);
    uint32_t indices[] = {1, 1};
    uint64_t ns[] = {0, 2};
    assert(h2_import(c, H2_SPARSE, indices, ns, 2, &out) == H2_INVALID);
    indices[1] = 2;
    ns[0] = 3;
    assert(h2_import(c, H2_CUMULATIVE, indices, ns, 2, &out) == H2_INVALID);
    ns[1] = 3;
    OK(h2_import(c, H2_CUMULATIVE, indices, ns, 2, &out));
    assert(h2_length(out) == 1);
    h2_free(out);
    out = NULL;
    assert(h2_import(c, H2_DENSE, NULL, ns, 2, &out) == H2_INVALID);
    uint32_t index = 99;
    assert(h2_index(c, 256, &index) == H2_RANGE && index == 99);
    uint64_t lo = 99, hi = 99;
    assert(h2_bounds(c, c.buckets, &lo, &hi) == H2_RANGE && lo == 99 && hi == 99);
    OK(h2_reset(h));
    OK(h2_record(h, 8, UINT64_MAX));
    OK(h2_record(h, 10, 1));
    assert(h2_downsample(h, 0, &out) == H2_OVERFLOW && out == NULL);
    h2_free(h);
    h2_free(mismatch);
}

/* Interleaving, overlap and either exhausted input must preserve every bucket. */
static void merge_edges(void) {
    h2_config c;
    OK(h2_config_init(&c, 2, 8));
    const uint64_t values[][4] = {{1, 3, 5, 7}, {0, 2, 4, 6}, {3, 5, 8, 9}};
    for (unsigned scenario = 0; scenario < 5; scenario++) {
        h2_hist *a = NULL, *b = NULL, *oracle = NULL;
        OK(h2_create(c, &a));
        OK(h2_create(c, &b));
        if (scenario != 3 && scenario != 4)
            for (unsigned i = 0; i < 4; i++)
                OK(h2_record(a, values[0][i], i + 1));
        if (scenario != 2 && scenario != 4)
            for (unsigned i = 0; i < 4; i++)
                OK(h2_record(b, values[scenario == 1 ? 2 : 1][i], 2 * i + 1));
        const h2_hist *inputs[] = {a, b};
        OK(h2_sum(inputs, 2, &oracle));
        for (h2_phase phase = H2_SPARSE; phase <= H2_CUMULATIVE; phase++) {
            h2_hist *ap = NULL, *bp = NULL;
            OK(h2_convert(a, phase, &ap));
            OK(h2_convert(b, phase, &bp));
            for (unsigned reverse = 0; reverse < 2; reverse++) {
                h2_hist *merged = NULL, *dense = NULL;
                OK(h2_merge(reverse ? bp : ap, reverse ? ap : bp, &merged));
                OK(h2_convert(merged, H2_DENSE, &dense));
                assert(memcmp(h2_counts(dense), h2_counts(oracle), c.buckets * sizeof(uint64_t)) ==
                       0);
                h2_free(dense);
                h2_free(merged);
            }
            h2_free(ap);
            h2_free(bp);
        }
        h2_free(a);
        h2_free(b);
        h2_free(oracle);
    }
}

static void empty_reports(void) {
    h2_config c;
    OK(h2_config_init(&c, 2, 8));
    h2_hist *h = NULL;
    OK(h2_create(c, &h));
    for (h2_phase phase = H2_DENSE; phase <= H2_CUMULATIVE; phase++) {
        h2_hist *empty = NULL;
        OK(h2_convert(h, phase, &empty));
        double ps[] = {0, .5, 1};
        h2_bucket sentinels[] = {{11, 12, 13}, {21, 22, 23}, {31, 32, 33}};
        h2_bucket buckets[3];
        memcpy(buckets, sentinels, sizeof buckets);
        assert(h2_percentiles(empty, ps, 3, buckets) == H2_EMPTY);
        assert(memcmp(buckets, sentinels, sizeof buckets) == 0);
        assert(h2_percentile(empty, .5, buckets) == H2_EMPTY);
        assert(memcmp(buckets, sentinels, sizeof buckets) == 0);
        double mean = 77;
        assert(h2_mean(empty, &mean) == H2_EMPTY && mean == 77);
        OK(h2_percentiles(empty, NULL, 0, NULL));
        h2_free(empty);
    }
    h2_free(h);
}

static void compact_conservation(void) {
    h2_config c;
    OK(h2_config_init(&c, 2, 8));
    uint32_t indices[] = {8, 9, 10};
    uint64_t counts[] = {2, 3, 5};
    h2_hist *s = NULL;
    OK(h2_import(c, H2_SPARSE, indices, counts, 3, &s));
    for (h2_phase phase = H2_SPARSE; phase <= H2_CUMULATIVE; phase++) {
        h2_hist *p = NULL, *merged = NULL, *down = NULL;
        OK(h2_convert(s, phase, &p));
        OK(h2_merge(p, p, &merged));
        OK(h2_downsample(p, 0, &down));
        h2_hist *cases[] = {merged, down};
        for (unsigned i = 0; i < 2; i++) {
            h2_hist *before = NULL, *after = NULL;
            OK(h2_convert(cases[i], H2_DENSE, &before));
            size_t length = h2_length(cases[i]);
            OK(h2_compact(cases[i]));
            assert(h2_get_phase(cases[i]) == phase && h2_length(cases[i]) == length);
            OK(h2_convert(cases[i], H2_DENSE, &after));
            assert(memcmp(h2_counts(before), h2_counts(after),
                          h2_length(before) * sizeof(uint64_t)) == 0);
            h2_free(before);
            h2_free(after);
        }
        h2_free(p);
        h2_free(merged);
        h2_free(down);
    }
    assert(h2_compact(NULL) == H2_INVALID);
    h2_free(s);
}

int main(void) {
    empty_reports();
    merge_edges();
    compact_conservation();
    phase_conservation();
    invalid_and_transactional();
    decimal_percentiles();
    initial_zero();
    geometry();
    contracts();
    overflow_import();
    puts("C invariant tests passed");
    return 0;
}

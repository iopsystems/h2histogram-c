#include "h2histogram.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct h2_hist {
    h2_config config;
    h2_phase phase;
    size_t length;
    uint64_t *counts;
    uint32_t *indices;
};

static int output_ok(h2_hist **out) {
    return out && !*out;
}

static int phase_ok(h2_phase p) {
    return p == H2_DENSE || p == H2_SPARSE || p == H2_CUMULATIVE;
}

static int same(h2_config a, h2_config b) {
    return a.grouping_power == b.grouping_power && a.max_value_power == b.max_value_power;
}

h2_status h2_config_init(h2_config *out, uint32_t gp, uint32_t mp) {
    if (!out || mp > 64 || gp >= mp || gp >= 31)
        return H2_INVALID;

    uint64_t n = ((uint64_t)mp - gp + 1) << gp;
    if (n > UINT32_MAX)
        return H2_INVALID;

    h2_config c = {gp, mp, (uint32_t)n, mp == 64 ? UINT64_MAX : (UINT64_C(1) << mp) - 1};
    *out = c;
    return H2_OK;
}

static int valid(h2_config c) {
    h2_config a;
    return h2_config_init(&a, c.grouping_power, c.max_value_power) == H2_OK &&
           a.buckets == c.buckets && a.max == c.max;
}

h2_status h2_config_infer(h2_config *out, uint32_t n, uint32_t mp) {
    if (!out || mp > 64)
        return H2_INVALID;
    for (uint32_t gp = 0; gp < mp; gp++) {
        h2_config c;
        if (h2_config_init(&c, gp, mp) == H2_OK && c.buckets == n) {
            *out = c;
            return H2_OK;
        }
    }
    return H2_INVALID;
}

static uint32_t index_unchecked(h2_config c, uint64_t v) {
    uint32_t gp = c.grouping_power;
    if (v < (UINT64_C(1) << (gp + 1)))
        return (uint32_t)v;

    uint32_t power;
#if defined(__GNUC__) || defined(__clang__)
    power = 63u - (uint32_t)__builtin_clzll(v);
#else
    power = 0;
    for (uint64_t x = v; x > 1; x >>= 1)
        power++;
#endif
    return ((power - gp + 1) << gp) + (uint32_t)((v - (UINT64_C(1) << power)) >> (power - gp));
}

static uint64_t lower(h2_config c, uint32_t i) {
    uint32_t g = i >> c.grouping_power;
    uint64_t h = i - ((uint64_t)g << c.grouping_power);
    return g == 0 ? h : (UINT64_C(1) << (c.grouping_power + g - 1)) + (h << (g - 1));
}

static uint64_t upper(h2_config c, uint32_t i) {
    return i == c.buckets - 1 ? c.max : lower(c, i + 1) - 1;
}

h2_status h2_index(h2_config c, uint64_t v, uint32_t *out) {
    if (!out || !valid(c))
        return H2_INVALID;
    if (v > c.max)
        return H2_RANGE;
    *out = index_unchecked(c, v);
    return H2_OK;
}

h2_status h2_bounds(h2_config c, uint32_t i, uint64_t *a, uint64_t *b) {
    if (!a || !b || !valid(c))
        return H2_INVALID;
    if (i >= c.buckets)
        return H2_RANGE;
    *a = lower(c, i);
    *b = upper(c, i);
    return H2_OK;
}

static h2_status allocate(h2_config c, h2_phase p, size_t n, h2_hist **out) {
    if (n > SIZE_MAX / sizeof(uint64_t) || n > SIZE_MAX / sizeof(uint32_t))
        return H2_NOMEM;

    h2_hist *h = calloc(1, sizeof *h);
    if (!h)
        return H2_NOMEM;
    h->config = c;
    h->phase = p;
    h->length = n;
    if (n) {
        h->counts = calloc(n, sizeof *h->counts);
        if (p != H2_DENSE)
            h->indices = malloc(n * sizeof *h->indices);
        if (!h->counts || (p != H2_DENSE && !h->indices)) {
            h2_free(h);
            return H2_NOMEM;
        }
    }
    *out = h;
    return H2_OK;
}

h2_status h2_create(h2_config c, h2_hist **out) {
    if (!valid(c) || !output_ok(out))
        return H2_INVALID;
    return allocate(c, H2_DENSE, c.buckets, out);
}

void h2_free(h2_hist *h) {
    if (h) {
        free(h->counts);
        free(h->indices);
        free(h);
    }
}

h2_config h2_get_config(const h2_hist *h) {
    h2_config zero = {0, 0, 0, 0};
    return h ? h->config : zero;
}

h2_phase h2_get_phase(const h2_hist *h) {
    return h ? h->phase : H2_DENSE;
}

size_t h2_length(const h2_hist *h) {
    return h ? h->length : 0;
}

const uint64_t *h2_counts(const h2_hist *h) {
    return h ? h->counts : NULL;
}

const uint32_t *h2_indices(const h2_hist *h) {
    return h ? h->indices : NULL;
}

static uint64_t individual(const h2_hist *h, size_t i) {
    return h->phase == H2_CUMULATIVE ? h->counts[i] - (i ? h->counts[i - 1] : 0) : h->counts[i];
}

static uint32_t at_index(const h2_hist *h, size_t i) {
    return h->phase == H2_DENSE ? (uint32_t)i : h->indices[i];
}

h2_status h2_import(h2_config c, h2_phase p, const uint32_t *is, const uint64_t *ns, size_t n,
                    h2_hist **out) {
    if (!valid(c) || !phase_ok(p) || !output_ok(out) || (n && !ns))
        return H2_INVALID;
    if (p == H2_DENSE) {
        if (n != c.buckets || is)
            return H2_INVALID;
    } else {
        if (n && !is)
            return H2_INVALID;
        if (p == H2_CUMULATIVE && n && ns[0] == 0)
            return H2_INVALID;
        for (size_t i = 0; i < n; i++)
            if (is[i] >= c.buckets || (i && is[i] <= is[i - 1]) ||
                (p == H2_CUMULATIVE && i && ns[i] < ns[i - 1]))
                return H2_INVALID;
    }
    size_t used = 0;
    for (size_t i = 0; i < n; i++)
        if (p == H2_DENSE || ns[i] != (p == H2_CUMULATIVE && i ? ns[i - 1] : 0))
            used++;
    h2_hist *h = NULL;
    h2_status e = allocate(c, p, used, &h);
    if (e)
        return e;

    size_t k = 0;
    for (size_t i = 0; i < n; i++)
        if (p == H2_DENSE || ns[i] != (p == H2_CUMULATIVE && i ? ns[i - 1] : 0)) {
            h->counts[k] = ns[i];
            if (p != H2_DENSE)
                h->indices[k] = is[i];
            k++;
        }
    *out = h;
    return H2_OK;
}

h2_status h2_record(h2_hist *h, uint64_t v, uint64_t n) {
    if (!h || h->phase != H2_DENSE)
        return H2_INVALID;
    if (v > h->config.max)
        return H2_RANGE;
    h->counts[index_unchecked(h->config, v)] += n;
    return H2_OK;
}

h2_status h2_reset(h2_hist *h) {
    if (!h || h->phase != H2_DENSE)
        return H2_INVALID;
    memset(h->counts, 0, h->length * sizeof *h->counts);
    return H2_OK;
}

static h2_status compatible_dense(const h2_hist *a, const h2_hist *b) {
    if (!a || !b || a->phase != H2_DENSE || b->phase != H2_DENSE)
        return H2_INVALID;
    return same(a->config, b->config) ? H2_OK : H2_MISMATCH;
}

h2_status h2_snapshot_into(const h2_hist *src, h2_hist *dst) {
    h2_status e = compatible_dense(src, dst);
    if (e)
        return e;
    if (src != dst)
        memcpy(dst->counts, src->counts, src->length * sizeof *src->counts);
    return H2_OK;
}

h2_status h2_drain_into(h2_hist *src, h2_hist *dst) {
    if (src == dst)
        return H2_INVALID;

    h2_status e = h2_snapshot_into(src, dst);
    if (e)
        return e;
    return h2_reset(src);
}

h2_status h2_add(h2_hist *dst, const h2_hist *src) {
    h2_status e = compatible_dense(dst, src);
    if (e)
        return e;
    for (size_t i = 0; i < src->length; i++)
        if (src->counts[i] > UINT64_MAX - dst->counts[i])
            return H2_OVERFLOW;
    for (size_t i = 0; i < src->length; i++)
        dst->counts[i] += src->counts[i];
    return H2_OK;
}

h2_status h2_sum(const h2_hist *const *hs, size_t n, h2_hist **out) {
    if (!output_ok(out) || !hs || !n || !hs[0])
        return H2_INVALID;
    for (size_t i = 0; i < n; i++) {
        h2_status e = compatible_dense(hs[0], hs[i]);
        if (e)
            return e;
    }
    h2_hist *h = NULL;
    h2_status e = h2_create(hs[0]->config, &h);
    if (e)
        return e;
    memcpy(h->counts, hs[0]->counts, h->length * sizeof *h->counts);
    for (size_t j = 1; j < n; j++)
        for (size_t i = 0; i < h->length; i++) {
            uint64_t v = hs[j]->counts[i];
            if (v > UINT64_MAX - h->counts[i]) {
                h2_free(h);
                return H2_OVERFLOW;
            }
            h->counts[i] += v;
        }
    *out = h;
    return H2_OK;
}

h2_status h2_total(const h2_hist *h, uint64_t *out) {
    if (!h || !out)
        return H2_INVALID;

    uint64_t n = 0;
    if (h->phase == H2_CUMULATIVE)
        n = h->length ? h->counts[h->length - 1] : 0;
    else
        for (size_t i = 0; i < h->length; i++) {
            if (h->counts[i] > UINT64_MAX - n)
                return H2_OVERFLOW;
            n += h->counts[i];
        }
    *out = n;
    return H2_OK;
}

h2_status h2_convert(const h2_hist *src, h2_phase p, h2_hist **out) {
    if (!src || !phase_ok(p) || !output_ok(out))
        return H2_INVALID;
    if (p == H2_CUMULATIVE) {
        uint64_t total;
        h2_status e = h2_total(src, &total);
        if (e)
            return e;
    }
    size_t n = 0;
    if (p == H2_DENSE)
        n = src->config.buckets;
    else
        for (size_t i = 0; i < src->length; i++)
            if (individual(src, i))
                n++;
    h2_hist *h = NULL;
    h2_status e = allocate(src->config, p, n, &h);
    if (e)
        return e;

    size_t k = 0;
    uint64_t running = 0;
    for (size_t i = 0; i < src->length; i++) {
        uint64_t v = individual(src, i);
        uint32_t ix = at_index(src, i);
        if (p == H2_DENSE)
            h->counts[ix] = v;
        else if (v) {
            h->indices[k] = ix;
            running += v;
            h->counts[k++] = p == H2_CUMULATIVE ? running : v;
        }
    }
    *out = h;
    return H2_OK;
}
/* Match the Rust floating-product nearest rank, protecting exact endpoints
 * and clamping before conversion because (double)UINT64_MAX rounds to 2^64. */
static uint64_t rank_for(double p, uint64_t total) {
    if (p == 0)
        return 1;
    if (p == 1)
        return total;

    double rank = ceil(p * (double)total);
    if (rank >= (double)total)
        return total;
    return rank < 1 ? 1 : (uint64_t)rank;
}

static h2_bucket select_bucket(const h2_hist *h, uint64_t rank) {
    size_t pos = 0;
    if (h->phase == H2_CUMULATIVE) {
        size_t end = h->length;
        while (pos < end) {
            size_t mid = pos + (end - pos) / 2;
            if (h->counts[mid] < rank)
                pos = mid + 1;
            else
                end = mid;
        }
    } else {
        uint64_t running = 0;
        for (; pos < h->length; pos++) {
            running += h->counts[pos];
            if (h->counts[pos] && running >= rank)
                break;
        }
    }
    uint32_t ix = at_index(h, pos);
    h2_bucket b = {individual(h, pos), lower(h->config, ix), upper(h->config, ix)};
    return b;
}

h2_status h2_percentiles(const h2_hist *h, const double *ps, size_t n, h2_bucket *out) {
    if (!h || (n && (!ps || !out)))
        return H2_INVALID;
    for (size_t i = 0; i < n; i++)
        if (!isfinite(ps[i]) || ps[i] < 0 || ps[i] > 1)
            return H2_INVALID;
    if (!n)
        return H2_OK;

    uint64_t total;
    h2_status e = h2_total(h, &total);
    if (e)
        return e;
    if (!total)
        return H2_EMPTY;
    for (size_t i = 0; i < n; i++)
        out[i] = select_bucket(h, rank_for(ps[i], total));
    return H2_OK;
}

h2_status h2_percentile(const h2_hist *h, double p, h2_bucket *out) {
    return h2_percentiles(h, &p, 1, out);
}

h2_status h2_mean(const h2_hist *h, double *out) {
    if (!h || !out)
        return H2_INVALID;

    uint64_t total;
    h2_status e = h2_total(h, &total);
    if (e)
        return e;
    if (!total)
        return H2_EMPTY;

    long double mean = 0;
    for (size_t i = 0; i < h->length; i++) {
        uint64_t n = individual(h, i);
        if (n) {
            uint32_t ix = at_index(h, i);
            long double midpoint =
                (long double)lower(h->config, ix) / 2 + (long double)upper(h->config, ix) / 2;
            mean += midpoint * ((long double)n / (long double)total);
        }
    }
    *out = (double)mean;
    return H2_OK;
}

h2_status h2_merge(const h2_hist *a, const h2_hist *b, h2_hist **out) {
    if (!a || !b || !output_ok(out))
        return H2_INVALID;
    if (!same(a->config, b->config) || a->phase != b->phase)
        return H2_MISMATCH;
    if (a->phase == H2_DENSE) {
        const h2_hist *hs[] = {a, b};
        return h2_sum(hs, 2, out);
    }
    if (a->length > SIZE_MAX - b->length)
        return H2_NOMEM;

    h2_hist *h = NULL;
    h2_status e = allocate(a->config, a->phase, a->length + b->length, &h);
    if (e)
        return e;

    size_t i = 0, j = 0, k = 0;
    uint64_t running = 0;
    while (i < a->length || j < b->length) {
        uint32_t ix;
        uint64_t n;
        if (j == b->length || (i < a->length && a->indices[i] < b->indices[j])) {
            ix = a->indices[i];
            n = individual(a, i++);
        } else if (i == a->length || b->indices[j] < a->indices[i]) {
            ix = b->indices[j];
            n = individual(b, j++);
        } else {
            ix = a->indices[i];
            n = individual(a, i++);
            uint64_t v = individual(b, j++);
            if (v > UINT64_MAX - n) {
                h2_free(h);
                return H2_OVERFLOW;
            }
            n += v;
        }
        if (a->phase == H2_CUMULATIVE) {
            if (n > UINT64_MAX - running) {
                h2_free(h);
                return H2_OVERFLOW;
            }
            running += n;
        }
        h->indices[k] = ix;
        h->counts[k++] = a->phase == H2_CUMULATIVE ? running : n;
    }
    h->length = k;
    *out = h;
    return H2_OK;
}

h2_status h2_downsample(const h2_hist *src, uint32_t gp, h2_hist **out) {
    if (!src || !output_ok(out) || gp >= src->config.grouping_power)
        return H2_INVALID;

    h2_config c;
    h2_status e = h2_config_init(&c, gp, src->config.max_value_power);
    if (e)
        return e;

    size_t capacity = src->phase == H2_DENSE ? c.buckets : src->length;
    h2_hist *h = NULL;
    e = allocate(c, src->phase, capacity, &h);
    if (e)
        return e;

    size_t used = 0;
    for (size_t i = 0; i < src->length; i++) {
        uint64_t n = individual(src, i);
        if (!n)
            continue;
        uint32_t ix = index_unchecked(c, lower(src->config, at_index(src, i)));
        size_t pos;
        if (src->phase == H2_DENSE)
            pos = ix;
        else {
            if (!used || h->indices[used - 1] != ix) {
                h->indices[used] = ix;
                used++;
            }
            pos = used - 1;
        }
        if (n > UINT64_MAX - h->counts[pos]) {
            h2_free(h);
            return H2_OVERFLOW;
        }
        h->counts[pos] += n;
    }
    if (src->phase != H2_DENSE)
        h->length = used;
    if (src->phase == H2_CUMULATIVE) {
        uint64_t running = 0;
        for (size_t i = 0; i < h->length; i++) {
            if (h->counts[i] > UINT64_MAX - running) {
                h2_free(h);
                return H2_OVERFLOW;
            }
            running += h->counts[i];
            h->counts[i] = running;
        }
    }
    *out = h;
    return H2_OK;
}

const char *h2_status_string(h2_status s) {
    switch (s) {
    case H2_OK:
        return "success";
    case H2_INVALID:
        return "invalid argument";
    case H2_RANGE:
        return "value or index out of range";
    case H2_NOMEM:
        return "allocation failed";
    case H2_OVERFLOW:
        return "count overflow";
    case H2_MISMATCH:
        return "incompatible geometry or phase";
    case H2_EMPTY:
        return "empty histogram";
    }
    return "unknown status";
}

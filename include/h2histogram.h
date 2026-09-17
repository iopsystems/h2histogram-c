#ifndef H2HISTOGRAM_H
#define H2HISTOGRAM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define H2_VERSION "0.1.0-alpha.1"
typedef enum { H2_OK, H2_INVALID, H2_RANGE, H2_NOMEM, H2_OVERFLOW, H2_MISMATCH, H2_EMPTY } h2_status;
typedef enum { H2_DENSE, H2_SPARSE, H2_CUMULATIVE } h2_phase;
typedef struct { uint32_t grouping_power, max_value_power, buckets; uint64_t max; } h2_config;
typedef struct h2_hist h2_hist;
typedef struct { uint64_t count, start, end; } h2_bucket;
/* Config values are validated on entry. Outputs stay unchanged on any error. */
h2_status h2_config_init(h2_config *out, uint32_t gp, uint32_t mvp);
h2_status h2_config_infer(h2_config *out, uint32_t buckets, uint32_t mvp);
h2_status h2_index(h2_config c, uint64_t value, uint32_t *out);
h2_status h2_bounds(h2_config c, uint32_t index, uint64_t *start, uint64_t *end);
/* Constructors require out != NULL and *out == NULL. All returned objects own
 * independent storage. Free once; do not dereference borrowed views after free.
 * Imported arrays are copied. Sparse indices must strictly ascend, including
 * zero-count entries; cumulative counts start positive and are nondecreasing. */
h2_status h2_create(h2_config c, h2_hist **out);
h2_status h2_import(h2_config c, h2_phase phase, const uint32_t *indices,
                    const uint64_t *counts, size_t length, h2_hist **out);
void h2_free(h2_hist *h);
h2_config h2_get_config(const h2_hist *h);
h2_phase h2_get_phase(const h2_hist *h);
size_t h2_length(const h2_hist *h);
const uint64_t *h2_counts(const h2_hist *h);
const uint32_t *h2_indices(const h2_hist *h); /* NULL for dense */
/* Dense-only mutation; all concurrency requires external synchronization.
 * Recording wraps modulo 2^64 and updates only the selected counter. */
h2_status h2_record(h2_hist *h, uint64_t value, uint64_t count);
h2_status h2_reset(h2_hist *h);
h2_status h2_snapshot_into(const h2_hist *src, h2_hist *dst);
h2_status h2_drain_into(h2_hist *src, h2_hist *dst);
h2_status h2_add(h2_hist *dst, const h2_hist *src); /* checked, transactional */
h2_status h2_sum(const h2_hist *const *inputs, size_t length, h2_hist **out);
/* Conversion, merge and downsampling retain owned independent storage. Merge
 * requires equal geometry AND phase. Downsample requires gp < current gp.
 * Sparse/cumulative transforms never reconstruct dense storage. */
h2_status h2_convert(const h2_hist *src, h2_phase phase, h2_hist **out);
h2_status h2_merge(const h2_hist *a, const h2_hist *b, h2_hist **out);
h2_status h2_downsample(const h2_hist *src, uint32_t gp, h2_hist **out);
/* Reports allocate nothing. Batch output holds length entries, preserves order
 * and duplicates. Dense/sparse use O(length * stored buckets) scans; cumulative
 * uses binary search. Empty nonzero requests return H2_EMPTY; no writes occur.
 * Totals exceeding UINT64_MAX return H2_OVERFLOW. Percentiles are finite [0,1],
 * nearest rank using ceil(p * (double)total), with exact p0/p1 endpoints.
 * Mean uses floating-point weighted inclusive bucket midpoints. */
h2_status h2_total(const h2_hist *h, uint64_t *out);
h2_status h2_percentile(const h2_hist *h, double p, h2_bucket *out);
h2_status h2_percentiles(const h2_hist *h, const double *ps, size_t length, h2_bucket *out);
h2_status h2_mean(const h2_hist *h, double *out);
const char *h2_status_string(h2_status status);
#ifdef __cplusplus
}
#endif
#endif

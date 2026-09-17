# h2histogram for C

Native C11 draft of the **h2histogram** design, version `0.1.0-alpha.1`.
Bucket geometry matches the Rust histogram crate. This is an initial API draft,
with no stable ABI or performance claim. MIT licensed; no runtime dependencies
beyond the C standard library and its math library. No serialization format,
concurrent writer support, SIMD requirement, or mandatory 128-bit integer type.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/h2_example
```

For sanitizer checks use a separate build with `-DH2_SANITIZE=ON` and
`-DCMAKE_BUILD_TYPE=Debug`. GCC and Clang are tested in Debug and Release.
`cmake --install build --prefix /your/prefix` installs the static library and
`include/h2histogram.h`. Link `h2histogram` through CMake or link the archive and
`-lm` directly on Unix. The library uses compiler count-leading-zero intrinsics
where available and a portable C11 fallback elsewhere.

## Choose a phase

| Phase | Intended use | Storage | Percentiles |
|---|---|---|---|
| `H2_DENSE` | Record and aggregate counters | 8 bytes per bucket | linear scan |
| `H2_SPARSE` | Immutable sparse snapshot and transforms | 12 bytes per stored entry | scan stored entries |
| `H2_CUMULATIVE` | Repeated percentile reports | 12 bytes per stored entry | binary search |

These sizes exclude the object, allocator overhead and spare transform capacity.
Mean currently scans the stored entries in every phase. Cumulative mean uses
individual count differences and the current geometry's inclusive midpoints.
Sparse/cumulative conversion, merge, downsampling and decumulation operate on
stored entries directly; they do not rebuild a dense histogram. Conversion
explicitly requested to `H2_DENSE` naturally allocates dense storage.

```c
h2_config config;
h2_hist *recorder = NULL, *report = NULL;
h2_config_init(&config, 7, 64);
h2_create(config, &recorder);
h2_record(recorder, 1000, 1);
h2_convert(recorder, H2_CUMULATIVE, &report);
h2_bucket p99;
h2_percentile(report, .99, &p99);
h2_free(report);
h2_free(recorder);
```

Check each returned status in production; `examples/basic.c` shows cleanup.
A returned bucket contains its count and inclusive start/end values. Bounds,
not original exact observations, describe values within a logarithmic bucket.

## Ownership, errors and reuse

Objects are opaque owners. Every constructor/transform takes an output pointer
that must initially be NULL and returns independent owned storage. Free each
object once with `h2_free` (NULL is accepted). Configs are values; accessor arrays
are borrowed read-only views, valid until their owner is freed. Dense views can
change after mutation. Never free or modify borrowed arrays. Copying an object
pointer does not create another owner. Import functions copy their input arrays.

Functions return `h2_status`; errors leave caller outputs and existing objects
unchanged. Provide valid pointers and output arrays of the documented length;
output buffers must not overlap input arrays or borrowed object storage.
Import validates all indices before discarding zero counts: sparse/cumulative
indices must be in range and strictly increasing, even for zero entries.
Cumulative prefixes start positive and never decrease; repeated positive
prefixes are normalized away. Empty snapshots are valid. Dense import requires
exactly the configured bucket count and a NULL indices pointer.

Recording wraps each counter modulo 2^64 and updates only that counter, with no
cached total/min/max writes. Checked addition preflights every bucket before
mutation, including self-add. Checked sum validates inputs, copies the first
source into private storage, then checks and adds the rest. Merge requires the
same geometry and phase; downsampling requires strictly lower grouping power.
Checked transformations report bucket overflow without partial public results.

`h2_snapshot_into` and `h2_drain_into` reuse compatible dense storage without
allocation; self-snapshot is valid and self-drain is rejected. Drain snapshots
then resets the source. Reset retains allocation. All mutations and snapshot
operations require external synchronization; drain is not atomic.

Scalar and caller-buffer percentile/mean/total queries allocate nothing. Batch
queries validate every request before writing, preserve order and duplicates,
and require room for the full result. Dense/sparse batches scan independently
per request (O(P*B) and O(P*K)); cumulative batches cost O(P*log K). No scratch
sorting or hidden allocation is used. Empty nonzero percentile requests and
mean return `H2_EMPTY`; an empty batch succeeds without writing output.

## Limits and numerical behavior

Configuration requires `gp < mvp <= 64` and
`(1 << gp) * (mvp - gp + 1) <= UINT32_MAX`. Geometry is valid independently of
whether dense storage fits in memory. Allocation-size multiplication is checked.
For gp=7, mvp=64 there are 7,424 buckets (59,392 bytes of dense counters).

Totals, cumulative prefixes and reports are limited to `UINT64_MAX`, even when
individual counters are valid. Reports return `H2_OVERFLOW` on larger totals;
no wrapped report totals are exposed. This is an explicit compatibility limit
relative to implementations with wider report accumulators. Mean uses floating
point weighted midpoints. Percentile ranks follow `ceil(p * (double)total)`;
large totals, especially above 2^53, incur floating-point rank rounding. p=0 and
p=1 always select the first and last populated buckets exactly, and conversion
of a rounded 2^64 rank is avoided. Percentiles must be finite and in [0,1].

Tests cover shared Rust-checked geometry fixtures, transactional errors,
independent snapshots, wrapping recording, endpoints and overflow. Linux tests
also inject failures into each allocation site and prove reused queries and
dense reuse operations perform zero allocations.

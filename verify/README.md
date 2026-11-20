# Bounded proofs with CBMC

These harnesses include `src/h2histogram.c` unmodified and ask
[CBMC](https://www.cprover.org/cbmc/) 6.x to prove each stated contract for
**every** input inside a bound, by translating the C program to a SAT problem.
A passing run is a proof for that bound, not a sample. Every run also checks
array bounds, pointer validity, unsigned and signed overflow, integer
conversions, shift widths and division by zero on the paths it executes.

The suite is 77 proofs and takes about 15 minutes of sequential solver time on
a laptop-class x86 core, peaking near 2 GB for a single proof.

```sh
verify/run.sh              # all proofs, sequential
verify/run.sh merge        # only harnesses whose name contains "merge"
cmake --build build --target verify
```

Install CBMC 6.6 from the [release page](https://github.com/diffblue/cbmc/releases)
or `apt install cbmc` where the packaged version is 6.x. CI pins 6.6.0.

## What is proved

| Harness | Contract | Bound |
|---|---|---|
| `geometry.c` | For every in-range value: index is within the bucket count, the bucket contains the value, consecutive buckets tile with no gap or overlap, the first bucket starts at 0 and the last ends at the configured maximum. Out-of-range values return `H2_RANGE` without touching the output. | Every valid configuration, gp 0..30 and mvp 1..64; every `uint64_t` value. This is the whole configuration space. |
| `rank.c` | `1 <= rank <= total`, with `rank == 1` at p=0 and `rank == total` at p=1. | Every finite `double` in [0,1]; every total 1..2^64-1. Floating point is bit-blasted, so rounding above 2^53 is included. |
| `add.c` | `h2_add` returns only `H2_OK` or `H2_OVERFLOW`; overflow is reported iff some bucket sum exceeds `UINT64_MAX`; on success every counter is the exact sum; on overflow the destination is unchanged; the source is never modified. Includes self-add. | 6-bucket geometry; every counter value. |
| `import.c` | Sparse/cumulative import succeeds iff indices are in range and strictly ascending and cumulative prefixes start positive and never decrease. Stored entries are nonzero and ascending, the dense round trip equals the input, and a rejected import leaves the output pointer untouched. | 6 buckets; up to 3 entries; every index and count. |
| `merge.c` | Sparse/cumulative merge yields the exact per-bucket sum with ascending nonzero entries, or `H2_OVERFLOW` with no partial result exactly when the sum is not representable. Sources are unchanged. | 6 buckets; every ascending index pattern (disjoint, interleaved, overlapping, empty). Sparse: every pair of lengths 0..3 with full 64-bit counts. Cumulative: every pair of lengths 0..2 with 8 symbolic bits per count, see below. |
| `downsample.c` | Every coarse bucket is the exact sum of the fine buckets mapping into it; `H2_OVERFLOW` iff such a sum is not representable; no partial result; entry count never grows. | Source gp=2, mvp=5 (16 buckets); target gp 0 and 1; dense with every counter value, sparse/cumulative with 0..3 entries of any index and 64-bit count. |
| `compact.c` | Compaction preserves length, phase and every entry, leaves exact capacity, releases empty storage, keeps the arrays of an already-exact object, and is a no-op when repeated. With allocation failure injected, the status is `H2_OK` or `H2_NOMEM`; on `H2_NOMEM` entries, capacity and both borrowed array pointers are unchanged, and only a real shrink can fail. | Capacity 4 with every length 0..4 and every entry value, both phases. |

## How the harnesses are built, and why

**Inputs are constructed directly, not through other transforms.** Because the
harness includes the source file, it can call the private `allocate` and fill a
valid object. Chaining `h2_convert` and `h2_sum` to build inputs makes every
allocation size symbolic, and an early version of `merge.c` exhausted 5 GB in
14 seconds that way. The direct version uses 90 MB. It is also a stronger
statement: the expected result is computed in the harness on a plain array, so
no other library function is trusted by the proof.

**Lengths are compile-time constants, one run per length.** Same reason. The
runner enumerates them.

**Allocation failure is injected explicitly.** `compact.c -DINJECT` wraps the
library's `malloc` with a nondeterministic failure. All runs pass
`--no-malloc-may-fail`. CBMC 6 enables its own may-fail allocator by default,
and in that mode it does not track the effect of `memcpy` into a pointer that
may be NULL: reads of the copied data come back unconstrained even on the
success path, which shows up as a spurious "contents not preserved" failure.
The same assertions pass once `malloc` cannot fail, and `tests/allocation.c`
checks compacted contents under ASan. So the claim is split: contents with a
reliable allocator, rollback with injection.

**Cumulative merge narrows the counts.** At full 64-bit width cumulative merge
is SAT-hard (prefix sums plus the subtraction that recovers individual counts):
two entries per side did not finish in five minutes, and three per side did not
finish in nine even when narrowed. So cumulative merge is proved for lengths
0..2 with 8 symbolic bits per count, twice. With the bits low (values 1..255)
overflow is impossible and the run proves exact sums for every interleaving.
With the bits shifted to the top of the word (multiples of 2^56) sums carry out
of bit 63, and the same run proves the overflow decision, the absence of a
partial result, and exact sums on the high bits. The merge code does not
depend on the value width, so both runs drive every branch; what is not
covered is a carry chain through the middle bits, which is the adder's job,
not the library's.

**The downsample target is a compile-time constant.** A symbolic target
grouping power makes the dense result's allocation size symbolic; that version
exhausted 5 GB. One run per target uses 50 MB.

**Runs are sequential and memory-capped.** A single proof can use several
gigabytes; four at once exhausted a 30 GB workstation. `run.sh` never runs two
solvers together and caps each with `ulimit -v`.

## Frama-C

Frama-C's Eva plugin was run over the geometry functions for every
configuration and value. It raised three alarms, all of the form "is this
shift width below 64?", at the two shifts in `lower` and the one in
`index_unchecked`. They are false alarms: Eva's interval domains cannot express
`power > gp`, which follows from the value-range test just above. They did
name the obligations precisely, and `geometry.c` now discharges them with
`--undefined-shift-check` over the whole configuration space. Deductive proofs
with Frama-C WP would remove the size bounds on the transforms, but WP needs
`why3` and an SMT prover such as Alt-Ergo, which the standalone Frama-C
distribution does not include.

## What is not proved

- Anything about geometries larger than those listed, other than `geometry.c`
  and `rank.c`, which are unbounded in the dimensions that matter. The
  transforms are proved on 6- and 16-bucket geometries. Their loops do not
  depend on the geometry beyond the bucket count, but that is an argument, not
  a proof.
- `h2_percentiles` bucket selection, `h2_mean`, `h2_convert`, `h2_sum`,
  `h2_snapshot_into` and `h2_drain_into`. These are covered by the test suite
  and by differential testing against the Rust crate.
- Allocation failure inside merge, downsample, import and convert. The
  allocation test suite injects failure at each of those sites.
- Concurrency. The library requires external synchronization.
- Anything about the compiler or the machine. CBMC reasons about C semantics.

## Relation to the other ports

The Zig port has no comparable verification tool. It implements the same
algorithms, consumes the same geometry fixtures, and is checked against this
port and the Rust crate by differential testing, so these proofs are indirect
evidence for it and nothing more.

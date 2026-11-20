/* CBMC harness: h2_compact preserves contents and is transactional under
 * allocation failure. The object under test is built directly with 4 slots of
 * capacity and LENGTH (0..4, one run each) arbitrary entries, which is every
 * spare-capacity state merge or downsample can leave behind.
 *
 * The claim is split into two runs per LENGTH because of an observed CBMC
 * 6.6 limitation: when a malloc result may be NULL, the effect of memcpy into
 * it is not tracked, so reads of the copied data come back unconstrained even
 * on the success path. The same assertions pass in 0.3 s once malloc cannot
 * fail, and the C allocation test checks compacted contents under ASan.
 *
 *   default      reliable allocator: compact returns H2_OK, entries are
 *                identical, capacity is exact, empty storage is released, an
 *                exact object keeps its arrays, repeat compact is a no-op.
 *   -DINJECT     every library malloc may return NULL: status is H2_OK or
 *                H2_NOMEM; on H2_NOMEM the entries, length, capacity and both
 *                borrowed array pointers are unchanged; only a real shrink
 *                can fail. Success-path contents are left to the default run.
 *
 * Both use --no-malloc-may-fail. Setup avoids h2_convert/h2_merge: they are
 * covered by merge.c, and replaying them here makes the model intractable. */
#include <stdbool.h>
#include <stdlib.h>

#ifdef INJECT
bool nondet_bool(void);
static void *verify_malloc(size_t n) {
    if (nondet_bool())
        return NULL;
    return malloc(n);
}
#define malloc verify_malloc
#endif
#include "../src/h2histogram.c"
#undef malloc

#ifndef PHASE
#define PHASE H2_SPARSE
#endif
#define CAP 4
#ifndef LENGTH
#define LENGTH 2
#endif

uint64_t nondet_u64(void);
uint32_t nondet_u32(void);

void harness(void) {
    h2_config c;
    __CPROVER_assume(h2_config_init(&c, 1, 3) == H2_OK);
    h2_hist *m = NULL;
    __CPROVER_assume(allocate(c, PHASE, CAP, &m) == H2_OK); /* prunes injected setup failures */
    const size_t length = LENGTH;
    __CPROVER_assert(length <= CAP, "harness LENGTH within capacity");
    m->length = length; /* as h2_merge and h2_downsample do after filling */

    uint64_t counts[CAP];
    uint32_t indices[CAP];
    for (size_t k = 0; k < CAP; k++) {
        counts[k] = m->counts[k] = nondet_u64();
        indices[k] = m->indices[k] = nondet_u32();
    }
    const uint64_t *old_counts = m->counts;
    const uint32_t *old_indices = m->indices;

    h2_status s = h2_compact(m);
    __CPROVER_assert(s == H2_OK || s == H2_NOMEM, "compact status");
    __CPROVER_assert(m->length == length, "length unchanged");
    __CPROVER_assert(m->phase == PHASE, "phase unchanged");
#ifdef INJECT
    if (s == H2_NOMEM)
#else
    __CPROVER_assert(s == H2_OK, "reliable allocator cannot fail");
#endif
        for (size_t k = 0; k < length; k++) {
            __CPROVER_assert(m->counts[k] == counts[k], "counts preserved");
            __CPROVER_assert(m->indices[k] == indices[k], "indices preserved");
        }
    if (s == H2_NOMEM) {
        __CPROVER_assert(length != 0 && length != CAP, "only a real shrink can fail");
        __CPROVER_assert(m->counts == old_counts && m->indices == old_indices,
                         "views preserved on NOMEM");
        __CPROVER_assert(m->capacity == CAP, "capacity unchanged on NOMEM");
    } else {
        __CPROVER_assert(m->capacity == length, "exact capacity after compact");
        if (length == 0)
            __CPROVER_assert(m->counts == NULL && m->indices == NULL, "empty storage released");
        if (length == CAP)
            __CPROVER_assert(m->counts == old_counts && m->indices == old_indices,
                             "exact object keeps its storage");
        const uint64_t *exact_counts = m->counts;
        const uint32_t *exact_indices = m->indices;
        __CPROVER_assert(h2_compact(m) == H2_OK, "repeat compact cannot fail");
        __CPROVER_assert(m->counts == exact_counts && m->indices == exact_indices,
                         "repeat compact keeps storage");
    }
    h2_free(m);
}

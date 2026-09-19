#!/usr/bin/env bash
# Run every CBMC proof sequentially. Each solver run is capped in memory and
# time, and runs are never parallelised: several bit-blasted 64-bit proofs at
# once can exhaust a workstation.
#
#   verify/run.sh            all proofs
#   verify/run.sh merge      only harnesses whose name contains "merge"
#
# Environment: CBMC (default cbmc), H2_VERIFY_MEM_KB (default 6000000),
# H2_VERIFY_TIMEOUT seconds per proof (default 900).
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
cbmc=${CBMC:-cbmc}
mem_kb=${H2_VERIFY_MEM_KB:-6000000}
limit=${H2_VERIFY_TIMEOUT:-900}
filter=${1:-}

command -v "$cbmc" >/dev/null || { echo "cbmc not found; install CBMC 6.x" >&2; exit 2; }

common=(-I"$root/include" --function harness --bounds-check --pointer-check
        --unsigned-overflow-check --signed-overflow-check --conversion-check
        --undefined-shift-check --div-by-zero-check --no-malloc-may-fail
        --unwinding-assertions)

pass=0
fail=0
failed=()

prove() { # name, then cbmc arguments
    local name=$1
    shift
    if [[ -n $filter && $name != *"$filter"* ]]; then
        return
    fi
    local started=$SECONDS out status
    out=$( (ulimit -v "$mem_kb"; timeout "$limit" "$cbmc" "$@" "${common[@]}") 2>&1)
    status=$?
    if [[ $status -eq 0 ]] && grep -q "VERIFICATION SUCCESSFUL" <<<"$out"; then
        pass=$((pass + 1))
        printf 'ok    %-34s %4ds\n' "$name" $((SECONDS - started))
    else
        fail=$((fail + 1))
        failed+=("$name")
        printf 'FAIL  %-34s %4ds (exit %d)\n' "$name" $((SECONDS - started)) "$status"
        grep -E "FAILURE|ut of memory|error" <<<"$out" | head -8 | sed 's/^/      /'
    fi
}

v=$here

prove geometry "$v/geometry.c" -DGP_MAX=30 -DMVP_MAX=64 --unwind 70
prove rank "$v/rank.c"
prove add "$v/add.c" --unwind 8

for phase in H2_SPARSE H2_CUMULATIVE; do
    tag=${phase#H2_}
    prove "import-$tag" "$v/import.c" -DPHASE=$phase --unwind 8
    for n in 0 1 2 3 4; do
        prove "compact-$tag-len$n" "$v/compact.c" -DPHASE=$phase -DLENGTH=$n --unwind 6
        prove "compact-$tag-len$n-nomem" "$v/compact.c" -DPHASE=$phase -DLENGTH=$n -DINJECT --unwind 6
    done
    if [[ $phase == H2_SPARSE ]]; then
        for la in 0 1 2 3; do
            for lb in 0 1 2 3; do
                prove "merge-$tag-${la}x${lb}" "$v/merge.c" -DPHASE=$phase -DLA=$la -DLB=$lb --unwind 8
            done
        done
    else
        # Full-width cumulative merge is SAT-hard; see merge.c and README.md.
        for la in 0 1 2; do
            for lb in 0 1 2; do
                prove "merge-$tag-${la}x${lb}-low" "$v/merge.c" -DPHASE=$phase -DLA=$la -DLB=$lb \
                    -DBITS=8 -DSHIFT=0 --unwind 8
                prove "merge-$tag-${la}x${lb}-high" "$v/merge.c" -DPHASE=$phase -DLA=$la -DLB=$lb \
                    -DBITS=8 -DSHIFT=56 --unwind 8
            done
        done
    fi
    for gp in 0 1; do
        for n in 0 1 2 3; do
            prove "downsample-$tag-gp$gp-len$n" "$v/downsample.c" -DPHASE=$phase -DGP=$gp -DLEN=$n --unwind 20
        done
    done
done
for gp in 0 1; do
    prove "downsample-DENSE-gp$gp" "$v/downsample.c" -DPHASE=H2_DENSE -DGP=$gp --unwind 20
done

echo
echo "$pass proved, $fail failed"
if ((fail)); then
    printf '  %s\n' "${failed[@]}"
    exit 1
fi

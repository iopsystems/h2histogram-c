# Contributing

This is a native C11 implementation of the **h2histogram** design. Keep recording
counter-only, and preserve documented ownership, overflow and failure contracts.

## Style

Use the LLVM-derived configuration in [`.clang-format`](.clang-format), with
**clang-format 21.1.8**. It specifies four-space indentation, spaces rather than
tabs, attached braces, a 100-column target, and expanded control flow/functions.
The [clang-format reference](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
describes the mechanical options; the project rules below also apply.

- Separate functions and distinct logical steps with a blank line. Keep related
  statements together; do not insert a blank line after every statement.
- Use descriptive `snake_case` names. Public symbols retain the `h2_` prefix;
  macros and enum constants use `H2_` names.
- Explain non-obvious arithmetic and invariants. Public API comments must state
  ownership, borrowed lifetimes, output capacity, aliasing, error behavior and
  whether existing state can change on failure.
- Keep validation, allocation/cleanup, and the main operation visually distinct.
  Keep hot-path changes small and justify them with measurements when relevant.
- Do not rely on signed overflow or unchecked allocation-size arithmetic. Use
  existing checked helpers and explicit wrapping unsigned recording semantics.

The formatter does not choose logical grouping or good names; review those
manually. Do not import LLVM's C++-specific naming or language rules into this C API.

## Format and verify

```sh
python3 -m venv .venv-format
.venv-format/bin/pip install -r requirements-format.txt
.venv-format/bin/clang-format -i include/*.h src/*.c tests/*.c examples/*.c
.venv-format/bin/clang-format --dry-run --Werror include/*.h src/*.c tests/*.c examples/*.c
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

CI installs the same pinned formatter and rejects formatting drift. Its compiler
matrix also checks Debug/Release and sanitizer builds. Keep formatter version
changes explicit and update the dependency pin and this guide together.

## CodeGraph

If this repository has a `.codegraph/` index, use `codegraph_explore` or
`codegraph explore` before searching or reading code. Do not create an index.

## Windows builds

On Windows, run Zig compilation, renderer builds, native/GPU test runners and
package verification with `exec_command` and `sandbox_permissions: "require_escalated"`, through the normal
approval mechanism. The restricted execution sandbox makes Zig 0.15.2 fail with
`error: Unexpected`, even for `int main(){return 0;}` and fresh repository-local
caches. The identical compiler command succeeds outside that sandbox.

Use `python scripts/build_renderer.py` for the production DLL. It checks the
compiler before the expensive renderer build. Keep the existing Zig version,
shader freshness gates and release-package verification.

## Structure and validation

Read `docs/architecture.md` for runtime ownership and `docs/development.md` for
the repository map and commands. New development tools belong in `scripts/`,
tests in `tests/`, and current documentation in `docs/`. Existing root Python
entry points and installer paths are public compatibility contracts.

Use `python scripts/run_tests.py --suite source --strict` for source/tooling
changes. For frame demand or capture ownership, also run the `architecture`
suite; use `gpu` for the affected device/image contracts. `--list` inspects a
selection without starting tools. Run affected tests beyond these focused suites.

After adding a test, run `python scripts/run_tests.py --update-discovery` (it
updates the inventory without executing tests). Add it to the relevant named
suite when it protects that contract. Artifacts belong in `fp.output_dir()`.
Acceptance reports must match the current source tree: skipped coverage and
STALE reports do not establish success. Component checks do not prove game FPS.

# Native effect ordering

The imported sky palette is composed when the sky phase ends, before native
world foreground, floating names and scene grading. Missing current context
retains the native sky. The catalog marks the native death sky as nonreplaceable.
Only the optional palette reader is required here; orbit, fog and shadow changes
are independent.

The existing coverage target uses red for unlit receiver protection and green
for all non-depth-writing alpha layers. AO applies the remaining lit-alpha share
after its blur; the final compositor still owns lighting and fog protection.
HDR celestial emission joins the base scene before the name stencil is applied.

Run `python scripts/run_tests.py --suite source --strict`, the `architecture`
suite and the `gpu` suite. The GPU suite in this branch contains runnable image
and state oracles; none establishes game FPS or live visual acceptance.

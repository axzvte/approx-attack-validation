# Legacy area alignment implementation plan

> Execute inline using the approved user request and superpowers:executing-plans. User manually runs synthesis after GitHub delivery.

**Goal:** Evaluate current fixed-bitflip hardware in the previous full arithmetic kernels and Yosys recipe with the identical Nangate45 library.

**Architecture:** Add a validated immutable legacy RTL reference bundle and generator. Extend the existing runner/report behind `--legacy-kernel`, keeping default mode compatible. Report the three requested area views and reproduction checks without optimization.

**Spec:** ../specs/2026-10-28-legacy-area-alignment-design.md

**Tech Stack:** Python stdlib, Verilog, Yosys/ABC; Icarus and C oracle for functional tests only.

## Constraints and review focus

- Preserve all existing source/image/results and current hardware parameters.
- Compare full-width native monitor values, signed bounds, and pre-flip outputs.
- Keep the historical Liberty SHA256 and recipe traceable.
- Dry-run never invokes Yosys, including version checks.
- Distinguish fewer retained configuration bits due to synthesis from fewer requested configuration bits; independent storage must retain all bits.

## Tasks

- [x] 1. Write failing generator, native-width, immutable-source and baseline-reproduction tests. Snapshot baseline RTL/provenance, implement opt-in full-kernel generator; run Icarus/C-oracle checks.
- [x] 2. Write failing runner/report tests for legacy scope, recipe, library identity, retained-bit accounting and historical baselines. Implement opt-in selection, fingerprints and scoped summaries; preserve default mode.
- [x] 3. Add manual VS Code commands and report definitions. Run area-only regression suite and non-synthesis tee/help checks. Review changes, commit and upload; verify remote blobs and preservation of other files.

## Execution record

- Read-only evidence: old Liberty hash equals current pinned Liberty; old script hash `20b9ebe63ef38735147ab48f49dd2f9dc6a6329375ad6c0fede9272418158157`.
- Ruling: use bundled exact old baseline RTL rather than importing local builder code, so the GitHub workflow has no hidden builder dependency.
- Ruling: preserve current12/13-bit bounds, sign-extend them to native monitor width; do not assume all monitored values are unsigned8-bit pixels.
- Validation: observed missing-generator and missing-runner-API failures before implementation. Full-kernel Icarus/C-oracle checks and mocked-report tests passed. Real-library dry-run succeeded with20 planned jobs and no Yosys invocation. Actual synthesis remains manual and unmeasured.
- Independent read-only code review found no blockers. Its configuration parse/hash race suggestion was reproduced by a failing regression and fixed by hashing before parsing and rechecking afterward. No deferred findings.

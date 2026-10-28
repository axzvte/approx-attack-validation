# Fixed-bitflip area implementation plan

> For agentic workers: execute the independent tasks using the declared interfaces, followed by a whole-change review.

**Goal:** Deliver reproducible user-run Yosys/Nangate45 area code without executing synthesis.
**Architecture:** Pure configuration/design generation and verified library loading feed a manual synthesis runner. Reusable RTL and functional test oracles are independent of runner fixture tests.
**Tech Stack:** Python standard library, SystemVerilog, Icarus Verilog for functional checks, user-run Yosys/ABC.
**Spec:** ../specs/2026-10-28-bitflip-area-design.md

## Global constraints

- No automatic synthesis, model inference, training or search. Push code, user runs it.
- Preserve historical data, writable bounds, frozen monitor/bit, signed12/13 semantics, mixed exact/5RP host baselines.
- Area percentage scope is selected arithmetic-unit bank, never whole application.
- Pin real library provenance and verify area units; no invented measurements.

## Review focus

- Swapped asymmetric 5RP inputs and signed boundaries.
- Config registers optimized away or unaccounted enable muxes.
- Liberty/LEF mismatch, internal/unmapped cell area and missing report data.
- Unchanged baseline fingerprint and complete 4/6/6/6 node configuration.
- Failed/dry-run state accidentally reported as measured synthesis.

## Tasks

- [x] RTL + C oracle: observed missing-RTL failures, implemented signed triggers/config storage and faithful5RP; temporary simulation includes16,640 C/RTL vectors.
- [x] Design/config/library: observed missing-feature failures, implemented20 scoped tops, frozen configuration/source fingerprints, manual pinned fetcher and Liberty/LEF area checks.
- [x] Runner/report: fixture-only tests cover all20 jobs, validation, failures/dry-run, writable bit counts and source drift. No real synthesis executed.
- [x] Integrate safe unit/functional tests and user documentation; read-only independent review found no blocking defect. Added unequal-input routing coverage and manifest-last cache completion.

Publication uses an expected-head branch lease and remote blob verification. Final remote commit is reported only after successful publication.

## Progress

Approved scope and source baseline inspection completed. Existing isolated code checkout is roi-integration, feature/u2net-roi-stage1. User WSL checkout/data remain unchanged. Remote base is 6c73b87037061e5471d554bba9e8fa01325ca1b2.

Ruling: whole-project CTest includes model/search experiments, so only the new area functional/fixture targets are run. Actual Liberty download and Yosys synthesis are deliberately left to the user. The report contains no measured area before that execution.

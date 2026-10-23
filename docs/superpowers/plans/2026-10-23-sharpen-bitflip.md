# Sharpening single-node bit-flip validation

**Goal:** Try the agreed low-area payload in the existing project: one fixed
node, one fixed monitor and one fixed non-sign output bit; image-specific
inclusive trigger intervals. Keep all-5RP baseline and data/mask/roi_mask.jpg.

**Architecture:** Append an explicit payload selector to AttackConfig, defaulting
to the existing alternate-unit payload. Enable bit flips only in Sharpening;
reject them in applications whose execution/search has not been adapted.
Use a dedicated single-node evaluator: for each pixel replay downstream baseline
units after flipping, including output clipping. Group final squared-error
changes by monitor value and use prefix sums for interval evaluation. This is
exact for this stateless per-pixel Sharpening graph, not a DCT approximation.

**Tech stack:** Existing C++17, OpenCV, CMake, EvoApproxLib, Ubuntu WSL.

**Constraints:** No changes inside FUs. Bits 0..11 only on signed 13-bit output.
Intervals cannot include samples causing downstream signed-12-bit overflow.
Global PSNR >=30 dB; select lowest ROI PSNR among feasible intervals. Report
ROI/non-ROI MSE changes and advantage as diagnostics and success criteria, not
as the interval-ranking objective. No synthesis/area claim in this trial.

- [x] Add tests for triggered/bypassed output, signed bit behavior, inclusive
  bounds, propagation, unsupported/sign bits and invalid downstream ranges.
- [x] Implement payload execution; run targeted tests and existing suite.
- [x] Add exact single-node interval evaluator with exhaustive small-image tests
  comparing predictions and interval selection against full application runs.
- [x] Add reproducible CLI: Stage1 (1..10) free structure search, Stage2 (11..20)
  fixed-structure screening over all 144 single-node structures, Stage3 (21..30)
  frozen hardware with interval adaptation. Emit all candidates, per-image
  summaries, chosen hardware and images. Clearly label existing images reused.
- [x] Run small image trial, then the 30-image single-node pipeline if runtime
  permits; retain failures. Check actual application output for chosen intervals.
- [x] Review, run full tests and document results; publish after verification.

Ruling: Reuse the existing isolated cleanup checkout, aligned to its published
commit before opening feature/sharpen-bitflip; no new project or edits to the
user's WSL working directory. Use a dedicated evaluator rather than changing the
multi-node DCT heuristic, because exact per-pixel scoring is available here.

Ruling: The user's approval to try the previously discussed method covers this
implementation and local experiment; no additional design approval is needed.

Validation: execution tests first failed at the missing bit-flip behavior;
exhaustive small-image oracle agrees for all 144 structures. Full Release suite
26/26 passed. Existing 30-image run completed. Stage3 A2/input2/bit8 succeeds on
9/10; mean ROI PSNR 28.759 dB at budget24, 26.484 dB with all endpoints.

Review: independent reviewer found report I/O failures could silently return
success. Reproduced using /dev/full, then enabled stream exceptions and explicit
closes; regression passes. Follow-up review found no blocking issues.

Ruling: Add a --fixed diagnostic entry to reproduce all-endpoint evaluation on
supplied frozen hardware without repeating Stage1/2. It changes intervals only.
Fixed CLI integration test passes. This is an interval-budget diagnostic on the
same existing images, not additional unseen-data validation.

Ruling: Publish to a new feature branch and draft PR against the cleanup branch,
following the user's existing GitHub workflow; keep their original WSL checkout
unchanged. Do not merge automatically.

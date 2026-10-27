# U2-Net ROI stage 1 implementation plan

**Goal:** Connect offline per-image U2-Net masks to existing ten-image hardware selection and fixed-hardware interval calibration, prioritizing Sharpening.

**Authority:** User's confirmed stage-1 request on 2026-10-27. No new design approval required.

**Architecture:** Lossless PNG masks named `roi/image_NN_mask.png` pair with the existing staged image IDs. Native entries accept `--roi-dir`; this explicitly selects strict per-image mode with no shared-mask fallback or resizing. The ten-image Python runner defaults to this mode, and exposes `--roi-mode legacy-fixed` only for historical reproduction. Native callers without `--roi-dir` retain their historical interface.

**Constraints:** Keep search implementations, arithmetic baselines, node/monitor/bit invariants and frozen seed protocol unchanged. U2-Net is offline software only. Never modify old data or outputs. Validate held-out pixels/masks only after hardware freezes. Hash each training mask and each test task's own mask for provenance and cache isolation. Current user explicitly authorizes real tests, superseding earlier user-run-only preferences in historical copies.

**Review focus:** Missing/swapped masks; wrong size or interpolated/nonbinary masks; empty/full masks; cache reuse across ROI changes; held-out information leaking into hardware selection.

## Tasks

- [x] 1. RED: add Python pairing and preparation tests and native ROI tests. Run to confirm missing interface failures. Implement strict path/pixel/size checks and lossless exporter; GREEN.
- [x] 2. RED: add integration tests for native ten-image and fixed-H entries and runner forwarding/cache isolation. Add `--roi-dir`, provenance, per-image hashes and delayed held-out validation without editing search algorithms. GREEN with synthetic inputs.
- [x] 3. Run existing regression tests and new integration tests in isolated build directories. Review changes, document exact VS Code/WSL preparation/training/single-image commands and limitations. Prepare commits based on remote heads for dedicated `feature/u2net-roi-stage1` branches without overwriting shared work. Publication identities are reported in the final handoff.

## Progress

- Remote approx base: c84e2682673773eb0135aff9e0f621dae83a40a7. Source snapshot checked against every remote blob SHA after CRLF normalization; data/archive excluded.
- Remote u2net base tree: 31bb06acda1cbd689a7ccb16ac81bc605bf03278.
- Ruling: use the established stage/image IDs rather than introducing a second native CSV parser. Export a provenance manifest for auditing; deterministic mask filenames are the runtime loading contract.
- Test environment: WSL Ubuntu-24.04 has CMake, g++ and OpenCV 4.6.0. Only generated synthetic fixtures are used; no claim about the user's WSL datasets or model weights.
- Verification: consumer full Release build and 62/62 CTest suites pass; producer original 7/7 and new exporter 2/2 pass. Real cross-repository synthetic smoke freezes H and completes all ten test calibrations; no user dataset or model inference executed.
- Review fixes: ROI location joins content hashes in cache identities (relocation becomes a safe miss); strict native decode ignores EXIF to match current Pillow inference. Both findings were reproduced RED and verified GREEN; full suite passes after fixes.
- Ruling: retain historical native defaults for compatibility, but require the new Python workflow's per-image default and explicit native ROI flags in all new commands. Legacy independent discovery/completion/retest adapters remain outside stage 1 and are documented as such.
- Review boundary: segmentation semantics, same-size source/ROI swaps and research performance need source mapping and user-data review; the test record makes no claim about them.

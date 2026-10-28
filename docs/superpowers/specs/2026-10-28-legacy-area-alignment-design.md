# Legacy-method bitflip area alignment

The user requests current storage, comparison/payload, and integrated overhead under the previous Yosys method and library, before optimization. Their standing preference is GitHub delivery and manual execution; no real synthesis is authorized for the assistant.

Use the existing checkout and add an opt-in `--legacy-kernel` mode. Preserve current nodes, monitors, bits and signed12/signed13 bounds (96/148 bits). Do not reduce widths or nodes. The previous and current Liberty bytes both have SHA256 `8d540a4d4cf6d09d27c87ad067857a9c0c2eeb023ab7a56e058cd3113db4e9b1`.

Snapshot the four baseline arithmetic kernels and structural Evo5RP from the user's verified `~/EDA/DC_Area/results/area_runtime/*/rtl/`. Preserve bytes and record provenance/hashes and historical baseline areas. No external Python builder execution. Keep pixel kernels at their old signed13 internal width and DCT's complete 1-D eight-point Q15 core at signed32, including multipliers/rounding. These are arithmetic kernels, not frame-level systems.

Generate the same five measurement kinds, replacing selected-unit banks with full legacy kernels for the paired integration. Full native monitor values must be compared against sign-extended current bounds; never truncate a native32 DCT monitor to12 bits. Flip the configured bit0..11 of the full native baseline value, then propagate through the original graph/clipping. Do not reconstruct a signed13 result for exact signed32 DCT nodes.

Recipe: read RTL; synth -top TOP -flatten -noabc; dfflibmap; abc; clean. No added dffunmap. Read library blackboxes after mapping for strict checks/JSON and emit reports without further optimization. All jobs use identical settings. Retained register bits may be lower in an integrated kernel due to optimization; record expected and retained counts and flag the reduction rather than equate it to a changed software configuration. Independent storage must retain every bit.

Reports distinguish independent config storage (DFF plus load logic), independent interval/payload logic, standalone complete addition, and full-kernel delta. Split sequential/combinational area by actual cell types. Record recipe/scope and compare remeasured baseline to historical values, without fabricating equivalence or assuming exact numbers across tool builds. Preserve default bank mode and old data.

Validation uses unit fixtures, original C5RP oracle and Icarus functional simulations, plus tee/help file output tests only. No synthesis, image workloads, model inference, training, or interval searches. Full-project tests that execute those workloads remain excluded by user instruction.

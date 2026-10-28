# Yosys fixed-bitflip area evaluation

User approved the proposed FreePDK45-based area plan and requested code on GitHub for manual VS Code execution. Do not execute synthesis, models, image experiments or interval searches. Functional RTL simulation and fixture-only unit tests are permitted validation.

## Measurement boundary

Use the current frozen structures: sharpen 4 nodes/96 configuration bits; conv, sobel, dct each 6 nodes/148 bits. One independent physical instance per selected software node; no time sharing. Monitor source and flip bit are elaboration constants. Bounds are signed writable registers with one parallel cfg_load; no reset or double buffer; load before use. Input monitors are 12 bits; pre-flip baseline output monitors 13 bits. Payload changes one fixed bit 0..11 of the signed13 baseline output. No ROI/model/search/PSNR hardware.

Generate five tops per application: attack_comb (bounds as ports), config_storage (bounds registers/enable logic), attack_complete (full standalone addition), host_baseline (bank of the selected original arithmetic units), host_attacked (same bank plus programmable additions). Exact/5RP choices must match current application defaults, including mixed baselines. The paired bank is NOT a complete image-processing application; no application-wide area percentage is claimed. Integrated delta is host_attacked minus host_baseline; separately synthesized components are not assumed additive.

## Reproducibility

Pin the OpenROAD-flow-scripts commit and Git blob hashes for Nangate typical Liberty, LEF and license. Fetch only when the user explicitly runs the download script. Validate payload integrity, nominal operating conditions, and Liberty cell area against matching LEF micron geometry. Report mapped cell area in um^2, not die area. The paper says DC/FreePDK45 but omits the exact library/corner/constraints; this is a documented compatible open setup, not exact reproduction. No asserted timing or power result.

## Interfaces

- `hardware/rtl/bitflip_trigger.sv`: module `bitflip_trigger #(MONITOR_WIDTH, FLIP_BIT)` ports `monitor_value`, `lower`, `upper` (signed W), `baseline_value`, `attacked_value` (13 bits); module `bitflip_bounds #(WIDTH)` ports `clk`, `cfg_load`, `cfg_lower`, `cfg_upper`, `lower`, `upper` (signed W).
- `hardware/rtl/add12se_5RP.sv`: module `add12se_5RP(input [11:0] a,b, output [12:0] y)`; a corresponds to C symbol A, b to B; software calls C(raw_b,raw_a). Gate-equivalent to bundled C, not ordinary addition.
- `scripts/bitflip_area_design.py`: `generate_design(output: Path, config_path: Path | None = None) -> dict`; requires fresh output. Writes `design_manifest.json`, `rtl/` copies/tops. Manifest schema `bitflip_area_design_v1`, `rtl_files` relative strings, `applications` list with `application`, `nodes`, `config_bits`, `tops` map of the five kind names above. Scope `selected_unit_bank_not_full_application`.
- `scripts/area_library.py`: `load_library_manifest(path) -> dict`; validates immutable files and geometry; returned `liberty_path` absolute, `area_unit` `um^2`, nominal conditions/source hashes. Download CLI creates `library_manifest.json` schema `nangate45_library_v1` with relative `liberty`, `lef`, SHA256 fields `liberty_sha256`, `lef_sha256`.
- `scripts/run_bitflip_area.py`: CLI `--library MANIFEST --output NEW [--config JSON] [--dry-run]`; imports functions above. Dry run generates plans/RTL/scripts but never invokes Yosys. User-run synthesis creates logs, stat JSON, mapped netlists, run manifest and summary CSV/Markdown. Reject output overlap, unsupported library, missing/empty area, unmapped cells and inconsistent counts. No synthesis on import. Source hashes/tool version preserved.

## Yosys policy

Use the same fixed area-evaluation mapping recipe for every top: synth -flatten -noabc, dffunmap, dfflibmap, abc with the pinned Liberty, clean/check, reject remaining internal cell types, stat JSON with Liberty. No invented clock target, physical implementation, timing closure or power result. All config register outputs remain observable/used. Preserve failures and partial logs; never call a failed run complete.

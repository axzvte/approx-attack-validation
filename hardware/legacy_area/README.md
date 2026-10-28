# Legacy arithmetic-core references

These are byte-preserved baseline RTL snapshots from the user's previous
`DC_Area/results/area_runtime/<app>/rtl/` experiment. Hashes and historical
measurements are in `provenance.json`. They are input references, not new results.
Pixel kernels include the original graph and output clipping; DCT includes the
1-D eight-point Q15 transform, multipliers and rounding, not a frame-level system.

`evo_5RP.v` is the structural translation of the project's
`third_party/evoapproxlib/add12se_5RP.c` (B,A C argument order; A,B RTL port order).
The original EvoApproxLib MIT copyright/license in that C source applies to the
translated network as well. The reference bytes are preserved for reproducibility;
this notice accompanies them without modifying their historical fingerprints.

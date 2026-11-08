# C arithmetic kernels

These files are the auditable arithmetic source for the unified IR backend.
The adjacent `*_interface.json` files specify image sampling and output placement;
they contain no arithmetic edges. `scripts/c_kernel_encoder.py` reads the C AST,
inlines helper calls, unrolls fixed loops, and derives every arithmetic edge.

| Kernel | Input order | Tile | Outer image border |
|---|---|---|---|
| sharpen | center, top, bottom, left, right | 1x1 | retain input |
| conv | row-major 3x3, centered internally by subtracting 128 | 1x1 | retain input |
| sobel | row-major 3x3, center unused | 1x1 | zero |
| dct | row-major 8x8, centered internally by subtracting 128 | 8x8 | replicate padding |

`circuit_add(group,a,b)` and `circuit_sub(group,a,b)` identify hardware
groups. Every dynamic invocation becomes its own SSA operation and can therefore
be considered separately for a flip. DCT uses 512 such operations sharing the
same 32 group IDs as the legacy row/column hardware. Its complete reconstruction
is encoded, including all inverse-transform coefficients and rounding. There is
no distinguished DC flip site.

The supported language is intentionally bounded: signed int and long integer
arithmetic, double, fixed arrays, scalar assignment, statically resolvable loops
and array indices, direct helper calls, compile-time conditions, unary signs,
addition/subtraction, multiplication by constants, constant integer shifts,
casts, and the named exact intrinsics. Runtime division/multiplication of two
signals, runtime branches, while loops, pointers, unsigned types, short,
float32, function pointers, external calls, and arbitrary preprocessing are
rejected with a source location. Only the intrinsic header include is accepted;
the frontend never follows includes or runs a preprocessor. Expansion is limited
to 4096 iterations per loop, 16 nested helper calls, and 200000 operations.
The target reference environment uses 32-bit int and 64-bit long/long long.

`circuit_round_shift` rounds signed values halfway away from zero.
`circuit_round_even` rounds ties to even; `circuit_trunc` rounds toward zero.
The header provides exact C reference definitions for compiler comparison.
`all_integer_adds` can replace explicit marking; it automatically groups integer
addition/subtraction sites by source location and reuses those groups when loops
or calls are expanded. `marked` leaves ordinary arithmetic exact.

The stencil kernels preserve the legacy arithmetic order and operand polarity,
including the convolution's balanced tree and Sobel's sums with negated operands.
DCT forward coefficients and per-product Q15 rounding match the legacy graph.
The inverse uses a separable double-precision orthonormal matrix in place of
OpenCV's optimized `idct`; small floating-point differences can change an output
at a rounding tie. Native-versus-IR equivalence tests check the C source itself;
they do not assert bitwise identity with OpenCV's inverse implementation.

Run `python tests/test_c_kernel_encoder.py -v` with pycparser installed. The suite
checks AST-derived topology, deterministic IDs, unsupported-language diagnostics,
helper and loop expansion, rounding, and all four compiled C kernels against an
exact IR interpreter. Compiled comparison requires GCC and uses floating-point
contraction disabled.

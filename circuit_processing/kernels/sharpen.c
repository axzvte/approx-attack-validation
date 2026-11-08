#include "circuit_intrinsics.h"
/* Input order: center, top, bottom, left, right. Border pixels retain input. */
void sharpen_kernel(int input[5], double output[1]) {
    int a = circuit_add(0, 5*input[0], -input[1]);
    a = circuit_add(1, a, -input[2]);
    a = circuit_add(2, a, -input[3]);
    a = circuit_add(3, a, -input[4]);
    output[0] = circuit_clip(a, 0, 255);
}

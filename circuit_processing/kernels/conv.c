#include "circuit_intrinsics.h"
/* Row-major 3x3 centered Laplacian; exactly the legacy balanced ADD tree. */
void conv_kernel(int input[9], double output[1]) {
    int w[9];
    for (int i=0; i<9; ++i) w[i] = -(input[i]-128);
    w[4] = 8*(input[4]-128);
    int c1=circuit_add(0,w[0],w[1]);
    int c2=circuit_add(1,w[2],w[3]);
    int c3=circuit_add(2,w[4],w[5]);
    int c4=circuit_add(3,w[6],w[7]);
    int c5=circuit_add(4,c1,c2);
    int c6=circuit_add(5,c3,c4);
    int c7=circuit_add(6,c5,c6);
    int c8=circuit_add(7,c7,w[8]);
    output[0]=circuit_clip(circuit_abs(c8),0,255);
}

#include "circuit_intrinsics.h"
/* Row-major 3x3. The legacy wrapper initializes the outer border to zero. */
void sobel_kernel(int input[9], double output[1]) {
    int x1=circuit_add(0,input[2],2*input[5]);
    int x2=circuit_add(1,x1,input[8]);
    int x3=circuit_add(2,input[0],2*input[3]);
    int x4=circuit_add(3,x3,input[6]);
    int x5=circuit_add(4,x2,-x4);
    int y1=circuit_add(5,input[6],2*input[7]);
    int y2=circuit_add(6,y1,input[8]);
    int y3=circuit_add(7,input[0],2*input[1]);
    int y4=circuit_add(8,y3,input[2]);
    int y5=circuit_add(9,y2,-y4);
    int m=circuit_add(10,circuit_abs(x5),circuit_abs(y5));
    output[0]=circuit_clip(m,0,255);
}

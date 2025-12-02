#include "quantum.h"
#include <math.h>

//- Complex Number Implementations

// Put complex number function implementations here
// Eg:
Complex ComplexAdd(Complex a, Complex b) {
    return (Complex) { a.a+b.a, a.b+b.b };
}

Complex ComplexSub(Complex a, Complex b) {
    return (Complex) { a.a-b.a, a.b-b.b };
}

Complex ComplexMul(Complex a, Complex b) {
    return (Complex) { ((a.a*b.a) - (a.b*b.b)), ((a.a*b.b) + (b.a*a.b)) };
}

Complex ComplexConjugate(Complex c) {
    return (Complex) { c.a, c.b*(-1) };
}

Complex ComplexDiv(Complex a, Complex b) {
    return (Complex) { ((a.a*b.a + a.b*b.b))/((b.a*b.a + b.b*b.b)), ((a.b*b.a - a.a*b.b))/((b.a*b.a + b.b*b.b)) };
}

f64 ComplexMag(Complex c) {
    return (f64) { sqrt((c.a*c.a) + (c.b*c.b)) };
}

f64 ComplexAngle(Complex c) {
    return (f64) { atan2(c.b,c.a) * 57.2958 };
}

Complex ComplexScale(Complex c, f64 s) {
    return (Complex) { (c.a*s),(c.b*s) };
}

Complex ComplexExp(f64 theta) {
    return (Complex) { cos(theta), sin(theta) };
}

b8 ComplexEpsEqual(Complex a, Complex b, f64 eps) {
    f64 da = fabs(a.a - b.a);
    f64 db = fabs(a.b - b.b);
    return (da <= eps && db <= eps);
}

void ComplexPrint(Complex c) {
    if (c.b >= 0)
        printf("%.6f + %.6fi", c.a, c.b);
    else
        printf("%.6f - %.6fi", c.a, -c.b);
}

//- Qubit Implementations

Qubit QubitNormalize(Qubit q) {
    f64 norm = sqrt((q.alpha.a*q.alpha.a + q.alpha.b*q.alpha.b)+ (q.beta.a*q.beta.a + q.beta.b*q.beta.b));
    return (Qubit) { {q.alpha.a/norm, q.alpha.b/norm}, {q.beta.a/norm, q.beta.b/norm} };
}

f64 QubitMeasure(Qubit q) {
    return (q.beta.a*q.beta.a) + (q.beta.b*q.beta.b);
}

b8 QubitEpsEqual(Qubit a, Qubit b, f64 eps) {
    return
        ComplexEpsEqual(a.alpha, b.alpha, eps) &&
        ComplexEpsEqual(a.beta, b.beta, eps);
}

void QubitPrint(Qubit q) {
    printf("");
    ComplexPrint(q.alpha);
    printf(" |0>+ ");
    ComplexPrint(q.beta);
    printf(" |1>");
}

//- QState Implementations
void QStateResize(QState* state, u32 bitcount) {
    int count = 1;
    for (int i = 0; i < bitcount; i++) count *= 2;
    
    if (state->cap < count) {
        state->state = realloc(state->state, count * sizeof(Complex));
        state->cap   = count;
    }
    
    state->bitcount = bitcount;
    state->size = count;
    memset(state->state, 0, state->cap * sizeof(Complex));
    
    state->state[0] = (Complex) { 1.0f, 0.0f };
}

//- QGate Implementations

static Complex ComplexAdd4(Complex a, Complex b, Complex c, Complex d) {
    return ComplexAdd(ComplexAdd(a, b), ComplexAdd(c, d));
}

// Put qgate function implementations here
void QGateApply(QGate* g, u32* inputs_idxs, QState* state) {
    Complex* psi = state->state;
    
    switch (g->size) {
        case QGate_1: {
            u32 stride = 1u << inputs_idxs[0];
            
            for (u32 base = 0; base < state->size; base += (stride << 1)) {
                for (u32 i = 0; i < stride; i++) {
                    u32 i0 = base + i;
                    u32 i1 = i0 + stride;
                    
                    Complex v0 = psi[i0];
                    Complex v1 = psi[i1];
                    
                    psi[i0] = ComplexAdd(ComplexMul(g->m1[0][0], v0), ComplexMul(g->m1[0][1], v1));
                    psi[i1] = ComplexAdd(ComplexMul(g->m1[1][0], v0), ComplexMul(g->m1[1][1], v1));
                }
            }
        } break;
        
        case QGate_2: {
            u32 q0 = inputs_idxs[0];
            u32 q1 = inputs_idxs[1];
            if (q1 < q0) {
                u32 t = q0; q0 = q1; q1 = t;
            }
            
            u32 s0 = 1u << q0;
            u32 s1 = 1u << q1;
            u32 b1 = s1 << 1;
            
            for (u32 base = 0; base < state->size; base += (b1 << 1)) {
                for (u32 b = 0; b < b1; b += (s1 << 1)) {
                    for (u32 i = 0; i < s0; i++) {
                        u32 i00 = base + b + i;
                        u32 i01 = i00 + s0;
                        u32 i10 = i00 + s1;
                        u32 i11 = i10 + s0;
                        
                        Complex v00 = psi[i00];
                        Complex v01 = psi[i01];
                        Complex v10 = psi[i10];
                        Complex v11 = psi[i11];
                        
                        psi[i00] = ComplexAdd4(ComplexMul(g->m2[0][0], v00),
                                               ComplexMul(g->m2[0][1], v01),
                                               ComplexMul(g->m2[0][2], v10),
                                               ComplexMul(g->m2[0][3], v11));
                        psi[i01] = ComplexAdd4(ComplexMul(g->m2[1][0], v00),
                                               ComplexMul(g->m2[1][1], v01),
                                               ComplexMul(g->m2[1][2], v10),
                                               ComplexMul(g->m2[1][3], v11));
                        psi[i10] = ComplexAdd4(ComplexMul(g->m2[2][0], v00),
                                               ComplexMul(g->m2[2][1], v01),
                                               ComplexMul(g->m2[2][2], v10),
                                               ComplexMul(g->m2[2][3], v11));
                        psi[i11] = ComplexAdd4(ComplexMul(g->m2[3][0], v00),
                                               ComplexMul(g->m2[3][1], v01),
                                               ComplexMul(g->m2[3][2], v10),
                                               ComplexMul(g->m2[3][3], v11));
                    }
                }
            }
        } break;
    }
}


void QGatePrint(QGate* g) {
    int len = 1;
    for (int i = 0; i < g->size; i++) len *= 2;
    
    printf("[");
    for (int i = 0; i < len; i++) {
        printf("[");
        for (int j = 0; j < len; j++) {
            ComplexPrint(g->m[i*len+j]);
            if (j != len-1) printf(", ");
        }
        if (i != len-1) printf("]\n");
    }
    printf("]");
}
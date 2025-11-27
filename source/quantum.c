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

//- QGate Implementations

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
            // TODO
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
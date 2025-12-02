/* date = November 13th 2025 0:44 pm */

#ifndef QUANTUM_H
#define QUANTUM_H

#include "base/base.h"

// a + bi
typedef struct Complex {
    f64 a;
    f64 b;
} Complex;

Complex ComplexAdd(Complex a, Complex b);
Complex ComplexSub(Complex a, Complex b);
Complex ComplexMul(Complex a, Complex b);
Complex ComplexDiv(Complex a, Complex b);
f64     ComplexMag(Complex c);
f64     ComplexAngle(Complex c);
Complex ComplexConjugate(Complex c);
Complex ComplexScale(Complex c, f64 s);
Complex ComplexExp(f64 theta);
b8      ComplexEpsEqual(Complex a, Complex b, f64 eps);
void    ComplexPrint(Complex c);

// Dirac's Notation:  alpha |0> + beta |1>
typedef struct Qubit {
    Complex alpha;
    Complex beta;
} Qubit;

Qubit QubitNormalize(Qubit q);
f64   QubitMeasure(Qubit q);
b8    QubitEpsEqual(Qubit a, Qubit b, f64 eps);
void  QubitPrint(Qubit q);

typedef struct QState {
    u32      bitcount;
    u32      size;
    u32      cap;
    Complex* state;
} QState;

void QStateResize(QState* state, u32 bitcount);

// https://en.wikipedia.org/wiki/Quantum_logic_gate
typedef enum QGateSize {
    QGate_1 = 1,
    QGate_2 = 2,
} QGateSize;

typedef struct QGate {
    QGateSize size;
    union {
        Complex m [16];
        Complex m1[2][2];
        Complex m2[4][4];
    };
} QGate;

void QGateApply(QGate* g, u32* inputs_idxs, QState* state);
void QGatePrint(QGate* g);

#endif //QUANTUM_H

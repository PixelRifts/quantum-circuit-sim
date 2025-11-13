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
Qubit QubitMeasure(Qubit q);
b8    QubitEpsEqual(Qubit a, Qubit b, f64 eps);
void  QubitPrint(Qubit q);

// https://en.wikipedia.org/wiki/Quantum_logic_gate
// Matrix form
typedef struct QGate {
    Complex m[2][2];
} QGate;

Qubit QGateApply(QGate g, Qubit q);
QGate QGateCompose(QGate a, QGate b); // Probably not gonna use this but ok
b8    QGateIsUnitary(QGate g, f64 eps);
b8 QGateEpsEqual(QGate a, QGate b, f64 eps);
void  QGatePrint(QGate g);

#endif //QUANTUM_H

#include "quantum.h"
#include <math.h>

//- Complex Number Implementations

// Put complex number function implementations here
// Eg:
Complex ComplexAdd(Complex a, Complex b) { //z1, z2
    return (Complex) { a.a+b.a, a.b+b.b };
}

Complex ComplexSub(Complex a, Complex b){
    return (Complex) {a.a-b.a, a.b-b.b};
}

Complex ComplexMul(Complex a, Complex b){
    return (Complex) {((a.a*b.a) - (a.b*b.b)), ((a.a*b.b) + (b.a*a.b))};
}

Complex ComplexConjugate(Complex c){
    return (Complex) {c.a, c.b*(-1) };
}

Complex ComplexDiv(Complex a, Complex b){
    return (Complex) {((a.a*b.a + a.b*b.b))/((b.a*b.a + b.b*b.b)), ((a.b*b.a - a.a*b.b))/((b.a*b.a + b.b*b.b))};
}

f64 ComplexMag(Complex c){
    return (f64) {sqrt((c.a*c.a) + (c.b*c.b))};
}

f64 ComplexAngle(Complex c){
    return (f64) {atan2(c.b,c.a) * (180/PI)};  //atan2 func used
}

Complex ComplexScale(Complex c, f64 s){ //scaling?
    return (Complex) {(c.a*s),(c.b*s)};
}

Complex ComplexExp(f64 theta){ //rotation?
    return (Complex) {cos(theta), sin(theta)};  //dbt
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

// Put qubit function implementations here
// TODO()

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
    printf(" |1>\n");
}

//- QGate Implementations

// Put qgate function implementations here
// TODO()


b8 QGateEpsEqual(QGate a, QGate b, f64 eps) {
    return
        ComplexEpsEqual(a.m[0][0], b.m[0][0], eps) &&
        ComplexEpsEqual(a.m[0][1], b.m[0][1], eps) &&
        ComplexEpsEqual(a.m[1][0], b.m[1][0], eps) &&
        ComplexEpsEqual(a.m[1][1], b.m[1][1], eps);
}

void QGatePrint(QGate g) {
    printf("[[ ");
    ComplexPrint(g.m[0][0]);
    printf(", ");
    ComplexPrint(g.m[0][1]);
    printf(" ],\n [ ");
    ComplexPrint(g.m[1][0]);
    printf(", ");
    ComplexPrint(g.m[1][1]);
    printf(" ]]\n");
}

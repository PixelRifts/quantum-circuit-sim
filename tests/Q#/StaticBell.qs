namespace QuantumApp {
    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Canon;

    @EntryPoint()
    operation Main() : Unit {
        use qs = Qubit[2];
        H(qs[0]);
        CNOT(qs[0], qs[1]);
    }
}

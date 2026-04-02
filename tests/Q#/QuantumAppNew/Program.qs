namespace QuantumAppNew {

    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Measurement;

    operation GHZState() : Unit {

        use q = Qubit[3];

        H(q[0]);

        CNOT(q[0], q[1]);
        CNOT(q[0], q[2]);

        let r0 = M(q[0]);
        let r1 = M(q[1]);
        let r2 = M(q[2]);

        Message($"GHZ Result: {r0} {r1} {r2}");

        ResetAll(q);
    }

    @EntryPoint()
    operation Main() : Unit {
        GHZState();
    }
}
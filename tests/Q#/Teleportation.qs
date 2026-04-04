namespace Teleportation {
    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Diagnostics;
    open Microsoft.Quantum.Measurement;

    @EntryPoint()
    operation Main() : Unit {
        Message("=== Quantum Teleportation (3-qubit) ===\n");

        let shots = 8;
        for i in 1..shots {
            use q = Qubit[3];
            H(q[0]);

            H(q[1]);
            CNOT(q[1], q[2]);

            CNOT(q[0], q[1]);
            H(q[0]);
            let m0 = MResetZ(q[0]);
            let m1 = MResetZ(q[1]);

            if m1 == One { X(q[2]); }
            if m0 == One { Z(q[2]); }

            let result = MResetZ(q[2]);
            Message($"Shot {i}: Alice(m0={m0}, m1={m1})  Bob={result}");
        }
    }
}
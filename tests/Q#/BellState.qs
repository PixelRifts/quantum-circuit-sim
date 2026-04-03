namespace BellState {
    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Canon;
    open Microsoft.Quantum.Diagnostics;
    open Microsoft.Quantum.Measurement;

    @EntryPoint()
    operation Main() : Unit {
        Message("=== 3-Qubit Bell State ===\n");

        // Run each preparation 5 times to show the correlated measurement statistics
        let shots = 5;

        Message("── |Φ+⟩ extended to 3 qubits: (|000⟩ + |111⟩)/√2 ──");
        for _ in 1..shots {
            use q = Qubit[3];
            // q0 in superposition, q1 and q2 entangled with q0
            H(q[0]);
            CNOT(q[0], q[1]);
            CNOT(q[0], q[2]);
            let r0 = MResetZ(q[0]);
            let r1 = MResetZ(q[1]);
            let r2 = MResetZ(q[2]);
            Message($"  Measured: {r0} {r1} {r2}");
        }
        Message("");

        Message("── |Φ-⟩ extended to 3 qubits: (|000⟩ - |111⟩)/√2 ──");
        for _ in 1..shots {
            use q = Qubit[3];
            H(q[0]);
            Z(q[0]);
            CNOT(q[0], q[1]);
            CNOT(q[0], q[2]);
            let r0 = MResetZ(q[0]);
            let r1 = MResetZ(q[1]);
            let r2 = MResetZ(q[2]);
            Message($"  Measured: {r0} {r1} {r2}");
        }
        Message("");

        Message("── |Ψ+⟩ extended to 3 qubits: (|011⟩ + |100⟩)/√2 ──");
        for _ in 1..shots {
            use q = Qubit[3];
            H(q[0]);
            CNOT(q[0], q[1]);
            CNOT(q[0], q[2]);
            X(q[1]);
            X(q[2]);
            let r0 = MResetZ(q[0]);
            let r1 = MResetZ(q[1]);
            let r2 = MResetZ(q[2]);
            Message($"  Measured: {r0} {r1} {r2}");
        }
        Message("");

        Message("── |Ψ-⟩ extended to 3 qubits: (|011⟩ - |100⟩)/√2 ──");
        for _ in 1..shots {
            use q = Qubit[3];
            H(q[0]);
            Z(q[0]);
            CNOT(q[0], q[1]);
            CNOT(q[0], q[2]);
            X(q[1]);
            X(q[2]);
            let r0 = MResetZ(q[0]);
            let r1 = MResetZ(q[1]);
            let r2 = MResetZ(q[2]);
            Message($"  Measured: {r0} {r1} {r2}");
        }
        Message("");

        Message("Done. All results should show perfect 3-way correlations.");
    }
}

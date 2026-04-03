namespace GHZ {
    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Measurement;

    @EntryPoint()
    operation Main() : Unit {
        Message("=== GHZ State (3-qubit) ===\n");

        // Run several shots to demonstrate the 50/50 split between |000⟩ and |111⟩
        let shots = 8;
        for i in 1..shots {
            use q = Qubit[3];

            // Prepare GHZ state: (|000⟩ + |111⟩) / √2
            H(q[0]);
            CNOT(q[0], q[1]);
            CNOT(q[0], q[2]);

            let r0 = MResetZ(q[0]);
            let r1 = MResetZ(q[1]);
            let r2 = MResetZ(q[2]);

            Message($"Shot {i}: {r0} {r1} {r2}");
        }

    }
}

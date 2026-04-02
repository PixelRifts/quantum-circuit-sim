namespace GroverAlgo {

    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Canon;
    open Microsoft.Quantum.Diagnostics;
    open Microsoft.Quantum.Measurement;

    @EntryPoint()
    operation Main() : Unit {

        use qs = Qubit[3];

        // Step 1: Equal superposition
        ApplyToEach(H, qs);
        Message("=== Initial Superposition ===");
        DumpMachine();

        // Step 2: Grover iteration
        Oracle011(qs);
        Diffusion(qs);

        Message("=== After Grover Iteration ===");
        DumpMachine();

        // Step 3: Measure
        let r0 = MResetZ(qs[0]);
        let r1 = MResetZ(qs[1]);
        let r2 = MResetZ(qs[2]);

        Message($"Measurement: [{r0}, {r1}, {r2}]");
        Message("Expected:    [Zero, One, One] = |011>");
    }

    operation Oracle011(qs : Qubit[]) : Unit {
        X(qs[0]);                           
        Controlled Z([qs[0], qs[1]], qs[2]);  
        X(qs[0]);                             
    }

    operation Diffusion(qs : Qubit[]) : Unit {
        ApplyToEach(H, qs);
        ApplyToEach(X, qs);
        Controlled Z([qs[0], qs[1]], qs[2]);
        ApplyToEach(X, qs);
        ApplyToEach(H, qs);
    }
}
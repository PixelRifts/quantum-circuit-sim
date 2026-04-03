namespace QuantumApp {

    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Canon;
    open Microsoft.Quantum.Diagnostics;
    open Microsoft.Quantum.Math;

    @EntryPoint()
    operation Main() : Unit {

        use qs = Qubit[3]; // allocate 3 qubits

        Message("Initial state |000>:");
        DumpMachine();

        // -------- QFT START --------

        // Qubit 0
        H(qs[0]);
        Controlled R1([qs[1]], (PI() / 2.0, qs[0]));
        Controlled R1([qs[2]], (PI() / 4.0, qs[0]));

        // Qubit 1
        H(qs[1]);
        Controlled R1([qs[2]], (PI() / 2.0, qs[1]));

        // Qubit 2
        H(qs[2]);

        // Swap qubits (reverse order)
        SWAP(qs[0], qs[2]);

        // -------- QFT END --------

        Message("After QFT:");
        DumpMachine();

        // Reset qubits
        ResetAll(qs);
    }
}
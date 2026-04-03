namespace DeutschJozsa {
    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Canon;
    open Microsoft.Quantum.Diagnostics;
    open Microsoft.Quantum.Measurement;

    @EntryPoint()
    operation Main() : Unit {
        Message("=== Deutsch-Jozsa Algorithm (3-qubit) ===\n");

        // ── Test 1: Constant Zero Oracle ──
        Message("Oracle: Constant Zero  f(x) = 0 for all x");
        let r1 = DeutschJozsa3(ConstantZeroOracle);
        Message($"Result: {r1}");
        Message("Expected: Constant\n");

        // ── Test 2: Constant One Oracle ──
        Message("Oracle: Constant One  f(x) = 1 for all x");
        let r2 = DeutschJozsa3(ConstantOneOracle);
        Message($"Result: {r2}");
        Message("Expected: Constant\n");

        // ── Test 3: Balanced - parity of all 3 bits ──
        Message("Oracle: Balanced (parity of all 3 bits)");
        let r3 = DeutschJozsa3(BalancedParityOracle);
        Message($"Result: {r3}");
        Message("Expected: Balanced\n");

        // ── Test 4: Balanced - first bit only ──
        Message("Oracle: Balanced (first bit only)");
        let r4 = DeutschJozsa3(BalancedFirstBitOracle);
        Message($"Result: {r4}");
        Message("Expected: Balanced\n");
    }

    // Fixed 3-qubit Deutsch-Jozsa circuit
    operation DeutschJozsa3(oracle : ((Qubit[], Qubit) => Unit)) : String {
        use inputQs = Qubit[3];
        use ancilla = Qubit();

        // Prepare ancilla in |-> state
        X(ancilla);
        H(ancilla);

        // Put input register into uniform superposition
        H(inputQs[0]);
        H(inputQs[1]);
        H(inputQs[2]);

        Message("State after Hadamard:");
        DumpMachine();

        // Apply oracle
        oracle(inputQs, ancilla);

        Message("State after Oracle:");
        DumpMachine();

        // Interfere
        H(inputQs[0]);
        H(inputQs[1]);
        H(inputQs[2]);

        // If all three input qubits measure |0>, f is constant; otherwise balanced
        mutable isConstant = true;
        if MResetZ(inputQs[0]) == One { set isConstant = false; }
        if MResetZ(inputQs[1]) == One { set isConstant = false; }
        if MResetZ(inputQs[2]) == One { set isConstant = false; }

        Reset(ancilla);

        return isConstant ? "Constant" | "Balanced";
    }

    // f(x) = 0  ->  do nothing to ancilla
    operation ConstantZeroOracle(inputQs : Qubit[], ancilla : Qubit) : Unit { }

    // f(x) = 1  ->  flip ancilla (global phase, all-ones result)
    operation ConstantOneOracle(inputQs : Qubit[], ancilla : Qubit) : Unit {
        X(ancilla);
    }

    // f(x) = x0 XOR x1 XOR x2  (balanced)
    operation BalancedParityOracle(inputQs : Qubit[], ancilla : Qubit) : Unit {
        CNOT(inputQs[0], ancilla);
        CNOT(inputQs[1], ancilla);
        CNOT(inputQs[2], ancilla);
    }

    // f(x) = x0  (balanced - half of inputs have x0=0, half have x0=1)
    operation BalancedFirstBitOracle(inputQs : Qubit[], ancilla : Qubit) : Unit {
        CNOT(inputQs[0], ancilla);
    }
}

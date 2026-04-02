namespace DeutschJozsa {

    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Canon;
    open Microsoft.Quantum.Diagnostics;
    open Microsoft.Quantum.Measurement;

    @EntryPoint()
    operation Main() : Unit {

        Message("=== Deutsch-Jozsa Algorithm ===\n");

        let n = 3;

        // ── Test 1: Constant Zero Oracle ──
        Message("Oracle: Constant Zero f(x) = 0 for all x");
        let r1 = DeutschJozsa(n, ConstantZeroOracle);
        Message($"Result: {r1}");
        Message("Expected: Constant\n");

        // ── Test 2: Constant One Oracle ──
        Message("Oracle: Constant One f(x) = 1 for all x");
        let r2 = DeutschJozsa(n, ConstantOneOracle);
        Message($"Result: {r2}");
        Message("Expected: Constant\n");

        // ── Test 3: Balanced - parity oracle ──
        Message("Oracle: Balanced (parity of all bits)");
        let r3 = DeutschJozsa(n, BalancedParityOracle);
        Message($"Result: {r3}");
        Message("Expected: Balanced\n");

        // ── Test 4: Balanced - first bit oracle ──
        Message("Oracle: Balanced (first bit only)");
        let r4 = DeutschJozsa(n, BalancedFirstBitOracle);
        Message($"Result: {r4}");
        Message("Expected: Balanced\n");
    }

    operation DeutschJozsa(
        n      : Int,
        oracle : ((Qubit[], Qubit) => Unit)
    ) : String {

        use inputQs  = Qubit[n];
        use ancilla  = Qubit();


        X(ancilla);
        H(ancilla);

       
        ApplyToEach(H, inputQs);

        Message("State after Hadamard:");
        DumpMachine();
        oracle(inputQs, ancilla);

        Message("State after Oracle:");
        DumpMachine();

        ApplyToEach(H, inputQs);

        mutable isConstant = true;
        for i in 0..n-1 {
            let result = MResetZ(inputQs[i]);
            if result == One {
                set isConstant = false;
            }
        }

        Reset(ancilla);

        return isConstant ? "Constant" | "Balanced";
    }


    operation ConstantZeroOracle(
        inputQs : Qubit[],
        ancilla : Qubit
    ) : Unit {
    }

 
    operation ConstantOneOracle(
        inputQs : Qubit[],
        ancilla : Qubit
    ) : Unit {
        X(ancilla);
        X(ancilla); 
       
        Z(ancilla); 
    }

    operation BalancedParityOracle(
        inputQs : Qubit[],
        ancilla : Qubit
    ) : Unit {
       
        for q in inputQs {
            CNOT(q, ancilla);
        }
    }

   
    operation BalancedFirstBitOracle(
        inputQs : Qubit[],
        ancilla : Qubit
    ) : Unit {
        CNOT(inputQs[0], ancilla);
    }
}
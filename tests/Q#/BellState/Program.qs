namespace BellStateApp {

    open Microsoft.Quantum.Intrinsic;
    open Microsoft.Quantum.Canon;
    open Microsoft.Quantum.Diagnostics;
    open Microsoft.Quantum.Measurement;

    @EntryPoint()
    operation Main() : (Result, Result)[] {

        // Allocate 2 qubits
        use register = Qubit[2];

        let bellStates = [
            ("|Φ+〉", PreparePhiPlus),
            ("|Φ-〉", PreparePhiMinus),
            ("|Ψ+〉", PreparePsiPlus),
            ("|Ψ-〉", PreparePsiMinus)
        ];

        mutable results = [];

        for (label, prepare) in bellStates {

            prepare(register);

            Message($"Preparing Bell state {label}:");

            // Show quantum state
            DumpMachine();

            // Measure and reset
            let r0 = MResetZ(register[0]);
            let r1 = MResetZ(register[1]);

            Message($"Measurement: {r0}, {r1}");

            set results += [(r0, r1)];
        }

        return results;
    }

    // |Φ+〉 = (|00〉 + |11〉)/√2
    operation PreparePhiPlus(register : Qubit[]) : Unit {
        ResetAll(register);
        H(register[0]);
        CNOT(register[0], register[1]);
    }

    // |Φ-〉 = (|00〉 - |11〉)/√2
    operation PreparePhiMinus(register : Qubit[]) : Unit {
        ResetAll(register);
        H(register[0]);
        Z(register[0]);
        CNOT(register[0], register[1]);
    }

    // |Ψ+〉 = (|01〉 + |10〉)/√2
    operation PreparePsiPlus(register : Qubit[]) : Unit {
        ResetAll(register);
        H(register[0]);
        CNOT(register[0], register[1]);
        X(register[1]);
    }

    // |Ψ-〉 = (|01〉 - |10〉)/√2
    operation PreparePsiMinus(register : Qubit[]) : Unit {
        ResetAll(register);
        H(register[0]);
        CNOT(register[0], register[1]);
        X(register[1]);
        Z(register[0]);
    }
}
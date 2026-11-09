# MetaQuantum Language (MQL) — Syntax & Programmer's Guide

MetaQuantum Language (**MQL**) is an intuitive, expressive, pseudo-code-like language designed for writing quantum algorithms with minimal boilerplate. MQL code compiles directly to intermediate representation (MQ IR) and transpiles natively to industry-standard quantum frameworks including **Qiskit**, **Cirq**, and **Q#**.

---

## 1. Quick Start: 2-Qubit Grover's Algorithm

Here is an example demonstrating MQL's pseudo-code-like syntax and built-in algorithmic macros:

```mql
circuit Grover {
    qubit q[2];
    
    // Apply Hadamard to all qubits in the range [0..1]
    H q[0..1];
    
    // Synthesize phase oracle for marked state |11>
    grover_oracle(q, "11");
    
    // Apply standard Grover diffusion operator
    grover_diffuser(q);
    
    // Vectorized measurement into 2 classical bits
    measure q -> c[2];
}
```

### Transpilation Commands

```bash
# View IR & register layout
composer grover.mql

# Transpile to Qiskit (Python)
composer grover.mql --to qiskit

# Transpile to Cirq (Python)
composer grover.mql --to cirq

# Transpile to Q# (.NET)
composer grover.mql --to qsharp
```

---

## 2. Circuit Structure & Registers

Every MQL program defines quantum circuits, custom gates, or operations using braces `{ ... }`.

### Declaring Quantum & Classical Registers

```mql
circuit MyCircuit {
    // Multi-qubit register
    qubit q[4];
    
    // Single (scalar) qubit
    qubit ancilla;
    
    // Classical bit register
    bit c[4];
    
    // Single classical bit
    bit flag;
}
```

> **Note:** If classical bit registers are omitted in `measure q -> c[2];`, MQL automatically creates and infers the classical register `c` with the required bit width.

---

## 3. Range Indexing & Vectorized Operations

MQL lets you reference individual qubits, sub-ranges, or entire registers seamlessly:

| Syntax | Meaning | Example |
| :--- | :--- | :--- |
| `q[i]` | Single qubit at index $i$ | `H q[0];` |
| `q[start..end]` | Inclusive range $[start, end]$ | `H q[0..3];` (applies H to $0, 1, 2, 3$) |
| `q[start:end]` | Slice range $[start, end)$ | `X q[0:2];` (applies X to $0, 1$) |
| `q[0, 2, 3]` | Specific list of qubit indices | `H q[0, 2];` |
| `q` | Entire register $q$ | `H q;` |

### Vectorized Gate Broadcasting
Instead of writing repetitive single-qubit gates across lines:
```mql
// Traditional / verbose:
H q[0];
H q[1];
H q[2];
H q[3];

// MQL Vectorized Range:
H q[0..3];

// Or apply to entire register:
H q;
```

---

## 4. Quantum Gate Reference

MQL supports all standard single-qubit, two-qubit, multi-qubit, and parameterized gates:

### Single-Qubit Gates
- **Pauli & Clifford Gates**: `I`, `H`, `X`, `Y`, `Z`, `S`, `Sdg`, `T`, `Tdg`
- **Parameterized Phase**: `P(theta)` or `Phase(theta)`
- **Rotations**: `RX(theta)`, `RY(theta)`, `RZ(theta)`
- **Universal Unitary**: `U(theta, phi, lambda)` or `U3(theta, phi, lambda)`

### Multi-Qubit Gates & Aliases
- **Entangling Gates**:
  - `CNOT q_ctrl -> q_target;` or `CX q_ctrl -> q_target;`
  - `CZ q_ctrl -> q_target;`
  - `CP(theta) q_ctrl -> q_target;`
  - `CRX(theta)`, `CRY(theta)`, `CRZ(theta)`
- **Swaps**:
  - `SWAP q[0], q[1];`
  - `ISWAP q[0], q[1];`
- **3-Qubit Gates**:
  - `Toffoli q[0], q[1], q[2];` or `CCX q[0], q[1], q[2];`
  - `Fredkin q[0], q[1], q[2];` or `CSWAP q[0], q[1], q[2];`
- **Two-Qubit Interactions**:
  - `RXX(theta) q[0], q[1];`
  - `RYY(theta) q[0], q[1];`
  - `RZZ(theta) q[0], q[1];`

### Arbitrary Multi-Control Modifiers
You can attach arbitrary positive (`ctrl[...]`) and negative (`ctrl0[...]`) controls to any gate:

```mql
// Multi-controlled X gate with control on q[0], q[1], q[2]
ctrl[q[0..2]] X q[3];

// Negative control (activates when q[0] is in |0>)
ctrl0[q[0]] X q[1];
```

---

## 5. Algorithmic Macros & High-Level Subroutines

MQL includes built-in algorithmic macros that expand directly to optimal gate sequences:

### 1. `grover_oracle(qubits, "pattern")`
Synthesizes a phase oracle marking a specified bitstring pattern (e.g. `"11"`, `"101"`, `"00"`).
```mql
grover_oracle(q, "11");
```

### 2. `grover_diffuser(qubits)`
Synthesizes the standard Grover diffusion operator $2|s\rangle\langle s| - I = H^{\otimes n} X^{\otimes n} (MCZ) X^{\otimes n} H^{\otimes n}$.
```mql
grover_diffuser(q);
```

### 3. `bell_pair(q0, q1)`
Prepares the maximally entangled Bell state $|\Phi^+\rangle = \frac{|00\rangle + |11\rangle}{\sqrt{2}}$ ($H \to CX$).
```mql
bell_pair(q[0], q[1]);
```

### 4. `qft(qubits)` & `iqft(qubits)`
Synthesizes the Quantum Fourier Transform (and Inverse QFT) with phase-ladder gates and bit-reversal swaps across any number of qubits.
```mql
qft(q[0..3]);
iqft(q[0..3]);
```

### 5. `teleport(msg, alice, bob)`
Synthesizes the complete Quantum Teleportation channel.
```mql
teleport(msg, alice, bob);
```

---

## 6. Measurements & Classical Feedback

### Vectorized Measurement
```mql
// Measure entire register into classical bits
measure q -> c[2];

// Measure specific range into specific classical bits
measure q[0..1] -> c[0..1];

// Measure single qubit
measure q[0] -> c[0];
```

### Conditioned Quantum Operations
```mql
// Apply X to bob if measurement outcome c[1] == 1
X bob if c[1] == 1;

// Apply Z to bob if measurement outcome c[0] == 1
Z bob if c[0] == 1;
```

---

## 7. Complete Worked Examples

### Example 1: Quantum Teleportation Protocol
```mql
// Quantum Teleportation Protocol
circuit Teleportation {
    qubit msg;
    qubit alice;
    qubit bob;
    bit c[2];

    // State preparation on msg
    H msg;

    // Distribute entangled pair between Alice and Bob
    bell_pair(alice, bob);

    // Bell-basis measurement by Alice
    CNOT msg -> alice;
    H msg;
    measure msg -> c[0];
    measure alice -> c[1];

    // Bob applies classical corrections
    X bob if c[1] == 1;
    Z bob if c[0] == 1;
}
```

### Example 2: 3-Qubit Quantum Fourier Transform (QFT)
```mql
circuit QFT {
    qubit qs[3];
    bit c[3];

    // Qubit 0
    H qs[0];
    ctrl[qs[1]] P(pi / 2) qs[0];
    ctrl[qs[2]] P(pi / 4) qs[0];

    // Qubit 1
    H qs[1];
    ctrl[qs[2]] P(pi / 2) qs[1];

    // Qubit 2
    H qs[2];

    // Bit reversal
    SWAP qs[0], qs[2];

    measure qs -> c[3];
}
```

### Example 3: 4-Qubit Greenberger–Horne–Zeilinger (GHZ) State
```mql
circuit GHZ {
    qubit q[4];
    bit c[4];

    H q[0];
    CNOT q[0] -> q[1];
    CNOT q[1] -> q[2];
    CNOT q[2] -> q[3];

    measure q -> c;
}
```

---

## 8. Command-Line Reference

```bash
# Compile and show internal MQ IR
composer <input.mql>

# Transpile to Qiskit Python file
composer <input.mql> --to qiskit -o circuit_qiskit.py

# Transpile to Cirq Python file
composer <input.mql> --to cirq -o circuit_cirq.py

# Transpile to Microsoft Q# file
composer <input.mql> --to qsharp -o circuit_qsharp.qs
```

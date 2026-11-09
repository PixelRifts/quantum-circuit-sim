# Translation Rules — Frontend and Backend Notes

This document describes how each frontend parses its source language into
MQ IR, and how each backend emits MQ IR as its target language.

The translation pipeline for every supported path is:

    Source file
        |
        v  (frontend)
      MQ IR
        |
        v  (backend)
    Output file

---

## 1. MQL Frontend

**Source:** `.mql` files
**Implementation:** `source/translate/mql.c`
**Entry point:** `mql_parse_program(MQL_Parser *p)`

### Parsing Strategy

MQL uses a hand-written recursive-descent lexer + parser (no tree-sitter).
The lexer produces a stream of `MQL_Token` objects with line/column tracking.
The parser directly builds `MQ_Program / MQ_Circuit / MQ_Stmt` nodes into the arena.

### Key Translation Rules

| MQL Construct | IR Representation |
|---------------|-------------------|
| `circuit Foo { ... }` | `MQ_Circuit` named "Foo" |
| `gate MyGate(q) { ... }` | `MQ_Routine` of kind Gate |
| `operation MyOp(q) { ... }` | `MQ_Routine` of kind Operation |
| `qubit q[4]` | `MQ_Register` (Quantum, size=4) + 4 flat IDs |
| `qubit q` | `MQ_Register` (Quantum, size=1) |
| `bit c[4]` | `MQ_Register` (Classical, size=4) |
| `H q[0]` | `MQ_Stmt_Instr(Gate_H, qubit=base+0)` |
| `H q[0..3]` | 4x `MQ_Stmt_Instr(Gate_H, ...)` expanded inline |
| `H q` | N x `MQ_Stmt_Instr(Gate_H, ...)` for every qubit in register |
| `CNOT q[0] -> q[1]` | `MQ_Stmt_Instr(Gate_CX, qubits=[0,1])` |
| `ctrl[q[0]] Z q[1]` | `MQ_Stmt_Instr(Gate_Z, controls=[0], control_states=[1])` |
| `ctrl0[q[0]] X q[1]` | `MQ_Stmt_Instr(Gate_X, controls=[0], control_states=[0])` |
| `measure q -> c[2]` | 2 x `MQ_Stmt_Instr(Measure, qubit=i, clbit=i)` |
| `measure q[0] -> c[0]` | `MQ_Stmt_Instr(Measure, qubit=0, clbit=0)` |
| `X q if c[1] == 1` | `MQ_Stmt_Instr` with `has_classical_cond=true, classical_bit=1, classical_val=1` |
| `if (cond) { }` | `MQ_Stmt_If` |
| `for i in 0..3 { }` | `MQ_Stmt_For` |
| `while (cond) { }` | `MQ_Stmt_While` |
| `adjoint { ... }` | `MQ_Stmt_Adjoint` |
| `let x = expr` | `MQ_Stmt_DeclClassical` |
| `set x = expr` | `MQ_Stmt_Set` |
| `barrier q[0], q[1]` | `MQ_Stmt_Instr(Barrier)` |
| `reset q[0]` | `MQ_Stmt_Instr(Reset)` |
| `delay 100ns q[0]` | `MQ_Stmt_Instr(Delay, duration=100, unit=ns)` |

### Algorithmic Macros

MQL macros are expanded at parse time into concrete gate sequences.
No macro nodes exist in the IR.

| Macro | Expansion |
|-------|-----------|
| `grover_oracle(q, "11")` | Phase oracle marking bitstring "11": X gates on unmarked qubits + MCZ + X gates reversed |
| `grover_diffuser(q)` | H⊗n · X⊗n · MCZ · X⊗n · H⊗n on register q |
| `bell_pair(q0, q1)` | H q0; CNOT q0 -> q1 |
| `qft(q)` | QFT with phase-ladder CP gates and bit-reversal SWAPs |
| `iqft(q)` | Inverse QFT (adjoint of QFT) |
| `teleport(msg, alice, bob)` | Bell pair + Bell-basis measurement + classical corrections |

### Register Auto-Declaration

If `measure q -> c[2]` is used without a prior `bit c[2]` declaration,
the parser automatically inserts a classical register of the required size.

---

## 2. MQL Backend (Emitter)

**Target:** `.mql` files
**Implementation:** `mql_emit()` in `source/translate/mql.c`
**Purpose:** Round-trip MQ IR back to MQL source (useful for IR inspection).

The emitter walks the `MQ_Program` tree and reconstructs valid MQL syntax.
Gate instructions are named using the canonical MQL gate table.

---

## 3. Qiskit Frontend

**Source:** Qiskit Python `.py` files
**Implementation:** `source/translate/qiskit.c`
**Entry points:** `qiskit_parse_file()` then `qiskit_tree_to_ir()`

### Parsing Strategy

Uses **tree-sitter-python** to parse the `.py` file into a concrete syntax tree (CST).
The IR lowering pass then walks the CST and recognizes Qiskit-specific patterns.

### Key Recognition Patterns

| Qiskit Pattern | IR Representation |
|----------------|-------------------|
| `qc = QuantumCircuit(n)` | `MQ_Circuit` with `n` flat qubits |
| `qc = QuantumCircuit(qr, cr)` | `MQ_Circuit` with named quantum+classical registers |
| `qc.h(q[0])` | `MQ_Stmt_Instr(Gate_H, qubit=0)` |
| `qc.cx(q[0], q[1])` | `MQ_Stmt_Instr(Gate_CX, qubits=[0,1])` |
| `qc.rx(theta, q[0])` | `MQ_Stmt_Instr(Gate_RX, qubit=0, param=theta)` |
| `qc.measure(q, c)` | Measure-all into classical register |
| `qc.measure(q[0], c[0])` | `MQ_Stmt_Instr(Measure, qubit=0, clbit=0)` |
| `qc.barrier()` | `MQ_Stmt_Instr(Barrier)` |
| `qc.reset(q[0])` | `MQ_Stmt_Instr(Reset)` |
| `gate.control(n)` | Sets `control_count` on the instruction |
| `.c_if(cr, val)` | Sets `has_classical_cond`, `classical_bit`, `classical_val` |
| `Parameter("theta")` | `MQ_Expr_Symbol` |

---

## 4. Qiskit Backend

**Target:** Qiskit Python `.py` files
**Implementation:** `mq_ir_to_code()` in `source/translate/qiskit.c`

### Key Emission Rules

| IR Node | Qiskit Output |
|---------|---------------|
| `MQ_Register (Quantum, name="q", size=n)` | `q = QuantumRegister(n, 'q')` |
| `MQ_Register (Classical, name="c", size=n)` | `c = ClassicalRegister(n, 'c')` |
| `MQ_Circuit` | `qc = QuantumCircuit(q, c)` |
| `Gate_H` | `qc.h(q[i])` |
| `Gate_CX` | `qc.cx(q[ctrl], q[tgt])` |
| `Gate_CCX` | `qc.ccx(q[c0], q[c1], q[tgt])` |
| `Gate_RX(theta)` concrete | `qc.rx(3.14159, q[i])` |
| `Gate_RX(theta)` symbolic | `qc.rx(theta, q[i])` |
| `Gate_P(theta)` | `qc.p(theta, q[i])` |
| `Gate_U(t,p,l)` | `qc.u(t, p, l, q[i])` |
| `Gate_SWAP` | `qc.swap(q[i], q[j])` |
| `Gate_CSWAP` | `qc.cswap(q[c], q[i], q[j])` |
| `Measure(qubit, clbit)` | `qc.measure(q[i], c[j])` |
| `Reset` | `qc.reset(q[i])` |
| `Barrier` | `qc.barrier()` |
| With controls | `.control(n).on(...)` or explicit `ccx`/`cx` |
| With `has_classical_cond` | `.c_if(c, val)` |
| `MQ_Expr_Symbol("theta")` | `from qiskit.circuit import Parameter; theta = Parameter('theta')` |
| `math.pi` variants | `numpy.pi` |

---

## 5. Cirq Frontend

**Source:** Cirq Python `.py` files
**Implementation:** `source/translate/cirq.c`
**Entry points:** `cirq_parse_file()` then `cirq_tree_to_ir()`

### Parsing Strategy

Uses **tree-sitter-python** to produce a CST, then walks it recognizing
Cirq-specific call patterns.

### Key Recognition Patterns

| Cirq Pattern | IR Representation |
|--------------|-------------------|
| `q = cirq.NamedQubit('q')` | `MQ_Register (Quantum, size=1)` |
| `q = [cirq.NamedQubit(f'q_{i}') for i in range(n)]` | `MQ_Register (Quantum, size=n)` |
| `cirq.LineQubit.range(n)` | `MQ_Register (Quantum, size=n, style=Line)` |
| `circuit = cirq.Circuit()` | `MQ_Circuit` |
| `circuit.append(cirq.H(q))` | `MQ_Stmt_Instr(Gate_H, qubit)` |
| `circuit.append(cirq.CNOT(q[0], q[1]))` | `MQ_Stmt_Instr(Gate_CX, qubits=[0,1])` |
| `circuit.append(cirq.rx(theta)(q))` | `MQ_Stmt_Instr(Gate_RX, param=theta)` |
| `circuit.append(cirq.measure(q, key='c0'))` | `MQ_Stmt_Instr(Measure, has_target=true)` |
| `circuit.append(cirq.measure(q))` | `MQ_Stmt_Instr(Measure, has_target=false)` |
| `.controlled_by(ctrl_q)` | Sets `control_count` on instruction |
| `.with_classical_controls('c0')` | Sets `has_classical_cond` |

### Gate Name Mapping (Cirq -> IR)
`H X Y Z S T CNOT/CX -> CX  CZ -> CZ  CCZ  CCX  SWAP  rx/Rx -> RX  ry/Ry -> RY  rz/Rz -> RZ`

---

## 6. Cirq Backend

**Target:** Cirq Python `.py` files
**Implementation:** `cirq_emit()` in `source/translate/cirq.c`

### Key Emission Rules

| IR Node | Cirq Output |
|---------|-------------|
| `MQ_Register (Quantum, size=1)` | `q = cirq.NamedQubit('q')` |
| `MQ_Register (Quantum, size=n)` | `q = [cirq.NamedQubit(f'q_{i}') for i in range(n)]` |
| `MQ_Circuit` | `def build_Name(): ... circuit = cirq.Circuit(); return circuit` |
| `Gate_H` | `circuit.append(cirq.H(q[i]))` |
| `Gate_CX` | `circuit.append(cirq.CNOT(q[ctrl], q[tgt]))` |
| `Gate_CCX` | `circuit.append(cirq.CCX(q[c0], q[c1], q[tgt]))` |
| `Gate_P(theta)` concrete | `circuit.append(cirq.ZPowGate(exponent=v/pi)(q))` |
| `Gate_P(theta)` symbolic | `circuit.append(cirq.ZPowGate(exponent=(theta)/math.pi)(q))` |
| `Gate_RX` | `circuit.append(cirq.rx(theta)(q))` |
| `Gate_RY` | `circuit.append(cirq.ry(theta)(q))` |
| `Gate_RZ` | `circuit.append(cirq.rz(theta)(q))` |
| `Gate_SWAP` | `circuit.append(cirq.SWAP(q[i], q[j]))` |
| `Gate_ISWAP` | `circuit.append(cirq.ISWAP(q[i], q[j]))` |
| `Measure(qubit, clbit=k)` | `circuit.append(cirq.measure(q, key='ck'))` |
| `Measure(qubit, discard)` | `circuit.append(cirq.measure(q))` |
| With controls | `.controlled_by(ctrl_q, ...)` |
| With `has_classical_cond` | `.with_classical_controls('ck')` |
| `MQ_Expr_Symbol` | `sympy.Symbol('theta')` (emitted as bare name) |
| pi variants | Normalized to `math.pi` |
| `MQ_Stmt_Comment` | Python `# comment` |

---

## 7. Q# Frontend

**Source:** Q# `.qs` files
**Implementation:** `source/translate/qsharp.c`
**Entry points:** `qs_parse_file()` then `qsharp_tree_to_ir()`

### Parsing Strategy

Uses **tree-sitter** with a Q# grammar to parse `.qs` files into a CST.
IR lowering walks the CST and recognizes Q#-specific patterns.

### Key Recognition Patterns

| Q# Pattern | IR Representation |
|------------|-------------------|
| `operation Foo(q : Qubit) : Unit` | `MQ_Routine` (Operation kind) |
| `function Bar(theta : Double) : ...` | `MQ_Routine` (Gate kind) |
| `use q = Qubit()` | `MQ_Stmt_DeclQubit (count=1)` |
| `use q = Qubit[n]()` | `MQ_Stmt_DeclQubit (count=n)` |
| `H(q)` | `MQ_Stmt_Instr(Gate_H)` |
| `CNOT(ctrl, tgt)` | `MQ_Stmt_Instr(Gate_CX)` |
| `Rx(theta, q)` | `MQ_Stmt_Instr(Gate_RX, param=theta)` |
| `M(q)` | `MQ_Stmt_Instr(Measure)` |
| `Reset(q)` | `MQ_Stmt_Instr(Reset)` |
| `Adjoint H(q)` | `MQ_Stmt_Adjoint { body = Instr(Gate_H) }` |
| `Controlled X([ctrl], tgt)` | Instr with control_count=1, control_states=[1] |
| `if (cond) { }` | `MQ_Stmt_If` |
| `for i in 0..n-1 { }` | `MQ_Stmt_For` |
| `Message("...")` | `MQ_Stmt_Comment` (no portable equivalent) |
| `DumpMachine()` | `MQ_Stmt_Comment` |

---

## 8. Q# Backend

**Target:** Q# `.qs` files
**Implementation:** `qsharp_emit()` in `source/translate/qsharp.c`

### Key Emission Rules

| IR Node | Q# Output |
|---------|-----------|
| `MQ_Routine (Gate)` | `function Name(params) : ...` |
| `MQ_Routine (Operation)` | `operation Name(params) : Unit { ... }` |
| `MQ_Circuit` with qubit registers | `operation Name() : Unit { use q = Qubit[n](); ... }` |
| `Gate_H` | `H(q[i]);` |
| `Gate_CX` | `CNOT(q[ctrl], q[tgt]);` |
| `Gate_CCX` | `CCNOT(q[c0], q[c1], q[tgt]);` |
| `Gate_RX(theta)` | `Rx(theta, q[i]);` |
| `Gate_RY(theta)` | `Ry(theta, q[i]);` |
| `Gate_RZ(theta)` | `Rz(theta, q[i]);` |
| `Gate_P(theta)` | `R1(theta, q[i]);` |
| `Gate_U(t,p,l)` | `U3(t, p, l, q[i]);` |
| `Gate_S` | `S(q[i]);` |
| `Gate_Sdg` | `Adjoint S(q[i]);` |
| `Gate_T` | `T(q[i]);` |
| `Gate_Tdg` | `Adjoint T(q[i]);` |
| `Gate_SWAP` | `SWAP(q[i], q[j]);` |
| `Measure(qubit, clbit)` | `let c_k = M(q[i]);` |
| `Reset` | `Reset(q[i]);` |
| `MQ_Stmt_Adjoint { body }` | `within { ... } apply { ... }` or `Adjoint Op(...)` |
| With controls | `Controlled Gate([ctrl], tgt)` |
| `MQ_Stmt_If` | `if (cond) { ... } elif (...) { ... } else { ... }` |
| `MQ_Stmt_For` | `for i in start..end { ... }` |
| `MQ_Stmt_Comment` | `// comment` |
| `PI()` constant | Replaced with `PI()` Q# built-in |
| `math.pi` from IR | `PI()` |

---

## 9. Cross-Compilation Path Matrix

All paths go through the MQ IR. For any `A -> B` translation:
1. Run frontend A to get `MQ_Program`
2. Run backend B on `MQ_Program`

| From \\ To | MQL | Qiskit | Cirq | Q# | MQ IR |
|------------|-----|--------|------|----|-------|
| **MQL** | (round-trip) | Yes | Yes | Yes | Yes |
| **Qiskit** | Yes | (round-trip) | Yes | Yes | Yes |
| **Cirq** | Yes | Yes | (round-trip) | Yes | Yes |
| **Q#** | Yes | Yes | Yes | (round-trip) | Yes |

---

## 10. Gate Name Mapping Reference

| MQ Gate | MQL | Qiskit | Cirq | Q# |
|---------|-----|--------|------|----|
| Gate_I | `I` | `qc.id()` | `cirq.I` | `I` |
| Gate_H | `H` | `qc.h()` | `cirq.H` | `H` |
| Gate_X | `X` | `qc.x()` | `cirq.X` | `X` |
| Gate_Y | `Y` | `qc.y()` | `cirq.Y` | `Y` |
| Gate_Z | `Z` | `qc.z()` | `cirq.Z` | `Z` |
| Gate_S | `S` | `qc.s()` | `cirq.S` | `S` |
| Gate_Sdg | `Sdg` | `qc.sdg()` | `cirq.ZPowGate(exp=-0.5)` | `Adjoint S` |
| Gate_T | `T` | `qc.t()` | `cirq.T` | `T` |
| Gate_Tdg | `Tdg` | `qc.tdg()` | `cirq.ZPowGate(exp=-0.25)` | `Adjoint T` |
| Gate_P(t) | `P(t)` | `qc.p(t)` | `cirq.ZPowGate(exp=t/pi)` | `R1(t)` |
| Gate_RX(t) | `RX(t)` | `qc.rx(t)` | `cirq.rx(t)` | `Rx(t)` |
| Gate_RY(t) | `RY(t)` | `qc.ry(t)` | `cirq.ry(t)` | `Ry(t)` |
| Gate_RZ(t) | `RZ(t)` | `qc.rz(t)` | `cirq.rz(t)` | `Rz(t)` |
| Gate_U(t,p,l) | `U(t,p,l)` | `qc.u(t,p,l)` | `cirq.MatrixGate(...)` | `U3(t,p,l)` |
| Gate_CX | `CNOT a->b` | `qc.cx(a,b)` | `cirq.CNOT(a,b)` | `CNOT(a,b)` |
| Gate_SWAP | `SWAP a,b` | `qc.swap(a,b)` | `cirq.SWAP(a,b)` | `SWAP(a,b)` |
| Gate_ISWAP | `ISWAP a,b` | `qc.iswap(a,b)` | `cirq.ISWAP(a,b)` | `ISWAP(a,b)` |
| Gate_CCX | `Toffoli a,b,c` | `qc.ccx(a,b,c)` | `cirq.CCX(a,b,c)` | `CCNOT(a,b,c)` |
| Gate_CSWAP | `Fredkin a,b,c` | `qc.cswap(a,b,c)` | `cirq.CSWAP(a,b,c)` | `CSWAP(a,b,c)` |

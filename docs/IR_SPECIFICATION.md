# MQ Intermediate Representation (MQ IR) - Specification v0.3

The MQ IR is the single, language-agnostic representation that all frontends
compile *into* and all backends compile *from*.

Source: `source/translate/ir.h` and `source/translate/ir.c`

---

## 1. Design Goals

- **Language-agnostic** - faithfully represents programs from Qiskit, Cirq, Q#, and MQL.
- **Lossless round-tripping** - backend metadata preserves enough context for idiomatic output.
- **Arena-allocated** - all IR nodes live in one `M_Arena`. One `arena_free()` call frees everything.
- **Flat qubit IDs** - every qubit has a monotonically increasing `u32` ID. Register metadata is stored
  separately and used only by emitters.

---

## 2. IR Version

    MQ_IR_VERSION_MAJOR = 0
    MQ_IR_VERSION_MINOR = 3
    MQ_IR_VERSION_PATCH = 0

Stored in `MQ_Program.mq_ir_version[3]`.

---

## 3. Node Hierarchy

    MQ_Program
      ├── MQ_Routine[]          (reusable gate/operation definitions)
      └── MQ_Circuit[]          (entry-point circuits)
            ├── MQ_Register[]   (quantum and classical register declarations)
            └── MQ_Stmt*        (circuit body — recursive tree)
                  ├── MQ_Stmt_Block
                  ├── MQ_Stmt_Instr  --->  MQ_Instruction
                  ├── MQ_Stmt_If / For / While ...
                  └── MQ_Stmt_Adjoint

---

## 4. Types — MQ_Type

| Kind | Notes |
|------|-------|
| Void | No value |
| Bool | 1-bit classical |
| Int | Classical integer; `width` = bit-width (e.g. 64) |
| Float | 64-bit double |
| Angle | Rotation angle in radians; semantic subtype of Float |
| Bit | Single classical bit |
| Qubit | Single qubit handle |
| BitReg | Classical register; `width` = number of bits |
| QubitReg | Quantum register; `width` = number of qubits |
| Array | Homogeneous array; `width` = element count, `element_type` = inner type |

---

## 5. Expressions — MQ_Expr

MQ_Expr nodes appear wherever a classical value is needed: gate rotation angles,
if/while conditions, loop bounds, and call arguments.

| Kind | Description | Key Fields |
|------|-------------|------------|
| BoolLit | Boolean literal | `.lit.bool_val` |
| IntLit | Integer literal | `.lit.int_val` |
| FloatLit | Float literal | `.lit.float_val` |
| Symbol | Unbound symbolic parameter | `.name` |
| Var | Classical variable reference | `.name` |
| QubitRef | Qubit reference by flat ID | `.qubit_id` |
| RegIndex | Register element `reg[i]` | `.reg.{name, index_expr}` |
| BinOp | Binary operation | `.bin.{op, lhs, rhs}` |
| UnOp | Unary operation | `.un.{op, operand}` |
| Call | Function call expression | `.call.{name, args, arg_count}` |
| Array | Array literal | `.call.{args, arg_count}` |
| BitRead | Read classical bit (mid-circuit feedback) | `.bit.{reg_name, index}` |

### Symbolic parameters
`MQ_Expr_Symbol` maps to:
- Qiskit: `qiskit.circuit.Parameter`
- Cirq: `sympy.Symbol`
- Q#: unbound `Double` / `Angle` function argument

### Binary Operators (MQ_BinOp)
`Add  Sub  Mul  Div  Mod  Pow  And  Or  Xor  Shl  Shr  Eq  Ne  Lt  Le  Gt  Ge  LogAnd  LogOr`

### Unary Operators (MQ_UnOp)
`Neg  Not  BitNot  Sin  Cos  Tan  Asin  Acos  Atan  Sqrt  Exp  Log  Abs`

---

## 6. Qubit Addressing — MQ_QubitMeta

The canonical qubit address inside the IR is a flat `u32 qubit_id`.
`MQ_QubitMeta` is an optional per-qubit record preserving framework-specific addressing.

| Style | Usage |
|-------|-------|
| Flat | Default; plain sequential index |
| Named | Q# named qubit variable — `.name` field |
| Line | Cirq `LineQubit(x)` — `.line_x` field |
| Grid | Cirq `GridQubit(row, col)` — `.grid_row`, `.grid_col` fields |

---

## 7. Gate Types — MQ_GateType

Only gates natively present in **all three frameworks** are enumerated.
Framework-specific gates use `MQ_Gate_Custom`.

### Single-qubit, no parameters
`I   H   X   Y   Z   S   Sdg   T   Tdg`

### Single-qubit, parametric (angles in radians)
| Gate | Parameters |
|------|------------|
| P | theta (phase angle) |
| RX | theta |
| RY | theta |
| RZ | theta |
| U | theta, phi, lambda |

### Two-qubit
`CX   SWAP   ISWAP   RZZ   RXX   RYY`

### Three-qubit
`CCX` (Toffoli gate),  `CSWAP` (Fredkin gate)

### Escape hatches
- `MQ_Gate_Custom` — user-defined gate; name in `.gate.custom_name`
- `MQ_Gate_Unitary` — arbitrary unitary matrix in `.gate.unitary_matrix` (2^n x 2^n, row-major)

---

## 8. Instructions — MQ_Instruction

The lowest-level IR node. Encodes one quantum primitive.

    type         : MQ_InstrType      // Gate | Measure | Reset | Barrier | Delay
    qubits[]     : u32               // flat qubit IDs (MQ_MAX_GATE_QUBITS = 3)
    qubit_count  : u8
    source_line  : u32
    source_col   : u32

    // Gate instruction fields:
    gate
      .gate                MQ_GateType
      .controls[]          u32    coherent control qubits (max MQ_MAX_CONTROLS = 8)
      .control_states[]    u8     0 = control on |0>, 1 = control on |1>
      .control_count       u8
      .classical_bit       u32    hardware-level c_if bit index
      .classical_val       u32    hardware-level c_if expected value
      .has_classical_cond  bool
      .param_count         u8
      .params_symbolic     bool
          false -> .params[]       (concrete f64 values)
          true  -> .param_exprs[]  (MQ_Expr* symbolic expressions)
      .custom_name         string  (when gate == Custom)
      .unitary_matrix      f64*   (when gate == Unitary, arena-allocated)

    // Measure instruction fields:
    measure
      .clbit       u32
      .has_target  bool   false = discard result; true = store in clbit

    // Delay instruction fields:
    delay
      .duration  f64
      .unit      MQ_TimeUnit   // Dt | ns | us | ms | s

### Controlled Gates
Any gate can be coherently controlled via `controls[]` + `control_states[]`.
This uniformly represents CX (1 control, on |1>), CCX (2 controls, on |1>|1>),
and arbitrary multi-controlled unitaries with mixed control polarities.

### Classical Conditioning
Hardware-level `if (cbit == val)` is encoded with `has_classical_cond + classical_bit + classical_val`.
This corresponds to Qiskit's `.c_if()` and OpenQASM 2 conditional gates.

For high-level feedback (mid-circuit measurement + classical branch), use
`MQ_Stmt_If` wrapping a `MQ_Expr_BitRead` condition instead.

---

## 9. Registers — MQ_Register

    kind        : MQ_RegKind     // Quantum | Classical
    name        : string
    size        : u32            // number of qubits or bits
    base_id     : u32            // flat id of the first element
    qubit_meta  : MQ_QubitMeta*  // NULL for classical registers

Flat ID of element `i` in register `r` is always `r.base_id + i`.

---

## 10. Statements — MQ_Stmt

| Kind | Description |
|------|-------------|
| Block | Ordered sequence of child statements |
| Instr | Wraps one MQ_Instruction |
| Adjoint | Circuit inversion (cirq.inverse / Q# Adjoint) |
| DeclQubit | Dynamic qubit allocation (Q# `use`, OpenQASM 3) |
| DeclClassical | Local classical variable declaration with optional initializer |
| DeclParam | Symbolic circuit parameter declaration |
| Set | Classical assignment; `augmented = true` for `+=`, `-=`, etc. |
| If | If/elif/else chain; branch with `cond == NULL` is the else branch |
| For | For loop over an iterable expression |
| While | While loop |
| Break / Continue | Loop control statements |
| Return | Return from routine; `return_val == NULL` for void |
| Call | Void subroutine call |
| Comment | Preserved source comment or backend diagnostic |
| Pragma | Key-value pragma annotation |

---

## 11. Routines — MQ_Routine

Named, reusable gate or operation definitions.

| Field | Description |
|-------|-------------|
| name | Routine identifier |
| kind | Gate (purely unitary, no measurement/reset) or Operation |
| params[] | Formal parameter list (name, type, optional default) |
| return_type | NULL = void |
| body | Statement tree (NULL if is_intrinsic) |
| is_intrinsic | When true, intrinsic_name maps to a framework primitive |
| intrinsic_name | E.g. "Message" (Q#), "print" (Python) |
| doc_comment | Preserved documentation string from source |

---

## 12. Circuit — MQ_Circuit

    name            string
    registers[]     MQ_Register[]     quantum + classical registers
    total_qubits    u32
    total_cbits     u32
    param_names[]   string[]          symbolic parameter names
    param_defaults[]MQ_Expr*[]        NULL = fully unbound parameter
    body            MQ_Stmt*          circuit body tree
    measure_map[]   i32[]             qubit_id -> cbit_id; -1 = unmeasured
    global_phase    f64               radians; 0.0 = not set
    backend_hint    MQ_BackendHint*   NULL if absent

### Backend Hints — MQ_BackendHint

Stores hardware metadata for accurate round-tripping:

| Field | Description |
|-------|-------------|
| backend_name | Hardware backend identifier, e.g. "ibmq_quito" |
| coupling_map | Flat (ctrl, tgt) u32 pairs; length = coupling_map_len * 2 |
| basis_gates | Comma-separated native gate names |
| dt | Timing resolution in seconds; 0.0 = unavailable |

---

## 13. Program — MQ_Program

The top-level compilation unit.

    name           string
    routines[]     MQ_Routine*[]
    circuits[]     MQ_Circuit*[]
    entry_circuit  u32             index of entry point circuit; default 0
    source_lang    MQ_SourceLang   Qiskit | QSharp | Cirq | MQ | ...
    source_file    string          empty if absent
    mq_ir_version  u32[3]          major, minor, patch

### Source Languages (MQ_SourceLang)
`Unknown  Qiskit  QSharp  Cirq  OpenQASM2  OpenQASM3  MQ`

---

## 14. Memory Model

All IR nodes are allocated from a single M_Arena passed through every construction helper.

    M_Arena arena = {0};
    arena_init(&arena);

    MQ_Program *prog = mq_program(&arena, str_lit("my_circuit"), MQ_Lang_MQ);
    // ... build the IR tree ...
    cirq_emit(stdout, prog);

    arena_free(&arena);   // frees all IR nodes in one call

String slices (`string = {u8*, u64}`) are non-owning views into arena memory.
A zero-initialized `string` (`{0}`) represents an absent/null name.

---

## 15. Construction Helpers — Quick Reference

All declared in `ir.h`, implemented in `ir.c`.

    // --- Expressions ---
    mq_expr_bool(arena, val)
    mq_expr_int(arena, 42)
    mq_expr_float(arena, 3.14159)
    mq_expr_symbol(arena, str_lit("theta"))
    mq_expr_var(arena, str_lit("x"))
    mq_expr_qubit_ref(arena, qubit_id)
    mq_expr_reg_index(arena, str_lit("q"), index_expr)
    mq_expr_binop(arena, MQ_BinOp_Div, lhs, rhs)
    mq_expr_unop(arena, MQ_UnOp_Sin, operand)
    mq_expr_call(arena, str_lit("foo"), args, arg_count)
    mq_expr_bit_read(arena, str_lit("c"), 0)

    // --- Instructions ---
    mq_instr_gate(MQ_Gate_H, qubits, 1)
    mq_instr_gate_p(MQ_Gate_RX, qubits, 1, params, 1)
    mq_instr_gate_sym(MQ_Gate_RZ, qubits, 1, param_exprs, 1)
    mq_instr_gate_custom(name, qubits, qubit_count, param_exprs, param_count)
    mq_instr_measure(qubit_id, cbit_id)
    mq_instr_measure_discard(qubit_id)
    mq_instr_reset(qubit_id)
    mq_instr_barrier(qubits, qubit_count)
    mq_instr_delay(qubits, qubit_count, duration, unit)
    mq_instr_add_control(&instr, ctrl_qubit, control_state)
    mq_instr_set_classical_cond(&instr, cbit, val)

    // --- Statements ---
    mq_stmt_block(arena, stmts, count)
    mq_stmt_instr(arena, instr)
    mq_stmt_adjoint(arena, body)
    mq_stmt_decl_qubit(arena, name, count, base_id)
    mq_stmt_decl_classical(arena, name, type, init)
    mq_stmt_decl_param(arena, name, default_val)
    mq_stmt_set(arena, lhs, rhs)
    mq_stmt_set_aug(arena, lhs, op, rhs)
    mq_stmt_if(arena, branches, count)
    mq_stmt_for(arena, var_name, var_type, iterable, body)
    mq_stmt_while(arena, cond, body)
    mq_stmt_break(arena)
    mq_stmt_continue(arena)
    mq_stmt_return(arena, val)
    mq_stmt_call(arena, callee, args, arg_count)
    mq_stmt_comment(arena, text)
    mq_stmt_pragma(arena, key, value)

    // --- Registers ---
    mq_register_quantum(name, size, base_id, meta)
    mq_register_classical(name, size, base_id)

    // --- Circuit & Program ---
    mq_circuit(arena, name, registers, reg_count, total_qubits, total_cbits)
    mq_program(arena, name, source_lang)
    mq_program_add_circuit(arena, prog, circuit)
    mq_program_add_routine(arena, prog, routine)
    mq_program_write(file, prog)       // dump IR as text

---

## 16. Useful Macros

    MQ_INSTR_IS_PARAMETRIC(i)        // true if gate has symbolic params
    MQ_INSTR_IS_CONTROLLED(i)        // true if gate has control qubits
    MQ_INSTR_IS_CLASSICALLY_COND(i)  // true if gate has hardware c_if

    MQ_GATE_IS_SELF_ADJOINT(g)       // true for I, H, X, Y, Z, SWAP, CCX, CSWAP

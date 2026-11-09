# MetaQuantum (MQ) - Cross-Framework Quantum Compiler and Circuit Designer

MetaQuantum is a modular, cross-compilation toolchain for quantum programs.
It exposes a unified, framework-agnostic **Intermediate Representation (MQ IR)**
that sits between multiple quantum programming frontends and backends.
Programs written in any supported language can be translated into any other
with a single command.

```
                    +---------------------------+
  MQL ------------> |                           | -----------> Qiskit (.py)
  Qiskit (.py) ---> |        MQ IR  v0.3        | -----------> Cirq   (.py)
  Cirq   (.py) ---> |   (language-agnostic IR)  | -----------> Q#     (.qs)
  Q#     (.qs) ---> |                           | -----------> MQ IR dump
                    +---------------------------+
```

---

## Supported Frameworks

| Framework                | Frontend (→ IR) | Backend (IR →) |
|--------------------------|:---------------:|:--------------:|
| MQL (MetaQuantum Language) | Yes           | Yes            |
| Qiskit (Python)          | Yes             | Yes            |
| Cirq (Python)            | Yes             | Yes            |
| Q# (.qs)                 | Yes             | Yes            |

---

## Repository Layout

```
quantum-circuit-sim-master/
├── source/
│   ├── main.c                     # CLI entry point and argument parsing
│   ├── edit.c / edit.h            # Drag-and-drop GUI circuit editor
│   ├── quantum.c / quantum.h      # State-vector simulator
│   ├── base/                      # Memory, strings, math, data structures
│   ├── client/                    # OpenGL windowing and immediate-mode UI
│   ├── os/                        # Platform abstraction
│   └── translate/
│       ├── ir.h / ir.c            # MQ IR - the universal representation
│       ├── mql.h / mql.c          # MQL frontend + emitter
│       ├── qiskit.h / qiskit.c    # Qiskit frontend + backend
│       ├── cirq.h / cirq.c        # Cirq frontend + backend
│       └── qsharp.h / qsharp.c   # Q# frontend + backend
├── tests/
│   ├── MQL/                       # .mql test programs
│   ├── Qiskit/                    # Reference Qiskit Python programs
│   ├── Cirq/                      # Reference Cirq Python programs
│   └── Q#/                        # Reference Q# programs
├── docs/
│   ├── MQL_SYNTAX_GUIDE.md        # MQL language reference
│   ├── IR_SPECIFICATION.md        # MQ IR internals and data model
│   └── TRANSLATION_RULES.md      # Per-frontend/backend translation notes
└── assets/                        # Fonts, textures, shaders for the GUI
```

---

## Building

```bat
build.bat
```

Links against: **GLFW + GLAD** (OpenGL windowing), **tree-sitter + tree-sitter-python** (Python parsing).
Output executable is placed in `bin/`.

---

## CLI Usage

```bash
# Inspect MQ IR of an MQL program
composer program.mql

# Transpile MQL -> Qiskit
composer program.mql --to qiskit -o out.py

# Transpile MQL -> Cirq
composer program.mql --to cirq -o out.py

# Transpile MQL -> Q#
composer program.mql --to qsharp -o out.qs

# Transpile Qiskit -> Cirq
composer --from qiskit bell.py --to cirq -o bell_cirq.py

# Transpile Cirq -> Q#
composer --from cirq sim.py --to qsharp -o sim.qs

# Transpile Q# -> Qiskit
composer program.qs --to qiskit -o out.py

# Open the drag-and-drop GUI circuit editor
composer --editor
```

### CLI Flags

| Flag | Description |
|------|-------------|
| `--from <fmt>` | Force input format: `mql`, `qiskit`, `cirq`, `qsharp` |
| `--to <fmt>` | Force output format: `mql`, `qiskit`, `cirq`, `qsharp`, `mq` |
| `-o <file>` | Write output to file (defaults to stdout) |
| `--editor` | Launch the interactive GUI circuit editor |

> **Note:** `.py` is ambiguous — both Qiskit and Cirq use Python.
> Always use `--from qiskit` or `--from cirq` for Python input files.

---

## GUI Editor

Launch with `composer --editor`.

| Action | How |
|--------|-----|
| Pick a gate | Drag from the gate palette in the top bar |
| Place a gate | Drop onto a qubit wire |
| Add a qubit | Click `+` (top-right) |
| Remove a qubit | Click `_` (top-right) |
| Controlled gate | Drop `ControlOn` or `ControlOff` on the same time-slice as the target |
| Insert time-slice | Hold **Shift** while dropping a gate onto an occupied cell |
| Save as MQL | **Ctrl+S** — writes `circuit.mql` to the working directory |
| Simulate and print | **Ctrl+P** — prints the full state-vector to the console |

**Available gates in the editor palette:**
H, X, Y, Z, S, T, ControlOn (positive control), ControlOff (negative/zero control),
Probability Inspector, Bloch Sphere Inspector, Amplitude Inspector.

---

## Quick Example

**Bell State in MQL:**
```
circuit BellState {
    qubit q[2];

    H q[0];
    CNOT q[0] -> q[1];
    measure q -> c[2];
}
```

**Transpile to all targets:**
```bash
composer bell.mql --to qiskit  -o bell_qiskit.py
composer bell.mql --to cirq    -o bell_cirq.py
composer bell.mql --to qsharp  -o bell.qs
```

---

## Documentation

| Document | Contents |
|----------|----------|
| [MQL Syntax Guide](docs/MQL_SYNTAX_GUIDE.md) | Full language reference for MQL |
| [IR Specification](docs/IR_SPECIFICATION.md) | MQ IR data model, node types, memory model |
| [Translation Rules](docs/TRANSLATION_RULES.md) | How each frontend/backend maps to/from the IR |

---

### Privacy and Telemetry Notice
MetaQuantum operates entirely as a local, offline compilation and simulation tool. 
- It does not harvest telemetry, user analytics, or hardware identifiers.
- It makes no outbound network connections.
- All quantum circuits and source files remain strictly confidential on the user's local machine.

## License

See [LICENSE](LICENSE).

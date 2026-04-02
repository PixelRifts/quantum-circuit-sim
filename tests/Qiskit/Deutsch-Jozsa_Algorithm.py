import pprint
from qiskit import QuantumCircuit, transpile
from qiskit_aer import AerSimulator

qc = QuantumCircuit(4, 3)   # 3 input qubits + 1 auxiliary

# Step 1: Initialization
qc.x(3)        # auxiliary qubit
qc.h(0)
qc.h(1)
qc.h(2)
qc.h(3)

# Step 2: Balanced Oracle
qc.cx(0, 3)
qc.cx(1, 3)
qc.cx(2, 3)

qc.barrier()

# Step 3: Final Hadamards
qc.h(0)
qc.h(1)
qc.h(2)

qc.measure(0, 0)
qc.measure(1, 1)
qc.measure(2, 2)

simulator = AerSimulator()
qc = transpile(qc, simulator)
result = simulator.run(qc, shots=16384).result()
counts = result.get_counts()
probs = {state: count / 16384 for state, count in counts.items()}
pprint.pprint(probs)
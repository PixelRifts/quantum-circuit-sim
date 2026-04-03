import pprint
from qiskit import QuantumCircuit, transpile
from qiskit_aer import AerSimulator

qc = QuantumCircuit(3, 2) # 3 qubits, 2 classical bits

# Step 1: Prepare state 
qc.h(0)

# Step 2: Bell pair
qc.h(1)
qc.cx(1, 2)

# Step 3: Bell measurement
qc.cx(0, 1)
qc.h(0)

# Step 4: Measure q0 and q1
qc.measure(0, 0)
qc.measure(1, 1)

# Step 5: Conditional corrections
qc.cx(1, 2)
qc.cz(0, 2)

qc.measure_all()
simulator = AerSimulator()
qc = transpile(qc, simulator)
result = simulator.run(qc, shots=16384).result()
counts = result.get_counts()
pprint.pprint(counts)
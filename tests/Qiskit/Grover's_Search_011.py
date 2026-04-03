import pprint
from qiskit import QuantumCircuit, transpile
from qiskit_aer import AerSimulator

qc = QuantumCircuit(3)

# equal superposition
qc.h(0)
qc.h(1)
qc.h(2)

# oracle for 011
qc.x(2)              
qc.ccz(0, 1, 2)      
qc.x(2)              

# diffuser

qc.h(0)
qc.h(1)
qc.h(2)

qc.x(0)
qc.x(1)
qc.x(2)

qc.ccz(0, 1, 2)

qc.x(0)
qc.x(1)
qc.x(2)

qc.h(0)
qc.h(1)
qc.h(2)

qc.measure_all()
simulator = AerSimulator()
qc = transpile(qc, simulator)
result = simulator.run(qc, shots=16384).result()
counts = result.get_counts()
probs = {state: count / 16384 for state, count in counts.items()}
pprint.pprint(probs)
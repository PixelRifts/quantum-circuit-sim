import pprint
from qiskit import QuantumCircuit, transpile
from qiskit_aer import AerSimulator
import math

qc = QuantumCircuit(3)

qc.h(0)
qc.cp(math.pi/2, 1, 0)   # controlled-R2
qc.cp(math.pi/4, 2, 0)   # controlled-R3

qc.h(1)
qc.cp(math.pi/2, 2, 1)   # controlled-R2

qc.h(2)
qc.swap(0, 2)

qc.measure_all()
simulator = AerSimulator()
qc = transpile(qc, simulator)
result = simulator.run(qc, shots=16384).result()
counts = result.get_counts()
probs = {state: count / 16384 for state, count in counts.items()}
pprint.pprint(probs)
import pprint
from qiskit import QuantumCircuit, transpile
from qiskit_aer import AerSimulator
from qiskit.circuit.library import XGate, YGate, ZGate, HGate, SGate, TGate

qc = QuantumCircuit(2, 2)
qc.h(0)
qc.cx(0,1)

qc.measure(0, 0)
qc.measure(1, 1)
simulator = AerSimulator()
qc = transpile(qc, simulator)
compiled_circuit = simulator.run(qc, shots=16384)
result = compiled_circuit.result()
counts = result.get_counts(qc)
probs = {state: count / 16384 for state, count in counts.items()}
pprint.pprint(probs)

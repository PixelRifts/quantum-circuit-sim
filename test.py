import pprint
from qiskit import QuantumCircuit, transpile
from qiskit_aer import AerSimulator
from qiskit.circuit.library import XGate, YGate, ZGate, HGate, SGate, TGate

qc = QuantumCircuit(3)
qc.h(0)
qc.append(SGate().control(1, ctrl_state='1'), [1, 0])
qc.append(TGate().control(1, ctrl_state='1'), [2, 0])
qc.h(1)
qc.append(SGate().control(1, ctrl_state='1'), [2, 1])
qc.h(2)

qc.measure_all()
simulator = AerSimulator()
qc = transpile(qc, simulator)
compiled_circuit = simulator.run(qc, shots=16384)
result = compiled_circuit.result()
counts = result.get_counts(qc)
probs = {state: count / 16384 for state, count in counts.items()}
pprint.pprint(probs)

from qiskit import QuantumCircuit
from qiskit_aer import AerSimulator

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)

qc.measure_all()
qc.draw()

simulator = AerSimulator()
compiled_circuit = simulator.run(qc, shots=1024)
result = compiled_circuit.result()
counts = result.get_counts(qc)
print(counts)

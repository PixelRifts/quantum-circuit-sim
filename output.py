from qiskit import QuantumCircuit

qc = QuantumCircuit(2)
qc.id(1)
qc.h(0)
qc.id(0)
qc.x(1)

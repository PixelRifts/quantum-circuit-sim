import cirq
import pprint

q0, q1, q2 = cirq.LineQubit.range(3)

circuit = cirq.Circuit()

circuit.append([cirq.H(q1), cirq.CNOT(q1, q2)])
circuit.append([cirq.CNOT(q0, q1), cirq.H(q0)])

circuit.append(cirq.measure(q0, q1, key='m'))

circuit.append([cirq.CNOT(q1, q2), cirq.CZ(q0, q2)])

circuit.append(cirq.measure(q2, key='res'))

sim = cirq.Simulator()
result = sim.run(circuit, repetitions=16384)

counts = result.histogram(key='res')
probs = {str(k): v/16384 for k, v in counts.items()}

pprint.pprint(probs)
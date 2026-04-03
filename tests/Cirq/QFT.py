import cirq
import pprint

q0, q1, q2 = cirq.LineQubit.range(3)

circuit = cirq.Circuit()

circuit.append(cirq.H(q0))
circuit.append(cirq.CZ(q1, q0)**0.5)
circuit.append(cirq.CZ(q2, q0)**0.25)

circuit.append(cirq.H(q1))
circuit.append(cirq.CZ(q2, q1)**0.5)

circuit.append(cirq.H(q2))
circuit.append(cirq.SWAP(q0, q2))

circuit.append(cirq.measure(q0, q1, q2, key='m'))

sim = cirq.Simulator()
result = sim.run(circuit, repetitions=16384)

counts = result.histogram(key='m')
probs = {format(k, '03b'): v/16384 for k, v in counts.items()}

pprint.pprint(probs)

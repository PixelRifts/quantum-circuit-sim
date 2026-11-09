import cirq
import pprint

q0, q1, q2 = cirq.LineQubit.range(3)

circuit = cirq.Circuit()

circuit.append(cirq.H(q0))
circuit.append(cirq.CNOT(q0, q1))
circuit.append(cirq.CNOT(q0, q2))

circuit.append(cirq.measure(q0, key='m0'))
circuit.append(cirq.measure(q1, key='m1'))
circuit.append(cirq.measure(q2, key='m2'))

sim = cirq.Simulator()
result = sim.run(circuit, repetitions=16384)

counts = result.histogram(key='m')
probs = {format(k, '03b'): v/16384 for k, v in counts.items()}

pprint.pprint(probs)

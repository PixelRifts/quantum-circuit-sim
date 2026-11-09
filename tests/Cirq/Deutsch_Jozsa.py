import cirq
import pprint

q0, q1, q2, q3 = cirq.LineQubit.range(4)

circuit = cirq.Circuit()

circuit.append(cirq.X(q3))

circuit.append(cirq.H(q0))
circuit.append(cirq.H(q1))
circuit.append(cirq.H(q2))
circuit.append(cirq.H(q3))

circuit.append(cirq.CNOT(q0, q3))
circuit.append(cirq.CNOT(q1, q3))
circuit.append(cirq.CNOT(q2, q3))

circuit.append(cirq.H(q0))
circuit.append(cirq.H(q1))
circuit.append(cirq.H(q2))

circuit.append(cirq.measure(q0, key='m0'))
circuit.append(cirq.measure(q1, key='m1'))
circuit.append(cirq.measure(q2, key='m2'))

sim = cirq.Simulator()
result = sim.run(circuit, repetitions=16384)

counts = result.histogram(key='m')
probs = {format(k, '02b'): v/16384 for k, v in counts.items()}

pprint.pprint(probs)

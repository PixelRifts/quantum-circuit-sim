import cirq
import pprint

q0, q1 = cirq.LineQubit.range(2)

circuit = cirq.Circuit()

circuit.append(cirq.H(q0))
circuit.append(cirq.CNOT(q0, q1))
circuit.append(cirq.X(q1))
circuit.append(cirq.Z(q0))

circuit.append(cirq.measure(q0, q1, key='m'))

sim = cirq.Simulator()
result = sim.run(circuit, repetitions=16384)

counts = result.histogram(key='m')
probs = {format(k, '02b'): v/16384 for k, v in counts.items()}

pprint.pprint(probs)

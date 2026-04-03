import cirq
import pprint

q0, q1, q2 = cirq.LineQubit.range(3)

circuit = cirq.Circuit()

# Superposition
circuit.append(cirq.H(q0))
circuit.append(cirq.H(q1))
circuit.append(cirq.H(q2))

# Oracle for 011
circuit.append(cirq.X(q0))
circuit.append(cirq.CCZ(q0, q1, q2))
circuit.append(cirq.X(q0))

# Diffusion
circuit.append(cirq.H(q0))
circuit.append(cirq.H(q1))
circuit.append(cirq.H(q2))

circuit.append(cirq.X(q0))
circuit.append(cirq.X(q1))
circuit.append(cirq.X(q2))

circuit.append(cirq.H(q2))
circuit.append(cirq.CCZ(q0, q1, q2))
circuit.append(cirq.H(q2))

circuit.append(cirq.X(q0))
circuit.append(cirq.X(q1))
circuit.append(cirq.X(q2))

circuit.append(cirq.H(q0))
circuit.append(cirq.H(q1))
circuit.append(cirq.H(q2))

circuit.append(cirq.measure(q0, q1, q2, key='m'))

sim = cirq.Simulator()
result = sim.run(circuit, repetitions=16384)

counts = result.histogram(key='m')
probs = {format(k, '03b'): v/16384 for k, v in counts.items()}

pprint.pprint(probs)

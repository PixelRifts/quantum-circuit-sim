import cirq
import pprint

q0, q1, q2 = cirq.LineQubit.range(3)

circuit = cirq.Circuit(
    cirq.H(q0),
    cirq.CNOT(q0, q1),
    cirq.CNOT(q1, q2),
    cirq.measure(q0, q1, q2, key='m')
)

sim = cirq.Simulator()
result = sim.run(circuit, repetitions=16384)

counts = result.histogram(key='m')
probs = {format(k, '03b'): v/16384 for k, v in counts.items()}

pprint.pprint(probs)
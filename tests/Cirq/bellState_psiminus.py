import cirq
import pprint

q0, q1 = cirq.LineQubit.range(2)

circuit = cirq.Circuit(
    cirq.H(q0),
    cirq.CNOT(q0, q1),
    cirq.X(q1),
    cirq.Z(q0),
    cirq.measure(q0, q1, key='m')
)

sim = cirq.Simulator()
result = sim.run(circuit, repetitions=16384)

counts = result.histogram(key='m')
probs = {format(k, '02b'): v/16384 for k, v in counts.items()}

pprint.pprint(probs)
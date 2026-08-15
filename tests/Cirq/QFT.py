import cirq
import math

def build_QFT():
    q = cirq.LineQubit.range(3)
    circuit = cirq.Circuit()

    # Qubit 0
    circuit.append(cirq.H(q[0]))
    circuit.append(cirq.CZ(q[1], q[0]) ** (1/2))
    circuit.append(cirq.CZ(q[2], q[0]) ** (1/4))

    # Qubit 1
    circuit.append(cirq.H(q[1]))
    circuit.append(cirq.CZ(q[2], q[1]) ** (1/2))

    # Qubit 2
    circuit.append(cirq.H(q[2]))

    # Bit reversal
    circuit.append(cirq.SWAP(q[0], q[2]))

    return circuit

if __name__ == '__main__':
    circuit = build_QFT()
    circuit.append(cirq.measure(*cirq.LineQubit.range(3), key='result'))
    simulator = cirq.Simulator()
    result = simulator.run(circuit, repetitions=1024)
    print(result.histogram(key='result'))

import cirq 
 
def build_Grover(): 
    q = cirq.LineQubit.range(2) 
    circuit = cirq.Circuit() 
     
    # Superposition 
    circuit.append(cirq.H(q[0]))
    circuit.append(cirq.H(q[1]))
     
    # Oracle 
    circuit.append(cirq.CZ(q[0], q[1])) 
     
    # Diffuser 
    circuit.append(cirq.H(q[0]))
    circuit.append(cirq.H(q[1]))
    circuit.append(cirq.X(q[0]))
    circuit.append(cirq.X(q[1]))
    circuit.append(cirq.CZ(q[0], q[1])) 
    circuit.append(cirq.X(q[0]))
    circuit.append(cirq.X(q[1]))
    circuit.append(cirq.H(q[0]))
    circuit.append(cirq.H(q[1]))
     
    return circuit 
 
if __name__ == '__main__': 
    circuit = build_Grover() 
    circuit.append(cirq.measure(*cirq.LineQubit.range(2), key='result')) 
    result = cirq.Simulator().run(circuit, repetitions=1024) 
    print(result.histogram(key='result')) 

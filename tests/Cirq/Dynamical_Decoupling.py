import cirq 
 
def build_DD(): 
    q = cirq.LineQubit.range(1) 
    circuit = cirq.Circuit() 
     
    # Prepare |+> 
    circuit.append(cirq.H(q[0])) 
     
    # XY-4 sequence 
    circuit.append(cirq.wait(q[0], nanos=100)) 
    circuit.append(cirq.X(q[0])) 
    circuit.append(cirq.wait(q[0], nanos=100)) 
    circuit.append(cirq.Y(q[0])) 
    circuit.append(cirq.wait(q[0], nanos=100)) 
    circuit.append(cirq.X(q[0])) 
    circuit.append(cirq.wait(q[0], nanos=100)) 
    circuit.append(cirq.Y(q[0])) 
     
    # Refocus 
    circuit.append(cirq.H(q[0])) 
     
    return circuit 
 
if __name__ == '__main__': 
    circuit = build_DD() 
    circuit.append(cirq.measure(*cirq.LineQubit.range(1), key='result')) 
    result = cirq.Simulator().run(circuit, repetitions=1024) 
    print(result.histogram(key='result'))

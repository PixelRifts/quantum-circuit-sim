import cirq 
 
def build_Teleportation(): 
    msg, anc, tgt = cirq.LineQubit.range(3) 
    circuit = cirq.Circuit() 
     
    # Prepare message in |+> 
    circuit.append(cirq.H(msg)) 
     
    # Bell pair 
    circuit.append(cirq.H(anc)) 
    circuit.append(cirq.CNOT(anc, tgt)) 
     
    circuit.append(cirq.Moment([cirq.ops.op_tree.flatten_to_ops([])]))  # barrier 
     
    # Bell measurement 
    circuit.append(cirq.CNOT(msg, anc)) 
    circuit.append(cirq.H(msg)) 
     
    # Mid-circuit measure 
    circuit.append(cirq.measure(msg, key='m0')) 
    circuit.append(cirq.measure(anc, key='m1')) 
     
    # Classical corrections 
    circuit.append(cirq.X(tgt).on(tgt).with_classical_controls('m1')) 
    circuit.append(cirq.Z(tgt).on(tgt).with_classical_controls('m0')) 
     
    return circuit 
 
if __name__ == '__main__': 
    circuit = build_Teleportation() 
    simulator = cirq.Simulator() 
    result = simulator.run(circuit, repetitions=10) 
    print(result) 

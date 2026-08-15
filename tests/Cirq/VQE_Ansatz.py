import cirq 
import sympy 
 
def build_VQE(): 
    q = cirq.LineQubit.range(4) 
    t = [sympy.Symbol(f'θ{i}') for i in range(8)] 
    circuit = cirq.Circuit() 
     
    # Rotation layer 
    circuit.append(cirq.ry(t[0])(q[0]))
    circuit.append(cirq.rz(t[1])(q[0]))
    circuit.append(cirq.ry(t[2])(q[1]))
    circuit.append(cirq.rz(t[3])(q[1]))
    circuit.append(cirq.ry(t[4])(q[2]))
    circuit.append(cirq.rz(t[5])(q[2]))
    circuit.append(cirq.ry(t[6])(q[3]))
    circuit.append(cirq.rz(t[7])(q[3]))
     
    # Entanglement layer 
    circuit.append(cirq.CNOT(q[0], q[1]))
    circuit.append(cirq.CNOT(q[1], q[2]))
    circuit.append(cirq.CNOT(q[2], q[3]))
     
    return circuit 
 
if __name__ == '__main__': 
    import numpy as np 
    circuit = build_VQE() 
    params = {f'θ{i}': 0.1 * (i + 1) for i in range(8)} 
    resolver = cirq.ParamResolver(params) 
    circuit.append(cirq.measure(*cirq.LineQubit.range(4), key='result')) 
    result = cirq.Simulator().run(circuit, param_resolver=resolver, 
repetitions=1024) 
    print(result.histogram(key='result')) 

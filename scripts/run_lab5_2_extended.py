"""Explicit extensions to the provided assembly interpreter, for extra tests only.
The official 20 tests always invoke the original main.py without this adapter.
"""
import ast
from pathlib import Path
import re
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'lab5_test'))
import instruction
import state_table
import type as operand_types
from interpreter import Interpreter

# The reference regex recognizes only three-digit temporaries. The emitted
# instruction format and x86 semantics do not impose that restriction.
class Pattern:
    def __init__(self,value):self.value=value
# Enum members cannot be replaced; use a proxy for the module-level enum class.
class Operands:
    Reg=Pattern(r'^(?:%r\w{1,2}|t\d+)$')
    Imm=operand_types.OperandEnum.Imm
instruction.OperandEnum=Operands
state_table.OperandEnum=Operands

def divide(self,state):
    divisor=state.get_operand(self._arg0);dividend=state.load_reg('%rax')
    if not divisor:raise ZeroDivisionError('idiv by zero')
    quotient=abs(dividend)//abs(divisor)
    if (dividend<0)!=(divisor<0):quotient=-quotient
    state.store_reg('%rax',quotient);state.store_reg('%rdx',dividend-quotient*divisor)
instruction.IdivInstruction.execute=divide
original_call=state_table.StateTable.call

def strict_load(self,address):
    if address not in self._mem_table:raise RuntimeError(f'Unmapped assembly memory read {address}')
    return self._mem_table[address]
state_table.StateTable.load_mem=strict_load

def call(self,label):
    name=label.replace('@PLT','')
    def arg(i):return self.load_reg(('%rdi','%rsi','%rdx','%rcx','%r8','%r9')[i])
    def text(i):
        value=self.load_mem(arg(i))
        if not isinstance(value,str):raise TypeError('String argument expected')
        return value
    def string(value):
        self._string_index+=1;address=self._string_address+self._string_index*8
        self.store_mem(address,value);return address
    if name=='not':result=int(arg(0)==0)
    elif name=='size':result=len(text(0))
    elif name=='concat':result=string(text(0)+text(1))
    elif name=='substring':
        value=text(0);start=arg(1);length=arg(2)
        if start<0 or length<0 or start+length>len(value):raise ValueError('substring bounds')
        result=string(value[start:start+length])
    elif name=='string_compare':result=(text(0)>text(1))-(text(0)<text(1))
    elif name=='flush':sys.stdout.flush();result=0
    elif name=='exit':raise SystemExit(arg(0))
    else:return original_call(self,label)
    self.store_reg('%rax',result)
state_table.StateTable.call=call

class ExtendedInterpreter(Interpreter):
    def _scan_lines(self,filename):
        super()._scan_lines(filename)
        # Preserve escaped quote/backslash/NUL bytes rather than the reference
        # scanner's string.replace behavior. Assembly source is not rewritten.
        label=None;rodata=False
        for raw in Path(filename).read_text().splitlines():
            line=raw.strip()
            if line=='.section .rodata':rodata=True;continue
            if not rodata:continue
            if line.endswith(':'):label=line[:-1]
            elif line.startswith('.string '):self._string_table[label]=ast.literal_eval(line[8:])

if __name__=='__main__':
    interpreter=ExtendedInterpreter(sys.argv[1]);interpreter.run()
    print('ASM_RESULT',interpreter._state_table.load_reg('%rax'),file=sys.stderr)

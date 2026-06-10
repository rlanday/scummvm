/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/debug.h"
#include "common/textconsole.h"

#include "cyberflix/vm.h"

namespace Cyberflix {

Common::String Value::toString() const {
	switch (type) {
	case kInt:    return Common::String::format("%d", intValue);
	case kBool:   return Common::String(intValue ? "true" : "false");
	case kSymbol: return Common::String::format("sym:%s", strValue.c_str());
	case kString: return Common::String::format("\"%s\"", strValue.c_str());
	default:      return Common::String("?");
	}
}

ScriptVM::ScriptVM() : _pc(0), _trace(false) {
}

Value ScriptVM::pop() {
	if (_stack.empty())
		return Value();
	Value v = _stack.back();
	_stack.pop_back();
	return v;
}

void ScriptVM::run(const Script &script, uint32 maxSteps) {
	_stack.clear();
	_pc = 0;

	uint32 steps = 0;
	while (_pc < script.getInstructionCount() && steps < maxSteps) {
		const Script::Instruction &inst = script.getInstruction(_pc);
		if (inst.opcode == Script::kOpEnd)
			break;

		uint32 here = _pc;
		_pc++;
		execute(script, here);

		if (_trace) {
			debug(0, "  %4u: %-8s a=%#06x b=%#010x  [stack=%u]", here,
					Script::opcodeName(inst.opcode), inst.operandA,
					inst.operandB, _stack.size());
		}
		steps++;
	}
}

void ScriptVM::execute(const Script &script, uint32 index) {
	const Script::Instruction &inst = script.getInstruction(index);
	switch (inst.opcode) {
	case Script::kOpPushInt:
		// 0x0006: push the 16-bit operandA as an integer constant. Confirmed
		// against the TI.EXE inline handler at vaddr 0x0040bc8c.
		push(Value::makeInt((int16)inst.operandA));
		break;

	case Script::kOpPushSym: {
		// 0x0005: push a symbol reference. operandA is a self-relative offset to
		// the symbol-name Pascal string (TI.EXE evaluator 0x00419cc0). The name
		// is resolved against the variable scope at execution time.
		Common::String sym = script.getSelfRelString(index);
		if (sym.empty())
			sym = Common::String::format("@%#x", inst.operandA);
		push(Value::makeSymbol(sym));
		break;
	}

	case Script::kOpPush3:
	case Script::kOpPush4:
		// Atom push variants that also carry a self-relative symbol/string.
		push(Value::makeSymbol(script.getSelfRelString(index)));
		break;

	default:
		if (Script::isOperator(inst.opcode)) {
			applyOperator(inst.opcode);
			break;
		}
		// Builtin (0x0Fxx) and method (0x2E/0x3E/0x4Exx) opcodes are mapped in
		// files/opcode-map.md and wired in incrementally. Skip so the VM can
		// still be stepped over real scripts.
		break;
	}
}

void ScriptVM::applyOperator(uint16 opcode) {
	// Binary infix operators. The TI.EXE evaluator (applier 0x00419f30) pops two
	// operands; lhs is the first-pushed value, rhs the second. See section 7 of
	// files/opcode-map.md for the verified opcode->operation mapping.
	Value rhs = pop();
	Value lhs = pop();
	int32 a = lhs.intValue;
	int32 b = rhs.intValue;

	switch (opcode) {
	case Script::kOpAdd: push(Value::makeInt(a + b)); break;
	case Script::kOpSub: push(Value::makeInt(a - b)); break;
	case Script::kOpMul: push(Value::makeInt(a * b)); break;
	case Script::kOpDiv:
		// Matches idiv at 0x41a048; guard the divide-by-zero the VM rejects.
		push(Value::makeInt(b != 0 ? a / b : 0));
		break;
	case Script::kOpAnd: push(Value::makeBool(a && b)); break;
	case Script::kOpOr:  push(Value::makeBool(a || b)); break;
	case Script::kOpConcat:
		push(Value::makeString(lhs.strValue + rhs.strValue));
		break;
	case Script::kOpEq:
		if (lhs.type == Value::kString || lhs.type == Value::kSymbol)
			push(Value::makeBool(lhs.strValue == rhs.strValue));
		else
			push(Value::makeBool(a == b));
		break;
	case Script::kOpNe:
		if (lhs.type == Value::kString || lhs.type == Value::kSymbol)
			push(Value::makeBool(lhs.strValue != rhs.strValue));
		else
			push(Value::makeBool(a != b));
		break;
	case Script::kOpGt: push(Value::makeBool(a > b)); break;
	case Script::kOpLt: push(Value::makeBool(a < b)); break;
	case Script::kOpGe: push(Value::makeBool(a >= b)); break;
	case Script::kOpLe: push(Value::makeBool(a <= b)); break;
	default: break;
	}
}

} // End of namespace Cyberflix

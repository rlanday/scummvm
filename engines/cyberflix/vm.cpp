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
	// Stack form of a binary operator (used by the flat tracing run()).
	Value rhs = pop();
	Value lhs = pop();
	push(applyBinary(opcode, lhs, rhs));
}

Value ScriptVM::applyBinary(uint16 opcode, const Value &lhs, const Value &rhs) {
	// Binary infix operators. The TI.EXE applier 0x00419f30 takes lhs as the
	// first-pushed operand and rhs as the second. See section 7 of
	// files/opcode-map.md for the verified opcode->operation mapping.
	int32 a = lhs.intValue;
	int32 b = rhs.intValue;

	switch (opcode) {
	case Script::kOpAdd: return Value::makeInt(a + b);
	case Script::kOpSub: return Value::makeInt(a - b);
	case Script::kOpMul: return Value::makeInt(a * b);
	case Script::kOpDiv:
		// Matches idiv at 0x41a048; guard the divide-by-zero the VM rejects.
		return Value::makeInt(b != 0 ? a / b : 0);
	case Script::kOpAnd: return Value::makeBool(a && b);
	case Script::kOpOr:  return Value::makeBool(a || b);
	case Script::kOpConcat: return Value::makeString(lhs.strValue + rhs.strValue);
	case Script::kOpEq:
		if (lhs.type == Value::kString || lhs.type == Value::kSymbol)
			return Value::makeBool(lhs.strValue == rhs.strValue);
		return Value::makeBool(a == b);
	case Script::kOpNe:
		if (lhs.type == Value::kString || lhs.type == Value::kSymbol)
			return Value::makeBool(lhs.strValue != rhs.strValue);
		return Value::makeBool(a != b);
	case Script::kOpGt: return Value::makeBool(a > b);
	case Script::kOpLt: return Value::makeBool(a < b);
	case Script::kOpGe: return Value::makeBool(a >= b);
	case Script::kOpLe: return Value::makeBool(a <= b);
	default: return Value();
	}
}

Value ScriptVM::decodeAtom(const Script &script, uint32 &pc) {
	const Script::Instruction &inst = script.getInstruction(pc);
	switch (inst.opcode) {
	case Script::kOpPushInt:
		pc++;
		return Value::makeInt((int16)inst.operandA);

	case Script::kOpPushSym:
	case Script::kOpPush3:
	case Script::kOpPush4: {
		Common::String sym = script.getSelfRelString(pc);
		pc++;
		return Value::makeSymbol(sym);
	}

	default:
		// Method-call atoms (0x3E80-0x4E9A) and atom-builtins (0x0FB5-0x0FBB)
		// consume a variable number of instructions and depend on subsystems
		// not yet wired (files/opcode-map.md section 8). Consume one instruction
		// and yield a placeholder so the evaluator stays well-formed.
		pc++;
		return Value();
	}
}

Value ScriptVM::evaluateExpression(const Script &script, uint32 &pc) {
	// atom (operator atom)* reduced by precedence, per evaluator 0x00419cf0.
	Common::Array<Value> operands;
	Common::Array<uint16> operators;

	operands.push_back(decodeAtom(script, pc));
	while (pc < script.getInstructionCount() &&
			Script::isOperator(script.getInstruction(pc).opcode)) {
		operators.push_back(script.getInstruction(pc).opcode);
		pc++;
		operands.push_back(decodeAtom(script, pc));
	}

	// Reduce by precedence class (0 binds tightest), left-to-right within a
	// class, matching the reduction loop at 0x00419e5e.
	for (uint8 prec = 0; prec <= 6; ++prec) {
		for (uint32 i = 0; i < operators.size();) {
			if (Script::operatorPrecedence(operators[i]) == prec) {
				operands[i] = applyBinary(operators[i], operands[i], operands[i + 1]);
				operands.remove_at(i + 1);
				operators.remove_at(i);
			} else {
				++i;
			}
		}
	}

	return operands[0];
}

uint32 ScriptVM::runProgram(const Script &script, uint32 maxSteps) {
	_stack.clear();
	_whileStack.clear();
	uint32 pc = 0;
	uint32 executed = 0;
	const uint32 count = script.getInstructionCount();

	while (pc < count && executed < maxSteps) {
		uint32 here = pc;
		uint16 op = script.getInstruction(pc).opcode;

		switch (op) {
		case Script::kOpEnd:
		case Script::kOpReturn:
			return executed;

		case Script::kOpPushInt:
			// Top-level integer atoms act as statement separators/padding; the
			// main loop skips runs of them (TI.EXE 0x0040c1a1-0x0040c1b2).
			pc++;
			break;

		case Script::kOpIf: {
			uint32 condPc = pc + 1;
			Value cond = evaluateExpression(script, condPc);
			if (isTruthy(cond)) {
				pc = condPc; // enter THEN block
			} else {
				int elseStart = script.findMatchingElse(here);
				if (elseStart >= 0) {
					pc = (uint32)elseStart; // enter ELSE block
				} else {
					int endIf = script.findMatchingEndIf(here);
					pc = (endIf >= 0) ? (uint32)endIf + 1 : count;
				}
			}
			break;
		}

		case Script::kOpElse: {
			// Reached only after a THEN block executed; skip the else body.
			int endIf = script.findEndIfFrom(pc + 1);
			pc = (endIf >= 0) ? (uint32)endIf + 1 : count;
			break;
		}

		case Script::kOpEndIf:
			pc++;
			break;

		case Script::kOpWhile: {
			// Save the condition start, evaluate it; on true enter the body and
			// remember where to re-test, on false skip past the matching
			// kOpEndWhile (TI.EXE 0x0040c066, while-stack 0x45ecf8).
			uint32 condPc = pc + 1;
			Value cond = evaluateExpression(script, condPc);
			if (isTruthy(cond)) {
				_whileStack.push_back(pc + 1);
				pc = condPc; // enter body
			} else {
				int endWhile = script.findEndWhileFrom(condPc);
				pc = (endWhile >= 0) ? (uint32)endWhile + 1 : count;
			}
			break;
		}

		case Script::kOpEndWhile: {
			// Re-test the saved condition; loop back to the body or pop and exit
			// (TI.EXE 0x0040c0eb).
			if (_whileStack.empty()) {
				pc++;
				break;
			}
			uint32 condPc = _whileStack.back();
			uint32 scan = condPc;
			Value cond = evaluateExpression(script, scan);
			if (isTruthy(cond)) {
				pc = scan; // back to body start
			} else {
				_whileStack.pop_back();
				pc++;
			}
			break;
		}

		case Script::kOpFor: {
			// For-loops bind a loop variable and iterate over a numeric range
			// (TI.EXE 0x0040bda9, frame stack 0x45ed48, with a kOpForTo bound
			// separator and kOpForNext terminator). Iteration needs the variable
			// scope model, which is not implemented yet, so skip the whole
			// construct for now rather than execute it incorrectly.
			int forNext = script.findForNextFrom(pc + 1);
			pc = (forNext >= 0) ? (uint32)forNext + 1 : count;
			break;
		}

		default: {
			// Method calls, assignments and other statement expressions. Consume
			// the expression so the PC advances; side effects are wired in as the
			// method handlers and subsystems land (files/opcode-map.md).
			uint32 next = pc;
			evaluateExpression(script, next);
			pc = (next > here) ? next : here + 1;
			break;
		}
		}

		if (_trace) {
			debug(0, "  stmt %4u: %-8s -> pc=%u", here,
					Script::opcodeName(op), pc);
		}
		executed++;
	}

	return executed;
}

} // End of namespace Cyberflix

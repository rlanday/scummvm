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

ScriptVM::ScriptVM() : _pc(0), _executed(0), _trace(false), _host(nullptr), _callDepth(0) {
}

Value ScriptVM::getVar(const Common::String &name) const {
	// Scope lookup: innermost local scope first (TI.EXE 0x004138f0 against the
	// per-call scope object), then the global object ([0x45f010]). Names are
	// matched case-insensitively (TI.EXE compares via the case-folding
	// FUN_0041ae80); we normalise to lowercase keys. Unbound names are
	// returned as symbol values so they can still flow through expressions
	// harmlessly (lookup miss at 0x0041395b returns the name).
	Common::String key = name;
	key.toLowercase();
	if (!_locals.empty() && _locals.back().contains(key))
		return _locals.back()[key];
	if (_vars.contains(key))
		return _vars[key];
	return Value::makeSymbol(name);
}

void ScriptVM::setVar(const Common::String &name, const Value &v) {
	// Assignment resolution order per the FUN_0040ba20 default case: store into
	// the local scope when the name is declared there, else into the global
	// scope. (TI.EXE errors 0x10 on fully undeclared names; we tolerate them
	// as implicit globals to stay permissive while subsystems land.)
	Common::String key = name;
	key.toLowercase();
	if (!_locals.empty() && _locals.back().contains(key)) {
		_locals.back()[key] = v;
		return;
	}
	_vars[key] = v;
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
	const uint32 count = script.getInstructionCount();

	switch (inst.opcode) {
	case Script::kOpPushInt:
		pc++;
		return Value::makeInt((int16)inst.operandA);

	case Script::kOpTrue:
		// 0x0fb5: boolean TRUE literal (atom decoder 0x0041a550).
		pc++;
		return Value::makeBool(true);

	case Script::kOpFalse:
		// 0x0fb6: boolean FALSE literal.
		pc++;
		return Value::makeBool(false);

	case Script::kOpNot: {
		// 0x0fb7: prefix logical NOT — decode the following atom and invert
		// (TI.EXE recurses then XORs the bool with 1).
		pc++;
		Value v = decodeAtom(script, pc);
		return Value::makeBool(!isTruthy(v));
	}

	case Script::kOpOpenParen: {
		// 0x0fb2 as an atom head is a parenthesised subexpression.
		pc++;
		Value v = evaluateExpression(script, pc);
		if (pc < count && script.getInstruction(pc).opcode == Script::kOpCloseParen)
			pc++;
		return v;
	}

	case Script::kOpSub: {
		// Leading kOpSub is unary minus: decode the following atom and negate
		// it (TI.EXE atom decoder 0x0041a8e5).
		pc++;
		Value v = decodeAtom(script, pc);
		return Value::makeInt(-v.intValue);
	}

	case Script::kOpPushSym:
	case Script::kOpPush3:
	case Script::kOpPush4: {
		Common::String sym = script.getSelfRelString(pc);
		uint16 headOp = inst.opcode;
		uint16 lowWord = inst.operandA;
		pc++;
		// push4 (0x0004) is a 32-bit INTEGER literal, not a name: the value's
		// low 16 bits live in operandA (atom decoder 0x0041a550 case 4 reads a
		// dword from the instruction record; e.g. actionframe(1) and for-loop
		// bounds use push4). Small literals confirmed; revisit the high word if
		// a >16-bit literal ever shows up in game scripts.
		if (headOp == Script::kOpPush4)
			return Value::makeInt((int16)lowWord);
		// A name immediately followed by '(' is a call atom: parse and evaluate
		// the balanced argument list, then dispatch (TI.EXE 0x0041a609 checks
		// the next opcode for kOpOpenParen and sizes the span via 0x0040b690).
		if (pc < count && script.getInstruction(pc).opcode == Script::kOpOpenParen) {
			Common::Array<Value> args;
			parseCallArgs(script, pc, args);
			// A symbol head is a script-function call, dispatched against the
			// library scope chain (TI.EXE 0x0041a609 -> FUN_0040b690).
			if (headOp == Script::kOpPushSym)
				return callFunction(sym, args);
			return callMethod(headOp, sym, args);
		}
		// push3 (0x0003) is a string literal from the pool (type 3).
		if (headOp == Script::kOpPush3)
			return Value::makeString(sym);
		// Otherwise a symbol reference evaluates to its bound value.
		return getVar(sym);
	}

	default: {
		// Method-call atoms (0x3E80-0x4E9A) and other name-bearing atoms also
		// take an optional '(' argument list (same 0x0041a580 path as symbols).
		uint16 headOp = inst.opcode;
		pc++;
		if (pc < count && script.getInstruction(pc).opcode == Script::kOpOpenParen) {
			// The message-carrying builtins sendtoactor (0x2ef0), sendtoscene
			// (0x2f02), sendtocast (0x2f10), sendtoshop (0x2f1b) and
			// sendtostage (0x2f26) pass their final argument UNevaluated: it is
			// a message `name(args)` matched against script definitions
			// (TI.EXE FUN_0040ad80 hands the raw code span to the dispatcher
			// FUN_0040b690). Leading arguments (e.g. the cast-file target) are
			// evaluated normally.
			if (headOp == 0x2ef0 || headOp == 0x2f10 || headOp == 0x2f1b || headOp == 0x2f26)
				return dispatchMessageBuiltin(script, pc, headOp);
			Common::Array<Value> args;
			parseCallArgs(script, pc, args);
			return callMethod(headOp, Common::String(), args);
		}
		return Value();
	}
	}
}

Value ScriptVM::dispatchMessageBuiltin(const Script &script, uint32 &pc, uint16 opcode) {
	// pc references the kOpOpenParen of e.g. sendtostage(advanceday()) or
	// sendtocast('gang.cst', initactors()). Evaluate any leading target
	// arguments, then capture the trailing message WITHOUT evaluating it.
	const uint32 count = script.getInstructionCount();
	Common::Array<Value> targets;
	Common::String message;
	Common::Array<Value> msgArgs;

	pc++; // consume '('
	while (pc < count) {
		const Script::Instruction &head = script.getInstruction(pc);
		if (head.opcode == Script::kOpCloseParen) {
			pc++;
			break;
		}
		// `name (` here is the message call: bind its name and evaluate its
		// arguments in the CALLER's scope (the matcher FUN_0040b870 evaluates
		// caller-arg values before binding them to the callee's formals).
		if (head.opcode == Script::kOpPushSym && pc + 1 < count &&
				script.getInstruction(pc + 1).opcode == Script::kOpOpenParen) {
			message = script.getSelfRelString(pc);
			message.toLowercase();
			pc++;
			parseCallArgs(script, pc, msgArgs);
		} else {
			targets.push_back(evaluateExpression(script, pc));
		}
		if (pc < count && script.getInstruction(pc).opcode == Script::kOpArgSep) {
			pc++;
			continue;
		}
		if (pc < count && script.getInstruction(pc).opcode == Script::kOpCloseParen) {
			pc++;
			break;
		}
		break; // malformed; statement loop resynchronises on separators
	}

	if (_trace)
		debug(0, "    message #%#06x -> %s(%u args)", opcode, message.c_str(), msgArgs.size());

	switch (opcode) {
	case 0x2f26: // sendtostage(message(...)) -> TI.EXE FUN_0040ad80
		if (_host)
			_host->sendToStage(message, msgArgs);
		break;
	case 0x2ef0: // sendtoactor(actor, message) -> dispatch against the actor's
	             // cast script (TI.EXE 0x2ef0 handler). Cast subsystem not yet
	             // implemented; no-op so initactors()-style loops run dry.
	case 0x2f10: // sendtocast('file.cst', message) -> per-cast script dispatch.
	case 0x2f1b: // sendtoshop('file.shp', message) -> per-shop script dispatch.
		if (_trace)
			debug(0, "    (cast/shop message '%s' ignored: subsystem pending)", message.c_str());
		break;
	default:
		break;
	}
	return Value();
}

void ScriptVM::parseCallArgs(const Script &script, uint32 &pc, Common::Array<Value> &outArgs) {
	// pc references a kOpOpenParen. Walk the argument list, evaluating each
	// comma-separated sub-expression with the precedence evaluator. The evaluator
	// stops at kOpArgSep and kOpCloseParen since neither is an operator, so the
	// arguments fall out naturally (TI.EXE argument scan 0x0040b690).
	const uint32 count = script.getInstructionCount();
	pc++; // consume '('
	if (pc < count && script.getInstruction(pc).opcode == Script::kOpCloseParen) {
		pc++; // empty argument list
		return;
	}
	while (pc < count) {
		outArgs.push_back(evaluateExpression(script, pc));
		if (pc >= count)
			break;
		uint16 op = script.getInstruction(pc).opcode;
		if (op == Script::kOpArgSep) {
			pc++; // next argument
			continue;
		}
		if (op == Script::kOpCloseParen) {
			pc++; // end of list
			break;
		}
		// Defensive: an unexpected token means the evaluator did not consume the
		// whole argument (e.g. an unimplemented atom span). Resynchronise on the
		// matching close paren so the outer statement loop stays aligned.
		int close = script.findCloseParen(pc);
		pc = (close >= 0) ? (uint32)close + 1 : count;
		break;
	}
}

Value ScriptVM::callMethod(uint16 opcode, const Common::String &name, const Common::Array<Value> &args) {
	// Dispatch a builtin method by opcode. The interpreter routes 0x2Exx/0x2Fxx
	// through dispatch B and 0x3Exx/0x4Exx through dispatch A (files/opcode-map.md
	// section 6); ~340 handlers are stubbed as logged no-ops until their
	// subsystems (renderer/video/audio/navigation) are implemented. Arguments are
	// fully evaluated so trace output reflects real call sites.
	if (_trace) {
		Common::String a;
		for (uint32 i = 0; i < args.size(); ++i) {
			if (i)
				a += ", ";
			a += args[i].toString();
		}
		const char *builtin = Script::methodName(opcode);
		const char *label = !name.empty() ? name.c_str()
				: (builtin ? builtin : "method");
		debug(0, "    call %s#%#06x(%s)", label, opcode, a.c_str());
	}

	// Forward the effectful builtins we implement to the engine host. Opcodes
	// not handled here remain logged no-ops (see files/method-catalog.md).
	// (The message-carrying send* builtins never reach this path; they are
	// routed through dispatchMessageBuiltin with the message unevaluated.)
	if (_host) {
		switch (opcode) {
		case 0x2ef1: // playmovie('name.mov')
			_host->playMovie(args.empty() ? Common::String() : args[0].strValue);
			break;
		case 0x2f1c: // openstagefile('name.stg')
			_host->openStageFile(args.empty() ? Common::String() : args[0].strValue);
			break;
		case 0x2f00: // opensetfile('name.set'[, scene[, view]]) -> FUN_00430690
			_host->openSetFile(args.size() > 0 ? args[0].strValue : Common::String(),
					args.size() > 1 ? args[1].strValue : Common::String(),
					args.size() > 2 ? args[2].strValue : Common::String());
			break;
		case 0x2f01: // closesetfile() -> TI.EXE set-archive close
			_host->closeSetFile();
			break;
		case 0x2f02: // sendtoscene('scenename') -> TI.EXE FUN_004311e0/FUN_00431200
			_host->sendToScene(args.empty() ? Common::String() : args[0].strValue);
			break;
		case 0x4e55: // currentset() -> open set name or 'none'
			return Value::makeString(_host->currentSet());
		case 0x4e73: // actionframe(n) -> bool, FUN_004362c0 (n must be 1 or 2)
			return Value::makeBool(_host->actionFrame(args.empty() ? 0 : args[0].intValue));
		case 0x3e96: // framerate(n): sets the global frame pacing (TI.EXE
		             // dispatch A case 0x15). Engine paces from per-frame
		             // authored holds instead; accept and ignore.
			break;
		case 0x4e2c: // countactors(): cast subsystem pending -> 0, so the
		             // initall()/advanceday() actor loops run dry.
		case 0x4e3f: // countprops(): shop subsystem pending -> 0.
			return Value::makeInt(0);
		default:
			break;
		}
	}
	return Value();
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
	_forStack.clear();

	// A script resource is a sequence of named definitions separated by
	// kOpScriptMarker. "Running the program" means running the FIRST
	// definition's body (e.g. BOOTFILE res1 starts with `boot()`); the
	// remaining definitions are message handlers reached only by dispatch
	// (TI.EXE never falls through a marker: FUN_0040ba20 treats it as end).
	uint32 start = 0;
	const Common::Array<Script::Definition> &defs = script.definitions();
	if (!defs.empty())
		start = defs[0].bodyStart;

	Value result;
	_executed = 0;
	runBody(script, start, result, maxSteps);
	return _executed;
}

Value ScriptVM::callFunction(const Common::String &name, const Common::Array<Value> &args,
		bool *handled) {
	// Message dispatch (TI.EXE FUN_0040b690 -> FUN_0040b7a0 -> FUN_0040b870):
	// walk the scope chain newest-first; in each library find a definition
	// matching the (case-insensitive) name, bind its formal parameters to the
	// already-evaluated argument values in a fresh local scope, and execute the
	// body. A kOpPass statement returns "pretend no match" (code 4) and the
	// next library is tried.
	if (handled)
		*handled = false;
	Value result;

	if (_callDepth >= 64) { // TI.EXE has no explicit guard; protect the engine
		warning("Cyberflix: script call depth overflow at '%s'", name.c_str());
		return result;
	}

	for (int li = (int)_libraries.size() - 1; li >= 0; --li) {
		const Script *lib = _libraries[li];
		const Script::Definition *def = lib->findDefinition(name);
		if (!def)
			continue;

		_locals.push_back(Common::HashMap<Common::String, Value>());
		for (uint32 i = 0; i < def->params.size(); ++i)
			_locals.back()[def->params[i]] = (i < args.size()) ? args[i] : Value();

		_callDepth++;
		RunResult rr = runBody(*lib, def->bodyStart, result, 100000);
		_callDepth--;
		_locals.pop_back();

		if (rr == kRunPassed)
			continue; // try the next scope on the chain
		if (handled)
			*handled = true;
		if (_trace)
			debug(0, "  dispatch %s(%u args) -> %s", name.c_str(), args.size(),
					result.toString().c_str());
		return result;
	}

	if (_trace)
		debug(0, "  dispatch %s: no matching definition", name.c_str());
	return result;
}

ScriptVM::RunResult ScriptVM::runBody(const Script &script, uint32 pc, Value &result,
		uint32 maxSteps) {
	uint32 executed = 0;
	const uint32 count = script.getInstructionCount();

	// Loop stacks are per-body in TI.EXE (depth counters local_24/local_22 in
	// FUN_0040ba20); save/restore so a dispatched call inside a loop body
	// cannot corrupt the caller's frames.
	uint32 whileBase = _whileStack.size();
	uint32 forBase = _forStack.size();

	while (pc < count && executed < maxSteps) {
		uint32 here = pc;
		uint16 op = script.getInstruction(pc).opcode;

		switch (op) {
		case Script::kOpEnd:
		case Script::kOpReturn:
		case Script::kOpScriptMarker:
			// End of this definition's body. kOpScriptMarker introduces the
			// NEXT definition and is never executed (FUN_0040ba20 returns on
			// it via the 0xfa4/stream-end paths).
			goto done;

		case Script::kOpExit:
			// 0x0fa5 'exit': early return with no result, unwinding any loop
			// bookkeeping opened inside this body (TI.EXE 0x0040bb05).
			goto done;

		case Script::kOpReturnValue: {
			// 0x0fb8 'return <expr>': evaluate into the caller's result slot
			// (TI.EXE 0x0040bdf3; err 5 there if the caller expected none).
			uint32 p = pc + 1;
			result = evaluateExpression(script, p);
			_whileStack.resize(whileBase);
			_forStack.resize(forBase);
			return kRunReturned;
		}

		case Script::kOpPass:
			// 0x0fb9 'pass': pretend this definition did not match; the
			// dispatcher continues down the scope chain (TI.EXE returns 4).
			_whileStack.resize(whileBase);
			_forStack.resize(forBase);
			return kRunPassed;

		case Script::kOpDeclGlobal:
		case Script::kOpDeclLocal: {
			// 0x0fa2/0x0fa3: declare a comma-separated symbol list into the
			// global or current local scope (TI.EXE FUN_0040c480). Entries are
			// created unset; we register them so assignment targets resolve to
			// the right scope.
			bool global = (op == Script::kOpDeclGlobal) || _locals.empty();
			pc++;
			while (pc < count && script.getInstruction(pc).opcode == Script::kOpPushSym) {
				Common::String var = script.getSelfRelString(pc);
				var.toLowercase();
				if (global) {
					if (!_vars.contains(var))
						_vars[var] = Value();
				} else {
					if (!_locals.back().contains(var))
						_locals.back()[var] = Value();
				}
				pc++;
				if (pc < count && script.getInstruction(pc).opcode == Script::kOpArgSep)
					pc++; // ',' continues the list
				else
					break;
			}
			break;
		}

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
			// for <var> = <start> to <end> ... next
			// Parse the header (TI.EXE 0x0040bda9): a loop-variable symbol, the
			// kOpEq '=' separator, the start expression, the kOpForTo 'to'
			// separator and the end expression; the body follows up to the
			// matching kOpForNext. Bind the variable and either enter the body
			// or skip the whole construct when the range is empty.
			uint32 p = pc + 1;
			Common::String loopVar = script.getSelfRelString(p);
			p++;
			if (p < count && script.getInstruction(p).opcode == Script::kOpEq)
				p++; // '=' separator
			Value start = evaluateExpression(script, p);
			if (p < count && script.getInstruction(p).opcode == Script::kOpForTo)
				p++; // 'to' separator
			Value end = evaluateExpression(script, p);
			uint32 bodyStart = p;

			setVar(loopVar, Value::makeInt(start.intValue));
			if (start.intValue <= end.intValue) {
				ForFrame frame;
				frame.var = loopVar;
				frame.end = end.intValue;
				frame.bodyStart = bodyStart;
				_forStack.push_back(frame);
				pc = bodyStart; // enter body
			} else {
				int forNext = script.findForNextFrom(bodyStart);
				pc = (forNext >= 0) ? (uint32)forNext + 1 : count;
			}
			break;
		}

		case Script::kOpForNext: {
			// Increment the loop variable and compare against the inclusive
			// bound; loop back to the body or pop the frame (TI.EXE 0x0040bda9
			// continuation via the kOpForNext branch).
			if (_forStack.empty()) {
				pc++;
				break;
			}
			ForFrame &frame = _forStack.back();
			int32 next = getVar(frame.var).intValue + 1;
			setVar(frame.var, Value::makeInt(next));
			if (next <= frame.end) {
				pc = frame.bodyStart;
			} else {
				_forStack.pop_back();
				pc++;
			}
			break;
		}

		default: {
			// Statement-level dispatch (FUN_0040ba20 default case). Three forms:
			//  - `pushSym name == expr`  ASSIGNMENT (store via 0x413610: local
			//    scope first, else global; TI.EXE errs 0x10 on undeclared).
			//  - `pushSym name ( args )` script-function CALL: recursive
			//    dispatch FUN_0040b690 over the SAME scope chain.
			//  - anything else: builtin statement (dispatch B FUN_00444c60),
			//    consumed by the expression evaluator which routes builtins
			//    through callMethod.
			if (op == Script::kOpPushSym) {
				uint32 p = pc + 1;
				uint16 nextOp = (p < count) ? script.getInstruction(p).opcode : 0;
				if (nextOp == Script::kOpEq) {
					Common::String var = script.getSelfRelString(pc);
					var.toLowercase();
					p++;
					Value v = evaluateExpression(script, p);
					setVar(var, v);
					if (_trace)
						debug(0, "    assign %s = %s", var.c_str(), v.toString().c_str());
					pc = p;
					break;
				}
				if (nextOp == Script::kOpOpenParen) {
					Common::String fn = script.getSelfRelString(pc);
					Common::Array<Value> args;
					parseCallArgs(script, p, args);
					callFunction(fn, args);
					pc = p;
					break;
				}
			}
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
		_executed++;
	}
done:
	_whileStack.resize(whileBase);
	_forStack.resize(forBase);
	return kRunDone;
}

} // End of namespace Cyberflix

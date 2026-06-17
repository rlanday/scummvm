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

#include <cstdlib>
#include <cstring>

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

static bool shouldLogTransitionDispatch(const Common::String &name) {
	return name.equalsIgnoreCase("dolife") ||
			name.equalsIgnoreCase("transtoflat") ||
			name.equalsIgnoreCase("transfromflat") ||
			name.equalsIgnoreCase("hideinterface") ||
			name.equalsIgnoreCase("showinterface") ||
			name.equalsIgnoreCase("activateinterface") ||
			name.equalsIgnoreCase("watchidle") ||
			name.equalsIgnoreCase("bagidle") ||
			name.equalsIgnoreCase("mapidle") ||
			name.equalsIgnoreCase("savestages") ||
			name.equalsIgnoreCase("openflat") ||
			name.equalsIgnoreCase("closeflat");
}

static int32 stringToNum(const Common::String &text) {
	return (int32)strtol(text.c_str(), nullptr, 10);
}

static Common::String findWord(const Common::String &text, const Common::String &delimiter,
		int32 wordIndex) {
	if (wordIndex < 1)
		return Common::String();

	if (delimiter.empty()) {
		if ((uint32)wordIndex > text.size())
			return Common::String();
		return Common::String(text.c_str() + wordIndex - 1, 1);
	}

	uint32 wordStart = 0;
	int32 remaining = wordIndex;
	for (uint32 i = 0; i + delimiter.size() <= text.size(); ++i) {
		bool atDelimiter =
				!memcmp(text.c_str() + i, delimiter.c_str(), delimiter.size());
		if (atDelimiter) {
			if (--remaining <= 0)
				return Common::String(text.c_str() + wordStart, i - wordStart);
			i += delimiter.size() - 1;
			wordStart = i + 1;
		}
	}

	return Common::String();
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
	return getVar(name, key);
}

Value ScriptVM::getVar(const Common::String &name, const Common::String &key) const {
	if (!_locals.empty() && _locals.back().contains(key)) {
		Value v = _locals.back()[key];
		if (key == "tour")
			debug(1, "Cyberflix: var tour local -> %s", v.toString().c_str());
		return v;
	}
	if (_vars.contains(key)) {
		Value v = _vars[key];
		if (key == "tour")
			debug(1, "Cyberflix: var tour global -> %s", v.toString().c_str());
		return v;
	}
	if (key == "tour")
		debug(1, "Cyberflix: var tour unbound");
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
		const Common::String &sym = script.getSelfRelStringRef(index);
		if (sym.empty())
			push(Value::makeSymbol(Common::String::format("@%#x", inst.operandA)));
		else
			push(Value::makeSymbol(sym));
		break;
	}

	case Script::kOpPush3:
	case Script::kOpPush4:
		// Atom push variants that also carry a self-relative symbol/string.
		push(Value::makeSymbol(script.getSelfRelStringRef(index)));
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
	const bool lhsString = lhs.type == Value::kString || lhs.type == Value::kSymbol;
	const bool rhsString = rhs.type == Value::kString || rhs.type == Value::kSymbol;

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
		if (lhsString || rhsString)
			return Value::makeBool(lhsString && rhsString &&
					lhs.strValue.equalsIgnoreCase(rhs.strValue));
		return Value::makeBool(a == b);
	case Script::kOpNe:
		if (lhsString || rhsString)
			return Value::makeBool(!(lhsString && rhsString &&
					lhs.strValue.equalsIgnoreCase(rhs.strValue)));
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

	case 0x0fba:
		// Push the dispatch context's "self" name (chain entry +0x1e: the
		// prop/shop/stage whose script is running; atom decoder 0x0041a550).
		// Prop scripts use this as the name argument to propvisible/propxy/
		// propview (e.g. HOUSE.SHP res 940 setupprop).
		pc++;
		return Value::makeString(_ctxSelf);

	case 0x0fbb:
		// Push the dispatch context's target-prop name (chain entry +0x3e).
		pc++;
		return Value::makeString(_ctxProp);

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

	case Script::kOpPush4: {
		// ScummVM-only hot-path cleanup: push4 shares the split-operand storage
		// format with self-relative symbols, but it is an integer literal, so it
		// must not touch the pool-string decoder/cache at all.
		pc++;
		return Value::makeInt((int32)script.getSplitOperand(pc - 1));
	}

	case Script::kOpPushSym:
	case Script::kOpPush3: {
		const uint32 atomPc = pc;
		const Common::String &sym = script.getSelfRelStringRef(atomPc);
		uint16 headOp = inst.opcode;
		pc++;
		// A name immediately followed by '(' is a call atom: parse and evaluate
		// the balanced argument list, then dispatch (TI.EXE 0x0041a609 checks
		// the next opcode for kOpOpenParen and sizes the span via 0x0040b690).
		if (pc < count && script.getInstruction(pc).opcode == Script::kOpOpenParen) {
			Common::Array<Value> args;
			parseCallArgs(script, pc, args);
			// A symbol head is a script-function call, dispatched against the
			// library scope chain (TI.EXE 0x0041a609 -> FUN_0040b690).
			if (headOp == Script::kOpPushSym)
				return callFunction(script.getSelfRelStringLowercaseRef(atomPc), args);
			return callMethod(headOp, sym, args);
		}
		// push3 (0x0003) is a string literal from the pool (type 3).
		if (headOp == Script::kOpPush3)
			return Value::makeString(sym);
		// Otherwise a symbol reference evaluates to its bound value.
		return getVar(sym, script.getSelfRelStringLowercaseRef(atomPc));
	}

	default: {
		// Method-call atoms (0x3E80-0x4E9A) and other name-bearing atoms also
		// take an optional '(' argument list (same 0x0041a580 path as symbols).
		uint16 headOp = inst.opcode;
		pc++;
		if (pc < count && script.getInstruction(pc).opcode == Script::kOpOpenParen) {
			// The message-carrying builtins sendtoactor (0x2ef0), sendtoscene
			// (0x2f02), sendtopuppet (0x2f0d), sendtocast (0x2f10),
			// sendtoprop (0x2f17), sendtoshop
			// (0x2f1b), sendtopainting (0x2f22), sendtobutton (0x2f24),
			// sendtoflat (0x2f25), sendtostage (0x2f26), sendtoboot
			// (0x2f31), and the value-returning 0x4e74-0x4e7f fx dispatch
			// family pass their final
			// argument UNevaluated: it is a message `name(args)` matched
			// against script definitions (TI.EXE FUN_0040ad80/FUN_0042ae80
			// hand the raw code span to the dispatcher FUN_0040b690). Leading
			// arguments (e.g. the shop-file or prop-name target) are
			// evaluated normally. Evaluating the message instead recurses:
			// boot res1's mousedown sends mousedown(thepoint) to the hit
			// scene/flat/button, which would re-enter itself.
			if (headOp == Script::kMethodSendToActor || headOp == Script::kMethodSendToScene ||
					headOp == Script::kMethodSendToPuppet || headOp == Script::kMethodSendToCast ||
					headOp == Script::kMethodSendToProp || headOp == Script::kMethodSendToShop ||
					headOp == Script::kMethodSendToPainting || headOp == Script::kMethodSendToButton ||
					headOp == Script::kMethodSendToFlat || headOp == Script::kMethodSendToStage ||
					headOp == Script::kMethodSendToBoot || headOp == Script::kMethodSendToActorFx ||
					headOp == Script::kMethodSendToSceneFx || headOp == Script::kMethodSendToPuppetFx ||
					headOp == Script::kMethodSendToCastFx || headOp == Script::kMethodSendToPropFx ||
					headOp == Script::kMethodSendToShopFx || headOp == Script::kMethodSendToPaintingFx ||
					headOp == Script::kMethodSendToSetFx || headOp == Script::kMethodSendToButtonFx ||
					headOp == Script::kMethodSendToFlatFx || headOp == Script::kMethodSendToStageFx ||
					headOp == Script::kMethodSendToBootFx)
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
	Value targets[3];
	uint32 targetCount = 0;
	const Common::String empty;
	// Non-owning alias to either `empty` or the immutable per-script lowercase
	// string cache. This is deliberately a raw pointer instead of a
	// Common::String copy/reference: the message atom is discovered inside the
	// loop, the empty fallback must remain assignable, and copying here showed up
	// as Common::String refcount churn in the decodeAtom() hot path. The pointed
	// string never outlives this call; `script` is the active caller script and
	// the cache entry is stable for the duration of dispatchMessageBuiltin().
	const Common::String *message = &empty;
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
			// Alias the cached folded message name; sendto* dispatch consumes it
			// synchronously below, so no ownership transfer is needed.
			message = &script.getSelfRelStringLowercaseRef(pc);
			pc++;
			parseCallArgs(script, pc, msgArgs);
		} else {
			Value target = evaluateExpression(script, pc);
			// sendto* builtins have at most three leading target arguments
			// (sendtopainting(scene, view, painting, message)). Store those in a
			// fixed array so hot idle/mouse dispatches do not allocate a
			// Common::Array just to forward one scene/prop name.
			if (targetCount < ARRAYSIZE(targets))
				targets[targetCount] = target;
			targetCount++;
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
		debug(0, "    message #%#06x -> %s(%u args)", opcode, message->c_str(), msgArgs.size());

	const Common::String &target0 = targetCount > 0 ? targets[0].strValue : empty;
	const Common::String &target1 = targetCount > 1 ? targets[1].strValue : empty;
	const Common::String &target2 = targetCount > 2 ? targets[2].strValue : empty;

	switch (opcode) {
	case Script::kMethodSendToStage: // sendtostage(message(...)) -> TI.EXE FUN_0040ad80
		if (_host)
			_host->sendToStage(*message, msgArgs);
		break;
	case Script::kMethodSendToStageFx:
		if (_host)
			return _host->sendToStageFx(*message, msgArgs);
		break;
	case Script::kMethodSendToBoot: // sendtoboot(message(...)) -> TI.EXE FUN_00439080/FUN_004390a0
		if (_host)
			_host->sendToBoot(*message, msgArgs);
		break;
	case Script::kMethodSendToBootFx:
		if (_host)
			return _host->sendToBootFx(*message, msgArgs);
		break;
	case Script::kMethodSendToShop: // sendtoshop('file.shp', message) -> TI.EXE FUN_0042b2b0:
	             // dispatch against [shop script, BOOTFILE res2].
		if (_host)
			_host->sendToShop(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToShopFx: // sendtoshopfx('file.shp', message) -> return dispatch result.
		if (_host)
			return _host->sendToShopFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToProp: // sendtoprop('propname', message) -> TI.EXE FUN_0042ae80:
	             // dispatch against [prop script, shop script, BOOTFILE res2].
		if (_host)
			_host->sendToProp(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToPropFx:
		if (_host)
			return _host->sendToPropFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToActor: // sendtoactor(actor, message) -> actor script, then cast script.
		if (_host)
			_host->sendToActor(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToActorFx:
		if (_host)
			return _host->sendToActorFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToCast: // sendtocast('file.cst', message) -> per-cast script dispatch.
		if (_host)
			_host->sendToCast(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToCastFx:
		if (_host)
			return _host->sendToCastFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToPuppet: // sendtopuppet(target, message) -> [PUP script, BOOTFILE res2].
		if (_host)
			_host->sendToPuppet(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToPuppetFx:
		if (_host)
			return _host->sendToPuppetFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToScene: // sendtoscene(scene, message) -> TI.EXE FUN_004311e0/
	             // FUN_00431200: dispatch against the scene's script chain.
		if (_host)
			_host->sendToScene(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToSceneFx:
		if (_host)
			return _host->sendToSceneFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToPainting: // sendtopainting(scene, view, painting, message) -> FUN_00432550/FUN_00432570
		if (_host)
			_host->sendToPainting(target0, target1, target2, *message, msgArgs);
		break;
	case Script::kMethodSendToPaintingFx:
		if (_host)
			return _host->sendToPaintingFx(target0, target1, target2, *message, msgArgs);
		break;
	case Script::kMethodSendToSetFx:
		if (_host)
			return _host->sendToSetFx(*message, msgArgs);
		break;
	case Script::kMethodSendToButton: // sendtobutton(flat, button, message) -> FUN_0040a410/FUN_0040a430:
	             // chain [button script, node script, stage script, res2].
		if (_host)
			_host->sendToButton(target0, target1, *message, msgArgs);
		break;
	case Script::kMethodSendToButtonFx:
		if (_host)
			return _host->sendToButtonFx(target0, target1, *message, msgArgs);
		break;
	case Script::kMethodSendToFlat: // sendtoflat(flat, message) -> FUN_0040a940/FUN_0040a960
		if (_host)
			_host->sendToFlat(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToFlatFx:
		if (_host)
			return _host->sendToFlatFx(target0, *message, msgArgs);
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

bool ScriptVM::callCoreMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodRandom: // random(n) -> FUN_004366c0/FUN_0041b060
		result = Value::makeInt(_host ? _host->randomNumber(args.empty() ? 0 : args[0].intValue) : 0);
		return true;
	case Script::kMethodStringToNum: // stringtonum(str) -> FUN_00436ee0
		result = Value::makeInt(stringToNum(args.empty() ? Common::String() : args[0].strValue));
		return true;
	case Script::kMethodNumToString: // numtostring(n) -> FUN_00436f60
		result = Value::makeString(Common::String::format("%d", args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodFindWord: // findword(str, delimiter, index) -> FUN_00437160, 1-based
		result = Value::makeString(findWord(args.size() > 0 ? args[0].strValue : Common::String(),
				args.size() > 1 ? args[1].strValue : Common::String(),
				args.size() > 2 ? args[2].intValue : 0));
		return true;
	case Script::kMethodStringLength: // stringlength(str) -> FUN_004373e0
		result = Value::makeInt(args.empty() ? 0 : args[0].strValue.size());
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callAudioMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenTrackFile: // opentrackfile('name.trk') -> FUN_00411be0/FUN_00411cc0
		_host->openTrackFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseTrackFile: // closetrackfile('name.trk') -> FUN_00412070
		_host->closeTrackFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPlayTheme: // playtheme('name.trk') -> FUN_00412250: start the track's
	             // theme playlist on the theme channel (replaces current)
		_host->playTheme(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodSingleSound: // singlesound(name) -> FUN_004122d0/FUN_0042fa80
	case Script::kMethodMultipleSound: // multiplesound(name) -> FUN_00412310/FUN_0042fb20
	case Script::kMethodDualSound: // dualsound(name) -> FUN_00412350/FUN_0042fbc0
	case Script::kMethodBothSound: // bothsound(name) -> FUN_00412390/FUN_0042fc30
		_host->playSound(args.empty() ? Common::String() : args[0].strValue,
				opcode - Script::kMethodSingleSound);
		return true;
	case Script::kMethodVoiceSound: // voicesound(name) -> FUN_004123d0/FUN_0042fc70
		_host->playVoice(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodHaltSound: // haltsound(1|2|3) -> FUN_00412430/FUN_0042f690
		_host->haltSound(args.empty() ? 3 : args[0].intValue);
		return true;
	case Script::kMethodHaltTheme: // halttheme() -> FUN_00412410: stop the theme channel
		_host->haltTheme();
		return true;
	case Script::kMethodHaltVoice: // haltvoice() -> FUN_004124d0/FUN_0042f690
		_host->haltVoice();
		return true;
	case Script::kMethodThemeVol: // themevol('name.trk', vol 0-255) -> FUN_004125c0
		_host->themeVolume(args.size() > 0 ? args[0].strValue : Common::String(),
				args.size() > 1 ? args[1].intValue : 255);
		return true;
	case Script::kMethodWaveVolume: { // wavevolume([level 0..9]) -> FUN_00436670/FUN_00439df0
		int level = args.empty() ? 0 : args[0].intValue;
		const int *newLevel = args.empty() ? nullptr : &level;
		result = Value::makeInt(_host->waveVolume(newLevel));
		return true;
	}
	case Script::kMethodSoundVol: { // soundvol(name[, volume 0..255]) -> FUN_00412ad0/FUN_004125c0
		if (args.empty()) {
			result = Value::makeInt(0);
			return true;
		}
		int volume = args.size() > 1 ? args[1].intValue : 0;
		const int *newVolume = args.size() > 1 ? &volume : nullptr;
		result = Value::makeInt(_host->soundVolume(args[0].strValue, newVolume));
		return true;
	}
	case Script::kMethodCurrentTheme: // currenttheme(1|2) -> FUN_00412f20: 1 = playing cue name,
	             // 2 = its track file name; 'none' if silent
		result = Value::makeString(_host->currentTheme(args.empty() ? 1 : args[0].intValue));
		return true;
	case Script::kMethodCurrentSound: // currentsound(1|2|3) -> FUN_00412e60: active SFX cue or 'None'
		result = Value::makeString(_host->currentSound(args.empty() ? 1 : args[0].intValue));
		return true;
	case Script::kMethodCurrentVoice: // currentvoice() -> FUN_00412ff0: active voice cue or 'None'
		result = Value::makeString(_host->currentVoice());
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callActorMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenCastFile: // opencastfile('name.cst') -> FUN_0041f1c0
		_host->openCastFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseCastFile: // closecastfile('name.cst') -> FUN_004211b0
		_host->closeCastFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCountActors: // countactors() -> FUN_00420a70
		result = Value::makeInt(_host->countActors());
		return true;
	case Script::kMethodIndexToActor: // indextoactor(i) -> native 1-based actor lookup
		result = Value::makeString(_host->indexToActor(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodActorVisible: { // actorvisible(name[, flag]) -> FUN_00420f10/FUN_00420d30
		bool visible = args.size() > 1 && args[1].intValue != 0;
		const bool *newVisible = args.size() > 1 ? &visible : nullptr;
		result = !args.empty() ?
				Value::makeBool(_host->actorVisible(args[0].strValue, newVisible)) :
				Value::makeBool(false);
		return true;
	}
	case Script::kMethodActorDeg: { // actordeg(name[, deg]) -> FUN_0041fef0 / getter
		if (args.size() >= 2) {
			int deg = args[1].intValue;
			result = Value::makeInt(_host->actorDeg(args[0].strValue, &deg));
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->actorDeg(args[0].strValue, nullptr));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	}
	case Script::kMethodActorXYZ: // actorxyz(name, x, y, z) or actorxyz(name, selector)
		if (args.size() >= 4) {
			_host->actorXYZ(args[0].strValue, args[1].intValue,
					args[2].intValue, args[3].intValue);
		} else if (args.size() == 2) {
			result = Value::makeInt(_host->actorXYZ(args[0].strValue, args[1].intValue));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	case Script::kMethodActorStar: // actorstar(name[, scene]) -> FUN_0041fbb0
		if (args.size() >= 2)
			result = Value::makeString(_host->actorStar(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->actorStar(args[0].strValue, nullptr));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorPose: // actorpose(name[, pose]) -> FUN_0041fd70
		if (args.size() >= 2)
			result = Value::makeString(_host->actorPose(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->actorPose(args[0].strValue, nullptr));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorSet: // actorset(name[, set]) -> FUN_0041f970
		if (args.size() >= 2)
			result = Value::makeString(_host->actorSet(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->actorSet(args[0].strValue, nullptr));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorSpeed: // actorspeed(name, speed)
		if (args.size() >= 2)
			_host->actorSpeed(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorScale: // actorscale(name, scale)
		if (args.size() >= 2)
			_host->actorScale(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorTurn: // actorturn(name, turn)
		if (args.size() >= 2)
			_host->actorTurn(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorOwner: // actorowner(name[, owner]) -> FUN_00422210
		if (args.size() >= 2)
			result = Value::makeString(_host->actorOwner(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->actorOwner(args[0].strValue, nullptr));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorValue: { // actorvalue(name[, value]) -> FUN_004222d0
		if (args.size() >= 2) {
			int value = args[1].intValue;
			result = Value::makeInt(_host->actorValue(args[0].strValue, &value));
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->actorValue(args[0].strValue, nullptr));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	}
	case Script::kMethodActorZClip: // actorzclip(name, zclip)
		if (args.size() >= 2)
			_host->actorZClip(args[0].strValue, args[1].intValue);
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callPropMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenShopFile: // openshopfile('name.shp') -> FUN_00428450: parse the .SHP,
	             // then dispatch openshop() and per-prop openprop().
		_host->openShopFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseShopFile: // closeshopfile('name.shp') -> FUN_0042a7e0
		_host->closeShopFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPropInstance: // propinstance(source, newName) -> FUN_00428940
		if (args.size() >= 2)
			_host->propInstance(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodPropVisible: // propvisible(name, flag) -> FUN_00429d00
		if (args.size() >= 2)
			_host->propVisible(args[0].strValue, args[1].intValue != 0);
		if (args.size() == 1)
			result = Value::makeBool(_host->propVisible(args[0].strValue));
		return true;
	case Script::kMethodPropView: // propview(name, shape) -> FUN_004293a0
		if (args.size() >= 2)
			_host->propView(args[0].strValue, args[1].strValue);
		if (args.size() == 1)
			result = Value::makeString(_host->propView(args[0].strValue));
		return true;
	case Script::kMethodPropSet: // propset(name, set) -> FUN_00428c20
		if (args.size() >= 2)
			_host->propSet(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodPropXYZ: // propxyz(name, x, y, z) -> FUN_0042a140 (mode=1)
		if (args.size() >= 4)
			_host->propXYZ(args[0].strValue, args[1].intValue,
					args[2].intValue, args[3].intValue);
		return true;
	case Script::kMethodPropXY: // propxy(name, x, y) -> FUN_0042a370 (mode=0, depth=-1)
		if (args.size() >= 3)
			_host->setPropXY(args[0].strValue, args[1].intValue, args[2].intValue);
		if (args.size() == 2)
			result = Value::makeInt(_host->propXY(args[0].strValue, args[1].intValue));
		return true;
	case Script::kMethodPropScale: // propscale(name, scale) -> FUN_00429870
		if (args.size() >= 2)
			_host->propScale(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodPropZClip: // propzclip(name, dist) -> FUN_00428ea0
		if (args.size() >= 2)
			_host->propZClip(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodPropDist: // propdist(name, d) -> FUN_004295c0
		if (args.size() >= 2)
			_host->propDist(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodPropDeg: // propdeg(name[, deg]) -> FUN_00429730/FUN_00429520
		if (args.size() >= 2) {
			int deg = args[1].intValue;
			result = Value::makeInt(_host->propDeg(args[0].strValue, &deg));
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->propDeg(args[0].strValue, nullptr));
		}
		return true;
	case Script::kMethodPropOwner: // propowner(name[, owner]) -> FUN_00428d40: get or set
		if (args.size() >= 2)
			result = Value::makeString(_host->propOwner(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->propOwner(args[0].strValue, nullptr));
		return true;
	case Script::kMethodPropValue: // propvalue(name[, value]) -> FUN_004290d0/FUN_00428e00
		if (args.size() >= 2) {
			int value = args[1].intValue;
			result = Value::makeInt(_host->propValue(args[0].strValue, &value));
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->propValue(args[0].strValue, nullptr));
		}
		return true;
	case Script::kMethodCountProps: // countprops() -> FUN_0042b4f0: global count, all shops
		result = Value::makeInt(_host->countProps());
		return true;
	case Script::kMethodIndexToProp: // indextoprop(i) -> FUN_0042b550: 1-based global index
		result = Value::makeString(_host->indexToProp(args.empty() ? 0 : args[0].intValue));
		return true;
	default:
		return false;
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

	Value result;
	if (callCoreMethod(opcode, args, result))
		return result;

	// Forward the effectful builtins we implement to the engine host. Opcodes
	// not handled here remain logged no-ops (see files/method-catalog.md).
	// (The message-carrying send* builtins never reach this path; they are
	// routed through dispatchMessageBuiltin with the message unevaluated.)

	if (_host) {
		if (callAudioMethod(opcode, args, result))
			return result;
		if (callActorMethod(opcode, args, result))
			return result;
		if (callPropMethod(opcode, args, result))
			return result;

		switch (opcode) {
		case Script::kMethodMessage: // message(text) -> FUN_00446240
			_host->message(args.empty() ? Common::String() : args[0].strValue);
			break;
		case Script::kMethodPlayMovie: // playmovie('name.mov')
			_host->playMovie(args.empty() ? Common::String() : args[0].strValue);
			break;
		case Script::kMethodOpenPuppetFile: // openpuppetfile('name.pup') -> FUN_004473c0/FUN_00447470
			_host->openPuppetFile(args.empty() ? Common::String() : args[0].strValue);
			break;
		case Script::kMethodOpenStageFile: // openstagefile('name.stg')
			_host->openStageFile(args.empty() ? Common::String() : args[0].strValue);
			break;
		case Script::kMethodCloseStageFile: // closestagefile() -> FUN_00409330
			_host->closeStageFile();
			break;
		case Script::kMethodGotoFlat: // gotoflat(name|index) -> FUN_00409460
			if (!args.empty())
				_host->gotoFlat(args[0]);
			break;
		case Script::kMethodOpenSetFile: // opensetfile('name.set'[, scene[, view]]) -> FUN_00430690
			_host->openSetFile(args.size() > 0 ? args[0].strValue : Common::String(),
					args.size() > 1 ? args[1].strValue : Common::String(),
					args.size() > 2 ? args[2].strValue : Common::String());
			break;
		case Script::kMethodCloseSetFile: // closesetfile() -> TI.EXE set-archive close
			_host->closeSetFile();
			break;
		// (sendtoscene 0x2f02 never reaches here: it always appears with a
		// parenthesised message and routes through dispatchMessageBuiltin.)
		case Script::kMethodClut: // clut(name): snap the hardware palette (FUN_00446500)
			_host->setClut(args.empty() ? Common::String() : args[0].strValue);
			break;
		case Script::kMethodPuppetClear: // puppetclear(): native display-list clear, rendering pending
			_host->puppetClear();
			break;
		case Script::kMethodClosePuppetFile: // closepuppetfile() -> FUN_00447880
			_host->closePuppetFile();
			break;
		case Script::kMethodPuppetSpeak: // puppetspeak(name[, mode]) -> FUN_00447ce0/FUN_00448b60
			_host->puppetSpeak(args.empty() ? Common::String() : args[0].strValue,
					args.size() > 1 ? args[1].intValue : 0);
			break;
		case Script::kMethodPuppetBevel: // puppetbevel(name[, mode]) -> FUN_00447b30
			_host->puppetBevel(args.empty() ? Common::String() : args[0].strValue,
					args.size() > 1 ? args[1].intValue : 0);
			break;
		case Script::kMethodPuppetGrab: // puppetgrab(bool) -> FUN_00447e30 stores DAT_00461248.
			_host->puppetGrab(!args.empty() && isTruthy(args[0]));
			break;
		case Script::kMethodPuppetScript: // puppetscript(name) -> FUN_004482c0
			_host->puppetScript(args.empty() ? Common::String() : args[0].strValue);
			break;
		case Script::kMethodBlackScreen: // blackscreen(): fill the window with black pixels (FUN_00446b80)
			_host->blackScreen();
			break;
		case Script::kMethodForceUpdate: // forceupdate() -> FUN_00446910 -> FUN_00423a60
			_host->forceUpdate();
			break;
		case Script::kMethodFlushEvents: // flushevents() -> FUN_00446cb0/FUN_00405110
			_host->flushEvents();
			break;
		case Script::kMethodDrawString: // drawstring(text, point, color, size) -> FUN_004460c0
			if (args.size() >= 2)
				_host->drawString(args[0].strValue, args[1].intValue,
						args.size() > 2 ? args[2].intValue : 0,
						args.size() > 3 ? args[3].intValue : 12);
			break;
		case Script::kMethodQuit: // quit() -> FUN_00446c80: restore cursor, set native quit flag
			_host->requestQuit();
			break;
		case Script::kMethodBlackToScreen: // blacktoscreen(target, steps): palette fade black -> target
			_host->fadePalette(args.size() > 0 ? args[0].strValue : Common::String("current"),
					args.size() > 1 ? args[1].intValue : 1, false);
			break;
		case Script::kMethodScreenToBlack: // screentoblack(target, steps): palette fade target -> black
			_host->fadePalette(args.size() > 0 ? args[0].strValue : Common::String("current"),
					args.size() > 1 ? args[1].intValue : 1, true);
			break;
		case Script::kMethodVisualEffect: // visualeffect(effect, dur): set default transition (FUN_00446400)
			// The effect names ('plain', ...) are bare method opcodes 0x5dc1..
			// 0x5dd5 used as atoms; decodeAtom yields Value() for them, so the
			// effect code is not currently propagated. Only 'plain' is used by
			// the boot scripts; forward the duration.
			_host->setVisualEffect(0x5dce, args.size() > 1 ? args[1].intValue : 0);
			break;
		case Script::kMethodMakeLoop: // makeloop(kind, target, message, delay) -> FUN_00423e60
			if (args.size() >= 4)
				_host->makeLoop(args[0].strValue, args[1].strValue,
						args[2].strValue, args[3].intValue);
			break;
		case Script::kMethodStopLoop: // stoploop(kind[, target]) -> FUN_00446d30/FUN_00423bf0
			if (!args.empty())
				_host->stopLoop(args[0].strValue,
						args.size() > 1 ? args[1].strValue : Common::String());
			break;
		case Script::kMethodPauseLoop: // pauseloop(kind, flag)
			if (args.size() >= 2)
				_host->pauseLoop(args[0].strValue, args[1].intValue != 0);
			break;
		case Script::kMethodMakeCricket: // makecricket(name, x, y, dist, angle, delay) -> FUN_00425640
			if (!args.empty())
				_host->makeCricket(args[0].strValue);
			break;
		case Script::kMethodStopCricket: // stopcricket(name) -> FUN_00446db0/FUN_00423c80
			if (!args.empty())
				_host->stopCricket(args[0].strValue);
			break;
		case Script::kMethodPauseCricket: // pausecricket(kind, flag)
			if (args.size() >= 2)
				_host->pauseCricket(args[0].strValue, args[1].intValue != 0);
			break;
		case Script::kMethodCurrentSet: // currentset() -> open set name or 'none'
			return Value::makeString(_host->currentSet());
		case Script::kMethodCurrentStage: // currentstage() -> open stage name or native 'None'
			return Value::makeString(_host->currentStage());
		case Script::kMethodStageVisible: { // stagevisible([flag]) -> DAT_00461156
			bool visible = args.empty() ? false : (args[0].intValue != 0);
			const bool *newVisible = args.empty() ? nullptr : &visible;
			return Value::makeBool(_host->stageVisible(newVisible));
		}
		case Script::kMethodCurrentFlat: // currentflat() -> current stage node name or native 'None'
			return Value::makeString(_host->currentFlat());
		case Script::kMethodCurrentView: // currentview() -> current SET view name
			return Value::makeString(_host->currentView());
		case Script::kMethodCurrentScene: { // currentscene([name|left|right|strait])
			const Common::String *target = args.empty() ? nullptr : &args[0].strValue;
			return Value::makeString(_host->currentScene(target));
		}
		case Script::kMethodCurrentPuppet: // currentpuppet() -> current puppet name or 'none'
			return Value::makeString(_host->currentPuppet());
		case Script::kMethodPuppetParam: { // puppetparam(selector[, value]) -> FUN_00448730/FUN_004485f0
			if (args.empty())
				return Value::makeInt(0);
			if (args.size() >= 2) {
				int value = args[1].intValue;
				return Value::makeInt(_host->puppetParam(args[0].intValue, &value));
			}
			return Value::makeInt(_host->puppetParam(args[0].intValue, nullptr));
		}
		case Script::kMethodPuppetVisible: { // puppetvisible([flag]) -> FUN_00448550/FUN_004485b0
			bool visible = !args.empty() && args[0].intValue != 0;
			const bool *newVisible = args.empty() ? nullptr : &visible;
			return Value::makeBool(_host->puppetVisible(newVisible));
		}
		case Script::kMethodPuppetBase: { // puppetbase([name]) -> FUN_00447ee0
			const Common::String *base = args.empty() ? nullptr : &args[0].strValue;
			return Value::makeString(_host->puppetBase(base));
		}
		case Script::kMethodSetVisible: { // setvisible([flag]) -> current SET visibility flag
			bool visible = args.empty() ? false : (args[0].intValue != 0);
			const bool *newVisible = args.empty() ? nullptr : &visible;
			return Value::makeBool(_host->setVisible(newVisible));
		}
		case Script::kMethodActionFrame: // actionframe(n) -> bool, FUN_004362c0 (n must be 1 or 2)
			return Value::makeBool(_host->actionFrame(args.empty() ? 0 : args[0].intValue));
		case Script::kMethodFrameRate: { // framerate([n]) -> DAT_00461126
			int rate = args.empty() ? 0 : args[0].intValue;
			const int *newRate = args.empty() ? nullptr : &rate;
			return Value::makeInt(_host->frameRate(newRate));
		}
		case Script::kMethodKeyAborts: { // keyaborts([resource, key, flag]) -> FUN_00435a00/FUN_00446e10
			bool enabled = args.size() > 2 && args[2].intValue != 0;
			return Value::makeBool(_host->keyAborts(
					args.size() > 0 ? &args[0].strValue : nullptr,
					args.size() > 1 ? &args[1].strValue : nullptr,
					args.size() > 2 ? &enabled : nullptr));
		}
		case Script::kMethodPath: { // path(slot[, value]) -> FUN_004462a0/FUN_00438450
			int slot = args.empty() ? 0 : args[0].intValue;
			const Common::String *newPath = args.size() >= 2 ? &args[1].strValue : nullptr;
			return Value::makeString(_host->pathSlot(slot, newPath));
		}
		case Script::kMethodCurrentCD: { // currentcd([name]) -> FUN_00439df0/FUN_0043a290
			const Common::String *requested = args.empty() ? nullptr : &args[0].strValue;
			return Value::makeString(_host->currentCD(requested));
		}
		case Script::kMethodPointX: // pointx(point) -> high word of packed (x << 16) | y
			return Value::makeInt(args.empty() ? 0 : (int16)(args[0].intValue >> 16));
		case Script::kMethodPointY: // pointy(point) -> low word of packed (x << 16) | y
			return Value::makeInt(args.empty() ? 0 : (int16)(args[0].intValue & 0xffff));
		case Script::kMethodMakePoint: // makepoint(x, y) -> packed (x << 16) | y
			return Value::makeInt(_host->makePoint(args.size() > 0 ? args[0].intValue : 0,
					args.size() > 1 ? args[1].intValue : 0));
		case Script::kMethodButton: // button() -> FUN_00436880: live left mouse button state
			return Value::makeBool(_host->buttonDown());
		case Script::kMethodStillDown: // stilldown() -> FUN_00436920: live mouse button state
			return Value::makeBool(_host->stillDown());
		case Script::kMethodTick: // tick() -> FUN_004368f0: native 60 Hz timer
			return Value::makeInt(_host->tick());
		case Script::kMethodQuestionDialog: // questiondialog(text) -> FUN_004363f0/FUN_00409030
			return Value::makeBool(_host->questionDialog(
					args.empty() ? Common::String() : args[0].strValue));
		case Script::kMethodOptionKey: // optionkey() -> FUN_004376e0/GetAsyncKeyState(VK_SHIFT)
			return Value::makeBool(_host->optionKey());
		case Script::kMethodCountPaintings: // countpaintings(scene, view) -> FUN_00431fe0
			if (args.size() >= 2)
				return Value::makeInt(_host->countPaintings(args[0].strValue, args[1].strValue));
			return Value::makeInt(0);
		case Script::kMethodIndexToPainting: // indextopainting(scene, view, index) -> FUN_00432120, 1-based
			if (args.size() >= 3)
				return Value::makeString(_host->indexToPainting(args[0].strValue,
						args[1].strValue, args[2].intValue));
			break;
		case Script::kMethodRoadAhead: // roadahead(scene, view) -> FUN_00431bd0/FUN_004337b0
			if (args.size() >= 2)
				return Value::makeBool(_host->roadAhead(args[0].strValue, args[1].strValue));
			return Value::makeBool(false);
		case Script::kMethodCountPuppets: // countpuppets() -> FUN_00448380: PUP resource-2 script count
			return Value::makeInt(_host->countPuppets());
		case Script::kMethodIndexToPuppet: // indextopuppet(i) -> FUN_004483f0, 1-based
			return Value::makeString(_host->indexToPuppet(args.empty() ? 0 : args[0].intValue));
		case Script::kMethodPuppetEvent: // puppetevent(timeout) -> FUN_00449e40 waits for a clicked bevel id
			return Value::makeInt(_host->puppetEvent(args.empty() ? -1 : args[0].intValue));
		case Script::kMethodPointInButton: // pointinbutton(flat, button, point) -> FUN_0040a0d0
			if (args.size() >= 3)
				return Value::makeBool(_host->pointInButton(args[0].strValue,
						args[1].strValue, args[2].intValue));
			return Value::makeBool(false);
		case Script::kMethodPointInPainting: // pointinpainting(scene, view, painting, point) -> FUN_004322e0
			if (args.size() >= 4)
				return Value::makeBool(_host->pointInPainting(args[0].strValue,
						args[1].strValue, args[2].strValue, args[3].intValue));
			return Value::makeBool(false);
		case Script::kMethodHitTest: // hittest(point) -> FUN_00435e70: name of the topmost item
		             // under the packed point; kind stored for result()
			if (!args.empty())
				return Value::makeString(_host->hitTest(args[0].intValue));
			break;
		case Script::kMethodCalcDeg: // calcdeg(pointA, pointB) -> FUN_00435c70
			if (args.size() >= 2)
				return Value::makeInt(_host->calcDeg(args[0].intValue, args[1].intValue));
			return Value::makeInt(0);
		case Script::kMethodStarXYZ: // starxyz(name, selector) -> FUN_00435a60/FUN_00432fc0
			if (args.size() >= 2)
				return Value::makeInt(_host->starXYZ(args[0].strValue, args[1].intValue));
			return Value::makeInt(0);
		case Script::kMethodCalcMod: // calcmod(a, b) -> FUN_004358f0
			if (args.size() >= 2)
				return Value::makeInt(_host->calcMod(args[0].intValue, args[1].intValue));
			return Value::makeInt(0);
		case Script::kMethodResult: // result() -> FUN_004366a0: last hittest kind (DAT_00461298)
			return Value::makeString(_host->hitTestResult());
		case Script::kMethodMouse: // mouse() -> FUN_004368b0: current mouse point, packed
			return Value::makeInt(_host->mousePoint());
		case Script::kMethodCursor: // cursor(id|name) -> FUN_00446920. Resource-name mapping
		             // verified against TI.EXE (see VMHost::setCursorResource):
		             // int -> CURS<n>; "arrow" -> CURS.ARROW; "watch" ->
		             // CURS2002; other names -> CURS.<NAME>.
			if (!args.empty()) {
				Common::String res;
				if (args[0].type == Value::kInt)
					res = Common::String::format("CURS%d", args[0].intValue);
				else if (args[0].strValue.equalsIgnoreCase("arrow"))
					res = "CURS.ARROW";
				else if (args[0].strValue.equalsIgnoreCase("watch"))
					res = "CURS2002";
				else
					res = Common::String("CURS.") + args[0].strValue;
				res.toUppercase();
				_host->setCursorResource(res);
			}
			break;
		case Script::kMethodSaveGame: // savegame(signature) -> FUN_00426620/FUN_00426790
			_host->saveGame(args.empty() ? Common::String() : args[0].strValue);
			break;
		case Script::kMethodOpenGame: // opengame(signature) -> FUN_004266e0/FUN_00426f00
			_host->openGame(args.empty() ? Common::String() : args[0].strValue);
			break;
		default:
			break;
		}
	}
	return Value();
}

Value ScriptVM::evaluateExpression(const Script &script, uint32 &pc) {
	return evaluateExpression(script, pc, 1);
}

Value ScriptVM::evaluateExpression(const Script &script, uint32 &pc, uint8 minBindingPower) {
	// ScummVM-only optimization of TI.EXE's expression reducer: native stores
	// atom/operator lists and then reduces precedence classes, but using the
	// same recovered precedence table as binding powers avoids per-expression
	// Common::Array allocations in idle/hittest hot paths.
	Value lhs = decodeAtom(script, pc);
	while (pc < script.getInstructionCount()) {
		const uint16 op = script.getInstruction(pc).opcode;
		if (!Script::isOperator(op))
			break;

		const uint8 prec = Script::operatorPrecedence(op);
		const uint8 bindingPower = 7 - prec; // native precedence 0 is tightest
		if (bindingPower < minBindingPower)
			break;

		pc++;
		Value rhs = evaluateExpression(script, pc, bindingPower + 1);
		lhs = applyBinary(op, lhs, rhs);
	}

	return lhs;
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
	const bool logTransitionDispatch = shouldLogTransitionDispatch(name);

	if (_callDepth >= 64) { // TI.EXE has no explicit guard; protect the engine
		warning("Cyberflix: script call depth overflow at '%s'", name.c_str());
		return result;
	}

	for (int li = (int)_libraries.size() - 1; li >= 0; --li) {
		const Script *lib = _libraries[li];
		const Script::Definition *def = lib->findDefinition(name);
		if (!def)
			continue;
		if (logTransitionDispatch)
			debug(1, "Cyberflix: script '%s' matched scope %d/%u body %u (%u args)",
					name.c_str(), li, _libraries.size(), def->bodyStart, args.size());

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
		if (logTransitionDispatch)
			debug(1, "Cyberflix: script '%s' handled by scope %d -> %s",
					name.c_str(), li, result.toString().c_str());
		if (_trace)
			debug(0, "  dispatch %s(%u args) -> %s", name.c_str(), args.size(),
					result.toString().c_str());
		return result;
	}

	if (logTransitionDispatch)
		debug(1, "Cyberflix: script '%s' had no matching definition (%u scopes)",
				name.c_str(), _libraries.size());
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
				Common::String var = script.getSelfRelStringLowercase(pc);
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

		case Script::kOpSwitch: {
			// 0x0fa9 'switch <selector>' (TI.EXE 0x0040bcd5): evaluate the
			// selector (int or string only, err 0xe otherwise), then scan
			// forward for a matching case value (FUN_0040c750): nested
			// switches are skipped; at depth 0 each kOpCase's value expr is
			// evaluated and compared (int ==, string case-insensitive via
			// 0x41ae80). On a match, consecutive extra `case <value>` clauses
			// sharing the body are skipped (FUN_0040c930) and execution jumps
			// to the body. If the closing kOpEndSwitch is reached with no
			// match, execution resumes just past it (FUN_0040c610).
			uint32 selPc = pc + 1;
			Value sel = evaluateExpression(script, selPc);
			uint32 scan = selPc;
			int depth = 0;
			int target = -1;
			while (scan < count) {
				uint16 sop = script.getInstruction(scan).opcode;
				if (sop == Script::kOpEnd || sop == Script::kOpReturn || sop == Script::kOpScriptMarker)
					break; // unterminated switch: TI.EXE errs 0x1f/0x1b
				if (sop == Script::kOpSwitch) {
					depth++;
					scan++;
				} else if (sop == Script::kOpEndSwitch) {
					if (depth == 0) {
						target = (int)scan + 1; // no case matched: past 0xfaa
						break;
					}
					depth--;
					scan++;
				} else if (sop == Script::kOpCase && depth == 0) {
					uint32 vp = scan + 1;
					Value cv = evaluateExpression(script, vp);
					bool match;
					if (sel.type == Value::kString || sel.type == Value::kSymbol)
						match = (cv.type == Value::kString || cv.type == Value::kSymbol) &&
								sel.strValue.equalsIgnoreCase(cv.strValue);
					else
						match = (cv.type != Value::kString && cv.type != Value::kSymbol) &&
								sel.intValue == cv.intValue;
					if (match) {
						// FUN_0040c930: skip immediately following extra
						// `case <value>` clauses (multi-value case bodies).
						uint32 body = vp;
						for (;;) {
							while (body < count && script.getInstruction(body).opcode == Script::kOpPushInt)
								body++;
							if (body < count && script.getInstruction(body).opcode == Script::kOpCase) {
								uint32 p2 = body + 1;
								evaluateExpression(script, p2); // skip value expr
								body = p2;
							} else {
								break;
							}
						}
						target = (int)body;
						break;
					}
					scan = vp; // continue scanning past the value expr
				} else {
					scan++;
				}
			}
			pc = (target >= 0) ? (uint32)target : count;
			break;
		}

		case Script::kOpCase: {
			// A matched case body ran into the next case label: C-style break,
			// skip past the closing kOpEndSwitch (TI.EXE 0x0040bd7c +
			// FUN_0040c610). No fall-through between cases.
			int endSwitch = script.findEndSwitchFrom(pc + 1);
			pc = (endSwitch >= 0) ? (uint32)endSwitch + 1 : count;
			break;
		}

		case Script::kOpEndSwitch:
			// End of the last case body (TI.EXE 0x0040bd63): just continue.
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
			Common::String loopVar = script.getSelfRelStringLowercase(p);
			p++;
			if (p < count && script.getInstruction(p).opcode == Script::kOpEq)
				p++; // '=' separator
			Value start = evaluateExpression(script, p);
			if (p < count && script.getInstruction(p).opcode == Script::kOpForTo)
				p++; // 'to' separator
			Value end = evaluateExpression(script, p);
			uint32 bodyStart = p;

			// The for-header BINDS the loop variable into the current local
			// scope (TI.EXE 0x0040bda9 via FUN_00413710/FUN_00413610), it does
			// not assign through the lookup chain: an undeclared loop var is
			// local to the running body, so loops in dispatched callees (prop
			// initprop() handlers also use `count`) cannot clobber it.
			{
				Common::String key = loopVar;
				if (!_locals.empty())
					_locals.back()[key] = Value::makeInt(start.intValue);
				else
					setVar(loopVar, Value::makeInt(start.intValue));
			}
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
					Common::String var = script.getSelfRelStringLowercase(pc);
					p++;
					Value v = evaluateExpression(script, p);
					setVar(var, v);
					if (_trace)
						debug(0, "    assign %s = %s", var.c_str(), v.toString().c_str());
					pc = p;
					break;
				}
				if (nextOp == Script::kOpOpenParen) {
					Common::String fn = script.getSelfRelStringLowercase(pc);
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

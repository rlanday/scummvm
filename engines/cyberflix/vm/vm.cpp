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

ScriptVM::ScriptVM() : _pc(0), _executed(0), _trace(false), _host(nullptr),
		_movieHost(nullptr), _navigationHost(nullptr), _systemHost(nullptr),
		_audioHost(nullptr), _inputHost(nullptr), _actorHost(nullptr),
		_interactionHost(nullptr), _saveHost(nullptr), _callDepth(0) {
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
	return static_cast<int32>(strtol(text.c_str(), nullptr, 10));
}

static Common::String findWord(const Common::String &text, const Common::String &delimiter,
		int32 wordIndex) {
	if (wordIndex < 1)
		return Common::String();

	if (delimiter.empty()) {
		if (static_cast<uint32>(wordIndex) > text.size())
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
		push(Value::makeInt(static_cast<int16>(inst.operandA)));
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
		return Value::makeInt(static_cast<int16>(inst.operandA));

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
		return Value::makeInt(static_cast<int32>(script.getSplitOperand(pc - 1)));
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
			_navigationHost->sendToStage(*message, msgArgs);
		break;
	case Script::kMethodSendToStageFx:
		if (_host)
			return _navigationHost->sendToStageFx(*message, msgArgs);
		break;
	case Script::kMethodSendToBoot: // sendtoboot(message(...)) -> TI.EXE FUN_00439080/FUN_004390a0
		if (_host)
			_navigationHost->sendToBoot(*message, msgArgs);
		break;
	case Script::kMethodSendToBootFx:
		if (_host)
			return _navigationHost->sendToBootFx(*message, msgArgs);
		break;
	case Script::kMethodSendToShop: // sendtoshop('file.shp', message) -> TI.EXE FUN_0042b2b0:
	             // dispatch against [shop script, BOOTFILE res2].
		if (_host)
			_interactionHost->sendToShop(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToShopFx: // sendtoshopfx('file.shp', message) -> return dispatch result.
		if (_host)
			return _interactionHost->sendToShopFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToProp: // sendtoprop('propname', message) -> TI.EXE FUN_0042ae80:
	             // dispatch against [prop script, shop script, BOOTFILE res2].
		if (_host)
			_interactionHost->sendToProp(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToPropFx:
		if (_host)
			return _interactionHost->sendToPropFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToActor: // sendtoactor(actor, message) -> actor script, then cast script.
		if (_host)
			_actorHost->sendToActor(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToActorFx:
		if (_host)
			return _actorHost->sendToActorFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToCast: // sendtocast('file.cst', message) -> per-cast script dispatch.
		if (_host)
			_actorHost->sendToCast(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToCastFx:
		if (_host)
			return _actorHost->sendToCastFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToPuppet: // sendtopuppet(target, message) -> [PUP script, BOOTFILE res2].
		if (_host)
			_navigationHost->sendToPuppet(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToPuppetFx:
		if (_host)
			return _navigationHost->sendToPuppetFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToScene: // sendtoscene(scene, message) -> TI.EXE FUN_004311e0/
	             // FUN_00431200: dispatch against the scene's script chain.
		if (_host)
			_navigationHost->sendToScene(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToSceneFx:
		if (_host)
			return _navigationHost->sendToSceneFx(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToPainting: // sendtopainting(scene, view, painting, message) -> FUN_00432550/FUN_00432570
		if (_host)
			_navigationHost->sendToPainting(target0, target1, target2, *message, msgArgs);
		break;
	case Script::kMethodSendToPaintingFx:
		if (_host)
			return _navigationHost->sendToPaintingFx(target0, target1, target2, *message, msgArgs);
		break;
	case Script::kMethodSendToSetFx:
		if (_host)
			return _navigationHost->sendToSetFx(*message, msgArgs);
		break;
	case Script::kMethodSendToButton: // sendtobutton(flat, button, message) -> FUN_0040a410/FUN_0040a430:
	             // chain [button script, node script, stage script, res2].
		if (_host)
			_navigationHost->sendToButton(target0, target1, *message, msgArgs);
		break;
	case Script::kMethodSendToButtonFx:
		if (_host)
			return _navigationHost->sendToButtonFx(target0, target1, *message, msgArgs);
		break;
	case Script::kMethodSendToFlat: // sendtoflat(flat, message) -> FUN_0040a940/FUN_0040a960
		if (_host)
			_navigationHost->sendToFlat(target0, *message, msgArgs);
		break;
	case Script::kMethodSendToFlatFx:
		if (_host)
			return _navigationHost->sendToFlatFx(target0, *message, msgArgs);
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
		pc = (close >= 0) ? static_cast<uint32>(close) + 1 : count;
		break;
	}
}

bool ScriptVM::callCoreMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodRandom: // random(n) -> FUN_004366c0/FUN_0041b060
		result = Value::makeInt(_systemHost ? _systemHost->randomNumber(args.empty() ? 0 : args[0].intValue) : 0);
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

Value ScriptVM::evaluateExpression(const Script &script, uint32 &pc) {
	return evaluateExpression(script, pc, 1);
}

Value ScriptVM::evaluateExpression(const Script &script, uint32 &pc, uint8 minBindingPower) {
	// ScummVM-only optimization of TI.EXE's expression reducer. The native
	// interpreter uses a two-pass list reducer: it first scans the infix
	// expression into temporary atom/operator lists, then walks the recovered
	// precedence classes from tightest to loosest, replacing each matching
	// "lhs op rhs" span with the computed atom. For example, "a + b * c" is
	// stored as atoms [a, b, c] and operators [+, *]; the multiply pass
	// collapses "b * c", then the add pass combines that result with "a".
	//
	// ScummVM uses the same recovered precedence table as binding powers in a
	// precedence-climbing parser, so it can reduce while it scans. For
	// "a + b * c", the outer call decodes "a" as lhs and sees "+". It then
	// recurses for the right-hand side with a higher minimum binding power; that
	// nested call decodes "b", sees that "*" binds tightly enough to stay inside
	// the nested call, consumes "c", and returns "(b * c)". The outer call then
	// applies "+" to produce "a + (b * c)". If the nested call saw another "+"
	// instead, it would stop and leave that operator for the outer level,
	// preserving the native left-to-right reduction order for equal-precedence
	// operators.
	//
	// The parse result matches the native reducer, but this avoids allocating
	// per-expression Common::Array temporaries in idle/hittest hot paths.
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

	for (int li = static_cast<int>(_libraries.size()) - 1; li >= 0; --li) {
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
					pc = static_cast<uint32>(elseStart); // enter ELSE block
				} else {
					int endIf = script.findMatchingEndIf(here);
					pc = (endIf >= 0) ? static_cast<uint32>(endIf) + 1 : count;
				}
			}
			break;
		}

		case Script::kOpElse: {
			// Reached only after a THEN block executed; skip the else body.
			int endIf = script.findEndIfFrom(pc + 1);
			pc = (endIf >= 0) ? static_cast<uint32>(endIf) + 1 : count;
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
						target = static_cast<int>(scan) + 1; // no case matched: past 0xfaa
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
						target = static_cast<int>(body);
						break;
					}
					scan = vp; // continue scanning past the value expr
				} else {
					scan++;
				}
			}
			pc = (target >= 0) ? static_cast<uint32>(target): count;
			break;
		}

		case Script::kOpCase: {
			// A matched case body ran into the next case label: C-style break,
			// skip past the closing kOpEndSwitch (TI.EXE 0x0040bd7c +
			// FUN_0040c610). No fall-through between cases.
			int endSwitch = script.findEndSwitchFrom(pc + 1);
			pc = (endSwitch >= 0) ? static_cast<uint32>(endSwitch) + 1 : count;
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
				pc = (endWhile >= 0) ? static_cast<uint32>(endWhile) + 1 : count;
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
				pc = (forNext >= 0) ? static_cast<uint32>(forNext) + 1 : count;
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

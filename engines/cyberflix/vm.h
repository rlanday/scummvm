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

#ifndef CYBERFLIX_VM_H
#define CYBERFLIX_VM_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

#include "cyberflix/script.h"

namespace Cyberflix {

/**
 * Tagged value held on the CyberFlix VM operand stack.
 *
 * The precise runtime value model of the "Bicycle" VM is still being recovered
 * from TI.EXE; this is the structural skeleton the
 * confirmed opcodes operate on and will be refined as the evaluator at
 * 0x00419cf0 is mapped. Keep changes additive so the disassembler/trace stays
 * usable while semantics are filled in.
 */
struct Value {
	enum Type {
		kInt = 0,    ///< Integer constant (pushInt / arithmetic result).
		kSymbol,     ///< Reference to a symbol in the script string pool.
		kString,     ///< String constant from the pool.
		kBool        ///< Boolean (result of comparison/logical operators).
	};

	Type type;
	int32 intValue;
	Common::String strValue;

	Value() : type(kInt), intValue(0) {}
	static Value makeInt(int32 v) { Value r; r.type = kInt; r.intValue = v; return r; }
	static Value makeBool(bool v) { Value r; r.type = kBool; r.intValue = v ? 1 : 0; return r; }
	static Value makeSymbol(const Common::String &s) { Value r; r.type = kSymbol; r.strValue = s; return r; }
	static Value makeString(const Common::String &s) { Value r; r.type = kString; r.strValue = s; return r; }

	Common::String toString() const;
};

/**
 * Pack a screen point the way every native point value is stored:
 * (x << 16) | (y & 0xffff). Computed in unsigned arithmetic because
 * left-shifting a negative value is undefined behavior before C++20.
 */
inline int32 packPoint(int x, int y) {
	return static_cast<int32>((static_cast<uint32>(static_cast<uint16>(x)) << 16) |
			static_cast<uint32>(static_cast<uint16>(y)));
}

class CyberflixEngine;

/**
 * Executes a parsed CyberFlix Script. This is the structural harness that
 * mirrors the TI.EXE interpreter: a program counter indexing the 8-byte
 * instruction array and an operand stack. Opcode semantics are added
 * incrementally; unimplemented opcodes are logged (in trace mode) and skipped
 * so the VM can be stepped over real scripts during reverse engineering.
 */
class ScriptVM {
public:
	ScriptVM();

	void setTrace(bool on) { _trace = on; }

	/**
	 * Attach the engine that realises effectful builtins (playMovie, ...).
	 * Without a host, those builtins are logged no-ops, which keeps the VM
	 * usable as a standalone reverse-engineering harness (the debug console
	 * runs hostless ScriptVMs). Not owned.
	 */
	void setHost(CyberflixEngine *host) { _host = host; }

	/** Run @p script from the top until the terminator or a step budget. */
	void run(const Script &script, uint32 maxSteps = 100000);

	/**
	 * Execute @p script as a statement program: a statement loop that handles
	 * the control-flow builtins (if/else/endif/return) using the verified block
	 * scanners, evaluating conditions through the expression evaluator. Other
	 * statements (method calls, assignments) are consumed as expressions for
	 * now; their side effects land as the subsystems are implemented.
	 *
	 * Returns the number of statements executed. Mirrors the TI.EXE main loop
	 * at vaddr 0x0040ba4f.
	 */
	uint32 runProgram(const Script &script, uint32 maxSteps = 100000);

	/**
	 * Register @p script as a function library on the dispatch scope chain
	 * (later additions are searched FIRST, mirroring the 2-entry chain built by
	 * TI.EXE FUN_0040ad80: [stage script, BOOTFILE res2 global library]).
	 * @p script is retained, not owned, and must outlive the VM's use.
	 */
	void addLibrary(const Script *script) {
		_libraries.push_back(script);
		_librarySelf.push_back(Common::String());
		_libraryProp.push_back(Common::String());
	}
	struct LibraryState {
		Common::Array<const Script *> libraries;
		Common::Array<Common::String> self;
		Common::Array<Common::String> prop;
	};

	/**
	 * Replace the whole dispatch scope chain, returning the previous one so a
	 * caller can restore it. Each TI.EXE dispatch passes a complete, freshly
	 * built chain to the call executor FUN_0040b690 — e.g. sendtoprop
	 * (FUN_0042ae80) builds exactly [prop script, shop script, BOOTFILE res2]
	 * and boot messages (FUN_004390a0) exactly [BOOTFILE res1, BOOTFILE res2]
	 * — so inner dispatches REPLACE the outer chain rather than stack on it.
	 */
	LibraryState swapLibrariesWithContexts(const Common::Array<const Script *> &libs,
			const Common::Array<Common::String> &self,
			const Common::Array<Common::String> &prop) {
		LibraryState prev;
		prev.libraries.swap(_libraries);
		prev.self.swap(_librarySelf);
		prev.prop.swap(_libraryProp);
		_libraries = libs;
		_librarySelf = self;
		_libraryProp = prop;
		_librarySelf.resize(_libraries.size());
		_libraryProp.resize(_libraries.size());
		return prev;
	}

	/**
	 * Hot-path variant for fixed native dispatch chains. @p scope1 is searched
	 * first, then @p scope2, @p scope3, and finally @p tail. The VM stores the
	 * chain in reverse because callFunction() walks from newest to oldest; this
	 * helper centralizes that ordering and avoids caller-side temporary arrays.
	 */
	LibraryState swapLibrariesFixed(const Script *scope1,
			const Script *scope2, const Script *scope3, const Script *tail) {
		LibraryState prev;
		prev.libraries.swap(_libraries);
		prev.self.swap(_librarySelf);
		prev.prop.swap(_libraryProp);
		_libraries.reserve(4);
		_librarySelf.reserve(4);
		_libraryProp.reserve(4);
		if (tail)
			_libraries.push_back(tail);
		if (scope3)
			_libraries.push_back(scope3);
		if (scope2)
			_libraries.push_back(scope2);
		if (scope1)
			_libraries.push_back(scope1);
		_librarySelf.resize(_libraries.size());
		_libraryProp.resize(_libraries.size());
		return prev;
	}

	void restoreLibraries(LibraryState &state) {
		_libraries.swap(state.libraries);
		_librarySelf.swap(state.self);
		_libraryProp.swap(state.prop);
	}

	/**
	 * Set the dispatch context strings read by the atom opcodes 0xfba ("self":
	 * the name of the prop/shop/stage whose script is running, chain entry
	 * +0x1e) and 0xfbb (the target prop's name, chain entry +0x3e). TI.EXE
	 * builds these per scope-chain entry in FUN_0042ae80/FUN_0040ad80; prop
	 * scripts use 0xfba as the name argument to propvisible/propxy/propview.
	 */
	void setDispatchContext(const Common::String &self, const Common::String &targetProp) {
		_ctxSelf = self;
		_ctxProp = targetProp;
	}
	const Common::String &contextSelf() const { return _ctxSelf; }
	const Common::String &contextProp() const { return _ctxProp; }

	/**
	 * Dispatch the message `name(args)` against the library chain: find a
	 * matching definition (TI.EXE FUN_0040b7a0/FUN_0040b870), bind the formal
	 * params to @p args in a fresh local scope, and run the body with the
	 * statement executor (FUN_0040ba20). A kOpPass statement makes the
	 * definition "pretend unhandled" and the next library is tried.
	 *
	 * @param handled optionally receives whether any definition ran to
	 *        completion (false = no match anywhere on the chain).
	 * @return the kOpReturnValue result, or a default Value.
	 */
	Value callFunction(const Common::String &name, const Common::Array<Value> &args,
			bool *handled = nullptr);

	const Common::HashMap<Common::String, Value> &globalVars() const { return _vars; }
	Common::HashMap<Common::String, Value> &globalVars() { return _vars; }

	/** True while a script body is executing (at any nesting depth). The
	 *  engine uses this to defer GMM loads that would destroy objects whose
	 *  scripts are still on the C++ stack. */
	bool executing() const { return _bodyDepth > 0; }

private:
	void execute(const Script &script, uint32 index);
	void applyOperator(uint16 opcode);

	/// Result codes of the statement executor, mirroring TI.EXE FUN_0040ba20.
	enum RunResult {
		kRunDone,     ///< Body finished (kOpReturn / marker / end).
		kRunReturned, ///< kOpReturnValue produced a result.
		kRunPassed    ///< kOpPass: pretend no match, try next scope (code 4).
	};

	/**
	 * Run one definition body (or a whole flat program when @p stopAtMarker is
	 * false) starting at @p pc. The single statement loop behind runProgram and
	 * callFunction; mirrors TI.EXE FUN_0040ba20.
	 */
	RunResult runBody(const Script &script, uint32 pc, Value &result, uint32 maxSteps);
	static bool isTruthy(const Value &v) {
		return (v.type == Value::kBool || v.type == Value::kInt) && v.intValue != 0;
	}

	/**
	 * Read a variable by name from the current scope. Unbound names yield a
	 * symbol Value (harmless when used as data); bound names yield their stored
	 * value. Mirrors the TI.EXE scope lookup at 0x004138f0 (object entries of
	 * 0x20 bytes, name at +0x10), reduced to a flat name->value map until the
	 * object scope chain is modelled.
	 */
	Value getVar(const Common::String &name) const;
	Value getVar(const Common::String &name, const Common::String &lowerName) const;

	/** Store @p v under @p name in the current scope (TI.EXE store 0x00413610). */
	void setVar(const Common::String &name, const Value &v);


	/**
	 * Evaluate the expression starting at instruction @p pc, advancing @p pc
	 * past every instruction consumed. Mirrors the TI.EXE evaluator 0x00419cf0:
	 * an atom followed by zero or more (operator, atom) pairs, reduced by the
	 * recovered operator precedence.
	 */
	Value evaluateExpression(const Script &script, uint32 &pc);
	Value evaluateExpression(const Script &script, uint32 &pc, uint8 minBindingPower);

	/** Decode a single atom (literal/symbol/call) at @p pc, advancing @p pc.
	 *  Bounds-checks @p pc and the recursion depth, then defers to the inner
	 *  decoder. Every operand read in the expression layer funnels through here. */
	Value decodeAtom(const Script &script, uint32 &pc);
	Value decodeAtomInner(const Script &script, uint32 &pc);

	/**
	 * Handle a message-carrying builtin (sendtostage/sendtocast/sendtoshop/
	 * sendtoactor) whose kOpOpenParen is at @p pc: evaluate leading target
	 * args, capture the trailing message UNevaluated and route it (TI.EXE
	 * FUN_0040ad80 -> dispatcher FUN_0040b690). Advances @p pc past ')'.
	 */
	Value dispatchMessageBuiltin(const Script &script, uint32 &pc, uint16 opcode);

	/**
	 * Parse a call argument list whose opening kOpOpenParen is at @p pc,
	 * evaluating each comma-separated argument and advancing @p pc past the
	 * closing kOpCloseParen. Mirrors the argument scan at TI.EXE 0x0040b690.
	 */
	void parseCallArgs(const Script &script, uint32 &pc, Common::Array<Value> &outArgs);

	/**
	 * Dispatch a builtin method @p opcode with already-evaluated @p args. The
	 * named handlers are filled in per subsystem; unimplemented opcodes are
	 * logged (in trace mode) and return a placeholder. @p name is the source
	 * symbol when the call head was a symbol atom, else empty.
	 */
	Value callMethod(uint16 opcode, const Common::String &name, const Common::Array<Value> &args);
	bool callCoreMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callAudioMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callActorMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callPropMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callPuppetMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callStageSetMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callInputMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callRuntimeMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);

	/** Apply @p opcode to two operand values (no stack involved). */
	Value applyBinary(uint16 opcode, const Value &lhs, const Value &rhs);

	void push(const Value &v) { _stack.push_back(v); }
	Value pop();

	Common::Array<Value> _stack;
	Common::Array<uint32> _whileStack; ///< Saved condition-start PCs for active whiles.

	/**
	 * Active for-loop frames. Each binds a loop variable that is incremented
	 * and compared against an inclusive upper bound at kOpForNext, looping back
	 * to the body start or falling through (TI.EXE for-setup 0x0040bda9,
	 * frame stack 0x45ed48).
	 */
	struct ForFrame {
		Common::String var; ///< Loop-variable name.
		int32 end;          ///< Inclusive upper bound.
		uint32 bodyStart;   ///< Instruction index of the body's first statement.
	};
	Common::Array<ForFrame> _forStack;

	/// Global variable scope (the TI.EXE global object at [0x45f010]).
	Common::HashMap<Common::String, Value> _vars;

	/**
	 * Local-scope stack: one map per active function call, created by the
	 * dispatcher to bind formal parameters and kOpDeclLocal variables (TI.EXE
	 * builds a fresh scope object per dispatch in FUN_0040b870; lookup checks
	 * local first via 0x4138f0, then global). Empty at top level.
	 */
	Common::Array<Common::HashMap<Common::String, Value> > _locals;

	/// Function-library scope chain for callFunction (latest searched first).
	Common::Array<const Script *> _libraries;
	Common::Array<Common::String> _librarySelf;
	Common::Array<Common::String> _libraryProp;
	uint32 _callDepth; ///< Recursion guard for script-to-script calls.
	uint32 _exprDepth; ///< Recursion guard for nested expression atoms.
	uint32 _bodyDepth; ///< Active runBody() nesting (see executing()).

	uint32 _pc;
	uint32 _executed; ///< Statements executed by the last runProgram (for the console).
	bool _trace;
	/// Unimplemented builtin opcodes already logged once via callMethod's
	/// fall-through, so ~128 stubbed opcodes don't spam every frame. Maps
	/// opcode -> true once reported.
	Common::HashMap<uint16, bool> _reportedOpcodes;
	CyberflixEngine *_host; ///< Engine host for effectful builtins; null = no-op. Not owned.

	/// Dispatch context for the atoms 0xfba/0xfbb (see setDispatchContext).
	Common::String _ctxSelf;
	Common::String _ctxProp;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_VM_H

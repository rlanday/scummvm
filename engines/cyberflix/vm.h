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
 * from TI.EXE (see files/vm-re-notes.md); this is the structural skeleton the
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
 * Host interface through which the VM drives engine subsystems (video, audio,
 * navigation, ...). The interpreter itself is engine-agnostic and only knows
 * how to evaluate scripts; effectful builtins are forwarded here so the engine
 * can realise them. A null host (the default) makes every effectful builtin a
 * logged no-op, which keeps the VM usable as a standalone reverse-engineering
 * harness.
 */
class VMHost {
public:
	virtual ~VMHost() {}

	/** Play the movie named @p name (a MOVIES/ basename, e.g. "logo.mov"). */
	virtual void playMovie(const Common::String &name) = 0;

	/** Open the stage file @p name (a DATA/ basename, e.g. "main.stg"). */
	virtual void openStageFile(const Common::String &name) {}

	/** Navigate to (and render) node @p node of the open stage. */
	virtual void sendToStage(int node) {}
};

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
	 * Attach the engine host that realises effectful builtins (playMovie, ...).
	 * Without a host, those builtins are logged no-ops. Not owned.
	 */
	void setHost(VMHost *host) { _host = host; }

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
	 * at vaddr 0x0040ba4f (see files/opcode-map.md sections 3 and 8).
	 */
	uint32 runProgram(const Script &script, uint32 maxSteps = 100000);

private:
	void execute(const Script &script, uint32 index);
	void applyOperator(uint16 opcode);
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

	/** Store @p v under @p name in the current scope (TI.EXE store 0x00413610). */
	void setVar(const Common::String &name, const Value &v);


	/**
	 * Evaluate the expression starting at instruction @p pc, advancing @p pc
	 * past every instruction consumed. Mirrors the TI.EXE evaluator 0x00419cf0:
	 * an atom followed by zero or more (operator, atom) pairs, reduced by the
	 * operator precedence recovered in files/opcode-map.md section 8.
	 */
	Value evaluateExpression(const Script &script, uint32 &pc);

	/** Decode a single atom (literal/symbol/call) at @p pc, advancing @p pc. */
	Value decodeAtom(const Script &script, uint32 &pc);

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
	 * frame stack 0x45ed48). See files/opcode-map.md section 9.
	 */
	struct ForFrame {
		Common::String var; ///< Loop-variable name.
		int32 end;          ///< Inclusive upper bound.
		uint32 bodyStart;   ///< Instruction index of the body's first statement.
	};
	Common::Array<ForFrame> _forStack;

	/// Variable scope. A flat name->value map standing in for the TI.EXE object
	/// scope chain (global object at [0x45f010]) until objects are modelled.
	Common::HashMap<Common::String, Value> _vars;

	uint32 _pc;
	bool _trace;
	VMHost *_host; ///< Engine host for effectful builtins; null = no-op. Not owned.
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_VM_H

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

#ifndef CYBERFLIX_SCRIPT_H
#define CYBERFLIX_SCRIPT_H

#include "common/array.h"
#include "common/str.h"

namespace Common {
class SeekableReadStream;
}

namespace Cyberflix {

/**
 * Parser for the CyberFlix script resources (directory entries whose @c info
 * tag is 0x0FA1), found in BOOTFILE and in the .STG / .SET containers.
 *
 * A script resource is a flat array of fixed 8-byte instructions followed by a
 * Pascal-string pool. Each instruction is little-endian:
 *
 *   +0x00  uint32  operandB  (frequently 0)
 *   +0x04  uint16  opcode
 *   +0x06  uint16  operandA
 *
 * The instruction stream is terminated by an opcode of 0x0000; the pool of
 * length-prefixed (Pascal) symbol and string-constant names begins immediately
 * after. This layout has been validated across all 141 script resources on the
 * two retail CDs (every one terminates cleanly at the pool boundary).
 *
 * Opcode families observed:
 *   0x0003..0x0006  stack pushes (0x0006 = push integer in operandA)
 *   0x0Fxx          built-in engine commands
 *   0x1Fxx..0x5Dxx  handler / method dispatch (long tail of distinct values)
 *
 * The precise semantics of each opcode are still being mapped; this class
 * provides faithful structural decoding and a disassembly aid to drive that
 * work, and will back the interpreter once the opcode set is pinned down.
 */
class Script {
public:
	enum {
		kOpEnd     = 0x0000, ///< Terminates the instruction stream.
		kOpPush3   = 0x0003,
		kOpPush4   = 0x0004,
		kOpPushSym = 0x0005, ///< Push symbol/variable reference.
		kOpPushInt = 0x0006, ///< Push integer constant (operandA).
		kOpCmdBase = 0x0FA0, ///< Start of the built-in command range.

		// Control-flow builtins dispatched by the main loop's small switch
		// (TI.EXE 0x0040ba4f, map 0x40c458). See files/opcode-map.md section 3.
		kOpReturn  = 0x0FA4, ///< End of script body; requires balanced blocks.
		kOpIf      = 0x0FA6, ///< Evaluate condition; enter or skip THEN block.
		kOpEndIf   = 0x0FA7, ///< Close an if/then(/else) block.
		kOpElse    = 0x0FA8, ///< Else marker; skipped after the THEN ran.
		kOpFor     = 0x0FAC, ///< For-loop setup (loop var + start bound).
		kOpForTo   = 0x0FAD, ///< For-loop bound separator ("to").
		kOpForNext = 0x0FAF, ///< For-loop iterate/close.
		kOpWhile   = 0x0FB0, ///< While: test condition, enter or skip body.
		kOpEndWhile = 0x0FB1, ///< While close: re-test and loop or exit.
		kOpAssign  = 0x0FB2, ///< Assignment marker (symbol kOpAssign expr).

		// Infix operator opcodes, applied by the TI.EXE evaluator
		// (applier 0x00419f30, jump table 0x41a484). See files/opcode-map.md.
		kOpAdd     = 0x1F41, ///< int: lhs + rhs.
		kOpSub     = 0x1F42, ///< int: lhs - rhs.
		kOpMul     = 0x1F43, ///< int: lhs * rhs.
		kOpDiv     = 0x1F44, ///< int: lhs / rhs (error on divide by zero).
		kOpAnd     = 0x1F45, ///< bool: lhs & rhs.
		kOpOr      = 0x1F46, ///< bool: lhs | rhs.
		kOpConcat  = 0x1F47, ///< string: concat(lhs, rhs).
		kOpEq      = 0x1F48, ///< any: lhs == rhs -> bool.
		kOpNe      = 0x1F49, ///< any: lhs != rhs -> bool.
		kOpGt      = 0x1F4A, ///< int: lhs > rhs -> bool.
		kOpLt      = 0x1F4B, ///< int: lhs < rhs -> bool.
		kOpGe      = 0x1F4C, ///< int: lhs >= rhs -> bool.
		kOpLe      = 0x1F4D, ///< int: lhs <= rhs -> bool.

		kOpOperatorFirst = 0x1F41,
		kOpOperatorLast  = 0x1F4D
	};

	/** True if @p opcode is an infix binary operator (0x1F41..0x1F4D). */
	static bool isOperator(uint16 opcode) {
		return opcode >= kOpOperatorFirst && opcode <= kOpOperatorLast;
	}

	/**
	 * Precedence class of an infix operator (0 binds tightest, 6 loosest).
	 *
	 * Recovered from the TI.EXE classifier at vaddr 0x0041a4c0 (byte map
	 * 0x41a534, value table 0x41a514). Returns 0xFF for non-operators.
	 */
	static uint8 operatorPrecedence(uint16 opcode);

	/**
	 * Index of the kOpEndIf that closes the if-block opened at @p index (which
	 * must be a kOpIf), accounting for nested ifs, or -1 if not found.
	 */
	int findMatchingEndIf(uint32 index) const { return findEndIfFrom(index + 1); }

	/**
	 * Index of the next kOpEndIf at the current nesting level, scanning forward
	 * from @p index which is already inside an if/then or else body. Mirrors the
	 * scanner at TI.EXE 0x0040c550; aborts (-1) on end/return.
	 */
	int findEndIfFrom(uint32 index) const;

	/**
	 * If the if-block opened at @p index (a kOpIf) has an else clause, return the
	 * index of the first instruction after its kOpElse; if the matching kOpEndIf
	 * is reached first (no else), return -1. Mirrors TI.EXE 0x0040c6d0.
	 */
	int findMatchingElse(uint32 index) const;

	/**
	 * Index of the kOpEndWhile at the current nesting level, scanning forward
	 * from @p index inside a while body (counting nested kOpWhile). Mirrors the
	 * scanner at TI.EXE 0x0040c5b0; aborts (-1) on end/return.
	 */
	int findEndWhileFrom(uint32 index) const;

	/**
	 * Index of the kOpForNext at the current nesting level, scanning forward from
	 * @p index inside a for body (counting nested kOpFor), or -1.
	 */
	int findForNextFrom(uint32 index) const;

	struct Instruction {
		uint32 operandB;
		uint16 opcode;
		uint16 operandA;
	};

	Script();

	/** Parse a script resource payload. Does not take ownership of @p stream. */
	bool parse(Common::SeekableReadStream *stream);

	bool isValid() const { return _valid; }
	uint32 getInstructionCount() const { return _code.size(); }
	const Instruction &getInstruction(uint32 i) const { return _code[i]; }

	/** True if the instruction stream ended with an explicit 0x0000. */
	bool isTerminated() const { return _terminated; }

	/** Offset (within the payload) where the string pool begins. */
	uint32 getPoolOffset() const { return _poolOffset; }

	/** Read the Pascal string at @p offset within the payload, or "". */
	Common::String getPoolString(uint32 offset) const;

	/**
	 * Resolve the self-relative symbol name referenced by instruction @p index.
	 *
	 * pushSym (0x0005) and related operand atoms encode the symbol name as a
	 * Pascal string located at (opcodeFieldOffset + operandA), i.e. the operand
	 * is a byte offset relative to the instruction's own opcode field. This was
	 * recovered from the TI.EXE evaluator (vaddr 0x00419cc0:
	 * "mov ecx,[eax+2]; add ecx,eax") and verified against the boot script.
	 */
	Common::String getSelfRelString(uint32 index) const;

	/** Human-readable mnemonic for @p opcode. */
	static const char *opcodeName(uint16 opcode);

private:
	bool _valid;
	bool _terminated;
	uint32 _poolOffset;
	Common::Array<Instruction> _code;
	Common::Array<byte> _payload;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_SCRIPT_H

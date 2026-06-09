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
		kOpCmdBase = 0x0FA0  ///< Start of the built-in command range.
	};

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

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

#include "common/stream.h"
#include "common/textconsole.h"

#include "cyberflix/script.h"

namespace Cyberflix {

Script::Script() : _valid(false), _terminated(false), _poolOffset(0) {
}

bool Script::parse(Common::SeekableReadStream *stream) {
	_valid = false;
	_terminated = false;
	_poolOffset = 0;
	_code.clear();
	_payload.clear();

	if (!stream)
		return false;

	uint32 size = (uint32)stream->size();
	if (size < 8)
		return false;

	// Keep a private copy of the payload so pool strings can be resolved later.
	_payload.resize(size);
	stream->seek(0);
	if (stream->read(_payload.begin(), size) != size)
		return false;

	// Fixed 8-byte instructions: [u32 operandB][u16 opcode][u16 operandA] (LE),
	// terminated by opcode 0x0000. The pool follows immediately afterwards.
	uint32 pos = 0;
	while (pos + 8 <= size) {
		Instruction inst;
		inst.operandB = READ_LE_UINT32(_payload.begin() + pos);
		inst.opcode = READ_LE_UINT16(_payload.begin() + pos + 4);
		inst.operandA = READ_LE_UINT16(_payload.begin() + pos + 6);
		pos += 8;

		if (inst.opcode == kOpEnd) {
			_terminated = true;
			break;
		}
		_code.push_back(inst);
	}

	_poolOffset = pos;
	_valid = true;
	return true;
}

Common::String Script::getPoolString(uint32 offset) const {
	if (offset >= _payload.size())
		return Common::String();

	uint32 len = _payload[offset];
	if (len == 0 || offset + 1 + len > _payload.size())
		return Common::String();

	Common::String result;
	for (uint32 i = 0; i < len; ++i) {
		byte c = _payload[offset + 1 + i];
		if (c < 32 || c > 126)
			return Common::String();
		result += (char)c;
	}
	return result;
}

Common::String Script::getSelfRelString(uint32 index) const {
	if (index >= _code.size())
		return Common::String();
	// The operand is relative to the instruction's opcode field, which sits 4
	// bytes into the 8-byte record (after the leading operandB dword).
	uint32 opcodeFieldOffset = index * 8 + 4;
	return getPoolString(opcodeFieldOffset + _code[index].operandA);
}

const char *Script::opcodeName(uint16 opcode) {
	switch (opcode) {
	case kOpEnd:     return "end";
	case kOpPush3:   return "push3";
	case kOpPush4:   return "push4";
	case kOpPushSym: return "pushSym";
	case kOpPushInt: return "pushInt";
	case kOpAdd:     return "add";
	case kOpSub:     return "sub";
	case kOpMul:     return "mul";
	case kOpDiv:     return "div";
	case kOpAnd:     return "and";
	case kOpOr:      return "or";
	case kOpConcat:  return "concat";
	case kOpEq:      return "eq";
	case kOpNe:      return "ne";
	case kOpGt:      return "gt";
	case kOpLt:      return "lt";
	case kOpGe:      return "ge";
	case kOpLe:      return "le";
	default:
		if (opcode >= kOpCmdBase && opcode < 0x1000)
			return "cmd";
		return "call";
	}
}

} // End of namespace Cyberflix

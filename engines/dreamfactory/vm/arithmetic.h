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

#ifndef DREAMFACTORY_VM_ARITHMETIC_H
#define DREAMFACTORY_VM_ARITHMETIC_H

#include "common/scummsys.h"

namespace DreamFactory {

// Verified by disassembling Titanic's Windows INSTALL/BINX/TI.EXE: the binary
// operator handler at 0x00419f30 uses ADD at 0x00419fa0, SUB at 0x00419fdf and
// two-operand IMUL at 0x0041a00b. These retain the low 32 bits of the result;
// the handler does not test the overflow flags before storing the result.
// This is instruction-level evidence, not a runtime test of overflow cases.
//
// DreamFactory integer arithmetic uses 32-bit machine words. Signed integer
// overflow is undefined in C++, so addition, subtraction and multiplication
// below use unsigned arithmetic to obtain well-defined wrapping modulo 2^32.
// Convert the result back without an out-of-range unsigned-to-signed conversion:
// words with the high bit set represent negative two's-complement values.
// For those words, 0xffffffff - value fits in int32, and -1 minus that distance
// gives the signed value (e.g. 0xffffffff becomes -1, 0x80000000 becomes INT_MIN).
inline int32 signedWord(uint32 value) {
	return value <= 0x7fffffffU ? static_cast<int32>(value) :
			-1 - static_cast<int32>(0xffffffffU - value);
}

inline int32 addWords(int32 a, int32 b) {
	return signedWord(static_cast<uint32>(a) + static_cast<uint32>(b));
}

inline int32 subtractWords(int32 a, int32 b) {
	return signedWord(static_cast<uint32>(a) - static_cast<uint32>(b));
}

inline int32 multiplyWords(int32 a, int32 b) {
	return signedWord(static_cast<uint32>(a) * static_cast<uint32>(b));
}

inline bool divideWords(int32 a, int32 b, int32 &result) {
	// The same TI.EXE handler checks for a zero divisor at 0x0041a038 and returns
	// error 0x37. Otherwise, CDQ at 0x0041a04c sign-extends the dividend into
	// EDX:EAX, and IDIV at 0x0041a04d produces a signed quotient rounded toward
	// zero. There is no guard for INT_MIN / -1, which would trap on x86.
	// Both cases are undefined in C++, so reject them before dividing. Our caller
	// warns and returns zero rather than reproducing the native error or trap.
	// Unlike the other operations, signed division cannot use unsigned division
	// because negative operands would then produce a different quotient.
	if (b == 0 || (a == (-2147483647 - 1) && b == -1))
		return false;
	result = a / b;
	return true;
}

} // End of namespace DreamFactory

#endif

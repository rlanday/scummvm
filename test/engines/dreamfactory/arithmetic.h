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

#include <cxxtest/TestSuite.h>

#include "engines/dreamfactory/vm/arithmetic.h"

class DreamFactoryArithmeticTestSuite : public CxxTest::TestSuite {
public:
	void testWordWrapping() {
		const int32 min = -2147483647 - 1;
		const int32 max = 2147483647;
		TS_ASSERT_EQUALS(DreamFactory::signedWord(0xffffffffU), -1);
		TS_ASSERT_EQUALS(DreamFactory::signedWord(0x80000000U), min);
		TS_ASSERT_EQUALS(DreamFactory::addWords(max, 1), min);
		TS_ASSERT_EQUALS(DreamFactory::subtractWords(min, 1), max);
		TS_ASSERT_EQUALS(DreamFactory::subtractWords(0, min), min);
		TS_ASSERT_EQUALS(DreamFactory::multiplyWords(min, -1), min);
		TS_ASSERT_EQUALS(DreamFactory::multiplyWords(0x40000000, 4), 0);
		TS_ASSERT_EQUALS(DreamFactory::multiplyWords(-3, 7), -21);
	}

	void testSignedDivision() {
		int32 result = 42;
		TS_ASSERT(!DreamFactory::divideWords(1, 0, result));
		TS_ASSERT_EQUALS(result, 42);
		TS_ASSERT(!DreamFactory::divideWords(-2147483647 - 1, -1, result));
		TS_ASSERT_EQUALS(result, 42);
		TS_ASSERT(DreamFactory::divideWords(-7, 3, result));
		TS_ASSERT_EQUALS(result, -2);
		TS_ASSERT(DreamFactory::divideWords(7, -3, result));
		TS_ASSERT_EQUALS(result, -2);
	}
};

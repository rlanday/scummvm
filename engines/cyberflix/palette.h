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

#ifndef CYBERFLIX_PALETTE_H
#define CYBERFLIX_PALETTE_H

#include "common/scummsys.h"

namespace Cyberflix {

enum {
	kPaletteColorCount = 256,
	kPaletteChannelCount = 3,
	kPaletteByteCount = kPaletteColorCount * kPaletteChannelCount,
	kPaletteLastColor = kPaletteColorCount - 1
};

static const double kDefaultPaletteGamma = 0.65;

struct Palette {
	typedef byte *iterator;
	typedef const byte *const_iterator;

	byte &operator[](uint index) { return _data[index]; }
	byte operator[](uint index) const { return _data[index]; }
	byte *data() { return _data; }
	const byte *data() const { return _data; }
	uint byteSize() const { return kPaletteByteCount; }
	uint colorCount() const { return kPaletteColorCount; }
	iterator begin() { return _data; }
	iterator end() { return _data + kPaletteByteCount; }
	const_iterator begin() const { return _data; }
	const_iterator end() const { return _data + kPaletteByteCount; }
	static uint colorOffset(uint color) { return color * kPaletteChannelCount; }
	void fill(byte value) {
		for (byte &component : _data)
			component = value;
	}

private:
	byte _data[kPaletteByteCount] = {};
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_PALETTE_H

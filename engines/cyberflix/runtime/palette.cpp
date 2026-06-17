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

#include "cyberflix/runtime/palette.h"

#include <math.h>

namespace Cyberflix {

void PaletteRuntime::copyCurrent(byte (&rgb)[256 * 3]) const {
	memcpy(rgb, _currentClut, sizeof(_currentClut));
}

void PaletteRuntime::setCurrent(const byte (&rgb)[256 * 3]) {
	memcpy(_currentClut, rgb, sizeof(_currentClut));
}

bool PaletteRuntime::isBlack() const {
	for (int i = 0; i < 256 * 3; ++i)
		if (_currentClut[i])
			return false;
	return true;
}

void PaletteRuntime::updateGammaTable() {
	if (!_gammaTableDirty)
		return;
	for (int c = 0; c < 3; ++c) {
		for (int i = 0; i < 256; ++i)
			_gammaTable[c][i] = (byte)(pow(i / 255.0, _gamma[c]) * 255.0); // trunc, like __ftol
	}
	_gammaTableDirty = false;
}

} // End of namespace Cyberflix

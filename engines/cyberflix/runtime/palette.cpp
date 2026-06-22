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

#include "common/algorithm.h"

#include "cyberflix/runtime/palette.h"

#include <math.h>

namespace Cyberflix {

void PaletteRuntime::copyCurrent(byte (&rgb)[256 * 3]) const {
	Common::copy(_currentClut, _currentClut + ARRAYSIZE(_currentClut), rgb);
}

void PaletteRuntime::setCurrent(const byte (&rgb)[256 * 3]) {
	Common::copy(rgb, rgb + ARRAYSIZE(rgb), _currentClut);
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
			_gammaTable[c][i] = static_cast<byte>((pow(i / 255.0, _gamma[c]) * 255.0)); // trunc, like __ftol
	}
	_gammaTableDirty = false;
}

} // End of namespace Cyberflix

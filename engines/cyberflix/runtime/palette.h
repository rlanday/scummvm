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

#ifndef CYBERFLIX_RUNTIME_PALETTE_H
#define CYBERFLIX_RUNTIME_PALETTE_H

#include "cyberflix/image.h"

namespace Cyberflix {

class PaletteRuntime {
public:
	void copyCurrent(Palette &rgb) const;
	void setCurrent(const Palette &rgb);
	bool isBlack() const;

	double &gamma(uint channel) { return _gamma[channel]; }
	double gamma(uint channel) const { return _gamma[channel]; }
	void setGamma(uint channel, double value) { _gamma[channel] = value; _gammaTableDirty = true; }
	void markGammaTableDirty() { _gammaTableDirty = true; }
	void updateGammaTable();
	byte gammaMapped(uint channel, byte value) const { return _gammaTable[channel][value]; }

private:
	Palette _currentClut = {};
	double _gamma[kPaletteChannelCount] = { kDefaultPaletteGamma, kDefaultPaletteGamma, kDefaultPaletteGamma };
	byte _gammaTable[kPaletteChannelCount][kPaletteColorCount] = {};
	bool _gammaTableDirty = true;
};

} // End of namespace Cyberflix

#endif

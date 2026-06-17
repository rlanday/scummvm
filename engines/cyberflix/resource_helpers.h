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

#ifndef CYBERFLIX_RESOURCE_HELPERS_H
#define CYBERFLIX_RESOURCE_HELPERS_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "cyberflix/archive.h"

namespace Cyberflix {

inline const byte *resourceEngineBase(const Common::Array<byte> &fileData,
		const Archive::Resource &res) {
	if (res.empty || res.dataOffset < 4 || res.dataOffset > fileData.size())
		return nullptr;
	return fileData.begin() + res.dataOffset - 4;
}

inline Common::String readPascalString(const byte *p,
		const Common::Array<byte> &fileData, bool allowTruncated = false) {
	if (!p || p < fileData.begin() || p >= fileData.end())
		return Common::String();
	uint len = *p;
	const byte *s = p + 1;
	if (s + len > fileData.end()) {
		if (!allowTruncated)
			return Common::String();
		len = (uint)(fileData.end() - s);
	}
	return Common::String((const char *)s, len);
}

} // End of namespace Cyberflix

#endif

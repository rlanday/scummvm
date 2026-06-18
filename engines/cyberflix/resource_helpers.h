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
#include "common/util.h"

#include "cyberflix/archive.h"

#include <math.h>

namespace Cyberflix {

/**
 * Master header resource tag (@c info==0x40000). Archive formats use this for
 * their top-level descriptor records; callers interpret the record-specific
 * fields through a data pointer at @c record+8 (== payload-4), so a field at
 * engine byte offset @c X is at @c payload[X-4].
 */
static const uint32 kMasterHeaderInfoTag = 0x00040000;

inline const byte *resourceEngineBase(const Common::Array<byte> &fileData,
		const Archive::Resource &res) {
	if (res.empty || res.dataOffset < 4 || res.dataOffset > fileData.size())
		return nullptr;
	return fileData.begin() + res.dataOffset - 4;
}

bool openArchiveFile(const Common::String &name, const char *kind,
		Common::Array<byte> &fileData, Archive &archive);

inline int resourceIndexById(const Archive &archive, uint32 id) {
	for (uint32 i = 0; i < archive.getResourceCount(); ++i)
		if (!archive.getResource(i).empty && archive.getResource(i).id == id)
			return static_cast<int>(i);
	return -1;
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
		len = static_cast<uint>((fileData.end() - s));
	}
	return Common::String(reinterpret_cast<const char *>(s), len);
}

inline int nativeAngleDistance(int a, int b) {
	int d = ABS(a - b) & 0xff;
	return d > 128 ? 256 - d : d;
}

inline int16 nativeTrigSin(int angle) {
	double v = sin(static_cast<double>(angle & 0xff) * 6.28318530717958647692 / 256.0) * 16384.0;
	return static_cast<int16>((v >= 0.0 ? v + 0.5 : v - 0.5));
}

inline int16 nativeTrigCos(int angle) {
	double v = cos(static_cast<double>(angle & 0xff) * 6.28318530717958647692 / 256.0) * 16384.0;
	return static_cast<int16>((v >= 0.0 ? v + 0.5 : v - 0.5));
}

// Camera headings come from SET panorama records and index TI.EXE's 16-bit
// sine/cosine tables in FUN_00442e90. Actor/prop facing angles are the separate
// 8-bit script-visible units handled by nativeTrigSin()/nativeTrigCos() above.
inline int16 nativeCameraTrigSin(int heading) {
	double v = sin(static_cast<double>(static_cast<uint16>(heading)) * 6.28318530717958647692 / 65536.0) * 16384.0;
	return static_cast<int16>((v >= 0.0 ? v + 0.5 : v - 0.5));
}

inline int16 nativeCameraTrigCos(int heading) {
	double v = cos(static_cast<double>(static_cast<uint16>(heading)) * 6.28318530717958647692 / 65536.0) * 16384.0;
	return static_cast<int16>((v >= 0.0 ? v + 0.5 : v - 0.5));
}

inline int fixedShift14(int value) {
	return (value + (value < 0 ? 0x3fff : 0)) >> 14;
}

inline int nativePointAngle(int dx, int dy) {
	int deg = static_cast<int>((atan2(static_cast<double>(dx), static_cast<double>(dy)) * (256.0 / 6.28318530717958647692)));
	deg %= 256;
	if (deg < 0)
		deg += 256;
	return deg;
}

} // End of namespace Cyberflix

#endif

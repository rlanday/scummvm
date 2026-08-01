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

#ifndef CYBERFLIX_SAVELOAD_H
#define CYBERFLIX_SAVELOAD_H

#include "common/array.h"
#include "common/str.h"
#include "common/stream.h"

namespace Cyberflix {

/**
 * Save-format constants and low-level readers shared between the engine's
 * save/load code (saveload.cpp) and the MetaEngine's save-list scanner
 * (metaengine.cpp). Keeping them here means a format/version bump cannot
 * silently desynchronise the two: listSaves() rejects saves whose version it
 * does not know, so a version bumped only in saveload.cpp would make every
 * new save vanish from the load chooser.
 */
static const char kCyberflixSaveMagic[4] = {'C', 'F', 'X', 'S'};

enum {
	kCyberflixSaveVersion = 1
};

/** Read a uint32-length-prefixed string, bounded by @p end. */
inline bool readSaveString(Common::SeekableReadStream &in, int64 end, Common::String &s) {
	if (in.pos() + 4 > end)
		return false;
	uint32 len = in.readUint32LE();
	if (in.pos() + len > end)
		return false;
	s.clear();
	if (!len)
		return !in.err();
	Common::Array<char> buf;
	buf.resize(len);
	if (in.read(buf.begin(), len) != len)
		return false;
	s = Common::String(buf.begin(), len);
	return true;
}

/** Read a 4-char chunk tag + uint32 payload size; @p end receives the chunk end. */
inline bool readChunkHeader(Common::SeekableReadStream &in, char tag[5], int64 &end) {
	if (in.pos() + 8 > in.size())
		return false;
	if (in.read(tag, 4) != 4)
		return false;
	tag[4] = 0;
	uint32 size = in.readUint32LE();
	end = in.pos() + size;
	return end <= in.size();
}

} // End of namespace Cyberflix

#endif // CYBERFLIX_SAVELOAD_H

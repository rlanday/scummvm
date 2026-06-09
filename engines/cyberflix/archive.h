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

#ifndef CYBERFLIX_ARCHIVE_H
#define CYBERFLIX_ARCHIVE_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/stream.h"

namespace Common {
class SeekableReadStream;
}

namespace Cyberflix {

/**
 * Reader for CyberFlix "Bicycle"-engine container files (the "LPPALPPA"
 * format) used by Titanic: Adventure Out of Time.
 *
 * Every asset file (BOOTFILE, *.SET, *.STG, *.CST, *.SHP, *.TRK, *.MOV, *.SFX)
 * shares one outer container header:
 *
 *   +0x00  uint32 LE  0x00010000   format/version magic
 *   +0x04  uint32 LE  file length  (matches the on-disk size exactly)
 *   +0x10  uint32 LE  first-section size
 *   +0x14  uint32 LE  resource count
 *   +0x20  "LPPALPPA"               signature ("APPL" word-reversed, twice)
 *
 * The payload is big-endian and was authored on Macintosh: decoding any header
 * by reversing each 4-byte group yields a clean HFS path (e.g.
 * "Internal:new converts for Ian:bootfile.Temp"). Multi-byte values in the
 * payload are therefore big-endian, and embedded text is stored in
 * 4-byte-reversed groups.
 *
 * NOTE: the internal resource directory is a serialized Macintosh handle heap
 * with absolute pointer fix-ups (0xCCCCCCCC fill marks free cells), not a
 * simple offset table. Decoding it is ongoing reverse engineering; this class
 * currently parses and validates the outer container and exposes the raw bytes
 * plus the word-swap helpers the rest of the engine will build on.
 */
class Archive {
public:
	Archive();
	~Archive();

	/** Parse and validate the container header. Takes ownership of @p stream. */
	bool open(Common::SeekableReadStream *stream, const Common::String &name);

	void close();

	bool isOpen() const { return _stream != nullptr; }

	/** Number of resources declared in the container header (+0x14). */
	uint32 getResourceCount() const { return _resourceCount; }

	/** File length declared in the container header (+0x04). */
	uint32 getDeclaredSize() const { return _declaredSize; }

	const Common::String &getName() const { return _name; }

	/** The "LPPALPPA" container signature, big-endian. */
	static const uint32 kSignature1 = MKTAG('L', 'P', 'P', 'A');

	/** Reverse each 4-byte group of @p data in place (Mac long byte-swap). */
	static void swapLongs(byte *data, uint32 size);

private:
	Common::SeekableReadStream *_stream;
	Common::String _name;

	uint32 _magic;
	uint32 _declaredSize;
	uint32 _firstSectionSize;
	uint32 _resourceCount;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_ARCHIVE_H

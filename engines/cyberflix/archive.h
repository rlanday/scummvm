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
#include "common/array.h"
#include "common/ptr.h"
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
 * After a fixed 0x200-byte handle-heap preamble at offset 0x200, the resource
 * directory is a flat offset table at a fixed offset (0x400): an array of
 * @c count little-endian uint32 file offsets. Each entry points to a record:
 *
 *   +0x00  uint32 LE  id      (sequential 0, 1, 2, ...)
 *   +0x04  uint32 LE  length  (payload size in bytes)
 *   +0x08  uint32 LE  info    (type/flags; e.g. 0x0FA1 = script/blob, and for
 *                              shapes the low 32 bits pack 16-bit width/height)
 *   +0x0C  payload (length bytes; records are padded to a 0x40 boundary)
 *
 * The container framing (header + directory) is little-endian; resource payload
 * data is big-endian (Mac-authored) and text is stored in 4-byte-reversed
 * groups, hence swapLongs().
 */
class Archive {
public:
	struct Resource {
		uint32 id = 0;          ///< Sequential resource id.
		uint32 length = 0;      ///< Payload size in bytes.
		uint32 info = 0;        ///< Type/flags (semantics still being mapped).
		uint32 dataOffset = 0;  ///< Absolute file offset of the payload.
		bool empty = false;     ///< True for placeholder slots with no data.
	};

	Archive();
	~Archive();

	/** Parse and validate the container. Takes ownership of @p stream. */
	bool open(Common::SeekableReadStream *stream, const Common::String &name);

	void close();

	bool isOpen() const { return _stream.get() != nullptr; }

	/** Number of resources declared in the container header (+0x14). */
	uint32 getResourceCount() const { return _resources.size(); }

	/** File length declared in the container header (+0x04). */
	uint32 getDeclaredSize() const { return _declaredSize; }

	const Common::String &getName() const { return _name; }

	/** Directory entry for resource @p index (< getResourceCount()). */
	const Resource &getResource(uint32 index) const { return _resources[index]; }

	/**
	 * Returns a newly allocated stream over the payload of resource @p index,
	 * or nullptr on a bad index. Caller owns the returned stream; it stays
	 * valid only while this Archive (and its backing stream) is open.
	 */
	Common::SeekableReadStream *createReadStreamForResource(uint32 index) const;

	/** The "LPPALPPA" container signature, big-endian. */
	static const uint32 kSignature1 = MKTAG('L', 'P', 'P', 'A');

	/** Fixed file offset of the resource offset table. */
	static const uint32 kDirectoryOffset = 0x400;

	/** Reverse each 4-byte group of @p data in place (Mac long byte-swap). */
	static void swapLongs(byte *data, uint32 size);

private:
	bool readDirectory();

	// Owns the backing stream (RAII); ScopedPtr is non-copyable, so Archive (and
	// the resource objects that hold one by value) are non-copyable too, which is
	// correct -- they are always referenced through ScopedPtr/SharedPtr.
	Common::ScopedPtr<Common::SeekableReadStream> _stream;
	Common::String _name;

	uint32 _magic;
	uint32 _declaredSize;
	uint32 _firstSectionSize;
	uint32 _resourceCount;

	Common::Array<Resource> _resources;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_ARCHIVE_H

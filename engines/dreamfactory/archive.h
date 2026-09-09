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

#ifndef DREAMFACTORY_ARCHIVE_H
#define DREAMFACTORY_ARCHIVE_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/ptr.h"
#include "common/str.h"
#include "common/stream.h"

namespace Common {
class SeekableReadStream;
}

namespace DreamFactory {

/**
 * Reader for CyberFlix DreamFactory container files (the "LPPALPPA"
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
 * The verified retail Titanic CD images are hybrid HFS/ISO 9660 discs. Their
 * Macintosh data forks and Windows copies contain identical game containers:
 * all 243 containers on CD 1 and 323 on CD 2 matched byte-for-byte, including
 * files relocated into the Macintosh installation directory. Their HFS
 * resource forks are empty. The Macintosh application itself is different;
 * shared assets do not imply support for its executable resources.
 *
 * After a fixed 0x200-byte handle-heap preamble at offset 0x200, the resource
 * directory is a flat offset table at a fixed offset (0x400): an array of
 * @c count little-endian uint32 file offsets. Each entry points to a record:
 *
 *   +0x00  uint32 LE  id      (stored resource identifier)
 *   +0x04  uint32 LE  length  (payload size in bytes)
 *   +0x08  uint32 LE  info    (type/flags; e.g. 0x0FA1 = script/blob, and for
 *                              shapes the low 32 bits pack 16-bit width/height)
 *   +0x0C  payload (length bytes; records are padded to a 0x40 boundary)
 *
 * Container framing and record headers are little-endian. Payload fields must
 * be decoded according to their resource format, not assumed big-endian just
 * because Macintosh uses the same files. Shop fields and script instructions,
 * for example, are read little-endian. Some authoring metadata contains text
 * in reversed four-byte groups (hence swapLongs()); that is not a rule for
 * all payload bytes or strings, and Archive does not swap entire payloads.
 */
class Archive {
public:
	struct Resource {
		uint32 id = 0;          ///< Resource id stored in the record header.
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
	/** Resource directory entries in directory order. */
	const Common::Array<Resource> &resources() const { return _resources; }
	/** First non-empty entry with stored @p id, or -1. IDs need not be unique. */
	int resourceIndexById(uint32 id) const;

	/**
	 * Returns a newly allocated stream over the payload of resource @p index,
	 * or nullptr on a bad index. Caller owns the returned stream; it stays
	 * valid only while this Archive (and its backing stream) is open.
	 */
	Common::SeekableReadStream *createReadStreamForResource(uint32 index) const;

	/**
	 * Returns a newly allocated stream over the payload of @p resource, or
	 * nullptr if its byte range is invalid. Caller owns the returned stream; it
	 * stays valid only while this Archive (and its backing stream) is open.
	 */
	Common::SeekableReadStream *createReadStreamForResource(const Resource &resource) const;

	/** The "LPPALPPA" container signature, big-endian. */
	static const uint32 kSignature1 = MKTAG('L', 'P', 'P', 'A');

	/** Fixed file offset of the resource offset table. */
	static const uint32 kDirectoryOffset = 0x400;

	/** Reverse each 4-byte group of @p data in place (Mac long byte-swap). */
	static void swapLongs(byte *data, uint32 size);

private:
	bool readDirectory();
	Common::HashMap<uint32, uint32> _resourceIndexById;

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

} // End of namespace DreamFactory

#endif // DREAMFACTORY_ARCHIVE_H

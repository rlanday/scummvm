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

#include "common/debug.h"
#include "common/stream.h"
#include "common/substream.h"
#include "common/textconsole.h"

#include "cyberflix/archive.h"

namespace Cyberflix {

Archive::Archive() :
		_magic(0), _declaredSize(0),
		_firstSectionSize(0), _resourceCount(0) {
}

Archive::~Archive() {
	close();
}

bool Archive::open(Common::SeekableReadStream *stream, const Common::String &name) {
	close();

	if (!stream)
		return false;

	_name = name;

	if (stream->size() < 0x28) {
		warning("Cyberflix::Archive: '%s' is too small to be a container", name.c_str());
		delete stream;
		return false;
	}

	// Outer container header (little-endian scalars).
	_magic            = stream->readUint32LE();
	_declaredSize     = stream->readUint32LE();
	stream->skip(8);                            // +0x08: reserved (zero)
	_firstSectionSize = stream->readUint32LE();
	_resourceCount    = stream->readUint32LE();
	stream->skip(8);                            // +0x18: reserved (zero)

	// Signature "LPPALPPA" at +0x20 (two big-endian 'APPL' tags).
	uint32 sig1 = stream->readUint32BE();
	uint32 sig2 = stream->readUint32BE();

	if (_magic != 0x00010000 || sig1 != kSignature1 || sig2 != kSignature1) {
		warning("Cyberflix::Archive: '%s' is not an LPPALPPA container "
				"(magic=0x%08x sig=0x%08x%08x)", name.c_str(), _magic, sig1, sig2);
		delete stream;
		return false;
	}

	if (_declaredSize != static_cast<uint32>(stream->size())) {
		// Non-fatal: warn but keep going, the header size is informational.
		warning("Cyberflix::Archive: '%s' declared size %u != file size %d",
				name.c_str(), _declaredSize, static_cast<int>(stream->size()));
	}

	_stream.reset(stream);

	if (!readDirectory()) {
		warning("Cyberflix::Archive: '%s' has an invalid resource directory", name.c_str());
		close();
		return false;
	}

	debug(1, "Cyberflix::Archive: opened '%s' (%u resources, %u bytes)",
			name.c_str(), getResourceCount(), _declaredSize);

	return true;
}

bool Archive::readDirectory() {
	const uint32 fileSize = static_cast<uint32>(_stream->size());

	if (kDirectoryOffset + static_cast<uint64>(_resourceCount) * 4 > fileSize)
		return false;

	_resources.clear();
	_resources.reserve(_resourceCount);

	for (uint32 i = 0; i < _resourceCount; ++i) {
		_stream->seek(kDirectoryOffset + i * 4);
		uint32 recOffset = _stream->readUint32LE();

		Resource res;
		res.id = i;
		res.length = 0;
		res.info = 0;
		res.dataOffset = 0;
		res.empty = true;

		// Offsets of 0 (or any value pointing inside the header/directory) are
		// empty placeholder slots: keep the index but mark it as having no data.
		if (recOffset != 0 && recOffset >= kDirectoryOffset && recOffset + 12 <= fileSize) {
			_stream->seek(recOffset);
			res.id = _stream->readUint32LE();
			res.length = _stream->readUint32LE();
			res.info = _stream->readUint32LE();
			res.dataOffset = recOffset + 12;
			res.empty = false;

			// The final record is sometimes a few bytes short of its declared
			// length (unpadded tail); clamp to the available bytes rather than
			// rejecting the container.
			if (static_cast<uint64>(res.dataOffset) + res.length > fileSize) {
				uint32 avail = fileSize - res.dataOffset;
				debug(2, "Cyberflix::Archive: resource %u length %u clamped to %u",
						i, res.length, avail);
				res.length = avail;
			}
		} else if (recOffset != 0) {
			debug(2, "Cyberflix::Archive: resource %u has out-of-range offset %#x, treating as empty",
					i, recOffset);
		}

		_resources.push_back(res);
	}

	return true;
}

Common::SeekableReadStream *Archive::createReadStreamForResource(uint32 index) const {
	if (!_stream || index >= _resources.size())
		return nullptr;

	const Resource &res = _resources[index];
	if (res.empty || res.length == 0)
		return nullptr;
	return new Common::SeekableSubReadStream(_stream.get(), res.dataOffset,
			res.dataOffset + res.length);
}

void Archive::close() {
	_stream.reset();
	_magic = _declaredSize = _firstSectionSize = _resourceCount = 0;
	_resources.clear();
	_name.clear();
}

void Archive::swapLongs(byte *data, uint32 size) {
	for (uint32 i = 0; i + 4 <= size; i += 4) {
		SWAP(data[i + 0], data[i + 3]);
		SWAP(data[i + 1], data[i + 2]);
	}
}

} // End of namespace Cyberflix

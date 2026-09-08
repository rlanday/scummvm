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
#include "common/ptr.h"
#include "common/stream.h"
#include "common/substream.h"
#include "common/textconsole.h"

#include "dreamfactory/archive.h"
#include "dreamfactory/debug.h"

namespace DreamFactory {

Archive::Archive() :
		_magic(0), _declaredSize(0),
		_firstSectionSize(0), _resourceCount(0) {
}

Archive::~Archive() {
	close();
}

bool Archive::open(Common::SeekableReadStream *stream, const Common::String &name) {
	close();

	// Take ownership of the stream immediately so every failure path below
	// frees it without a manual delete.
	Common::ScopedPtr<Common::SeekableReadStream> owned(stream);
	if (!owned)
		return false;

	_name = name;

	if (stream->size() < 0x28 || stream->size() > 0xffffffffLL) {
		warning("DreamFactory::Archive: '%s' has an unsupported container size", name.c_str());
		close();
		return false;
	}

	// Outer container header (little-endian scalars).
	if (!stream->seek(0)) {
		close();
		return false;
	}
	_magic            = stream->readUint32LE();
	_declaredSize     = stream->readUint32LE();
	if (stream->err() || stream->eos() || !stream->seek(0x10)) {
		close();
		return false;
	}
	_firstSectionSize = stream->readUint32LE();
	_resourceCount    = stream->readUint32LE();
	if (stream->err() || stream->eos() || !stream->seek(0x20)) {
		close();
		return false;
	}

	// Signature "LPPALPPA" at +0x20 (two big-endian 'APPL' tags).
	uint32 sig1 = stream->readUint32BE();
	uint32 sig2 = stream->readUint32BE();

	if (stream->err() || stream->eos() || _magic != 0x00010000 || sig1 != kSignature1 || sig2 != kSignature1) {
		warning("DreamFactory::Archive: '%s' is not an LPPALPPA container "
				"(magic=0x%08x sig=0x%08x%08x)", name.c_str(), _magic, sig1, sig2);
		close();
		return false;
	}

	if (_declaredSize != static_cast<uint32>(stream->size())) {
		// Non-fatal: warn but keep going, the header size is informational.
		warning("DreamFactory::Archive: '%s' declared size %u != file size %d",
				name.c_str(), _declaredSize, static_cast<int>(stream->size()));
	}

	_stream.reset(owned.release());

	if (!readDirectory()) {
		warning("DreamFactory::Archive: '%s' has an invalid resource directory", name.c_str());
		close();
		return false;
	}

	debugC(1, kDebugResources, "DreamFactory::Archive: opened '%s' (%u resources, %u bytes)",
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
		if (!_stream->seek(kDirectoryOffset + i * 4))
			return false;
		uint32 recOffset = _stream->readUint32LE();
		if (_stream->err() || _stream->eos())
			return false;

		Resource res;
		res.id = i;
		res.length = 0;
		res.info = 0;
		res.dataOffset = 0;
		res.empty = true;

		// Offsets of 0 (or any value pointing below the directory) are empty
		// placeholder slots: keep the index but mark it as having no data. The
		// record-header bound is computed in uint64 so an offset near
		// 0xffffffff cannot wrap past the check.
		if (recOffset != 0 && recOffset >= kDirectoryOffset &&
				static_cast<uint64>(recOffset) + 12 <= fileSize) {
			if (!_stream->seek(recOffset))
				return false;
			res.id = _stream->readUint32LE();
			res.length = _stream->readUint32LE();
			res.info = _stream->readUint32LE();
			res.dataOffset = recOffset + 12;
			res.empty = false;

			// Some files have an unpadded tail shorter than the declared payload.
			// Preserve the existing tolerance for any entry extending beyond EOF;
			// consumers still validate complete records within the clamped payload.
			if (static_cast<uint64>(res.dataOffset) + res.length > fileSize) {
				uint32 avail = fileSize - res.dataOffset;
				debugC(2, kDebugResources, "DreamFactory::Archive: resource %u length %u clamped to %u",
						i, res.length, avail);
				res.length = avail;
			}
		} else if (recOffset != 0) {
			debugC(2, kDebugResources, "DreamFactory::Archive: resource %u has out-of-range offset %#x, treating as empty",
					i, recOffset);
		}

		_resources.push_back(res);
		// Preserve the linear lookup's first non-empty match for duplicate IDs.
		if (!res.empty && !_resourceIndexById.contains(res.id))
			_resourceIndexById[res.id] = i;
		if (_stream->err() || _stream->eos())
			return false;
	}

	return true;
}

Common::SeekableReadStream *Archive::createReadStreamForResource(uint32 index) const {
	if (!_stream || index >= _resources.size())
		return nullptr;
	return createReadStreamForResource(_resources[index]);
}

Common::SeekableReadStream *Archive::createReadStreamForResource(const Resource &resource) const {
	if (!_stream || resource.empty || resource.length == 0)
		return nullptr;

	const uint64 end = static_cast<uint64>(resource.dataOffset) + resource.length;
	if (end > static_cast<uint64>(_stream->size()))
		return nullptr;

	return new Common::SeekableSubReadStream(_stream.get(), resource.dataOffset, end);
}

void Archive::close() {
	_stream.reset();
	_magic = _declaredSize = _firstSectionSize = _resourceCount = 0;
	_resources.clear();
	_resourceIndexById.clear();
	_name.clear();
}

void Archive::swapLongs(byte *data, uint32 size) {
	const uint32 completeBytes = size - size % 4;
	for (uint32 i = 0; i < completeBytes; i += 4) {
		SWAP(data[i + 0], data[i + 3]);
		SWAP(data[i + 1], data[i + 2]);
	}
}

int Archive::resourceIndexById(uint32 id) const {
	Common::HashMap<uint32, uint32>::const_iterator entry = _resourceIndexById.find(id);
	return entry == _resourceIndexById.end() ? -1 : static_cast<int>(entry->_value);
}

} // End of namespace DreamFactory

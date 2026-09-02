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
#include "common/endian.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/util.h"

#include "cyberflix/archive.h"

#include <math.h>

namespace CyberFlix {

/**
 * Master header resource tag (@c info==0x40000). Archive formats use this for
 * their top-level descriptor records; callers interpret the record-specific
 * fields through a data pointer at @c record+8 (== payload-4), so a field at
 * engine byte offset @c X is at @c payload[X-4].
 */
static const uint32 kMasterHeaderInfoTag = 0x00040000;

/** A recoverably bounds-checked view of bytes in one archive resource. */
class ResourceView {
public:
	ResourceView() : _data(nullptr), _size(0) {}
	ResourceView(const byte *data, uint64 size) : _data(data), _size(data ? size : 0) {}

	bool valid() const { return _data != nullptr; }
	uint64 size() const { return _size; }

	bool contains(uint64 offset, uint64 length = 1) const {
		return valid() && offset <= _size && length <= _size - offset;
	}

	const byte *dataAt(uint64 offset, uint64 length = 1) const {
		return contains(offset, length) ? _data + offset : nullptr;
	}

	ResourceView subview(uint64 offset, uint64 length) const {
		return contains(offset, length) ? ResourceView(_data + offset, length) : ResourceView();
	}

	ResourceView recordAt(uint64 offset, uint32 index, uint32 stride) const {
		if (stride == 0 || offset > _size)
			return ResourceView();
		const uint64 recordCount = (_size - offset) / stride;
		if (index >= recordCount)
			return ResourceView();
		return ResourceView(_data + offset + static_cast<uint64>(index) * stride, stride);
	}

	bool readUint16LE(uint64 offset, uint16 &value) const {
		const byte *p = dataAt(offset, sizeof(value));
		if (!p)
			return false;
		value = READ_LE_UINT16(p);
		return true;
	}

	bool readUint32LE(uint64 offset, uint32 &value) const {
		const byte *p = dataAt(offset, sizeof(value));
		if (!p)
			return false;
		value = READ_LE_UINT32(p);
		return true;
	}

	Common::String readPascalString(uint64 offset, bool allowTruncated = false) const {
		const byte *p = dataAt(offset);
		if (!p)
			return Common::String();
		uint length = *p;
		if (!contains(offset + 1, length)) {
			if (!allowTruncated)
				return Common::String();
			length = static_cast<uint>(_size - offset - 1);
		}
		return Common::String(reinterpret_cast<const char *>(p + 1), length);
	}

	bool pascalEqualsIgnoreCase(uint64 offset, const Common::String &name) const {
		const byte *p = dataAt(offset);
		if (!p)
			return false;
		const uint length = *p;
		if (length != name.size() || !contains(offset + 1, length))
			return false;
		for (uint i = 0; i < length; ++i)
			if (tolower(static_cast<unsigned char>(p[i + 1])) !=
						tolower(static_cast<unsigned char>(name[i])))
				return false;
		return true;
	}

private:
	const byte *_data;
	uint64 _size;
};

/** A validated sequence of fixed-size records within a ResourceView. */
class RecordRange {
public:
	RecordRange() : _offset(0), _count(0), _stride(0), _valid(false) {}
	RecordRange(const ResourceView &view, uint64 offset, uint32 count, uint32 stride) :
			_view(view), _offset(offset), _count(count), _stride(stride), _valid(false) {
		if (stride == 0 || offset > view.size())
			return;
		if (count > (view.size() - offset) / stride)
			return;
		_valid = view.valid();
	}

	bool valid() const { return _valid; }
	uint32 size() const { return _valid ? _count : 0; }
	const ResourceView &view() const { return _view; }
	ResourceView record(uint32 index) const {
		return _valid && index < _count ? _view.recordAt(_offset, index, _stride) : ResourceView();
	}

private:
	ResourceView _view;
	uint64 _offset;
	uint32 _count;
	uint32 _stride;
	bool _valid;
};

inline ResourceView resourcePayloadView(const Common::Array<byte> &fileData,
		const Archive::Resource &res) {
	if (res.empty || res.dataOffset > fileData.size() ||
			res.length > fileData.size() - res.dataOffset)
		return ResourceView();
	return ResourceView(fileData.begin() + res.dataOffset, res.length);
}

inline ResourceView resourceEngineView(const Common::Array<byte> &fileData,
		const Archive::Resource &res) {
	if (res.empty || res.dataOffset < 4 || res.dataOffset - 4 > fileData.size() ||
			static_cast<uint64>(res.length) + 4 > fileData.size() - (res.dataOffset - 4))
		return ResourceView();
	return ResourceView(fileData.begin() + res.dataOffset - 4, static_cast<uint64>(res.length) + 4);
}

bool openArchiveFile(const Common::String &name, const char *kind,
		Common::Array<byte> &fileData, Archive &archive);

inline int resourceIndexById(const Archive &archive, uint32 id) {
	for (uint32 i = 0; i < archive.getResourceCount(); ++i)
		if (!archive.getResource(i).empty && archive.getResource(i).id == id)
			return static_cast<int>(i);
	return -1;
}

/**
 * Index of the first non-empty master-header resource (info ==
 * kMasterHeaderInfoTag), or -1. Every container's open() locates its
 * top-level descriptor this way.
 */
inline int findMasterHeaderIndex(const Archive &archive) {
	for (uint32 i = 0; i < archive.getResourceCount(); ++i)
		if (!archive.getResource(i).empty && archive.getResource(i).info == kMasterHeaderInfoTag)
			return static_cast<int>(i);
	return -1;
}

/**
 * True when the half-open range [@p offset, @p offset + @p length) fits in
 * @p size.
 */
inline bool hasRange(uint64 size, uint64 offset, uint64 length) {
	return offset <= size && length <= size - offset;
}

/** Clamp a file-supplied count to the complete records that fit in a range. */
inline uint32 boundedRecordCount(uint32 count, uint64 size, uint64 offset, uint32 stride) {
	if (stride == 0 || offset > size)
		return 0;
	const uint64 available = (size - offset) / stride;
	return available < count ? static_cast<uint32>(available) : count;
}

inline bool fitsInt16(int64 value) {
	return value >= -32768 && value <= 32767;
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

} // End of namespace CyberFlix

#endif

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
#include "common/endian.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/path.h"

#include "cyberflix/set.h"
#include "cyberflix/sound.h" // kMasterHeaderInfoTag

namespace Cyberflix {

const byte *Set::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	// The runtime addresses every resource through a "record+8" data pointer
	// (the info tag), four bytes before the payload Archive exposes via
	// dataOffset (== record+12). Master-header offsets are in that frame, and
	// it is also the source the frame decoder expects ({uint16 H, uint16 P}
	// header == the info word). See files/decomp/stage-notes.md.
	if (res.empty || res.dataOffset < 4 || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset - 4;
}

const byte *Set::payload(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	if (res.empty || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset;
}

int Set::resourceIndexById(uint32 id) const {
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i)
		if (!_archive.getResource(i).empty && _archive.getResource(i).id == id)
			return (int)i;
	return -1;
}

const byte *Set::sceneRecord(uint32 scene) const {
	if (scene >= _sceneCount || _sceneTable < 0)
		return nullptr;
	const byte *table = payload((uint32)_sceneTable);
	if (!table)
		return nullptr;
	const byte *rec = table + scene * kSceneRecordStride;
	if (rec + kSceneRecordStride > _fileData.end())
		return nullptr;
	return rec;
}

const byte *Set::panoramaTable(uint32 scene, uint32 table, uint32 &count) const {
	count = 0;
	const byte *rec = sceneRecord(scene);
	if (!rec || table > 1)
		return nullptr;
	uint32 panoId = READ_LE_UINT32(rec + (table == 0 ? kScenePanoramaAOffset : kScenePanoramaBOffset));
	int idx = resourceIndexById(panoId);
	if (idx < 0)
		return nullptr;
	const byte *pano = payload((uint32)idx);
	if (!pano || pano + kPanoramaCountOffset + 4 > _fileData.end())
		return nullptr;
	uint32 c = READ_LE_UINT32(pano + kPanoramaCountOffset);
	// Bound the record array against the resource payload.
	uint32 len = _archive.getResource((uint32)idx).length;
	if ((uint64)c * kPanoramaRecordStride + 4 > len)
		return nullptr;
	count = c;
	return pano;
}

bool Set::open(const Common::String &name) {
	_master = -1;
	_sceneTable = -1;
	_sceneCount = 0;
	_width = _height = 0;
	_viewLeft = _viewTop = 0;
	_name = name;

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open set '%s'", name.c_str());
		return false;
	}
	uint32 size = (uint32)file.size();
	_fileData.resize(size);
	if (file.read(_fileData.begin(), size) != size) {
		warning("Cyberflix: could not read set '%s'", name.c_str());
		return false;
	}
	file.close();

	if (!_archive.open(new Common::MemoryReadStream(_fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid set container", name.c_str());
		return false;
	}

	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		if (!_archive.getResource(i).empty && _archive.getResource(i).info == kMasterHeaderInfoTag) {
			_master = (int)i;
			break;
		}
	}
	if (_master < 0) {
		warning("Cyberflix: set '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase((uint32)_master);
	if (!hdr || hdr + kSceneTableIdOffset + 4 > _fileData.end()) {
		warning("Cyberflix: set '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}
	_width = READ_LE_UINT16(hdr + kMasterWidthOffset);
	_height = READ_LE_UINT16(hdr + kMasterHeightOffset);
	_viewLeft = (int16)READ_LE_UINT16(hdr + kMasterViewLeftOffset);
	_viewTop = (int16)READ_LE_UINT16(hdr + kMasterViewTopOffset);

	// Embedded names TI.EXE copies out of the master header (FUN_004307f0):
	// the set's own name (what currentset() returns) and the default scene
	// and view used when opensetfile gets no scene/view arguments.
	const byte *end = _fileData.end();
	auto readPascal = [end](const byte *p) -> Common::String {
		if (!p || p >= end)
			return Common::String();
		uint len = *p;
		if (p + 1 + len > end)
			len = (uint)(end - (p + 1));
		return Common::String((const char *)p + 1, len);
	};
	_setName = readPascal(hdr + kMasterNameOffset);
	_defaultScene = readPascal(hdr + kMasterDefaultSceneOffset);
	_defaultView = readPascal(hdr + kMasterDefaultViewOffset);

	uint32 sceneTableId = READ_LE_UINT32(hdr + kSceneTableIdOffset);
	_sceneTable = resourceIndexById(sceneTableId);
	if (_sceneTable < 0) {
		warning("Cyberflix: set '%s' references missing scene table %u", name.c_str(), sceneTableId);
		_master = -1;
		return false;
	}
	// The scene table is a tight array of fixed-size records, no count header.
	_sceneCount = _archive.getResource((uint32)_sceneTable).length / kSceneRecordStride;

	debug(1, "Cyberflix: opened set '%s': %ux%u, %u scene(s)",
			name.c_str(), _width, _height, _sceneCount);
	return true;
}

Common::String Set::sceneName(uint32 index) const {
	const byte *rec = sceneRecord(index);
	if (!rec)
		return Common::String();
	byte len = rec[kSceneNameOffset];
	if (len == 0 || len >= 16 || rec + kSceneNameOffset + 1 + len > _fileData.end())
		return Common::String();
	return Common::String((const char *)rec + kSceneNameOffset + 1, len);
}

int Set::findScene(const Common::String &name) const {
	for (uint32 i = 0; i < _sceneCount; ++i)
		if (sceneName(i).equalsIgnoreCase(name))
			return (int)i;
	return -1;
}

uint32 Set::angleCount(uint32 scene, uint32 table) const {
	uint32 count = 0;
	panoramaTable(scene, table, count);
	return count;
}

int Set::findView(uint32 scene, const Common::String &name) const {
	if (name.empty())
		return -1;
	const byte *rec = sceneRecord(scene);
	if (!rec)
		return -1;
	uint32 dirId = READ_LE_UINT32(rec + kSceneViewDirOffset);
	int idx = resourceIndexById(dirId);
	if (idx < 0)
		return -1;
	const byte *dir = payload((uint32)idx);
	if (!dir || dir + kViewDirRecordsOffset > _fileData.end())
		return -1;
	uint32 count = READ_LE_UINT32(dir + kViewDirCountOffset);
	for (uint32 i = 0; i < count; ++i) {
		const byte *v = dir + kViewDirRecordsOffset + i * kViewRecordStride;
		if (v + kViewRecordStride > _fileData.end())
			break;
		byte len = v[kViewNameOffset];
		if (len && len < 16 &&
				name.equalsIgnoreCase(Common::String((const char *)v + kViewNameOffset + 1, len)))
			return (int)i;
	}
	return -1;
}

int Set::angleForView(uint32 scene, uint32 table, int viewIdx) const {
	if (viewIdx < 0)
		return -1;
	uint32 count = 0;
	const byte *pano = panoramaTable(scene, table, count);
	if (!pano)
		return -1;
	// TI's panorama records run from base+0xc with base = payload-4, i.e.
	// payload+8; the view-index tag sits at record+0x38 (FUN_004425e0 reads
	// piVar5[0xe]). Records not facing a view directly are tagged -1.
	for (uint32 i = 0; i < count; ++i) {
		const byte *r = pano + 8 + i * kPanoramaRecordStride;
		if (r + kPanoramaRecordStride > _fileData.end())
			break;
		if ((int32)READ_LE_UINT32(r + 0x38) == viewIdx)
			return (int)i;
	}
	return -1;
}

bool Set::renderScene(uint32 scene, uint32 table, uint32 angle, FrameImage &out) {
	uint32 count = 0;
	const byte *pano = panoramaTable(scene, table, count);
	if (!pano) {
		warning("Cyberflix: set '%s' scene %u panorama table %u missing", _name.c_str(), scene, table);
		return false;
	}
	if (angle >= count) {
		warning("Cyberflix: set '%s' scene %u table %u angle %u out of range (%u)",
				_name.c_str(), scene, table, angle, count);
		return false;
	}

	// The panorama is a continuous cyclic delta-animation (TI.EXE FUN_00442970:
	// it advances/wraps a frame index and applies each frame onto the retained
	// framebuffer via FUN_00423600). Frame 0 is the cold-start keyframe; every
	// later angle is inter-coded against the previous one. So to show an
	// arbitrary angle from cold we replay frames 0..angle, exactly the buffer
	// state the engine would have built up. Decoding a delta angle standalone
	// would leave its "copy from previous" regions unwritten (visible garbage).
	FrameSequence seq;
	for (uint32 a = 0; a <= angle; ++a) {
		uint32 frameId = READ_LE_UINT32(pano + 4 + a * kPanoramaRecordStride + kPanoramaFrameIdOffset);
		int idx = resourceIndexById(frameId);
		if (idx < 0) {
			warning("Cyberflix: set '%s' scene %u angle %u references missing frame res %u",
					_name.c_str(), scene, a, frameId);
			return false;
		}
		const byte *frame = engineBase((uint32)idx);
		if (!frame)
			return false;
		uint32 frameLen = _archive.getResource((uint32)idx).length + 4; // payload + info word
		if (seq.applyFrame(frame, frameLen) == 0) {
			warning("Cyberflix: set '%s' scene %u angle %u frame decode failed", _name.c_str(), scene, a);
			return false;
		}
	}

	seq.copyTo(out);
	return true;
}

bool Set::loadSetPalette(byte *rgb) const {
	if (_fileData.empty())
		return false;
	return loadPalette(_fileData.begin(), _fileData.size(), rgb);
}

} // End of namespace Cyberflix

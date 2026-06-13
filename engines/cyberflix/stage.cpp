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

#include "cyberflix/stage.h"
#include "cyberflix/sound.h" // kMasterHeaderInfoTag

namespace Cyberflix {

const byte *Stage::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	// The runtime addresses every resource through a "record+8" data pointer
	// (the info tag), four bytes before the payload Archive exposes via
	// dataOffset (== record+12). All master-header/node-table offsets are in
	// that frame. See files/decomp/stage-notes.md.
	if (res.empty || res.dataOffset < 4 || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset - 4;
}

int Stage::resourceIndexById(uint32 id) const {
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i)
		if (!_archive.getResource(i).empty && _archive.getResource(i).id == id)
			return (int)i;
	return -1;
}

const byte *Stage::nodeRecord(uint32 node) const {
	if (node >= _nodeCount || _master < 0)
		return nullptr;
	const byte *hdr = engineBase((uint32)_master);
	if (!hdr)
		return nullptr;
	const byte *rec = hdr + kNodeTableOffset + node * kNodeRecordStride;
	if (rec + kNodeRecordStride > _fileData.end())
		return nullptr;
	return rec;
}

Common::String Stage::nodeName(uint32 node) const {
	const byte *rec = nodeRecord(node);
	if (!rec)
		return Common::String();
	const byte *p = rec + kNodeNameOffset;
	uint len = *p;
	if (len > kNodeRecordStride - kNodeNameOffset - 1)
		len = kNodeRecordStride - kNodeNameOffset - 1;
	return Common::String((const char *)p + 1, len);
}

bool Stage::open(const Common::String &name) {
	_master = -1;
	_nodeCount = 0;
	_width = _height = 0;
	_name = name;

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open stage '%s'", name.c_str());
		return false;
	}
	uint32 size = (uint32)file.size();
	_fileData.resize(size);
	if (file.read(_fileData.begin(), size) != size) {
		warning("Cyberflix: could not read stage '%s'", name.c_str());
		return false;
	}
	file.close();

	if (!_archive.open(new Common::MemoryReadStream(_fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid stage container", name.c_str());
		return false;
	}

	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		if (!_archive.getResource(i).empty && _archive.getResource(i).info == kMasterHeaderInfoTag) {
			_master = (int)i;
			break;
		}
	}
	if (_master < 0) {
		warning("Cyberflix: stage '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase((uint32)_master);
	if (!hdr || hdr + kNodeTableOffset > _fileData.end()) {
		warning("Cyberflix: stage '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}
	_width = READ_LE_UINT16(hdr + kMasterWidthOffset);
	_height = READ_LE_UINT16(hdr + kMasterHeightOffset);
	_nodeCount = READ_LE_UINT32(hdr + kNodeCountOffset);

	// Bound the node table against the file so a corrupt count can't run off.
	const byte *tableEnd = hdr + kNodeTableOffset + (uint32)_nodeCount * kNodeRecordStride;
	if (tableEnd > _fileData.end()) {
		warning("Cyberflix: stage '%s' node table overruns file (count %u)", name.c_str(), _nodeCount);
		_nodeCount = 0;
	}

	debug(1, "Cyberflix: opened stage '%s': %ux%u, %u node(s)",
			name.c_str(), _width, _height, _nodeCount);
	return true;
}

bool Stage::renderNode(uint32 node, FrameImage &out) {
	if (node >= _nodeCount) {
		warning("Cyberflix: stage '%s' node %u out of range (%u)", _name.c_str(), node, _nodeCount);
		return false;
	}

	// Inter-coded frames: scan back to the nearest keyframe, then replay every
	// node's background frame forward into a persistent surface (FUN_0040b180).
	uint32 start = node;
	while (start > 0) {
		const byte *rec = nodeRecord(start);
		if (!rec)
			return false;
		if (READ_LE_UINT32(rec + kNodeFlagsOffset) & kNodeFlagKeyframe)
			break;
		--start;
	}

	FrameSequence seq;
	for (uint32 n = start; n <= node; ++n) {
		const byte *rec = nodeRecord(n);
		if (!rec)
			return false;
		uint32 imgId = READ_LE_UINT32(rec + kNodeImageResOffset);
		int idx = resourceIndexById(imgId);
		if (idx < 0) {
			warning("Cyberflix: stage '%s' node %u references missing image res %u",
					_name.c_str(), n, imgId);
			return false;
		}
		const byte *frame = engineBase((uint32)idx);
		if (!frame)
			return false;
		uint32 frameLen = _archive.getResource((uint32)idx).length + 4; // payload + info word
		if (seq.applyFrame(frame, frameLen) == 0) {
			warning("Cyberflix: stage '%s' node %u frame decode failed", _name.c_str(), n);
			return false;
		}
	}

	seq.copyTo(out);
	return true;
}

bool Stage::loadStagePalette(byte *rgb) const {
	if (_fileData.empty())
		return false;
	return loadPalette(_fileData.begin(), _fileData.size(), rgb);
}

} // End of namespace Cyberflix

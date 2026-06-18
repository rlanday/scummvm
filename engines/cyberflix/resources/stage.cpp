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
#include "common/ptr.h"

#include "cyberflix/stage.h"
#include "cyberflix/resource_helpers.h"

namespace Cyberflix {

static bool pointInButtonRect(const byte *rec, int16 x, int16 y) {
	int16 top = static_cast<int16>(READ_LE_UINT16(rec + Stage::kButtonRectOffset));
	int16 left = static_cast<int16>(READ_LE_UINT16(rec + Stage::kButtonRectOffset + 2));
	int16 bottom = static_cast<int16>(READ_LE_UINT16(rec + Stage::kButtonRectOffset + 4));
	int16 right = static_cast<int16>(READ_LE_UINT16(rec + Stage::kButtonRectOffset + 6));
	return x >= left && x < right && y >= top && y < bottom;
}

const byte *Stage::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	return resourceEngineBase(_fileData, _archive.getResource(index));
}

const byte *Stage::payload(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	if (res.empty || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset;
}

int Stage::resourceIndexById(uint32 id) const {
	return Cyberflix::resourceIndexById(_archive, id);
}

Common::String Stage::pascalString(const byte *p) const {
	return readPascalString(p, _fileData);
}

const Script *Stage::scriptById(uint32 id) const {
	int idx = resourceIndexById(id);
	if (idx < 0 || static_cast<uint32>(idx) >= _scripts.size())
		return nullptr;
	return _scripts[static_cast<uint32>(idx)].get();
}

const byte *Stage::nodeRecord(uint32 node) const {
	if (node >= _nodeCount || _master < 0)
		return nullptr;
	const byte *hdr = engineBase(static_cast<uint32>(_master));
	if (!hdr)
		return nullptr;
	const byte *rec = hdr + kNodeTableOffset + node * kNodeRecordStride;
	if (rec + kNodeRecordStride > _fileData.end())
		return nullptr;
	return rec;
}

const byte *Stage::buttonTable(uint32 node, uint32 &count, uint32 &length) const {
	count = length = 0;
	const byte *rec = nodeRecord(node);
	if (!rec)
		return nullptr;
	uint32 tableId = READ_LE_UINT32(rec + kNodeButtonTableOffset);
	int idx = resourceIndexById(tableId);
	if (idx < 0)
		return nullptr;
	const Archive::Resource &res = _archive.getResource(static_cast<uint32>(idx));
	if (res.length < kButtonCountOffset + 4)
		return nullptr;
	const byte *table = payload(static_cast<uint32>(idx));
	if (!table)
		return nullptr;
	uint32 c = READ_LE_UINT32(table + kButtonCountOffset);
	if (kButtonRecordsOffset + static_cast<uint64>(c) * kButtonRecordStride > res.length)
		return nullptr;
	count = c;
	length = res.length;
	return table;
}

const byte *Stage::buttonRecord(uint32 node, const Common::String &button) const {
	uint32 count = 0, length = 0;
	const byte *table = buttonTable(node, count, length);
	if (!table)
		return nullptr;
	for (uint32 i = 0; i < count; ++i) {
		const byte *rec = table + kButtonRecordsOffset + i * kButtonRecordStride;
		if (kButtonRecordsOffset + i * kButtonRecordStride + kButtonRecordStride > length)
			break;
		if (button.equalsIgnoreCase(pascalString(rec + kButtonNameOffset)))
			return rec;
	}
	return nullptr;
}

Common::String Stage::nodeName(uint32 node) const {
	const byte *rec = nodeRecord(node);
	if (!rec)
		return Common::String();
	const byte *p = rec + kNodeNameOffset;
	uint len = *p;
	if (len > kNodeRecordStride - kNodeNameOffset - 1)
		len = kNodeRecordStride - kNodeNameOffset - 1;
	return Common::String(reinterpret_cast<const char *>(p ) + 1, len);
}

int Stage::findNode(const Common::String &name) const {
	for (uint32 i = 0; i < _nodeCount; ++i)
		if (nodeName(i).equalsIgnoreCase(name))
			return static_cast<int>(i);
	return -1;
}

Common::String Stage::hitTestButton(uint32 node, int16 x, int16 y) const {
	uint32 count = 0, length = 0;
	const byte *table = buttonTable(node, count, length);
	if (!table)
		return Common::String();
	for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
		const byte *rec = table + kButtonRecordsOffset + static_cast<uint32>(i) * kButtonRecordStride;
		if (kButtonRecordsOffset + static_cast<uint32>(i) * kButtonRecordStride + kButtonRecordStride > length)
			break;
		if (pointInButtonRect(rec, x, y))
			return pascalString(rec + kButtonNameOffset);
	}
	return Common::String();
}

bool Stage::pointInButton(uint32 node, const Common::String &button, int16 x, int16 y) const {
	const byte *rec = buttonRecord(node, button);
	return rec && pointInButtonRect(rec, x, y);
}

const Script *Stage::stageScript() const {
	return scriptById(_stageScriptId);
}

const Script *Stage::nodeScript(uint32 node) const {
	const byte *rec = nodeRecord(node);
	if (!rec)
		return nullptr;
	return scriptById(READ_LE_UINT32(rec + kNodeScriptResOffset));
}

const Script *Stage::buttonScript(uint32 node, const Common::String &button) const {
	const byte *rec = buttonRecord(node, button);
	if (!rec)
		return nullptr;
	return scriptById(READ_LE_UINT32(rec + kButtonScriptOffset));
}

bool Stage::hasButton(uint32 node, const Common::String &button) const {
	return buttonRecord(node, button) != nullptr;
}

bool Stage::open(const Common::String &name) {
	_master = -1;
	_nodeCount = 0;
	_stageScriptId = 0;
	_scripts.clear();
	_width = _height = 0;
	_name = name;

	if (!openArchiveFile(name, "stage", _fileData, _archive))
		return false;

	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		if (!_archive.getResource(i).empty && _archive.getResource(i).info == kMasterHeaderInfoTag) {
			_master = static_cast<int>(i);
			break;
		}
	}
	if (_master < 0) {
		warning("Cyberflix: stage '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase(static_cast<uint32>(_master));
	if (!hdr || hdr + kNodeTableOffset > _fileData.end()) {
		warning("Cyberflix: stage '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}
	_width = READ_LE_UINT16(hdr + kMasterWidthOffset);
	_height = READ_LE_UINT16(hdr + kMasterHeightOffset);
	_stageScriptId = READ_LE_UINT32(hdr + kStageScriptIdOffset);
	_nodeCount = READ_LE_UINT32(hdr + kNodeCountOffset);

	// Bound the node table against the file so a corrupt count can't run off.
	const byte *tableEnd = hdr + kNodeTableOffset + static_cast<uint32>(_nodeCount) * kNodeRecordStride;
	if (tableEnd > _fileData.end()) {
		warning("Cyberflix: stage '%s' node table overruns file (count %u)", name.c_str(), _nodeCount);
		_nodeCount = 0;
	}

	_scripts.resize(_archive.getResourceCount());
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		const Archive::Resource &res = _archive.getResource(i);
		if (res.empty || res.info != Script::kScriptInfoTag)
			continue;
		Common::ScopedPtr<Common::SeekableReadStream> stream(_archive.createReadStreamForResource(i));
		Common::SharedPtr<Script> script(new Script());
		if (stream && script->parse(stream.get()))
			_scripts[i] = script;
		else
			warning("Cyberflix: failed to parse stage '%s' script resource %u", name.c_str(), res.id);
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
		const byte *frame = engineBase(static_cast<uint32>(idx));
		if (!frame)
			return false;
		uint32 frameLen = _archive.getResource(static_cast<uint32>(idx)).length + 4; // payload + info word
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

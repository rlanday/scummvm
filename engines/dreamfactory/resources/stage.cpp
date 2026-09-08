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

#include "dreamfactory/stage.h"
#include "dreamfactory/debug.h"
#include "dreamfactory/resource_helpers.h"

namespace DreamFactory {

static bool pointInButtonRect(const ResourceView &record, int16 x, int16 y) {
	const byte *rec = record.dataAt(0, Stage::kButtonRecordStride);
	if (!rec)
		return false;
	int16 top = static_cast<int16>(READ_LE_UINT16(rec + Stage::kButtonRectOffset));
	int16 left = static_cast<int16>(READ_LE_UINT16(rec + Stage::kButtonRectOffset + 2));
	int16 bottom = static_cast<int16>(READ_LE_UINT16(rec + Stage::kButtonRectOffset + 4));
	int16 right = static_cast<int16>(READ_LE_UINT16(rec + Stage::kButtonRectOffset + 6));
	return x >= left && x < right && y >= top && y < bottom;
}

ResourceView Stage::engineView(uint32 index) const {
	if (index >= _resourceFile.archive().getResourceCount())
		return ResourceView();
	return resourceEngineView(_resourceFile.data(), _resourceFile.archive().getResource(index));
}

ResourceView Stage::payloadView(uint32 index) const {
	if (index >= _resourceFile.archive().getResourceCount())
		return ResourceView();
	return resourcePayloadView(_resourceFile.data(), _resourceFile.archive().getResource(index));
}

int Stage::resourceIndexById(uint32 id) const {
	return DreamFactory::resourceIndexById(_resourceFile.archive(), id);
}

bool Stage::parseScriptResource(uint32 id) {
	if (id == 0)
		return false;
	int idx = resourceIndexById(id);
	if (idx < 0 || static_cast<uint32>(idx) >= _scripts.size())
		return false;
	if (_scripts[static_cast<uint32>(idx)])
		return true;

	Common::ScopedPtr<Common::SeekableReadStream> stream(_resourceFile.archive().createReadStreamForResource(static_cast<uint32>(idx)));
	Common::SharedPtr<Script> script(new Script());
	if (stream && script->parse(stream.get())) {
		_scripts[static_cast<uint32>(idx)] = script;
		return true;
	}

	warning("DreamFactory: failed to parse stage '%s' script resource %u", _name.c_str(), id);
	return false;
}

const Script *Stage::scriptById(uint32 id) const {
	int idx = resourceIndexById(id);
	if (idx < 0 || static_cast<uint32>(idx) >= _scripts.size())
		return nullptr;
	return _scripts[static_cast<uint32>(idx)].get();
}

ResourceView Stage::nodeRecord(uint32 node) const {
	if (node >= _nodeCount || _master < 0)
		return ResourceView();
	return engineView(static_cast<uint32>(_master)).recordAt(
			kNodeTableOffset, node, kNodeRecordStride);
}

RecordRange Stage::buttonTable(uint32 node) const {
	uint32 tableId;
	if (!nodeRecord(node).readUint32LE(kNodeButtonTableOffset, tableId))
		return RecordRange();
	int idx = resourceIndexById(tableId);
	if (idx < 0)
		return RecordRange();
	const ResourceView table = payloadView(static_cast<uint32>(idx));
	uint32 count;
	if (!table.readUint32LE(kButtonCountOffset, count))
		return RecordRange();
	return RecordRange(table, kButtonRecordsOffset, count, kButtonRecordStride);
}

ResourceView Stage::buttonRecord(uint32 node, const Common::String &button) const {
	const RecordRange buttons = buttonTable(node);
	for (uint32 i = 0; i < buttons.size(); ++i) {
		const ResourceView record = buttons.record(i);
		// In-place compare: this runs per button record on mouse-move hit tests.
		if (record.pascalEqualsIgnoreCase(kButtonNameOffset, button))
			return record;
	}
	return ResourceView();
}

Common::String Stage::nodeName(uint32 node) const {
	return nodeRecord(node).readPascalString(kNodeNameOffset, true);
}

int Stage::findNode(const Common::String &name) const {
	for (uint32 i = 0; i < _nodeCount; ++i)
		if (nodeName(i).equalsIgnoreCase(name))
			return static_cast<int>(i);
	return -1;
}

Common::String Stage::hitTestButton(uint32 node, int16 x, int16 y) const {
	const RecordRange buttons = buttonTable(node);
	for (int i = static_cast<int>(buttons.size()) - 1; i >= 0; --i) {
		const ResourceView record = buttons.record(static_cast<uint>(i));
		if (pointInButtonRect(record, x, y))
			return record.readPascalString(kButtonNameOffset);
	}
	return Common::String();
}

bool Stage::pointInButton(uint32 node, const Common::String &button, int16 x, int16 y) const {
	return pointInButtonRect(buttonRecord(node, button), x, y);
}

const Script *Stage::stageScript() const {
	return scriptById(_stageScriptId);
}

const Script *Stage::nodeScript(uint32 node) const {
	uint32 scriptId;
	if (!nodeRecord(node).readUint32LE(kNodeScriptResOffset, scriptId))
		return nullptr;
	return scriptById(scriptId);
}

const Script *Stage::buttonScript(uint32 node, const Common::String &button) const {
	uint32 scriptId;
	if (!buttonRecord(node, button).readUint32LE(kButtonScriptOffset, scriptId))
		return nullptr;
	return scriptById(scriptId);
}

bool Stage::hasButton(uint32 node, const Common::String &button) const {
	return buttonRecord(node, button).valid();
}

void Stage::reset() {
	_scripts.clear();
	_resourceFile.close();

	_master = -1;
	_nodeCount = 0;
	_stageScriptId = 0;
	_width = _height = 0;
	_name.clear();
}

bool Stage::open(const Common::String &name) {
	reset();
	_name = name;

	if (!_resourceFile.open(name, "stage"))
		return false;

	_master = findMasterHeaderIndex(_resourceFile.archive());
	if (_master < 0) {
		warning("DreamFactory: stage '%s' has no master header", name.c_str());
		reset();
		return false;
	}

	const ResourceView master = engineView(static_cast<uint32>(_master));
	const byte *hdr = master.dataAt(0, kNodeTableOffset);
	if (!hdr) {
		warning("DreamFactory: stage '%s' master header truncated", name.c_str());
		reset();
		return false;
	}
	_width = READ_LE_UINT16(hdr + kMasterWidthOffset);
	_height = READ_LE_UINT16(hdr + kMasterHeightOffset);
	_stageScriptId = READ_LE_UINT32(hdr + kStageScriptIdOffset);
	_nodeCount = READ_LE_UINT32(hdr + kNodeCountOffset);

	if (!RecordRange(master, kNodeTableOffset, _nodeCount, kNodeRecordStride).valid()) {
		warning("DreamFactory: stage '%s' node table overruns file (count %u)", name.c_str(), _nodeCount);
		_nodeCount = 0;
	}

	_scripts.resize(_resourceFile.archive().getResourceCount());
	for (const Archive::Resource &res : _resourceFile.archive().resources()) {
		if (res.empty || res.info != Script::kScriptInfoTag)
			continue;
		parseScriptResource(res.id);
	}

	// Stage metadata is authoritative for script identity. We first hit this in
	// BLKJACK.STG: node 0's script resource is referenced from the node table,
	// but its directory info word is 0x1F44 because it starts with comment
	// opcodes rather than the usual script tag 0x0FA1.
	parseScriptResource(_stageScriptId);
	for (uint32 node = 0; node < _nodeCount; ++node) {
		uint32 nodeScriptId;
		if (!nodeRecord(node).readUint32LE(kNodeScriptResOffset, nodeScriptId))
			continue;
		parseScriptResource(nodeScriptId);

		const RecordRange buttons = buttonTable(node);
		for (uint32 button = 0; button < buttons.size(); ++button) {
			uint32 buttonScriptId;
			if (buttons.record(button).readUint32LE(kButtonScriptOffset, buttonScriptId))
				parseScriptResource(buttonScriptId);
		}
	}

	debugC(1, kDebugResources, "DreamFactory: opened stage '%s': %ux%u, %u node(s)",
			name.c_str(), _width, _height, _nodeCount);
	return true;
}

bool Stage::renderNode(uint32 node, FrameImage &out) {
	if (node >= _nodeCount) {
		warning("DreamFactory: stage '%s' node %u out of range (%u)", _name.c_str(), node, _nodeCount);
		return false;
	}

	// Inter-coded frames: scan back to the nearest keyframe, then replay every
	// node's background frame forward into a persistent surface (FUN_0040b180).
	uint32 start = node;
	while (start > 0) {
		uint32 flags;
		if (!nodeRecord(start).readUint32LE(kNodeFlagsOffset, flags))
			return false;
		if (flags & kNodeFlagKeyframe)
			break;
		--start;
	}

	FrameSequence seq;
	for (uint32 n = start; n <= node; ++n) {
		uint32 imgId;
		if (!nodeRecord(n).readUint32LE(kNodeImageResOffset, imgId))
			return false;
		int idx = resourceIndexById(imgId);
		if (idx < 0) {
			warning("DreamFactory: stage '%s' node %u references missing image res %u",
					_name.c_str(), n, imgId);
			return false;
		}
		const ResourceView frame = engineView(static_cast<uint32>(idx));
		if (!frame.valid())
			return false;
		if (seq.applyFrame(frame.dataAt(0, frame.size()), static_cast<uint32>(frame.size())) == 0) {
			warning("DreamFactory: stage '%s' node %u frame decode failed", _name.c_str(), n);
			return false;
		}
	}

	seq.copyTo(out);
	return true;
}

bool Stage::loadStagePalette(Palette &rgb) const {
	if (_resourceFile.data().empty())
		return false;
	return loadPalette(_resourceFile.data().begin(), _resourceFile.data().size(), rgb);
}

} // End of namespace DreamFactory

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

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/ptr.h"
#include "graphics/surface.h"

#include "cyberflix/image.h"
#include "cyberflix/puppet.h"
#include "cyberflix/audio/cbx_audio.h"
#include "cyberflix/resource_helpers.h"

namespace CyberFlix {

ResourceView Puppet::engineView(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return ResourceView();
	return resourceEngineView(_fileData, _archive.getResource(index));
}

ResourceView Puppet::payloadView(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return ResourceView();
	return resourcePayloadView(_fileData, _archive.getResource(index));
}

int Puppet::resourceIndexById(uint32 id) const {
	return CyberFlix::resourceIndexById(_archive, id);
}

Common::SharedPtr<Script> Puppet::parseScriptResource(uint32 resId) const {
	int idx = resourceIndexById(resId);
	if (idx < 0)
		return Common::SharedPtr<Script>();
	Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource(static_cast<uint32>(idx)));
	Common::SharedPtr<Script> script(new Script());
	if (s && script->parse(s.get()))
		return script;
	return Common::SharedPtr<Script>();
}

bool Puppet::open(const Common::String &name) {
	_master = -1;
	_globalResourceId = 0;
	_baseCount = 0;
	Common::fill(_baseDisplayListResources,
			_baseDisplayListResources + ARRAYSIZE(_baseDisplayListResources), 0);
	_scripts.clear();
	_actions.clear();
	_scriptIndexByName.clear();
	_actionIndexByName.clear();
	_celCache.clear();
	_actionFrameCache.clear();
	_puppetName.clear();
	_sourceName = name;
	_sourceName.toLowercase();

	if (!openArchiveFile(name, "puppet", _fileData, _archive))
		return false;

	_master = findMasterHeaderIndex(_archive);
	if (_master < 0) {
		warning("CyberFlix: puppet '%s' has no master header", name.c_str());
		_archive.close();
		_fileData.clear();
		return false;
	}

	const ResourceView master = engineView(static_cast<uint32>(_master));
	const byte *hdr = master.dataAt(0, kMasterBaseCountOffset + 4);
	if (!hdr) {
		warning("CyberFlix: puppet '%s' master header truncated", name.c_str());
		_master = -1;
		_archive.close();
		_fileData.clear();
		return false;
	}
	_globalResourceId = READ_LE_UINT32(hdr + kMasterGlobalResourceOffset);
	_puppetName = master.readPascalString(kMasterNameOffset);
	_baseCount = READ_LE_UINT32(hdr + kMasterBaseCountOffset);
	_baseCount = boundedRecordCount(_baseCount, master.size(), kMasterBaseTableOffset, kMasterBaseStride);
	const RecordRange actions(master, kMasterBaseTableOffset, _baseCount, kMasterBaseStride);
	for (uint32 i = 0; i < actions.size(); ++i) {
		const ResourceView actionRecord = actions.record(i);
		const byte *entry = actionRecord.dataAt(0, kMasterBaseStride);
		ActionEntry action;
		action.baseState = static_cast<int16>(READ_LE_UINT16(entry + kMasterActionBaseStateOffset));
		action.frameCount = READ_LE_UINT16(entry + kMasterActionFrameCountOffset);
		action.audioResourceId = READ_LE_UINT32(entry + kMasterActionAudioResourceOffset);
		action.frameResourceId = READ_LE_UINT32(entry + kMasterActionFrameResourceOffset);
		action.text = actionRecord.readPascalString(kMasterActionTextOffset);
		action.name = actionRecord.readPascalString(kMasterActionNameOffset);
		action.cacheIndex = i;
		const uint32 actionIndex = _actions.size();
		_actions.push_back(action);
		Common::String actionName = action.name;
		actionName.toLowercase();
		if (!actionName.empty() && !_actionIndexByName.contains(actionName))
			_actionIndexByName[actionName] = actionIndex;
		Common::String actionText = action.text;
		actionText.toLowercase();
		if (!actionText.empty() && !_actionIndexByName.contains(actionText))
			_actionIndexByName[actionText] = actionIndex;
	}

	int baseIdx = resourceIndexById(kBaseControllerResourceId);
	const ResourceView base = baseIdx >= 0
			? engineView(static_cast<uint32>(baseIdx)) : ResourceView();
	if (base.contains(kBaseControllerResourceOffset, kBaseControllerStateCount * 4)) {
		for (uint32 i = 0; i < kBaseControllerStateCount; ++i)
			_baseDisplayListResources[i] =
					READ_LE_UINT32(base.dataAt(kBaseControllerResourceOffset + i * 4, 4));
	}

	int tableIdx = resourceIndexById(kScriptTableResourceId);
	const ResourceView table = tableIdx >= 0
			? payloadView(static_cast<uint32>(tableIdx)) : ResourceView();
	const byte *tableData = table.dataAt(0, kScriptTableEntriesOffset);
	if (!tableData) {
		warning("CyberFlix: puppet '%s' script table missing", name.c_str());
		return true;
	}

	uint32 count = boundedRecordCount(READ_LE_UINT16(tableData + kScriptTableCountOffset),
			table.size(), kScriptTableEntriesOffset, kScriptEntryStride);
	const RecordRange scriptRecords(table, kScriptTableEntriesOffset, count, kScriptEntryStride);
	for (uint32 i = 0; i < scriptRecords.size(); ++i) {
		const ResourceView scriptRecord = scriptRecords.record(i);
		const byte *entry = scriptRecord.dataAt(0, kScriptEntryStride);
		ScriptEntry se;
		se.resId = READ_LE_UINT32(entry + kScriptEntryResourceOffset);
		se.name = scriptRecord.readPascalString(kScriptEntryNameOffset);
		se.script = parseScriptResource(se.resId);
		if (!se.name.empty()) {
			Common::String key = se.name;
			key.toLowercase();
			if (!_scriptIndexByName.contains(key))
				_scriptIndexByName[key] = _scripts.size();
			_scripts.push_back(se);
		}
	}

	debug(1, "CyberFlix: opened puppet '%s': name '%s', %u script(s), %u action(s)",
			_sourceName.c_str(), _puppetName.c_str(), _scripts.size(), _actions.size());
	return true;
}

Common::String Puppet::scriptName(uint32 index) const {
	if (index >= _scripts.size())
		return Common::String();
	return _scripts[index].name;
}

Common::SharedPtr<Script> Puppet::scriptByName(const Common::String &name) const {
	Common::String key = name;
	key.toLowercase();
	Common::HashMap<Common::String, uint32>::const_iterator it = _scriptIndexByName.find(key);
	if (it != _scriptIndexByName.end() && it->_value < _scripts.size())
		return _scripts[it->_value].script;
	return Common::SharedPtr<Script>();
}

const Puppet::ActionEntry *Puppet::actionAt(uint32 index) const {
	if (index >= _actions.size())
		return nullptr;
	return &_actions[index];
}

const Puppet::ActionEntry *Puppet::actionByName(const Common::String &name) const {
	if (name.empty())
		return nullptr;
	Common::String key = name;
	key.toLowercase();
	Common::HashMap<Common::String, uint32>::const_iterator it = _actionIndexByName.find(key);
	if (it != _actionIndexByName.end() && it->_value < _actions.size())
		return &_actions[it->_value];
	return nullptr;
}

Common::SharedPtr<CelImage> Puppet::celResource(uint32 resId) const {
	Common::HashMap<uint32, Common::SharedPtr<CelImage> >::const_iterator cached =
			_celCache.find(resId);
	if (cached != _celCache.end())
		return cached->_value;

	int idx = resourceIndexById(resId);
	if (idx < 0)
		return Common::SharedPtr<CelImage>();
	const Archive::Resource &res = _archive.getResource(static_cast<uint32>(idx));
	uint16 width = static_cast<uint16>(res.info >> 16);
	uint16 height = static_cast<uint16>(res.info & 0xffff);
	if (!width || !height)
		return Common::SharedPtr<CelImage>();

	Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource(static_cast<uint32>(idx)));
	if (!s)
		return Common::SharedPtr<CelImage>();
	Common::SharedPtr<CelImage> cel(new CelImage());
	bool ok = decodeCel(*s, width, height, *cel);
	if (!ok) {
		warning("CyberFlix: puppet '%s' could not decode cel resource %u",
				_sourceName.c_str(), resId);
		return Common::SharedPtr<CelImage>();
	}
	_celCache[resId] = cel;
	return cel;
}

const Common::Array<Puppet::RenderFrame> *Puppet::cachedActionFrames(
		const ActionEntry &action) const {
	if (action.frameCount == 0)
		return nullptr;

	Common::HashMap<uint32, Common::Array<RenderFrame> >::const_iterator cached =
			_actionFrameCache.find(action.cacheIndex);
	if (cached != _actionFrameCache.end())
		return &cached->_value;

	Common::Array<RenderFrame> &frames = _actionFrameCache[action.cacheIndex];
	frames.resize(action.frameCount);

	int frameIdx = resourceIndexById(action.frameResourceId);
	if (frameIdx < 0)
		return &frames;
	const ResourceView frameTable = engineView(static_cast<uint32>(frameIdx));
	if (!frameTable.valid())
		return &frames;

	uint32 displayList = baseDisplayListResource(action.baseState);
	if (displayList == 0)
		return &frames;
	int displayIdx = resourceIndexById(displayList);
	const ResourceView display = displayIdx >= 0
			? engineView(static_cast<uint32>(displayIdx)) : ResourceView();
	if (!display.valid())
		return &frames;

	// Puppet speech re-renders every action frame at 30 fps. Cache the resolved
	// frame/layer/CEL pointers once per action so playback avoids repeated
	// resource-table scans and hash lookups in the sampled inner loop.
	const uint32 availableFrames = boundedRecordCount(
			action.frameCount, frameTable.size(), 0, kFrameRecordStride);
	const RecordRange frameRecords(frameTable, 0, availableFrames, kFrameRecordStride);
	for (uint32 frame = 0; frame < availableFrames; ++frame) {
		const ResourceView frameRecord = frameRecords.record(frame);
		const RecordRange layers(frameRecord, kFrameRecordLayersOffset,
				kDisplayLayerCount, kFrameLayerStride);
		if (!layers.valid())
			continue;
		for (uint32 layer = 0; layer < kDisplayLayerCount; ++layer) {
			const byte *entry = layers.record(layer).dataAt(0, kFrameLayerStride);
			int16 celIndex = static_cast<int16>(READ_LE_UINT16(entry));
			if (celIndex < 0)
				continue;

			uint32 off = kDisplayLayerOffset + layer * kDisplayLayerStride;
			const byte *displayEntry = display.dataAt(off, 2);
			if (!displayEntry)
				continue;
			int16 count = static_cast<int16>(READ_LE_UINT16(displayEntry));
			if (count < 0 || count > kDisplayLayerMaxResources || celIndex >= count)
				continue;

			const uint64 resOffset = static_cast<uint64>(off) + kDisplayLayerResourceListOffset +
					static_cast<uint64>(celIndex) * 4;
			const byte *resEntry = display.dataAt(resOffset, 4);
			if (!resEntry)
				continue;
			uint32 celResId = READ_LE_UINT32(resEntry);
			if (celResId == 0xffffffff || celResId == 0)
				continue;

			Common::SharedPtr<CelImage> cel = celResource(celResId);
			if (!cel)
				continue;
			RenderLayer cachedLayer;
			cachedLayer.layer = static_cast<uint8>(layer);
			cachedLayer.nativeY = static_cast<int16>(READ_LE_UINT16(entry + 2));
			cachedLayer.nativeX = static_cast<int16>(READ_LE_UINT16(entry + 4));
			cachedLayer.cel = cel;
			frames[frame].layers.push_back(cachedLayer);
		}
	}
	return &frames;
}

bool Puppet::renderCelImage(const CelImage &cel, int16 nativeY, int16 nativeX,
		Graphics::Surface &screen) const {
	const uint64 pixelCount = static_cast<uint64>(cel.width) * cel.height;
	if (cel.pixels.size() < pixelCount || cel.opaque.size() < pixelCount)
		return false;

	// TI.EXE FUN_0043b940 treats both the frame record and CEL header
	// coordinate words as QuickDraw-style vertical then horizontal values.
	const int top = nativeY - cel.originX;
	const int left = nativeX - cel.originY;

	int srcLeft = 0;
	int dstLeft = left;
	int width = cel.width;
	if (dstLeft < 0) {
		srcLeft = -dstLeft;
		width -= srcLeft;
		dstLeft = 0;
	}
	if (dstLeft + width > screen.w)
		width = screen.w - dstLeft;

	int srcTop = 0;
	int dstTop = top;
	int height = cel.height;
	if (dstTop < 0) {
		srcTop = -dstTop;
		height -= srcTop;
		dstTop = 0;
	}
	if (dstTop + height > screen.h)
		height = screen.h - dstTop;

	if (width <= 0 || height <= 0)
		return true;

	for (int yy = 0; yy < height; ++yy) {
		const uint row = static_cast<uint>(srcTop + yy) * cel.width + srcLeft;
		const byte *src = cel.pixels.begin() + row;
		const byte *opaque = cel.opaque.begin() + row;
		byte *dst = reinterpret_cast<byte *>(screen.getBasePtr(dstLeft, dstTop + yy));
		for (int xx = 0; xx < width;) {
			while (xx < width && !opaque[xx])
				++xx;
			const int start = xx;
			while (xx < width && opaque[xx])
				++xx;
			if (start != xx)
				memcpy(dst + start, src + start, xx - start);
		}
	}
	return true;
}

uint32 Puppet::baseDisplayListResource(int16 baseState) const {
	if (baseState < 0 || baseState >= static_cast<int16>(kBaseControllerStateCount))
		baseState = 0;
	uint32 resId = _baseDisplayListResources[baseState];
	if (resId == 0 && _baseDisplayListResources[0] != 0)
		resId = _baseDisplayListResources[0];
	return resId;
}

bool Puppet::renderCelResource(uint32 resId, int16 nativeY, int16 nativeX,
		Graphics::Surface &screen) const {
	Common::SharedPtr<CelImage> cel = celResource(resId);
	if (!cel)
		return false;

	return renderCelImage(*cel, nativeY, nativeX, screen);
}

bool Puppet::actionFramesVisuallyEqual(const ActionEntry &action, uint32 frameA,
		uint32 frameB, bool skipLayer0) const {
	if (frameA == frameB)
		return true;
	if (action.frameCount == 0)
		return true;
	if (frameA >= action.frameCount)
		frameA = action.frameCount - 1;
	if (frameB >= action.frameCount)
		frameB = action.frameCount - 1;

	const Common::Array<RenderFrame> *frames = cachedActionFrames(action);
	if (!frames || frameA >= frames->size() || frameB >= frames->size())
		return false;

	const RenderFrame &a = (*frames)[frameA];
	const RenderFrame &b = (*frames)[frameB];
	uint ia = 0;
	uint ib = 0;
	for (;;) {
		while (skipLayer0 && ia < a.layers.size() && a.layers[ia].layer == 0)
			++ia;
		while (skipLayer0 && ib < b.layers.size() && b.layers[ib].layer == 0)
			++ib;
		if (ia >= a.layers.size() || ib >= b.layers.size())
			return ia >= a.layers.size() && ib >= b.layers.size();

		const RenderLayer &la = a.layers[ia++];
		const RenderLayer &lb = b.layers[ib++];
		if (la.layer != lb.layer || la.nativeY != lb.nativeY ||
				la.nativeX != lb.nativeX || la.cel.get() != lb.cel.get())
			return false;
	}
}

bool Puppet::renderActionFrame(const ActionEntry &action, uint32 frameIndex,
		Graphics::Surface &screen, bool skipLayer0) const {
	if (action.frameCount == 0)
		return false;
	if (frameIndex >= action.frameCount)
		frameIndex = action.frameCount - 1;
	const Common::Array<RenderFrame> *frames = cachedActionFrames(action);
	if (!frames || frameIndex >= frames->size())
		return false;

	bool drew = false;
	const RenderFrame &frame = (*frames)[frameIndex];
	for (const RenderLayer &layer : frame.layers) {
		if (skipLayer0 && layer.layer == 0)
			continue;
		drew |= renderCelImage(*layer.cel, layer.nativeY, layer.nativeX, screen);
	}
	return drew;
}

bool Puppet::renderBevelBackdrop(Graphics::Surface &screen, int screenHeight, int screenWidth) const {
	if (!_globalResourceId)
		return false;
	// TI.EXE FUN_00449370 draws DAT_0046120c (master +0x85a) at
	// {v = screenH - 0x3c, h = screenW / 2}. For Titanic this centers the
	// authored 512x120 option panel over y=264..384.
	return renderCelResource(_globalResourceId, screenHeight - 0x3c, screenWidth / 2, screen);
}

bool Puppet::decodeActionAudio(const ActionEntry &action, Common::Array<byte> &pcm) const {
	int idx = resourceIndexById(action.audioResourceId);
	if (idx < 0)
		return false;
	const Archive::Resource &res = _archive.getResource(static_cast<uint32>(idx));
	const ResourceView audio = resourcePayloadView(_fileData, res);
	if (res.info != kAudioResourceInfoTag || !audio.valid())
		return false;
	uint32 before = pcm.size();
	decodeCbxAudio(audio.dataAt(0, audio.size()), static_cast<uint32>(audio.size()), pcm);
	return pcm.size() != before;
}

bool Puppet::loadPuppetPalette(Palette &rgb) const {
	if (_fileData.empty())
		return false;
	return loadPalette(_fileData.begin(), _fileData.size(), rgb);
}

} // End of namespace CyberFlix

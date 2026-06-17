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
#include "common/ptr.h"
#include "graphics/surface.h"

#include "cyberflix/image.h"
#include "cyberflix/puppet.h"
#include "cyberflix/cbx_audio.h" // kMasterHeaderInfoTag
#include "cyberflix/resource_helpers.h"

namespace Cyberflix {

const byte *Puppet::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	if (res.empty || res.dataOffset < 4 || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset - 4;
}

const byte *Puppet::payload(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	if (res.empty || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset;
}

int Puppet::resourceIndexById(uint32 id) const {
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i)
		if (!_archive.getResource(i).empty && _archive.getResource(i).id == id)
			return (int)i;
	return -1;
}

Common::String Puppet::pascalString(const byte *p) const {
	return readPascalString(p, _fileData);
}

Common::SharedPtr<Script> Puppet::parseScriptResource(uint32 resId) const {
	int idx = resourceIndexById(resId);
	if (idx < 0)
		return Common::SharedPtr<Script>();
	Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource((uint32)idx));
	Common::SharedPtr<Script> script(new Script());
	if (s && script->parse(s.get()))
		return script;
	return Common::SharedPtr<Script>();
}

bool Puppet::open(const Common::String &name) {
	_master = -1;
	_globalResourceId = 0;
	_baseCount = 0;
	memset(_baseDisplayListResources, 0, sizeof(_baseDisplayListResources));
	_scripts.clear();
	_actions.clear();
	_scriptIndexByName.clear();
	_actionIndexByName.clear();
	_celCache.clear();
	_actionFrameCache.clear();
	_puppetName.clear();
	_sourceName = name;
	_sourceName.toLowercase();

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open puppet '%s'", name.c_str());
		return false;
	}
	uint32 size = (uint32)file.size();
	_fileData.resize(size);
	if (file.read(_fileData.begin(), size) != size) {
		warning("Cyberflix: could not read puppet '%s'", name.c_str());
		return false;
	}
	file.close();

	if (!_archive.open(new Common::MemoryReadStream(_fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid puppet container", name.c_str());
		return false;
	}

	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		if (!_archive.getResource(i).empty && _archive.getResource(i).info == kMasterHeaderInfoTag) {
			_master = (int)i;
			break;
		}
	}
	if (_master < 0) {
		warning("Cyberflix: puppet '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase((uint32)_master);
	if (!hdr || hdr + kMasterBaseTableOffset > _fileData.end()) {
		warning("Cyberflix: puppet '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}
	_globalResourceId = READ_LE_UINT32(hdr + kMasterGlobalResourceOffset);
	_puppetName = pascalString(hdr + kMasterNameOffset);
	_baseCount = READ_LE_UINT32(hdr + kMasterBaseCountOffset);
	for (uint32 i = 0; i < _baseCount; ++i) {
		const byte *entry = hdr + kMasterBaseTableOffset + i * kMasterBaseStride;
		if (entry + kMasterBaseStride > _fileData.end())
			break;
		ActionEntry action;
		action.baseState = (int16)READ_LE_UINT16(entry + kMasterActionBaseStateOffset);
		action.frameCount = READ_LE_UINT16(entry + kMasterActionFrameCountOffset);
		action.audioResourceId = READ_LE_UINT32(entry + kMasterActionAudioResourceOffset);
		action.frameResourceId = READ_LE_UINT32(entry + kMasterActionFrameResourceOffset);
		action.text = pascalString(entry + kMasterActionTextOffset);
		action.name = pascalString(entry + kMasterActionNameOffset);
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
	const byte *base = baseIdx >= 0 ? engineBase((uint32)baseIdx) : nullptr;
	if (base) {
		for (uint32 i = 0; i < kBaseControllerStateCount; ++i)
			_baseDisplayListResources[i] =
					READ_LE_UINT32(base + kBaseControllerResourceOffset + i * 4);
	}

	int tableIdx = resourceIndexById(kScriptTableResourceId);
	const byte *table = tableIdx >= 0 ? payload((uint32)tableIdx) : nullptr;
	if (!table || _archive.getResource((uint32)tableIdx).length < kScriptTableEntriesOffset) {
		warning("Cyberflix: puppet '%s' script table missing", name.c_str());
		return true;
	}

	uint32 count = READ_LE_UINT16(table + kScriptTableCountOffset);
	uint32 tableLen = _archive.getResource((uint32)tableIdx).length;
	for (uint32 i = 0; i < count; ++i) {
		uint32 off = kScriptTableEntriesOffset + i * kScriptEntryStride;
		if (off + kScriptEntryStride > tableLen)
			break;
		const byte *entry = table + off;
		ScriptEntry se;
		se.resId = READ_LE_UINT32(entry + kScriptEntryResourceOffset);
		se.name = pascalString(entry + kScriptEntryNameOffset);
		se.script = parseScriptResource(se.resId);
		if (!se.name.empty()) {
			Common::String key = se.name;
			key.toLowercase();
			if (!_scriptIndexByName.contains(key))
				_scriptIndexByName[key] = _scripts.size();
			_scripts.push_back(se);
		}
	}

	debug(1, "Cyberflix: opened puppet '%s': name '%s', %u script(s), %u action(s)",
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
	const Archive::Resource &res = _archive.getResource((uint32)idx);
	uint16 width = (uint16)(res.info >> 16);
	uint16 height = (uint16)(res.info & 0xffff);
	if (!width || !height)
		return Common::SharedPtr<CelImage>();

	Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource((uint32)idx));
	if (!s)
		return Common::SharedPtr<CelImage>();
	Common::SharedPtr<CelImage> cel(new CelImage());
	bool ok = decodeCel(*s, width, height, *cel);
	if (!ok) {
		warning("Cyberflix: puppet '%s' could not decode cel resource %u",
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
	const Archive::Resource &res = _archive.getResource((uint32)frameIdx);
	const byte *base = engineBase((uint32)frameIdx);
	if (!base)
		return &frames;

	uint32 displayList = baseDisplayListResource(action.baseState);
	if (displayList == 0)
		return &frames;
	int displayIdx = resourceIndexById(displayList);
	const byte *displayBase = displayIdx >= 0 ? engineBase((uint32)displayIdx) : nullptr;
	if (!displayBase)
		return &frames;

	// Puppet speech re-renders every action frame at 30 fps. Cache the resolved
	// frame/layer/CEL pointers once per action so playback avoids repeated
	// resource-table scans and hash lookups in the sampled inner loop.
	const uint32 availableFrames = MIN<uint32>(action.frameCount,
			(uint32)(((uint64)res.length + 4) / kFrameRecordStride));
	for (uint32 frame = 0; frame < availableFrames; ++frame) {
		const byte *record = base + frame * kFrameRecordStride;
		if (record + kFrameRecordLayersOffset + kDisplayLayerCount * kFrameLayerStride >
				_fileData.end())
			break;
		for (uint32 layer = 0; layer < kDisplayLayerCount; ++layer) {
			const byte *entry = record + kFrameRecordLayersOffset + layer * kFrameLayerStride;
			int16 celIndex = (int16)READ_LE_UINT16(entry);
			if (celIndex < 0)
				continue;

			uint32 off = kDisplayLayerOffset + layer * kDisplayLayerStride;
			if (displayBase + off + 2 > _fileData.end())
				continue;
			int16 count = (int16)READ_LE_UINT16(displayBase + off);
			if (count < 0 || count > kDisplayLayerMaxResources || celIndex >= count)
				continue;

			const byte *resEntry = displayBase + off + kDisplayLayerResourceListOffset +
					celIndex * 4;
			if (resEntry + 4 > _fileData.end())
				continue;
			uint32 celResId = READ_LE_UINT32(resEntry);
			if (celResId == 0xffffffff || celResId == 0)
				continue;

			Common::SharedPtr<CelImage> cel = celResource(celResId);
			if (!cel)
				continue;
			RenderLayer cachedLayer;
			cachedLayer.layer = (uint8)layer;
			cachedLayer.nativeY = (int16)READ_LE_UINT16(entry + 2);
			cachedLayer.nativeX = (int16)READ_LE_UINT16(entry + 4);
			cachedLayer.cel = cel;
			frames[frame].layers.push_back(cachedLayer);
		}
	}
	return &frames;
}

bool Puppet::renderCelImage(const CelImage &cel, int16 nativeY, int16 nativeX,
		Graphics::Surface &screen) const {
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
		const uint row = (uint)(srcTop + yy) * cel.width + srcLeft;
		const byte *src = cel.pixels.begin() + row;
		const byte *opaque = cel.opaque.begin() + row;
		byte *dst = (byte *)screen.getBasePtr(dstLeft, dstTop + yy);
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
	if (baseState < 0 || baseState >= (int16)kBaseControllerStateCount)
		baseState = 0;
	uint32 resId = _baseDisplayListResources[baseState];
	if (resId == 0 && _baseDisplayListResources[0] != 0)
		resId = _baseDisplayListResources[0];
	return resId;
}

uint32 Puppet::displayLayerResourceId(uint32 displayListResourceId,
		uint32 layer, int16 celIndex) const {
	if (layer >= kDisplayLayerCount || celIndex < 0)
		return 0;
	int idx = resourceIndexById(displayListResourceId);
	if (idx < 0)
		return 0;
	const byte *base = engineBase((uint32)idx);
	if (!base)
		return 0;
	uint32 off = kDisplayLayerOffset + layer * kDisplayLayerStride;
	if (base + off + 2 > _fileData.end())
		return 0;
	int16 count = (int16)READ_LE_UINT16(base + off);
	if (count < 0 || count > kDisplayLayerMaxResources || celIndex >= count)
		return 0;
	const byte *entry = base + off + kDisplayLayerResourceListOffset + celIndex * 4;
	if (entry + 4 > _fileData.end())
		return 0;
	uint32 resId = READ_LE_UINT32(entry);
	return resId == 0xffffffff ? 0 : resId;
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
	for (uint32 i = 0; i < frame.layers.size(); ++i) {
		const RenderLayer &layer = frame.layers[i];
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
	const Archive::Resource &res = _archive.getResource((uint32)idx);
	if (res.info != kAudioResourceInfoTag || res.dataOffset + res.length > _fileData.size())
		return false;
	uint32 before = pcm.size();
	decodeCbxAudio(_fileData.begin() + res.dataOffset, res.length, pcm);
	return pcm.size() != before;
}

bool Puppet::loadPuppetPalette(byte *rgb) const {
	if (_fileData.empty())
		return false;
	return loadPalette(_fileData.begin(), _fileData.size(), rgb);
}

} // End of namespace Cyberflix

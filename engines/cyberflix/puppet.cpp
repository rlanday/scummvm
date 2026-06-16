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
#include "graphics/surface.h"

#include "cyberflix/image.h"
#include "cyberflix/puppet.h"
#include "cyberflix/sound.h" // kMasterHeaderInfoTag

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
	if (!p || p >= _fileData.end())
		return Common::String();
	uint len = *p;
	if (p + 1 + len > _fileData.end())
		return Common::String();
	return Common::String((const char *)p + 1, len);
}

Common::SharedPtr<Script> Puppet::parseScriptResource(uint32 resId) const {
	int idx = resourceIndexById(resId);
	if (idx < 0)
		return Common::SharedPtr<Script>();
	Common::SeekableReadStream *s = _archive.createReadStreamForResource((uint32)idx);
	Common::SharedPtr<Script> script(new Script());
	if (s && script->parse(s)) {
		delete s;
		return script;
	}
	delete s;
	return Common::SharedPtr<Script>();
}

bool Puppet::open(const Common::String &name) {
	_master = -1;
	_globalResourceId = 0;
	_baseCount = 0;
	memset(_baseDisplayListResources, 0, sizeof(_baseDisplayListResources));
	_scripts.clear();
	_actions.clear();
	_celCache.clear();
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
		_actions.push_back(action);
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
		if (!se.name.empty())
			_scripts.push_back(se);
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
	for (uint32 i = 0; i < _scripts.size(); ++i)
		if (_scripts[i].name.equalsIgnoreCase(name))
			return _scripts[i].script;
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
	for (uint32 i = 0; i < _actions.size(); ++i) {
		if (_actions[i].name.equalsIgnoreCase(name) ||
				_actions[i].text.equalsIgnoreCase(name))
			return &_actions[i];
	}
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

	Common::SeekableReadStream *s = _archive.createReadStreamForResource((uint32)idx);
	if (!s)
		return Common::SharedPtr<CelImage>();
	Common::SharedPtr<CelImage> cel(new CelImage());
	bool ok = decodeCel(*s, width, height, *cel);
	delete s;
	if (!ok) {
		warning("Cyberflix: puppet '%s' could not decode cel resource %u",
				_sourceName.c_str(), resId);
		return Common::SharedPtr<CelImage>();
	}
	_celCache[resId] = cel;
	return cel;
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

	// TI.EXE FUN_0043b940 treats both the frame record and CEL header
	// coordinate words as QuickDraw-style vertical then horizontal values.
	int top = nativeY - cel->originX;
	int left = nativeX - cel->originY;
	for (int yy = 0; yy < cel->height; ++yy) {
		int sy = top + yy;
		if (sy < 0 || sy >= screen.h)
			continue;
		for (int xx = 0; xx < cel->width; ++xx) {
			if (!cel->isOpaque(xx, yy))
				continue;
			int sx = left + xx;
			if (sx >= 0 && sx < screen.w)
				*((byte *)screen.getBasePtr(sx, sy)) =
						cel->pixels[(uint)yy * cel->width + xx];
		}
	}
	return true;
}

bool Puppet::renderActionFrame(const ActionEntry &action, uint32 frameIndex,
		Graphics::Surface &screen, bool skipLayer0) const {
	if (action.frameCount == 0)
		return false;
	if (frameIndex >= action.frameCount)
		frameIndex = action.frameCount - 1;
	int frameIdx = resourceIndexById(action.frameResourceId);
	if (frameIdx < 0)
		return false;
	const Archive::Resource &res = _archive.getResource((uint32)frameIdx);
	const byte *base = engineBase((uint32)frameIdx);
	if (!base || base + (uint64)frameIndex * kFrameRecordStride +
			kFrameRecordLayersOffset + kDisplayLayerCount * kFrameLayerStride >
			_fileData.end())
		return false;
	if ((uint64)(frameIndex + 1) * kFrameRecordStride > res.length + 4)
		return false;

	uint32 displayList = baseDisplayListResource(action.baseState);
	if (displayList == 0)
		return false;
	const byte *record = base + frameIndex * kFrameRecordStride;
	bool drew = false;
	for (uint32 layer = 0; layer < kDisplayLayerCount; ++layer) {
		if (skipLayer0 && layer == 0)
			continue;
		const byte *entry = record + kFrameRecordLayersOffset + layer * kFrameLayerStride;
		int16 celIndex = (int16)READ_LE_UINT16(entry);
		if (celIndex < 0)
			continue;
		int16 y = (int16)READ_LE_UINT16(entry + 2);
		int16 x = (int16)READ_LE_UINT16(entry + 4);
		uint32 celResId = displayLayerResourceId(displayList, layer, celIndex);
		if (celResId)
			drew |= renderCelResource(celResId, y, x, screen);
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

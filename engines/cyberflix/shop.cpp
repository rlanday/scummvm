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

#include "cyberflix/shop.h"
#include "cyberflix/sound.h" // kMasterHeaderInfoTag

namespace Cyberflix {

const byte *Shop::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	// All offsets are in the runtime's "record+8" frame, four bytes before the
	// payload (== record+12). See files/decomp/stage-notes.md.
	if (res.empty || res.dataOffset < 4 || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset - 4;
}

int Shop::resourceIndexById(uint32 id) const {
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i)
		if (!_archive.getResource(i).empty && _archive.getResource(i).id == id)
			return (int)i;
	return -1;
}

Common::String Shop::pascalString(const byte *p) const {
	if (!p || p >= _fileData.end())
		return Common::String();
	uint len = *p;
	if (p + 1 + len > _fileData.end())
		return Common::String();
	return Common::String((const char *)p + 1, len);
}

bool Shop::open(const Common::String &name) {
	_master = -1;
	_props.clear();
	_script.reset();
	_name = name;

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open shop '%s'", name.c_str());
		return false;
	}
	uint32 size = (uint32)file.size();
	_fileData.resize(size);
	if (file.read(_fileData.begin(), size) != size) {
		warning("Cyberflix: could not read shop '%s'", name.c_str());
		return false;
	}
	file.close();

	if (!_archive.open(new Common::MemoryReadStream(_fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid shop container", name.c_str());
		return false;
	}

	// Locate the master header by its info tag. NOT by id: HOUSE.SHP carries
	// an empty placeholder slot with resource id 0 ahead of the real master.
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		if (!_archive.getResource(i).empty && _archive.getResource(i).info == kMasterHeaderInfoTag) {
			_master = (int)i;
			break;
		}
	}
	if (_master < 0) {
		warning("Cyberflix: shop '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase((uint32)_master);
	if (!hdr || hdr + kMasterPropTableOffset > _fileData.end()) {
		warning("Cyberflix: shop '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}

	// Parse and retain the shop's script (message handlers like initprops).
	uint32 scriptRes = READ_LE_UINT32(hdr + kMasterScriptOffset);
	int scriptIdx = resourceIndexById(scriptRes);
	if (scriptIdx >= 0) {
		Common::SeekableReadStream *s = _archive.createReadStreamForResource((uint32)scriptIdx);
		Common::ScopedPtr<Script> script(new Script());
		if (s && script->parse(s))
			_script.reset(script.release());
		delete s;
	}
	if (!_script)
		warning("Cyberflix: shop '%s' script res %u missing", name.c_str(), scriptRes);

	// Build the prop list (TI.EXE FUN_00428750 per master id).
	uint32 propCount = READ_LE_UINT32(hdr + kMasterPropCountOffset);
	const byte *entry = hdr + kMasterPropTableOffset;
	for (uint32 i = 0; i < propCount; ++i, entry += kMasterPropStride) {
		if (entry + kMasterPropStride > _fileData.end())
			break;
		uint32 masterId = READ_LE_UINT32(entry);
		int mIdx = resourceIndexById(masterId);
		const byte *pm = mIdx >= 0 ? engineBase((uint32)mIdx) : nullptr;
		if (!pm || pm + kPropShapeTableOffset > _fileData.end()) {
			warning("Cyberflix: shop '%s' prop master %u missing", name.c_str(), masterId);
			continue;
		}

		Prop prop;
		prop.masterResId = masterId;
		prop.scriptResId = READ_LE_UINT32(pm + kPropScriptOffset);
		prop.name = pascalString(pm + kPropNameOffset);
		prop.name.toLowercase();
		prop.setName = pascalString(pm + kPropSetOffset);
		prop.sceneName = pascalString(pm + kPropSceneOffset);

		uint32 shapeCount = READ_LE_UINT32(pm + kPropShapeCountOffset);
		const byte *se = pm + kPropShapeTableOffset;
		for (uint32 j = 0; j < shapeCount && se + kPropShapeStride <= _fileData.end(); ++j, se += kPropShapeStride) {
			Shape shape;
			shape.resId = READ_LE_UINT32(se);
			shape.name = pascalString(se + kPropShapeNameOffset);
			shape.name.toLowercase();
			prop.shapes.push_back(shape);
		}
		// Initial view = the first shape's name (record +0x7e <- master +0x6e).
		prop.shapeName = prop.shapes.empty() ? "none" : prop.shapes[0].name;

		int sIdx = prop.scriptResId ? resourceIndexById(prop.scriptResId) : -1;
		if (sIdx >= 0) {
			Common::SeekableReadStream *s = _archive.createReadStreamForResource((uint32)sIdx);
			Common::SharedPtr<Script> script(new Script());
			if (s && script->parse(s))
				prop.script = script;
			delete s;
		}

		_props.push_back(prop);
	}

	debug(1, "Cyberflix: opened shop '%s': %u prop(s)", name.c_str(), _props.size());
	return true;
}

Shop::Prop *Shop::findProp(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _props.size(); ++i)
		if (_props[i].name == key)
			return &_props[i];
	return nullptr;
}

bool Shop::shapePoseCount(const Prop &prop, const Common::String &shape, uint16 &poseCount) const {
	Common::String key = shape;
	key.toLowercase();
	for (uint32 i = 0; i < prop.shapes.size(); ++i) {
		if (prop.shapes[i].name != key)
			continue;
		int idx = resourceIndexById(prop.shapes[i].resId);
		const byte *sh = idx >= 0 ? engineBase((uint32)idx) : nullptr;
		if (!sh || sh + kShapeCellTableOffset > _fileData.end())
			return false;
		poseCount = READ_LE_UINT16(sh + kShapePoseCountOffset);
		return true;
	}
	return false;
}

// Angular distance with wraparound, mirroring the cell selector's metric
// (FUN_00426250; cell angles are degrees 0..359).
static int angleDistance(int a, int b) {
	int d = ABS(a - b) % 360;
	return d > 180 ? 360 - d : d;
}

bool Shop::renderProp(const Prop &prop, CelImage &cel, Common::Rect &rect) const {
	// Resolve the current shape resource (FUN_0042bed0).
	const Shape *shape = nullptr;
	for (uint32 i = 0; i < prop.shapes.size(); ++i)
		if (prop.shapes[i].name == prop.shapeName) {
			shape = &prop.shapes[i];
			break;
		}
	if (!shape) {
		debug(1, "Cyberflix: renderProp('%s'): shape '%s' not in master",
				prop.name.c_str(), prop.shapeName.c_str());
		return false;
	}
	int shIdx = resourceIndexById(shape->resId);
	const byte *sh = shIdx >= 0 ? engineBase((uint32)shIdx) : nullptr;
	if (!sh || sh + kShapeCellTableOffset > _fileData.end()) {
		debug(1, "Cyberflix: renderProp('%s'): shape res %u missing",
				prop.name.c_str(), shape->resId);
		return false;
	}

	uint16 poseCount = READ_LE_UINT16(sh + kShapePoseCountOffset);
	uint16 cellCount = READ_LE_UINT16(sh + kShapeCellCountOffset);
	if (!poseCount || !cellCount)
		return false;
	// Pose id from the pose table; cells store poseId-1 in their id field.
	uint16 poseIdx = 0; // propview leaves the prop on its shape's last pose
	if (poseCount > 0)
		poseIdx = poseCount - 1;
	uint16 poseId = READ_LE_UINT16(sh + kShapePoseTableOffset + poseIdx * 2);

	const byte *best = nullptr;
	int bestDist = 0x7fffffff;
	const byte *cellTable = sh + kShapeCellTableOffset;
	for (uint16 i = 0; i < cellCount; ++i) {
		const byte *c = cellTable + (uint32)i * kShapeCellStride;
		if (c + kShapeCellStride > _fileData.end())
			break;
		if (READ_LE_UINT16(c + kCellIdOffset) != (uint16)(poseId - 1))
			continue;
		int dist = angleDistance(READ_LE_INT16(c + kCellAngleOffset), prop.angle);
		if (dist < bestDist) {
			bestDist = dist;
			best = c;
		}
	}
	if (!best) {
		debug(1, "Cyberflix: renderProp('%s'): no cell for pose %u in shape '%s'",
				prop.name.c_str(), poseId, prop.shapeName.c_str());
		return false;
	}

	// Cel frame resource: info tag packs the dimensions (width = info >> 16).
	uint32 frameRes = READ_LE_UINT32(best + kCellFrameResOffset);
	int fIdx = resourceIndexById(frameRes);
	if (fIdx < 0)
		return false;
	const Archive::Resource &fres = _archive.getResource((uint32)fIdx);
	uint16 w = (uint16)(fres.info >> 16);
	uint16 h = (uint16)(fres.info & 0xffff);
	Common::SeekableReadStream *fs = _archive.createReadStreamForResource((uint32)fIdx);
	if (!fs)
		return false;
	bool ok = decodeCel(*fs, w, h, cel);
	delete fs;
	if (!ok) {
		debug(1, "Cyberflix: renderProp('%s'): cel res %u decode failed",
				prop.name.c_str(), frameRes);
		return false;
	}

	// Display-item rect (FUN_0042bb90, screen mode): position minus the cell's
	// registration point; extent from the cell bounds (the +40 bias cancels).
	int16 cTop = READ_LE_INT16(best + kCellRectOffset);
	int16 cLeft = READ_LE_INT16(best + kCellRectOffset + 2);
	int16 cBottom = READ_LE_INT16(best + kCellRectOffset + 4);
	int16 cRight = READ_LE_INT16(best + kCellRectOffset + 6);
	int16 regV = READ_LE_INT16(best + kCellRegVOffset);
	int16 regH = READ_LE_INT16(best + kCellRegHOffset);
	rect.top = prop.y - regV;
	rect.left = prop.x - regH;
	rect.bottom = rect.top + (cBottom - cTop);
	rect.right = rect.left + (cRight - cLeft);
	return true;
}

} // End of namespace Cyberflix

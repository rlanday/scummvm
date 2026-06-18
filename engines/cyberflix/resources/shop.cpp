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

#include "cyberflix/shop.h"
#include "cyberflix/resource_helpers.h"

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
	return readPascalString(p, _fileData);
}

bool Shop::open(const Common::String &name) {
	_master = -1;
	_props.clear();
	_celCache.clear();
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
		Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource((uint32)scriptIdx));
		Common::ScopedPtr<Script> script(new Script());
		if (s && script->parse(s.get()))
			_script.reset(script.release());
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
		if (!prop.shapes.empty() && shapePoseCount(prop, prop.shapeName, prop.poseCount))
			prop.poseIndex = prop.poseCount ? prop.poseCount - 1 : 0;

		int sIdx = prop.scriptResId ? resourceIndexById(prop.scriptResId) : -1;
		if (sIdx >= 0) {
			Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource((uint32)sIdx));
			Common::SharedPtr<Script> script(new Script());
			if (s && script->parse(s.get()))
				prop.script = script;
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

bool Shop::addPropInstance(const Prop &source, const Common::String &newName) {
	if (newName.empty())
		return false;
	Prop clone = source;
	clone.name = newName;
	clone.name.toLowercase();
	_props.push_back(clone);
	return true;
}

void Shop::advancePropPoses() {
	for (uint32 i = 0; i < _props.size(); ++i) {
		Prop &prop = _props[i];
		if (prop.poseCount == 0)
			continue;
		prop.poseIndex++;
		if (prop.poseIndex >= prop.poseCount)
			prop.poseIndex = 0;
	}
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

Common::SharedPtr<CelImage> Shop::celResource(uint32 resId) const {
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
	if (!ok)
		return Common::SharedPtr<CelImage>();

	_celCache[resId] = cel;
	return cel;
}

bool Shop::resolvePropCel(const Prop &prop, int angle, Common::SharedPtr<CelImage> &cel,
		Common::Rect &cellRect, int16 &regV, int16 &regH,
		int16 &cellScale) const {
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
	uint16 poseIdx = prop.poseIndex < poseCount ? prop.poseIndex : poseCount - 1;
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
		int dist = nativeAngleDistance(READ_LE_INT16(c + kCellAngleOffset), angle);
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
	cel = celResource(frameRes);
	if (!cel) {
		debug(1, "Cyberflix: renderProp('%s'): cel res %u decode failed",
				prop.name.c_str(), frameRes);
		return false;
	}

	cellRect.top = READ_LE_INT16(best + kCellRectOffset);
	cellRect.left = READ_LE_INT16(best + kCellRectOffset + 2);
	cellRect.bottom = READ_LE_INT16(best + kCellRectOffset + 4);
	cellRect.right = READ_LE_INT16(best + kCellRectOffset + 6);
	regV = READ_LE_INT16(best + kCellRegVOffset);
	regH = READ_LE_INT16(best + kCellRegHOffset);
	cellScale = READ_LE_INT16(best + kCellScaleOffset);
	return true;
}

bool Shop::renderProp(const Prop &prop, Common::SharedPtr<CelImage> &cel, Common::Rect &rect) const {
	Common::Rect cellRect;
	int16 regV = 0, regH = 0, cellScale = 0;
	if (!resolvePropCel(prop, prop.angle, cel, cellRect, regV, regH, cellScale))
		return false;

	// Display-item rect (FUN_0042bb90, screen mode): position minus the cell's
	// registration point; extent from the cell bounds (the +40 bias cancels).
	rect.top = prop.y - regV;
	rect.left = prop.x - regH;
	rect.bottom = rect.top + cellRect.height();
	rect.right = rect.left + cellRect.width();
	return true;
}

bool Shop::renderWorldProp(const Prop &prop, const WorldCamera &camera,
		const Common::String &setName, Common::SharedPtr<CelImage> &cel,
		Common::Rect &rect, int16 &depth) const {
	if (!prop.visible || prop.mode == 0 || !prop.setName.equalsIgnoreCase(setName))
		return false;

	const int relX = prop.x - camera.cameraX;
	const int relY = prop.y - camera.cameraY;
	const int sinH = nativeTrigSin(camera.heading);
	const int cosH = nativeTrigCos(camera.heading);
	const int projectedDepth = fixedShift14(relY * sinH + relX * cosH);
	if (projectedDepth < 1)
		return false;

	const int zClippedDepth = MAX(projectedDepth - prop.zClip, 0);
	const int nearLimit = (camera.nearPlane + (camera.nearPlane < 0 ? 3 : 0)) >> 2;
	if (projectedDepth <= nearLimit || zClippedDepth > camera.farPlane)
		return false;

	const int projectedH = fixedShift14(relY * cosH - relX * sinH);
	const int screenX = camera.centerX + projectedH * camera.focal / projectedDepth;
	const int screenY = camera.centerY -
			((prop.z - camera.baseZ - camera.cameraZ) * camera.focal) / projectedDepth;
	const int angleToCamera = nativePointAngle(camera.cameraY - prop.y, camera.cameraX - prop.x);
	const int viewAngle = (prop.angle - angleToCamera) & 0xff;

	Common::Rect cellRect;
	int16 regV = 0, regH = 0, cellScale = 0;
	if (!resolvePropCel(prop, viewAngle, cel, cellRect, regV, regH, cellScale))
		return false;

	const int sourceH = cellRect.height();
	const int sourceW = cellRect.width();
	if (sourceH <= 0 || sourceW <= 0)
		return false;
	const int effectiveScale = (prop.scale * cellScale) / 1000;
	const int scaledH = (effectiveScale * sourceH) / projectedDepth;
	const int scaledW = (effectiveScale * sourceW) / projectedDepth;
	if (scaledH <= 0 || scaledW <= 0)
		return false;

	rect.top = screenY - (scaledH * regV) / sourceH;
	rect.left = screenX - (scaledW * regH) / sourceW;
	rect.bottom = rect.top + scaledH;
	rect.right = rect.left + scaledW;
	Common::Rect viewport(camera.viewportLeft, camera.viewportTop,
			camera.viewportRight, camera.viewportBottom);
	if (!rect.intersects(viewport))
		return false;
	depth = (int16)projectedDepth;
	return true;
}

} // End of namespace Cyberflix

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

#include "cyberflix/shop.h"
#include "cyberflix/resource_helpers.h"

namespace Cyberflix {

const byte *Shop::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	return resourceEngineBase(_fileData, _archive.getResource(index));
}

int Shop::resourceIndexById(uint32 id) const {
	return Cyberflix::resourceIndexById(_archive, id);
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

	if (!openArchiveFile(name, "shop", _fileData, _archive))
		return false;

	// Locate the master header by its info tag. NOT by id: HOUSE.SHP carries
	// an empty placeholder slot with resource id 0 ahead of the real master.
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		if (!_archive.getResource(i).empty && _archive.getResource(i).info == kMasterHeaderInfoTag) {
			_master = static_cast<int>(i);
			break;
		}
	}
	if (_master < 0) {
		warning("Cyberflix: shop '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase(static_cast<uint32>(_master));
	if (!hdr || hdr + kMasterPropTableOffset > _fileData.end()) {
		warning("Cyberflix: shop '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}

	// Parse and retain the shop's script (message handlers like initprops).
	uint32 scriptRes = READ_LE_UINT32(hdr + kMasterScriptOffset);
	int scriptIdx = resourceIndexById(scriptRes);
	if (scriptIdx >= 0) {
		Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource(static_cast<uint32>(scriptIdx)));
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
		const byte *pm = mIdx >= 0 ? engineBase(static_cast<uint32>(mIdx)) : nullptr;
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
		if (!prop.shapes.empty()) {
			ShapePoseResult pose = shapePoseCount(prop, prop.shapeName);
			if (pose.valid) {
				prop.poseCount = pose.poseCount;
				// The native engine primes the pose immediately before its first
				// compositor advance. We represent the same state explicitly so
				// pose 0 is also safe if an on-demand repaint happens first.
				prop.poseIndex = 0;
				prop.poseAdvancePending = true;
			}
		}

		int sIdx = prop.scriptResId ? resourceIndexById(prop.scriptResId) : -1;
		if (sIdx >= 0) {
			Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource(static_cast<uint32>(sIdx)));
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
		if (prop.poseAdvancePending) {
			prop.poseAdvancePending = false;
			continue;
		}
		prop.poseIndex++;
		if (prop.poseIndex >= prop.poseCount)
			prop.poseIndex = 0;
	}
}

Shop::ShapePoseResult Shop::shapePoseCount(const Prop &prop, const Common::String &shape) const {
	ShapePoseResult result;
	Common::String key = shape;
	key.toLowercase();
	for (uint32 i = 0; i < prop.shapes.size(); ++i) {
		if (prop.shapes[i].name != key)
			continue;
		int idx = resourceIndexById(prop.shapes[i].resId);
		const byte *sh = idx >= 0 ? engineBase(static_cast<uint32>(idx)) : nullptr;
		if (!sh || sh + kShapeCellTableOffset > _fileData.end())
			return result;
		result.valid = true;
		result.poseCount = READ_LE_UINT16(sh + kShapePoseCountOffset);
		return result;
	}
	return result;
}

Common::SharedPtr<CelImage> Shop::celResource(uint32 resId) const {
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
	if (!ok)
		return Common::SharedPtr<CelImage>();

	_celCache[resId] = cel;
	return cel;
}

Shop::PropCellResult Shop::resolvePropCel(const Prop &prop, int angle) const {
	PropCellResult result;
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
		return result;
	}
	int shIdx = resourceIndexById(shape->resId);
	const byte *sh = shIdx >= 0 ? engineBase(static_cast<uint32>(shIdx)) : nullptr;
	const uint64 shapeLen = shIdx >= 0 ?
			static_cast<uint64>(_archive.getResource(static_cast<uint32>(shIdx)).length) + 4 : 0;
	if (!sh || shapeLen < kShapeCellTableOffset) {
		debug(1, "Cyberflix: renderProp('%s'): shape res %u missing",
				prop.name.c_str(), shape->resId);
		return result;
	}

	uint16 poseCount = READ_LE_UINT16(sh + kShapePoseCountOffset);
	uint16 cellCount = READ_LE_UINT16(sh + kShapeCellCountOffset);
	if (!poseCount || !cellCount)
		return result;
	if (static_cast<uint64>(kShapePoseTableOffset) + static_cast<uint64>(poseCount) * 2 > shapeLen ||
			static_cast<uint64>(kShapeCellTableOffset) + static_cast<uint64>(cellCount) * kShapeCellStride > shapeLen)
		return result;
	// Pose id from the pose table; cells store poseId-1 in their id field.
	uint16 poseIdx = prop.poseIndex < poseCount ? prop.poseIndex : poseCount - 1;
	uint16 poseId = READ_LE_UINT16(sh + kShapePoseTableOffset + poseIdx * 2);

	const byte *best = nullptr;
	int bestDist = 0x7fffffff;
	const byte *cellTable = sh + kShapeCellTableOffset;
	for (uint16 i = 0; i < cellCount; ++i) {
		const byte *c = cellTable + static_cast<uint32>(i) * kShapeCellStride;
		if (c + kShapeCellStride > _fileData.end())
			break;
		if (READ_LE_UINT16(c + kCellIdOffset) != static_cast<uint16>(poseId - 1))
			continue;
		int dist = nativeAngleDistance(READ_LE_INT16(c + kCellAngleOffset), angle);
		if (dist < bestDist) {
			bestDist = dist;
			best = c;
		}
	}
	if (!best) {
		debug(2, "Cyberflix: renderProp('%s'): no cell for pose %u in shape '%s'",
				prop.name.c_str(), poseId, prop.shapeName.c_str());
		return result;
	}

	// Cel frame resource: info tag packs the dimensions (width = info >> 16).
	uint32 frameRes = READ_LE_UINT32(best + kCellFrameResOffset);
	result.cel = celResource(frameRes);
	if (!result.cel) {
		debug(1, "Cyberflix: renderProp('%s'): cel res %u decode failed",
				prop.name.c_str(), frameRes);
		return result;
	}

	result.cellRect.top = READ_LE_INT16(best + kCellRectOffset);
	result.cellRect.left = READ_LE_INT16(best + kCellRectOffset + 2);
	result.cellRect.bottom = READ_LE_INT16(best + kCellRectOffset + 4);
	result.cellRect.right = READ_LE_INT16(best + kCellRectOffset + 6);
	result.regV = READ_LE_INT16(best + kCellRegVOffset);
	result.regH = READ_LE_INT16(best + kCellRegHOffset);
	result.cellScale = READ_LE_INT16(best + kCellScaleOffset);
	result.valid = true;
	return result;
}

Shop::PropRenderResult Shop::renderProp(const Prop &prop) const {
	PropRenderResult result;
	PropCellResult cell = resolvePropCel(prop, prop.angle);
	if (!cell.valid)
		return result;

	// Display-item rect (FUN_0042bb90, screen mode): position minus the cell's
	// registration point; extent from the cell bounds (the +40 bias cancels).
	result.cel = cell.cel;
	result.rect.top = prop.y - cell.regV;
	result.rect.left = prop.x - cell.regH;
	result.rect.bottom = result.rect.top + cell.cellRect.height();
	result.rect.right = result.rect.left + cell.cellRect.width();
	result.valid = true;
	return result;
}

Shop::PropRenderResult Shop::renderWorldProp(const Prop &prop, const WorldCamera &camera,
		const Common::String &setName) const {
	PropRenderResult result;
	if (!prop.visible || prop.mode == 0 || !prop.setName.equalsIgnoreCase(setName))
		return result;

	const int relX = prop.x - camera.cameraX;
	const int relY = prop.y - camera.cameraY;
	// Native yaw rotation followed by pinhole perspective projection:
	// x' = x*f/z, y' = y*f/z. SET panorama records store camera headings in the
	// same 8-bit circle as prop-facing angles (TI.EXE FUN_00442e90 fills the
	// yaw table entries consumed by FUN_00443340). See Foley/van Dam et al.,
	// Computer Graphics: Principles and Practice, viewing pipeline chapter.
	const int sinH = nativeTrigSin(camera.heading);
	const int cosH = nativeTrigCos(camera.heading);
	const int projectedDepth = fixedShift14(relY * sinH + relX * cosH);
	if (projectedDepth < 1)
		return result;

	const int zClippedDepth = MAX(projectedDepth - prop.zClip, 0);
	const int nearLimit = (camera.nearPlane + (camera.nearPlane < 0 ? 3 : 0)) >> 2;
	if (projectedDepth <= nearLimit || zClippedDepth > camera.farPlane)
		return result;

	const int projectedH = fixedShift14(relY * cosH - relX * sinH);
	const int screenX = camera.centerX + projectedH * camera.focal / projectedDepth;
	const int screenY = camera.centerY -
			((prop.z - camera.baseZ - camera.cameraZ) * camera.focal) / projectedDepth;
	const int angleToCamera = nativePointAngle(camera.cameraY - prop.y, camera.cameraX - prop.x);
	const int viewAngle = (prop.angle - angleToCamera) & 0xff;

	PropCellResult cell = resolvePropCel(prop, viewAngle);
	if (!cell.valid)
		return result;

	const int sourceH = cell.cellRect.height();
	const int sourceW = cell.cellRect.width();
	if (sourceH <= 0 || sourceW <= 0)
		return result;
	const int effectiveScale = (prop.scale * cell.cellScale) / 1000;
	const int scaledH = (effectiveScale * sourceH) / projectedDepth;
	const int scaledW = (effectiveScale * sourceW) / projectedDepth;
	if (scaledH <= 0 || scaledW <= 0)
		return result;

	result.cel = cell.cel;
	result.rect.top = screenY - (scaledH * cell.regV) / sourceH;
	result.rect.left = screenX - (scaledW * cell.regH) / sourceW;
	result.rect.bottom = result.rect.top + scaledH;
	result.rect.right = result.rect.left + scaledW;
	Common::Rect viewport(camera.viewportLeft, camera.viewportTop,
			camera.viewportRight, camera.viewportBottom);
	if (!result.rect.intersects(viewport))
		return result;
	result.depth = static_cast<int16>(projectedDepth);
	result.depthBucket = camera.nearPlane ? static_cast<int16>(zClippedDepth / camera.nearPlane) : 0;
	result.valid = true;
	return result;
}

} // End of namespace Cyberflix

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

namespace CyberFlix {

ResourceView Shop::engineView(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return ResourceView();
	return resourceEngineView(_fileData, _archive.getResource(index));
}

int Shop::resourceIndexById(uint32 id) const {
	return CyberFlix::resourceIndexById(_archive, id);
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
	_master = findMasterHeaderIndex(_archive);
	if (_master < 0) {
		warning("CyberFlix: shop '%s' has no master header", name.c_str());
		_archive.close();
		_fileData.clear();
		return false;
	}

	const ResourceView master = engineView(static_cast<uint32>(_master));
	const byte *hdr = master.dataAt(0, kMasterPropTableOffset);
	if (!hdr) {
		warning("CyberFlix: shop '%s' master header truncated", name.c_str());
		_master = -1;
		_archive.close();
		_fileData.clear();
		return false;
	}

	// Parse and retain the shop's script (message handlers like initprops).
	// Archive ids start at 0, so a zero id means "no script", not resource 0.
	uint32 scriptRes = READ_LE_UINT32(hdr + kMasterScriptOffset);
	if (scriptRes != 0) {
		int scriptIdx = resourceIndexById(scriptRes);
		if (scriptIdx >= 0) {
			Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource(static_cast<uint32>(scriptIdx)));
			Common::ScopedPtr<Script> script(new Script());
			if (s && script->parse(s.get()))
				_script.reset(script.release());
			if (!_script)
				warning("CyberFlix: shop '%s' script res %u failed to parse", name.c_str(), scriptRes);
		} else {
			warning("CyberFlix: shop '%s' script res %u missing", name.c_str(), scriptRes);
		}
	}

	// Build the prop list (TI.EXE FUN_00428750 per master id). Clamp the
	// file-supplied count to the master resource's length (engine-base frame is
	// res.length + 4 bytes) so a corrupt count cannot parse neighbouring
	// resources' bytes as prop entries.
	uint32 propCount = READ_LE_UINT32(hdr + kMasterPropCountOffset);
	propCount = boundedRecordCount(propCount, master.size(), kMasterPropTableOffset, kMasterPropStride);
	const RecordRange propRecords(master, kMasterPropTableOffset, propCount, kMasterPropStride);
	for (uint32 i = 0; i < propRecords.size(); ++i) {
		const byte *entry = propRecords.record(i).dataAt(0, kMasterPropStride);
		uint32 masterId = READ_LE_UINT32(entry);
		int mIdx = resourceIndexById(masterId);
		const ResourceView propMaster = mIdx >= 0
				? engineView(static_cast<uint32>(mIdx)) : ResourceView();
		const byte *pm = propMaster.dataAt(0, kPropShapeTableOffset);
		if (!pm) {
			warning("CyberFlix: shop '%s' prop master %u missing", name.c_str(), masterId);
			continue;
		}

		Prop prop;
		prop.masterResId = masterId;
		prop.scriptResId = READ_LE_UINT32(pm + kPropScriptOffset);
		prop.name = propMaster.readPascalString(kPropNameOffset);
		prop.name.toLowercase();
		prop.setName = propMaster.readPascalString(kPropSetOffset);
		prop.sceneName = propMaster.readPascalString(kPropSceneOffset);

		// Clamp the shape count to the prop-master resource, like propCount above.
		uint32 shapeCount = READ_LE_UINT32(pm + kPropShapeCountOffset);
		shapeCount = boundedRecordCount(shapeCount, propMaster.size(), kPropShapeTableOffset, kPropShapeStride);
		const RecordRange shapeRecords(propMaster, kPropShapeTableOffset, shapeCount, kPropShapeStride);
		for (uint32 j = 0; j < shapeRecords.size(); ++j) {
			const ResourceView shapeRecord = shapeRecords.record(j);
			const byte *se = shapeRecord.dataAt(0, kPropShapeStride);
			Shape shape;
			shape.resId = READ_LE_UINT32(se);
			shape.name = shapeRecord.readPascalString(kPropShapeNameOffset);
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

	debug(1, "CyberFlix: opened shop '%s': %u prop(s)", name.c_str(), _props.size());
	return true;
}

Shop::Prop *Shop::findProp(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (Prop &prop : _props)
		if (prop.name == key)
			return &prop;
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
	for (Prop &prop : _props) {
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
	for (const Shape &shapeEntry : prop.shapes) {
		if (shapeEntry.name != key)
			continue;
		int idx = resourceIndexById(shapeEntry.resId);
		const ResourceView shapeView = idx >= 0
				? engineView(static_cast<uint32>(idx)) : ResourceView();
		const byte *sh = shapeView.dataAt(0, kShapeCellTableOffset);
		if (!sh)
			return result;
		const uint16 poseCount = READ_LE_UINT16(sh + kShapePoseCountOffset);
		if (poseCount > (kShapePoseCountOffset - kShapePoseTableOffset) / 2)
			return result;
		result.valid = true;
		result.poseCount = poseCount;
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
	for (const Shape &candidate : prop.shapes)
		if (candidate.name == prop.shapeName) {
			shape = &candidate;
			break;
		}
	if (!shape) {
		debug(1, "CyberFlix: renderProp('%s'): shape '%s' not in master",
				prop.name.c_str(), prop.shapeName.c_str());
		return result;
	}
	int shIdx = resourceIndexById(shape->resId);
	const ResourceView shapeView = shIdx >= 0
			? engineView(static_cast<uint32>(shIdx)) : ResourceView();
	const byte *sh = shapeView.dataAt(0, kShapeCellTableOffset);
	if (!sh) {
		debug(1, "CyberFlix: renderProp('%s'): shape res %u missing",
				prop.name.c_str(), shape->resId);
		return result;
	}

	uint16 poseCount = READ_LE_UINT16(sh + kShapePoseCountOffset);
	uint16 cellCount = READ_LE_UINT16(sh + kShapeCellCountOffset);
	if (!poseCount || !cellCount ||
			poseCount > (kShapePoseCountOffset - kShapePoseTableOffset) / 2)
		return result;
	if (!shapeView.contains(kShapePoseTableOffset, static_cast<uint64>(poseCount) * 2))
		return result;
	const RecordRange cells(shapeView, kShapeCellTableOffset, cellCount, kShapeCellStride);
	if (!cells.valid())
		return result;
	// Pose id from the pose table; cells store poseId-1 in their id field.
	uint16 poseIdx = prop.poseIndex < poseCount ? prop.poseIndex : poseCount - 1;
	uint16 poseId = READ_LE_UINT16(sh + kShapePoseTableOffset + poseIdx * 2);
	if (poseId == 0)
		return result;

	ResourceView best;
	int bestDist = 0x7fffffff;
	for (uint16 i = 0; i < cellCount; ++i) {
		const ResourceView cell = cells.record(i);
		const byte *c = cell.dataAt(0, kShapeCellStride);
		if (READ_LE_UINT16(c + kCellIdOffset) != static_cast<uint16>(poseId - 1))
			continue;
		int dist = nativeAngleDistance(READ_LE_INT16(c + kCellAngleOffset), angle);
		if (dist < bestDist) {
			bestDist = dist;
			best = cell;
		}
	}
	const byte *bestData = best.dataAt(0, kShapeCellStride);
	if (!bestData) {
		debug(2, "CyberFlix: renderProp('%s'): no cell for pose %u in shape '%s'",
				prop.name.c_str(), poseId, prop.shapeName.c_str());
		return result;
	}

	// Cel frame resource: info tag packs the dimensions (width = info >> 16).
	uint32 frameRes = READ_LE_UINT32(bestData + kCellFrameResOffset);
	result.cel = celResource(frameRes);
	if (!result.cel) {
		debug(1, "CyberFlix: renderProp('%s'): cel res %u decode failed",
				prop.name.c_str(), frameRes);
		return result;
	}

	result.cellRect.top = READ_LE_INT16(bestData + kCellRectOffset);
	result.cellRect.left = READ_LE_INT16(bestData + kCellRectOffset + 2);
	result.cellRect.bottom = READ_LE_INT16(bestData + kCellRectOffset + 4);
	result.cellRect.right = READ_LE_INT16(bestData + kCellRectOffset + 6);
	result.regV = READ_LE_INT16(bestData + kCellRegVOffset);
	result.regH = READ_LE_INT16(bestData + kCellRegHOffset);
	result.cellScale = READ_LE_INT16(bestData + kCellScaleOffset);
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
	const int sourceH = cell.cellRect.height();
	const int sourceW = cell.cellRect.width();
	const int64 top = static_cast<int>(prop.y) - cell.regV;
	const int64 left = static_cast<int>(prop.x) - cell.regH;
	const int64 bottom = top + sourceH;
	const int64 right = left + sourceW;
	if (sourceH <= 0 || sourceW <= 0 || !fitsInt16(top) || !fitsInt16(left) ||
			!fitsInt16(bottom) || !fitsInt16(right))
		return result;

	result.cel = cell.cel;
	result.rect.top = static_cast<int16>(top);
	result.rect.left = static_cast<int16>(left);
	result.rect.bottom = static_cast<int16>(bottom);
	result.rect.right = static_cast<int16>(right);
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

	const int64 zClippedDepth = MAX<int64>(static_cast<int64>(projectedDepth) - prop.zClip, 0);
	const int nearLimit = (camera.nearPlane + (camera.nearPlane < 0 ? 3 : 0)) >> 2;
	if (projectedDepth <= nearLimit || zClippedDepth > camera.farPlane)
		return result;

	const int projectedH = fixedShift14(relY * cosH - relX * sinH);
	const int64 screenX = camera.centerX +
			static_cast<int64>(projectedH) * camera.focal / projectedDepth;
	const int64 screenY = camera.centerY -
			(static_cast<int64>(prop.z) - camera.baseZ - camera.cameraZ) * camera.focal / projectedDepth;
	const int angleToCamera = nativePointAngle(camera.cameraY - prop.y, camera.cameraX - prop.x);
	const int viewAngle = (prop.angle - angleToCamera) & 0xff;

	PropCellResult cell = resolvePropCel(prop, viewAngle);
	if (!cell.valid)
		return result;

	const int sourceH = cell.cellRect.height();
	const int sourceW = cell.cellRect.width();
	if (sourceH <= 0 || sourceW <= 0)
		return result;
	const int64 effectiveScale = (static_cast<int64>(prop.scale) * cell.cellScale) / 1000;
	const int64 scaledH = (effectiveScale * sourceH) / projectedDepth;
	const int64 scaledW = (effectiveScale * sourceW) / projectedDepth;
	if (scaledH <= 0 || scaledW <= 0 || scaledH > 32767 || scaledW > 32767)
		return result;

	const int64 top = screenY - (scaledH * cell.regV) / sourceH;
	const int64 left = screenX - (scaledW * cell.regH) / sourceW;
	const int64 bottom = top + scaledH;
	const int64 right = left + scaledW;
	const int64 depthBucket = camera.nearPlane ? zClippedDepth / camera.nearPlane : 0;
	if (!fitsInt16(top) || !fitsInt16(left) || !fitsInt16(bottom) || !fitsInt16(right) ||
			!fitsInt16(projectedDepth) || !fitsInt16(depthBucket))
		return result;

	result.cel = cell.cel;
	result.rect.top = static_cast<int16>(top);
	result.rect.left = static_cast<int16>(left);
	result.rect.bottom = static_cast<int16>(bottom);
	result.rect.right = static_cast<int16>(right);
	Common::Rect viewport(camera.viewportLeft, camera.viewportTop,
			camera.viewportRight, camera.viewportBottom);
	if (!result.rect.intersects(viewport))
		return result;
	result.depth = static_cast<int16>(projectedDepth);
	result.depthBucket = static_cast<int16>(depthBucket);
	result.valid = true;
	return result;
}

} // End of namespace CyberFlix

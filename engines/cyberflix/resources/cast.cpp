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

#include "cyberflix/cast.h"
#include "cyberflix/image.h"
#include "cyberflix/resource_helpers.h"

namespace Cyberflix {

const byte *Cast::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	return resourceEngineBase(_fileData, _archive.getResource(index));
}

int Cast::resourceIndexById(uint32 id) const {
	return Cyberflix::resourceIndexById(_archive, id);
}

Common::String Cast::pascalString(const byte *p) const {
	return readPascalString(p, _fileData);
}

bool Cast::open(const Common::String &name) {
	_master = -1;
	_actors.clear();
	_actorIndexByName.clear();
	_script.reset();
	_name = name;

	if (!openArchiveFile(name, "cast", _fileData, _archive))
		return false;

	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		if (!_archive.getResource(i).empty && _archive.getResource(i).info == kMasterHeaderInfoTag) {
			_master = static_cast<int>(i);
			break;
		}
	}
	if (_master < 0) {
		warning("Cyberflix: cast '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase(static_cast<uint32>(_master));
	if (!hdr || hdr + kMasterActorTableOffset > _fileData.end()) {
		warning("Cyberflix: cast '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}

	uint32 scriptRes = READ_LE_UINT32(hdr + kMasterScriptOffset);
	int scriptIdx = resourceIndexById(scriptRes);
	if (scriptIdx >= 0) {
		Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource(static_cast<uint32>(scriptIdx)));
		Common::ScopedPtr<Script> script(new Script());
		if (s && script->parse(s.get()))
			_script.reset(script.release());
	}
	if (!_script)
		warning("Cyberflix: cast '%s' script res %u missing", name.c_str(), scriptRes);

	uint32 actorCount = READ_LE_UINT32(hdr + kMasterActorCountOffset);
	const byte *entry = hdr + kMasterActorTableOffset;
	for (uint32 i = 0; i < actorCount; ++i, entry += kMasterActorStride) {
		if (entry + kMasterActorStride > _fileData.end())
			break;
		uint32 masterId = READ_LE_UINT32(entry);
		int mIdx = resourceIndexById(masterId);
		const byte *am = mIdx >= 0 ? engineBase(static_cast<uint32>(mIdx)) : nullptr;
		if (!am || am + kActorShapeTableOffset > _fileData.end()) {
			warning("Cyberflix: cast '%s' actor master %u missing", name.c_str(), masterId);
			continue;
		}

		Common::SharedPtr<Actor> actor(new Actor());
		actor->masterResId = masterId;
		actor->scriptResId = READ_LE_UINT32(am + kActorScriptOffset);
		actor->name = pascalString(am + kActorNameOffset);
		actor->name.toLowercase();
		actor->setName = pascalString(am + kActorSetOffset);
		actor->setName.toLowercase();
		actor->sceneName = pascalString(am + kActorSceneOffset);
		actor->sceneName.toLowercase();
		actor->owner = "none";

		uint32 shapeCount = READ_LE_UINT32(am + kActorShapeCountOffset);
		const byte *shapeEntry = am + kActorShapeTableOffset;
		for (uint32 shape = 0; shape < shapeCount; ++shape, shapeEntry += kActorShapeStride) {
			if (shapeEntry + kActorShapeStride > _fileData.end())
				break;
			Actor::Shape actorShape;
			actorShape.resId = READ_LE_UINT32(shapeEntry);
			actorShape.name = pascalString(shapeEntry + kActorShapeNameOffset);
			actorShape.name.toLowercase();
			if (!actorShape.name.empty()) {
				if (actor->shapeName.empty())
					actor->shapeName = actorShape.name;
				actor->shapes.push_back(actorShape);
			}
		}
		if (actor->shapeName.empty())
			actor->shapeName = "none";

		int sIdx = actor->scriptResId ? resourceIndexById(actor->scriptResId) : -1;
		if (sIdx >= 0) {
			Common::ScopedPtr<Common::SeekableReadStream> s(_archive.createReadStreamForResource(static_cast<uint32>(sIdx)));
			Common::SharedPtr<Script> script(new Script());
			if (s && script->parse(s.get()))
				actor->script = script;
		}

		if (!actor->name.empty()) {
			if (!_actorIndexByName.contains(actor->name))
				_actorIndexByName[actor->name] = _actors.size();
			_actors.push_back(actor);
		}
	}

	debug(1, "Cyberflix: opened cast '%s': %u actor(s)", name.c_str(), _actors.size());
	return true;
}

Common::SharedPtr<Cast::Actor> Cast::findActor(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	Common::HashMap<Common::String, uint32>::const_iterator it = _actorIndexByName.find(key);
	if (it != _actorIndexByName.end() && it->_value < _actors.size())
		return _actors[it->_value];
	return Common::SharedPtr<Actor>();
}

bool Cast::resolveActorCel(const Actor &actor, int angle, CelImage &cel,
		Common::Rect &cellRect, int16 &regV, int16 &regH,
		int16 &cellScale) const {
	const Actor::Shape *shape = nullptr;
	for (uint32 i = 0; i < actor.shapes.size(); ++i) {
		if (actor.shapes[i].name == actor.shapeName) {
			shape = &actor.shapes[i];
			break;
		}
	}
	if (!shape) {
		debug(1, "Cyberflix: renderActor('%s'): shape '%s' not in master",
				actor.name.c_str(), actor.shapeName.c_str());
		return false;
	}
	int shIdx = resourceIndexById(shape->resId);
	const byte *sh = shIdx >= 0 ? engineBase(static_cast<uint32>(shIdx)) : nullptr;
	if (!sh || sh + kShapeCellTableOffset > _fileData.end()) {
		debug(1, "Cyberflix: renderActor('%s'): shape res %u missing",
				actor.name.c_str(), shape->resId);
		return false;
	}

	uint16 poseCount = READ_LE_UINT16(sh + kShapePoseCountOffset);
	uint16 cellCount = READ_LE_UINT16(sh + kShapeCellCountOffset);
	if (!poseCount || !cellCount)
		return false;
	uint16 poseIdx = actor.poseIndex < poseCount ? actor.poseIndex : 0;
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
		debug(1, "Cyberflix: renderActor('%s'): no cell for pose %u in shape '%s'",
				actor.name.c_str(), poseId, actor.shapeName.c_str());
		return false;
	}

	uint32 frameRes = READ_LE_UINT32(best + kCellFrameResOffset);
	int fIdx = resourceIndexById(frameRes);
	if (fIdx < 0)
		return false;
	const Archive::Resource &fres = _archive.getResource(static_cast<uint32>(fIdx));
	uint16 w = static_cast<uint16>(fres.info >> 16);
	uint16 h = static_cast<uint16>(fres.info & 0xffff);
	Common::ScopedPtr<Common::SeekableReadStream> fs(_archive.createReadStreamForResource(static_cast<uint32>(fIdx)));
	if (!fs)
		return false;
	bool ok = decodeCel(*fs, w, h, cel);
	if (!ok) {
		debug(1, "Cyberflix: renderActor('%s'): cel res %u decode failed",
				actor.name.c_str(), frameRes);
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

bool Cast::renderWorldActor(const Actor &actor, const Shop::WorldCamera &camera,
		const Common::String &setName, CelImage &cel, Common::Rect &rect,
		int16 &depth, int16 &depthBucket) const {
	if (!actor.visible || !actor.setName.equalsIgnoreCase(setName))
		return false;

	const int relX = actor.x - camera.cameraX;
	const int relY = actor.y - camera.cameraY;
	// Native yaw rotation followed by pinhole perspective projection:
	// x' = x*f/z, y' = y*f/z. SET panorama records store camera headings in the
	// same 8-bit circle as actor-facing angles (TI.EXE FUN_00442e90 fills the
	// yaw table entries consumed by FUN_00443340). See Foley/van Dam et al.,
	// Computer Graphics: Principles and Practice, viewing pipeline chapter.
	const int sinH = nativeTrigSin(camera.heading);
	const int cosH = nativeTrigCos(camera.heading);
	const int projectedDepth = fixedShift14(relY * sinH + relX * cosH);
	if (projectedDepth < 1)
		return false;

	const int zClippedDepth = MAX(projectedDepth - actor.zClip, 0);
	const int nearLimit = (camera.nearPlane + (camera.nearPlane < 0 ? 3 : 0)) >> 2;
	if (projectedDepth <= nearLimit || zClippedDepth > camera.farPlane)
		return false;

	const int projectedH = fixedShift14(relY * cosH - relX * sinH);
	const int screenX = camera.centerX + projectedH * camera.focal / projectedDepth;
	const int screenY = camera.centerY -
			((actor.z - camera.baseZ - camera.cameraZ) * camera.focal) / projectedDepth;
	const int angleToCamera = nativePointAngle(camera.cameraY - actor.y, camera.cameraX - actor.x);
	const int viewAngle = (actor.angle - angleToCamera) & 0xff;

	Common::Rect cellRect;
	int16 regV = 0, regH = 0, cellScale = 0;
	if (!resolveActorCel(actor, viewAngle, cel, cellRect, regV, regH, cellScale))
		return false;

	const int sourceH = cellRect.height();
	const int sourceW = cellRect.width();
	if (sourceH <= 0 || sourceW <= 0)
		return false;
	const int effectiveScale = (actor.scale * cellScale) / 1000;
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
	depth = static_cast<int16>(projectedDepth);
	depthBucket = camera.nearPlane ? static_cast<int16>(zClippedDepth / camera.nearPlane) : 0;
	return true;
}

} // End of namespace Cyberflix

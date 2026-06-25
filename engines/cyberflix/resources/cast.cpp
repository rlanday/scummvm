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

Common::SharedPtr<Cast::Actor> Cast::findActorByMasterResId(uint32 masterResId) {
	for (uint32 i = 0; i < _actors.size(); ++i) {
		if (_actors[i]->masterResId == masterResId)
			return _actors[i];
	}
	return Common::SharedPtr<Actor>();
}

bool Cast::addActorInstance(const Actor &source, const Common::String &newName) {
	if (newName.empty())
		return false;
	Common::String key = newName;
	key.toLowercase();
	if (_actorIndexByName.contains(key))
		return false;

	Common::SharedPtr<Actor> clone(new Actor(source));
	clone->name = key;
	_actorIndexByName[clone->name] = _actors.size();
	_actors.push_back(clone);
	return true;
}

Cast::ActorCellResult Cast::resolveActorCell(const Actor &actor, int angle) const {
	ActorCellResult result;
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
		return result;
	}
	int shIdx = resourceIndexById(shape->resId);
	const byte *sh = shIdx >= 0 ? engineBase(static_cast<uint32>(shIdx)) : nullptr;
	const uint64 shapeLen = shIdx >= 0 ?
			static_cast<uint64>(_archive.getResource(static_cast<uint32>(shIdx)).length) + 4 : 0;
	if (!sh || shapeLen < kShapeCellTableOffset) {
		debug(1, "Cyberflix: renderActor('%s'): shape res %u missing",
				actor.name.c_str(), shape->resId);
		return result;
	}

	uint16 poseCount = READ_LE_UINT16(sh + kShapePoseCountOffset);
	uint16 cellCount = READ_LE_UINT16(sh + kShapeCellCountOffset);
	if (!poseCount || !cellCount)
		return result;
	if (static_cast<uint64>(kShapePoseTableOffset) + static_cast<uint64>(poseCount) * 2 > shapeLen ||
			static_cast<uint64>(kShapeCellTableOffset) + static_cast<uint64>(cellCount) * kShapeCellStride > shapeLen)
		return result;
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
		return result;
	}

	result.frameRes = READ_LE_UINT32(best + kCellFrameResOffset);
	int fIdx = resourceIndexById(result.frameRes);
	if (fIdx < 0)
		return result;

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

Cast::ActorProjectionResult Cast::projectWorldActor(const Actor &actor, const Shop::WorldCamera &camera,
		const Common::String &setName) const {
	ActorProjectionResult result;
	if (!actor.visible || actor.mode == 0 || !actor.setName.equalsIgnoreCase(setName))
		return result;

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
		return result;

	const int zClippedDepth = MAX(projectedDepth - actor.zClip, 0);
	const int nearLimit = (camera.nearPlane + (camera.nearPlane < 0 ? 3 : 0)) >> 2;
	if (projectedDepth <= nearLimit || zClippedDepth > camera.farPlane)
		return result;

	const int projectedH = fixedShift14(relY * cosH - relX * sinH);
	const int screenX = camera.centerX + projectedH * camera.focal / projectedDepth;
	const int screenY = camera.centerY -
			((actor.z - camera.baseZ - camera.cameraZ) * camera.focal) / projectedDepth;
	const int angleToCamera = nativePointAngle(camera.cameraY - actor.y, camera.cameraX - actor.x);
	const int viewAngle = (actor.angle - angleToCamera) & 0xff;

	result.cell = resolveActorCell(actor, viewAngle);
	if (!result.cell.valid)
		return result;

	const int sourceH = result.cell.cellRect.height();
	const int sourceW = result.cell.cellRect.width();
	if (sourceH <= 0 || sourceW <= 0)
		return result;
	const int effectiveScale = (actor.scale * result.cell.cellScale) / 1000;
	const int scaledH = (effectiveScale * sourceH) / projectedDepth;
	const int scaledW = (effectiveScale * sourceW) / projectedDepth;
	if (scaledH <= 0 || scaledW <= 0)
		return result;

	result.rect.top = screenY - (scaledH * result.cell.regV) / sourceH;
	result.rect.left = screenX - (scaledW * result.cell.regH) / sourceW;
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

Cast::ActorRenderResult Cast::renderWorldActor(const Actor &actor, const Shop::WorldCamera &camera,
		const Common::String &setName) const {
	ActorRenderResult result;
	ActorProjectionResult projected = projectWorldActor(actor, camera, setName);
	if (!projected.valid)
		return result;

	int fIdx = resourceIndexById(projected.cell.frameRes);
	if (fIdx < 0)
		return result;
	const Archive::Resource &fres = _archive.getResource(static_cast<uint32>(fIdx));
	uint16 w = static_cast<uint16>(fres.info >> 16);
	uint16 h = static_cast<uint16>(fres.info & 0xffff);
	Common::ScopedPtr<Common::SeekableReadStream> fs(_archive.createReadStreamForResource(static_cast<uint32>(fIdx)));
	if (!fs)
		return result;
	bool ok = decodeCel(*fs, w, h, result.cel);
	if (!ok) {
		debug(1, "Cyberflix: renderActor('%s'): cel res %u decode failed",
				actor.name.c_str(), projected.cell.frameRes);
		return result;
	}

	result.rect = projected.rect;
	result.depth = projected.depth;
	result.depthBucket = projected.depthBucket;
	result.valid = true;
	return result;
}

} // End of namespace Cyberflix

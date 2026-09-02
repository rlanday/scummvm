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

namespace CyberFlix {

ResourceView Cast::engineView(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return ResourceView();
	return resourceEngineView(_fileData, _archive.getResource(index));
}

int Cast::resourceIndexById(uint32 id) const {
	return CyberFlix::resourceIndexById(_archive, id);
}

bool Cast::open(const Common::String &name) {
	_master = -1;
	_actors.clear();
	_actorIndexByName.clear();
	_script.reset();
	_name = name;

	if (!openArchiveFile(name, "cast", _fileData, _archive))
		return false;

	_master = findMasterHeaderIndex(_archive);
	if (_master < 0) {
		warning("CyberFlix: cast '%s' has no master header", name.c_str());
		_archive.close();
		_fileData.clear();
		return false;
	}

	const ResourceView master = engineView(static_cast<uint32>(_master));
	const byte *hdr = master.dataAt(0, kMasterActorTableOffset);
	if (!hdr) {
		warning("CyberFlix: cast '%s' master header truncated", name.c_str());
		_master = -1;
		_archive.close();
		_fileData.clear();
		return false;
	}

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
				warning("CyberFlix: cast '%s' script res %u failed to parse", name.c_str(), scriptRes);
		} else {
			warning("CyberFlix: cast '%s' script res %u missing", name.c_str(), scriptRes);
		}
	}

	// Clamp the file-supplied actor count to the master resource's length
	// (engine-base frame is res.length + 4 bytes), as puppet.cpp does, so a
	// corrupt count cannot parse neighbouring resources' bytes as actors.
	uint32 actorCount = READ_LE_UINT32(hdr + kMasterActorCountOffset);
	actorCount = boundedRecordCount(actorCount, master.size(), kMasterActorTableOffset, kMasterActorStride);
	const RecordRange actorRecords(master, kMasterActorTableOffset, actorCount, kMasterActorStride);
	for (uint32 i = 0; i < actorRecords.size(); ++i) {
		const byte *entry = actorRecords.record(i).dataAt(0, kMasterActorStride);
		uint32 masterId = READ_LE_UINT32(entry);
		int mIdx = resourceIndexById(masterId);
		const ResourceView actorMaster = mIdx >= 0
				? engineView(static_cast<uint32>(mIdx)) : ResourceView();
		const byte *am = actorMaster.dataAt(0, kActorShapeTableOffset);
		if (!am) {
			warning("CyberFlix: cast '%s' actor master %u missing", name.c_str(), masterId);
			continue;
		}

		Common::SharedPtr<Actor> actor(new Actor());
		actor->masterResId = masterId;
		actor->scriptResId = READ_LE_UINT32(am + kActorScriptOffset);
		actor->name = actorMaster.readPascalString(kActorNameOffset);
		actor->name.toLowercase();
		actor->setName = actorMaster.readPascalString(kActorSetOffset);
		actor->setName.toLowercase();
		actor->sceneName = actorMaster.readPascalString(kActorSceneOffset);
		actor->sceneName.toLowercase();
		actor->owner = "none";

		uint32 shapeCount = boundedRecordCount(READ_LE_UINT32(am + kActorShapeCountOffset),
				actorMaster.size(), kActorShapeTableOffset, kActorShapeStride);
		const RecordRange shapeRecords(actorMaster, kActorShapeTableOffset, shapeCount, kActorShapeStride);
		for (uint32 shape = 0; shape < shapeRecords.size(); ++shape) {
			const ResourceView shapeRecord = shapeRecords.record(shape);
			const byte *shapeEntry = shapeRecord.dataAt(0, kActorShapeStride);
			Actor::Shape actorShape;
			actorShape.resId = READ_LE_UINT32(shapeEntry);
			actorShape.name = shapeRecord.readPascalString(kActorShapeNameOffset);
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

	debug(1, "CyberFlix: opened cast '%s': %u actor(s)", name.c_str(), _actors.size());
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
	for (const Common::SharedPtr<Actor> &actor : _actors) {
		if (actor->masterResId == masterResId)
			return actor;
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

uint16 Cast::shapePoseCountFor(const Actor &actor) const {
	const Actor::Shape *shape = nullptr;
	for (const Actor::Shape &candidate : actor.shapes) {
		if (candidate.name == actor.shapeName) {
			shape = &candidate;
			break;
		}
	}
	if (!shape)
		return 0;
	int shIdx = resourceIndexById(shape->resId);
	const ResourceView shapeView = shIdx >= 0
			? engineView(static_cast<uint32>(shIdx)) : ResourceView();
	uint16 poseCount;
	if (!shapeView.readUint16LE(kShapePoseCountOffset, poseCount))
		return 0;
	if (poseCount > (kShapePoseCountOffset - kShapePoseTableOffset) / 2)
		return 0;
	return poseCount;
}

void Cast::advanceActorPoses() {
	for (const Common::SharedPtr<Actor> &actor : _actors) {
		Actor &a = *actor;
		const uint16 count = shapePoseCountFor(a);
		if (count <= 1)
			continue;
		a.poseIndex++;
		if (a.poseIndex >= count)
			a.poseIndex = 0;
	}
}

Cast::ActorCellResult Cast::resolveActorCell(const Actor &actor, int angle) const {
	ActorCellResult result;
	const Actor::Shape *shape = nullptr;
	for (const Actor::Shape &candidate : actor.shapes) {
		if (candidate.name == actor.shapeName) {
			shape = &candidate;
			break;
		}
	}
	if (!shape) {
		debug(1, "CyberFlix: renderActor('%s'): shape '%s' not in master",
				actor.name.c_str(), actor.shapeName.c_str());
		return result;
	}
	int shIdx = resourceIndexById(shape->resId);
	const ResourceView shapeView = shIdx >= 0
			? engineView(static_cast<uint32>(shIdx)) : ResourceView();
	const byte *sh = shapeView.dataAt(0, kShapeCellTableOffset);
	if (!sh) {
		debug(1, "CyberFlix: renderActor('%s'): shape res %u missing",
				actor.name.c_str(), shape->resId);
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
	uint16 poseIdx = actor.poseIndex < poseCount ? actor.poseIndex : 0;
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
		debug(1, "CyberFlix: renderActor('%s'): no cell for pose %u in shape '%s'",
				actor.name.c_str(), poseId, actor.shapeName.c_str());
		return result;
	}

	result.frameRes = READ_LE_UINT32(bestData + kCellFrameResOffset);
	int fIdx = resourceIndexById(result.frameRes);
	if (fIdx < 0)
		return result;

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

	const int64 zClippedDepth = MAX<int64>(static_cast<int64>(projectedDepth) - actor.zClip, 0);
	const int nearLimit = (camera.nearPlane + (camera.nearPlane < 0 ? 3 : 0)) >> 2;
	if (projectedDepth <= nearLimit || zClippedDepth > camera.farPlane)
		return result;

	const int projectedH = fixedShift14(relY * cosH - relX * sinH);
	const int64 screenX = camera.centerX +
			static_cast<int64>(projectedH) * camera.focal / projectedDepth;
	const int64 screenY = camera.centerY -
			(static_cast<int64>(actor.z) - camera.baseZ - camera.cameraZ) * camera.focal / projectedDepth;
	const int angleToCamera = nativePointAngle(camera.cameraY - actor.y, camera.cameraX - actor.x);
	const int viewAngle = (actor.angle - angleToCamera) & 0xff;

	result.cell = resolveActorCell(actor, viewAngle);
	if (!result.cell.valid)
		return result;

	const int sourceH = result.cell.cellRect.height();
	const int sourceW = result.cell.cellRect.width();
	if (sourceH <= 0 || sourceW <= 0)
		return result;
	const int64 effectiveScale = (static_cast<int64>(actor.scale) * result.cell.cellScale) / 1000;
	const int64 scaledH = (effectiveScale * sourceH) / projectedDepth;
	const int64 scaledW = (effectiveScale * sourceW) / projectedDepth;
	if (scaledH <= 0 || scaledW <= 0 || scaledH > 32767 || scaledW > 32767)
		return result;

	const int64 top = screenY - (scaledH * result.cell.regV) / sourceH;
	const int64 left = screenX - (scaledW * result.cell.regH) / sourceW;
	const int64 bottom = top + scaledH;
	const int64 right = left + scaledW;
	const int64 depthBucket = camera.nearPlane ? zClippedDepth / camera.nearPlane : 0;
	if (!fitsInt16(top) || !fitsInt16(left) || !fitsInt16(bottom) || !fitsInt16(right) ||
			!fitsInt16(projectedDepth) || !fitsInt16(depthBucket))
		return result;

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
		debug(1, "CyberFlix: renderActor('%s'): cel res %u decode failed",
				actor.name.c_str(), projected.cell.frameRes);
		return result;
	}

	result.rect = projected.rect;
	result.depth = projected.depth;
	result.depthBucket = projected.depthBucket;
	result.valid = true;
	return result;
}

} // End of namespace CyberFlix

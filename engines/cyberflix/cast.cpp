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

#include "cyberflix/cast.h"
#include "cyberflix/sound.h" // kMasterHeaderInfoTag

namespace Cyberflix {

const byte *Cast::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	if (res.empty || res.dataOffset < 4 || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset - 4;
}

int Cast::resourceIndexById(uint32 id) const {
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i)
		if (!_archive.getResource(i).empty && _archive.getResource(i).id == id)
			return (int)i;
	return -1;
}

Common::String Cast::pascalString(const byte *p) const {
	if (!p || p >= _fileData.end())
		return Common::String();
	uint len = *p;
	if (p + 1 + len > _fileData.end())
		return Common::String();
	return Common::String((const char *)p + 1, len);
}

bool Cast::open(const Common::String &name) {
	_master = -1;
	_actors.clear();
	_actorIndexByName.clear();
	_script.reset();
	_name = name;

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open cast '%s'", name.c_str());
		return false;
	}
	uint32 size = (uint32)file.size();
	_fileData.resize(size);
	if (file.read(_fileData.begin(), size) != size) {
		warning("Cyberflix: could not read cast '%s'", name.c_str());
		return false;
	}
	file.close();

	if (!_archive.open(new Common::MemoryReadStream(_fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid cast container", name.c_str());
		return false;
	}

	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		if (!_archive.getResource(i).empty && _archive.getResource(i).info == kMasterHeaderInfoTag) {
			_master = (int)i;
			break;
		}
	}
	if (_master < 0) {
		warning("Cyberflix: cast '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase((uint32)_master);
	if (!hdr || hdr + kMasterActorTableOffset > _fileData.end()) {
		warning("Cyberflix: cast '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}

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
		warning("Cyberflix: cast '%s' script res %u missing", name.c_str(), scriptRes);

	uint32 actorCount = READ_LE_UINT32(hdr + kMasterActorCountOffset);
	const byte *entry = hdr + kMasterActorTableOffset;
	for (uint32 i = 0; i < actorCount; ++i, entry += kMasterActorStride) {
		if (entry + kMasterActorStride > _fileData.end())
			break;
		uint32 masterId = READ_LE_UINT32(entry);
		int mIdx = resourceIndexById(masterId);
		const byte *am = mIdx >= 0 ? engineBase((uint32)mIdx) : nullptr;
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
		if (shapeCount > 0 && am + kActorShapeTableOffset + kActorShapeNameOffset < _fileData.end()) {
			actor->shapeName = pascalString(am + kActorShapeTableOffset + kActorShapeNameOffset);
			actor->shapeName.toLowercase();
		}
		if (actor->shapeName.empty())
			actor->shapeName = "none";

		int sIdx = actor->scriptResId ? resourceIndexById(actor->scriptResId) : -1;
		if (sIdx >= 0) {
			Common::SeekableReadStream *s = _archive.createReadStreamForResource((uint32)sIdx);
			Common::SharedPtr<Script> script(new Script());
			if (s && script->parse(s))
				actor->script = script;
			delete s;
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

} // End of namespace Cyberflix

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
#include "common/util.h"

#include "cyberflix/cast.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/set.h"

namespace Cyberflix {

Common::SharedPtr<Cast> ActorRuntime::findCastShared(const Common::String &name) const {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _casts.size(); ++i)
		if (_casts[i]->name() == key)
			return _casts[i];
	return Common::SharedPtr<Cast>();
}

ActorRuntime::ActorRef ActorRuntime::findActorRef(const Common::String &name) const {
	ActorRef ref;
	for (uint32 i = 0; i < _casts.size(); ++i) {
		Common::SharedPtr<Cast::Actor> actor = _casts[i]->findActor(name);
		if (actor) {
			ref.cast = _casts[i];
			ref.actor = actor;
			return ref;
		}
	}
	return ref;
}

bool ActorRuntime::resolveActorStar(CyberflixEngine &engine, Cast::Actor &actor) {
	if (!engine._setRuntime.set() || !engine._setRuntime.set()->isOpen() ||
			!actor.setName.equalsIgnoreCase(engine._setRuntime.set()->setName()))
		return false;

	int16 x = 0, y = 0, z = 0;
	if (!engine._setRuntime.set()->starXYZ(actor.sceneName, x, y, z))
		return false;

	if (actor.x != x || actor.y != y || actor.z != z) {
		actor.x = x;
		actor.y = y;
		actor.z = z;
		engine._propRuntime.setDirty(true);
	}
	return true;
}

void ActorRuntime::refreshActorStarPositions(CyberflixEngine &engine) {
	if (!engine._setRuntime.set() || !engine._setRuntime.set()->isOpen())
		return;

	for (uint32 c = 0; c < _casts.size(); ++c) {
		for (uint32 i = 0; i < _casts[c]->actorCount(); ++i)
			resolveActorStar(engine, _casts[c]->actor(i));
	}
}

void ActorRuntime::collectWorldActors(CyberflixEngine &engine, Common::Array<const Cast::Actor *> &draw,
		Common::Array<const Cast *> &drawCast, Common::Array<int16> &depths,
		const Shop::WorldCamera &camera) const {
	if (!engine._setRuntime.set() || !engine._setRuntime.set()->isOpen())
		return;
	const Common::String &setName = engine._setRuntime.set()->setName();
	for (uint32 c = 0; c < _casts.size(); ++c) {
		for (uint32 i = 0; i < _casts[c]->actorCount(); ++i) {
			const Cast::Actor &actor = _casts[c]->actor(i);
			if (!actor.visible || !actor.setName.equalsIgnoreCase(setName))
				continue;
			CelImage cel;
			Common::Rect rect;
			int16 depth = 0;
			if (!_casts[c]->renderWorldActor(actor, camera, setName, cel, rect, depth))
				continue;
			draw.push_back(&actor);
			drawCast.push_back(_casts[c].get());
			depths.push_back(depth);
		}
	}

	for (uint32 i = 1; i < draw.size(); ++i) {
		const Cast::Actor *actor = draw[i];
		const Cast *cast = drawCast[i];
		int16 depth = depths[i];
		uint32 j = i;
		for (; j > 0 && depths[j - 1] < depth; --j) {
			draw[j] = draw[j - 1];
			drawCast[j] = drawCast[j - 1];
			depths[j] = depths[j - 1];
		}
		draw[j] = actor;
		drawCast[j] = cast;
		depths[j] = depth;
	}
}

void ActorRuntime::openCastFile(CyberflixEngine &engine, const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	if (findCastShared(key)) {
		debug(1, "Cyberflix: cast '%s' already open", key.c_str());
		return;
	}

	Common::SharedPtr<Cast> cast(new Cast());
	if (!cast->open(key))
		return;
	_casts.push_back(cast);
	refreshActorStarPositions(engine);
	engine._propRuntime.setDirty(true);
}

void ActorRuntime::closeCastFile(CyberflixEngine &engine, const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _casts.size(); ++i) {
		if (_casts[i]->name() == key) {
			debug(1, "Cyberflix: cast '%s' closed", key.c_str());
			_casts.remove_at(i);
			engine._propRuntime.setDirty(true);
			return;
		}
	}
	debug(1, "Cyberflix: closecastfile('%s'): cast not open", key.c_str());
}

void ActorRuntime::sendToCast(CyberflixEngine &engine, const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtocast('%s') -> %s(%u args)", castName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Cast> cast = findCastShared(castName);
	if (!cast) {
		warning("Cyberflix: sendtocast('%s'): cast not open", castName.c_str());
		return;
	}
	engine.dispatchWithScopes(cast->castScript(), nullptr, cast->name(), Common::String(),
			message, args, "cast");
}

Value ActorRuntime::sendToCastFx(CyberflixEngine &engine, const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtocastfx('%s') -> %s(%u args)", castName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Cast> cast = findCastShared(castName);
	if (!cast) {
		warning("Cyberflix: sendtocastfx('%s'): cast not open", castName.c_str());
		return Value();
	}
	return engine.dispatchWithScopesValue(cast->castScript(), nullptr, cast->name(),
			Common::String(), message, args, "castfx");
}

void ActorRuntime::sendToActor(CyberflixEngine &engine, const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoactor('%s') -> %s(%u args)", actorName.c_str(),
			message.c_str(), args.size());
	ActorRef ref = findActorRef(actorName);
	if (!ref.actor) {
		warning("Cyberflix: sendtoactor('%s'): no such actor", actorName.c_str());
		return;
	}

	// Actor dispatch always searches [actor script, cast script, BOOTFILE res2].
	// Avoid the generic scope-chain array in the sampled actor->puppet cascade.
	engine.dispatchWithScopes(ref.actor->script.get(), ref.cast->castScript(),
			ref.actor->name, ref.actor->name, message, args, "actor");
}

Value ActorRuntime::sendToActorFx(CyberflixEngine &engine, const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoactorfx('%s') -> %s(%u args)", actorName.c_str(),
			message.c_str(), args.size());
	ActorRef ref = findActorRef(actorName);
	if (!ref.actor) {
		warning("Cyberflix: sendtoactorfx('%s'): no such actor", actorName.c_str());
		return Value();
	}

	return engine.dispatchWithScopesValue(ref.actor->script.get(), ref.cast->castScript(),
			ref.actor->name, ref.actor->name, message, args, "actorfx");
}

int ActorRuntime::countActors() const {
	uint32 count = 0;
	for (uint32 i = 0; i < _casts.size(); ++i)
		count += _casts[i]->actorCount();
	return static_cast<int>(count);
}

Common::String ActorRuntime::indexToActor(int index) const {
	if (index < 1)
		return Common::String();
	uint32 remaining = static_cast<uint32>(index);
	for (uint32 i = 0; i < _casts.size(); ++i) {
		if (remaining <= _casts[i]->actorCount())
			return _casts[i]->actor(remaining - 1).name;
		remaining -= _casts[i]->actorCount();
	}
	return Common::String();
}

bool ActorRuntime::actorVisible(CyberflixEngine &engine, const Common::String &name, const bool *newVisible) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorvisible('%s'): no such actor", name.c_str());
		return false;
	}
	if (newVisible && ref.actor->visible != *newVisible) {
		ref.actor->visible = *newVisible;
		engine._propRuntime.setDirty(true);
	}
	return ref.actor->visible;
}

Common::String ActorRuntime::actorSet(CyberflixEngine &engine, const Common::String &name, const Common::String *newSet) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorset('%s'): no such actor", name.c_str());
		return Common::String();
	}
	if (newSet) {
		Common::String key = *newSet;
		key.toLowercase();
		if (ref.actor->setName != key) {
			ref.actor->setName = key;
			engine._propRuntime.setDirty(true);
		}
	}
	return ref.actor->setName;
}

Common::String ActorRuntime::actorStar(CyberflixEngine &engine, const Common::String &name, const Common::String *newStar) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorstar('%s'): no such actor", name.c_str());
		return Common::String();
	}
	if (newStar) {
		Common::String key = *newStar;
		key.toLowercase();
		if (ref.actor->sceneName != key) {
			ref.actor->sceneName = key;
			engine._propRuntime.setDirty(true);
		}
		resolveActorStar(engine, *ref.actor);
	}
	return ref.actor->sceneName;
}

Common::String ActorRuntime::actorPose(CyberflixEngine &engine, const Common::String &name, const Common::String *newPose) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorpose('%s'): no such actor", name.c_str());
		return Common::String();
	}
	if (newPose) {
		Common::String key = *newPose;
		key.toLowercase();
		if (ref.actor->shapeName != key) {
			ref.actor->shapeName = key;
			engine._propRuntime.setDirty(true);
		}
	}
	return ref.actor->shapeName;
}

void ActorRuntime::actorXYZ(CyberflixEngine &engine, const Common::String &name, int x, int y, int z) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorxyz('%s'): no such actor", name.c_str());
		return;
	}
	if (ref.actor->x != static_cast<int16>(x) || ref.actor->y != static_cast<int16>(y) || ref.actor->z != static_cast<int16>(z)) {
		ref.actor->x = static_cast<int16>(x);
		ref.actor->y = static_cast<int16>(y);
		ref.actor->z = static_cast<int16>(z);
		engine._propRuntime.setDirty(true);
	}
}

int ActorRuntime::actorXYZ(CyberflixEngine &engine, const Common::String &name, int selector) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorxyz('%s'): no such actor", name.c_str());
		return 0;
	}
	switch (selector) {
	case 1:
		return ref.actor->x;
	case 2:
		return ref.actor->y;
	case 3:
		return ref.actor->z;
	case 4:
		return engine.makePoint(ref.actor->x, ref.actor->y);
	default:
		return 0;
	}
}

int ActorRuntime::actorDeg(CyberflixEngine &engine, const Common::String &name, const int *newDeg) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actordeg('%s'): no such actor", name.c_str());
		return 0;
	}
	if (newDeg && ref.actor->angle != static_cast<int16>(*newDeg & 0xff)) {
		ref.actor->angle = static_cast<int16>(*newDeg & 0xff);
		engine._propRuntime.setDirty(true);
	}
	return ref.actor->angle;
}

int ActorRuntime::actorValue(const Common::String &name, const int *newValue) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorvalue('%s'): no such actor", name.c_str());
		return 0;
	}
	if (newValue)
		ref.actor->value = *newValue;
	return ref.actor->value;
}

Common::String ActorRuntime::actorOwner(const Common::String &name,
		const Common::String *newOwner) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorowner('%s'): no such actor", name.c_str());
		return Common::String();
	}
	if (newOwner) {
		Common::String key = *newOwner;
		key.toLowercase();
		ref.actor->owner = key;
	}
	return ref.actor->owner;
}

void ActorRuntime::actorZClip(CyberflixEngine &engine, const Common::String &name, int zClip) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorzclip('%s'): no such actor", name.c_str());
		return;
	}
	if (ref.actor->zClip != zClip) {
		ref.actor->zClip = zClip;
		engine._propRuntime.setDirty(true);
	}
}

void ActorRuntime::actorSpeed(const Common::String &name, int speed) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorspeed('%s'): no such actor", name.c_str());
		return;
	}
	ref.actor->speed = speed;
}

void ActorRuntime::actorScale(CyberflixEngine &engine, const Common::String &name, int scale) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorscale('%s'): no such actor", name.c_str());
		return;
	}
	const int newScale = MAX(1, scale);
	if (ref.actor->scale != newScale) {
		ref.actor->scale = newScale;
		engine._propRuntime.setDirty(true);
	}
}

void ActorRuntime::actorTurn(const Common::String &name, int turn) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorturn('%s'): no such actor", name.c_str());
		return;
	}
	ref.actor->turn = turn;
}

int ActorRuntime::starXYZ(CyberflixEngine &engine, const Common::String &name, int selector) const {
	if (!engine._setRuntime.set() || !engine._setRuntime.set()->isOpen())
		return 0;

	int16 x = 0, y = 0, z = 0;
	if (!engine._setRuntime.set()->starXYZ(name, x, y, z))
		return 0;

	switch (selector) {
	case 1:
		return x;
	case 2:
		return y;
	case 3:
		return z;
	case 4:
		return engine.makePoint(x, y);
	default:
		return 0;
	}
}


} // End of namespace Cyberflix

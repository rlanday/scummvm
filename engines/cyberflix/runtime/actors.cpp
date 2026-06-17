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
	if (!engine._set || !engine._set->isOpen() ||
			!actor.setName.equalsIgnoreCase(engine._set->setName()))
		return false;

	int16 x = 0, y = 0, z = 0;
	if (!engine._set->starXYZ(actor.sceneName, x, y, z))
		return false;

	if (actor.x != x || actor.y != y || actor.z != z) {
		actor.x = x;
		actor.y = y;
		actor.z = z;
		engine._propsDirty = true;
	}
	return true;
}

void ActorRuntime::refreshActorStarPositions(CyberflixEngine &engine) {
	if (!engine._set || !engine._set->isOpen())
		return;

	for (uint32 c = 0; c < _casts.size(); ++c) {
		for (uint32 i = 0; i < _casts[c]->actorCount(); ++i)
			resolveActorStar(engine, _casts[c]->actor(i));
	}
}

void CyberflixEngine::refreshActorStarPositions() {
	_actorRuntime.refreshActorStarPositions(*this);
}

void CyberflixEngine::openCastFile(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	if (_actorRuntime.findCastShared(key)) {
		debug(1, "Cyberflix: cast '%s' already open", key.c_str());
		return;
	}

	Common::SharedPtr<Cast> cast(new Cast());
	if (!cast->open(key))
		return;
	_actorRuntime.casts().push_back(cast);
	refreshActorStarPositions();
	_propsDirty = true;
}

void CyberflixEngine::closeCastFile(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	Common::Array<Common::SharedPtr<Cast> > &casts = _actorRuntime.casts();
	for (uint32 i = 0; i < casts.size(); ++i) {
		if (casts[i]->name() == key) {
			debug(1, "Cyberflix: cast '%s' closed", key.c_str());
			casts.remove_at(i);
			_propsDirty = true;
			return;
		}
	}
	debug(1, "Cyberflix: closecastfile('%s'): cast not open", key.c_str());
}

void CyberflixEngine::sendToCast(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtocast('%s') -> %s(%u args)", castName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Cast> cast = _actorRuntime.findCastShared(castName);
	if (!cast) {
		warning("Cyberflix: sendtocast('%s'): cast not open", castName.c_str());
		return;
	}
	dispatchWithScopes(cast->castScript(), nullptr, cast->name(), Common::String(),
			message, args, "cast");
}

Value CyberflixEngine::sendToCastFx(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtocastfx('%s') -> %s(%u args)", castName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Cast> cast = _actorRuntime.findCastShared(castName);
	if (!cast) {
		warning("Cyberflix: sendtocastfx('%s'): cast not open", castName.c_str());
		return Value();
	}
	return dispatchWithScopesValue(cast->castScript(), nullptr, cast->name(),
			Common::String(), message, args, "castfx");
}

void CyberflixEngine::sendToActor(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoactor('%s') -> %s(%u args)", actorName.c_str(),
			message.c_str(), args.size());
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(actorName);
	if (!ref.actor) {
		warning("Cyberflix: sendtoactor('%s'): no such actor", actorName.c_str());
		return;
	}

	// Actor dispatch always searches [actor script, cast script, BOOTFILE res2].
	// Avoid the generic scope-chain array in the sampled actor->puppet cascade.
	dispatchWithScopes(ref.actor->script.get(), ref.cast->castScript(),
			ref.actor->name, ref.actor->name, message, args, "actor");
}

Value CyberflixEngine::sendToActorFx(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoactorfx('%s') -> %s(%u args)", actorName.c_str(),
			message.c_str(), args.size());
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(actorName);
	if (!ref.actor) {
		warning("Cyberflix: sendtoactorfx('%s'): no such actor", actorName.c_str());
		return Value();
	}

	return dispatchWithScopesValue(ref.actor->script.get(), ref.cast->castScript(),
			ref.actor->name, ref.actor->name, message, args, "actorfx");
}

int CyberflixEngine::countActors() {
	uint32 count = 0;
	const Common::Array<Common::SharedPtr<Cast> > &casts = _actorRuntime.casts();
	for (uint32 i = 0; i < casts.size(); ++i)
		count += casts[i]->actorCount();
	return (int)count;
}

Common::String CyberflixEngine::indexToActor(int index) {
	if (index < 1)
		return Common::String();
	uint32 remaining = (uint32)index;
	const Common::Array<Common::SharedPtr<Cast> > &casts = _actorRuntime.casts();
	for (uint32 i = 0; i < casts.size(); ++i) {
		if (remaining <= casts[i]->actorCount())
			return casts[i]->actor(remaining - 1).name;
		remaining -= casts[i]->actorCount();
	}
	return Common::String();
}

bool CyberflixEngine::actorVisible(const Common::String &name, const bool *newVisible) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorvisible('%s'): no such actor", name.c_str());
		return false;
	}
	if (newVisible && ref.actor->visible != *newVisible) {
		ref.actor->visible = *newVisible;
		_propsDirty = true;
	}
	return ref.actor->visible;
}

Common::String CyberflixEngine::actorSet(const Common::String &name, const Common::String *newSet) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorset('%s'): no such actor", name.c_str());
		return Common::String();
	}
	if (newSet) {
		Common::String key = *newSet;
		key.toLowercase();
		if (ref.actor->setName != key) {
			ref.actor->setName = key;
			_propsDirty = true;
		}
	}
	return ref.actor->setName;
}

Common::String CyberflixEngine::actorStar(const Common::String &name, const Common::String *newStar) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorstar('%s'): no such actor", name.c_str());
		return Common::String();
	}
	if (newStar) {
		Common::String key = *newStar;
		key.toLowercase();
		if (ref.actor->sceneName != key) {
			ref.actor->sceneName = key;
			_propsDirty = true;
		}
		_actorRuntime.resolveActorStar(*this, *ref.actor);
	}
	return ref.actor->sceneName;
}

Common::String CyberflixEngine::actorPose(const Common::String &name, const Common::String *newPose) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorpose('%s'): no such actor", name.c_str());
		return Common::String();
	}
	if (newPose) {
		Common::String key = *newPose;
		key.toLowercase();
		if (ref.actor->shapeName != key) {
			ref.actor->shapeName = key;
			_propsDirty = true;
		}
	}
	return ref.actor->shapeName;
}

void CyberflixEngine::actorXYZ(const Common::String &name, int x, int y, int z) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorxyz('%s'): no such actor", name.c_str());
		return;
	}
	if (ref.actor->x != (int16)x || ref.actor->y != (int16)y || ref.actor->z != (int16)z) {
		ref.actor->x = (int16)x;
		ref.actor->y = (int16)y;
		ref.actor->z = (int16)z;
		_propsDirty = true;
	}
}

int CyberflixEngine::actorXYZ(const Common::String &name, int selector) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
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
		return makePoint(ref.actor->x, ref.actor->y);
	default:
		return 0;
	}
}

int CyberflixEngine::actorDeg(const Common::String &name, const int *newDeg) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actordeg('%s'): no such actor", name.c_str());
		return 0;
	}
	if (newDeg && ref.actor->angle != (int16)(*newDeg & 0xff)) {
		ref.actor->angle = (int16)(*newDeg & 0xff);
		_propsDirty = true;
	}
	return ref.actor->angle;
}

int CyberflixEngine::actorValue(const Common::String &name, const int *newValue) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorvalue('%s'): no such actor", name.c_str());
		return 0;
	}
	if (newValue)
		ref.actor->value = *newValue;
	return ref.actor->value;
}

Common::String CyberflixEngine::actorOwner(const Common::String &name,
		const Common::String *newOwner) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
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

void CyberflixEngine::actorZClip(const Common::String &name, int zClip) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorzclip('%s'): no such actor", name.c_str());
		return;
	}
	if (ref.actor->zClip != zClip) {
		ref.actor->zClip = zClip;
		_propsDirty = true;
	}
}

void CyberflixEngine::actorSpeed(const Common::String &name, int speed) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorspeed('%s'): no such actor", name.c_str());
		return;
	}
	ref.actor->speed = speed;
}

void CyberflixEngine::actorScale(const Common::String &name, int scale) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorscale('%s'): no such actor", name.c_str());
		return;
	}
	const int newScale = MAX(1, scale);
	if (ref.actor->scale != newScale) {
		ref.actor->scale = newScale;
		_propsDirty = true;
	}
}

void CyberflixEngine::actorTurn(const Common::String &name, int turn) {
	ActorRuntime::ActorRef ref = _actorRuntime.findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorturn('%s'): no such actor", name.c_str());
		return;
	}
	ref.actor->turn = turn;
}

int CyberflixEngine::starXYZ(const Common::String &name, int selector) {
	if (!_set || !_set->isOpen())
		return 0;

	int16 x = 0, y = 0, z = 0;
	if (!_set->starXYZ(name, x, y, z))
		return 0;

	switch (selector) {
	case 1:
		return x;
	case 2:
		return y;
	case 3:
		return z;
	case 4:
		return makePoint(x, y);
	default:
		return 0;
	}
}


} // End of namespace Cyberflix

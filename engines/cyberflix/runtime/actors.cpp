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
#include "cyberflix/runtime/set_helpers.h"
#include "cyberflix/set.h"

#include <cstring>

namespace Cyberflix {

static const char *const kExtraBaseActorNames[] = {
	"bruce1",
	"jim1",
	"jay1",
	"brown1",
	"paul1",
	"ani1",
	"molly1",
	"life1"
};

static bool isExtraBaseActorName(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint i = 0; i < ARRAYSIZE(kExtraBaseActorNames); ++i)
		if (key == kExtraBaseActorNames[i])
			return true;
	return false;
}

static bool parseGeneratedExtraActorName(const Common::String &name, Common::String &source,
		Common::String &where) {
	Common::String key = name;
	key.toLowercase();

	for (uint i = 0; i < ARRAYSIZE(kExtraBaseActorNames); ++i) {
		const char *base = kExtraBaseActorNames[i];
		if (!strcmp(base, "life1"))
			continue;
		const uint len = strlen(base);
		if (key.size() <= len + 1 || strncmp(key.c_str(), base, len))
			continue;

		const char *suffix = key.c_str() + len;
		if (suffix[0] != 'a' && suffix[0] != 'b' && suffix[0] != 'c')
			continue;
		for (const char *p = suffix + 1; *p; ++p)
			if (*p < '0' || *p > '9')
				return false;

		source = base;
		where = Common::String::format("ex.%c.%s", suffix[0], suffix + 1);
		return true;
	}

	return false;
}

bool ActorRuntime::isGeneratedExtraActorName(const Common::String &name) {
	Common::String sourceName;
	Common::String where;
	return parseGeneratedExtraActorName(name, sourceName, where);
}

int ActorRuntime::findWalkRecord(const Common::String &name) const {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _walks.size(); ++i)
		if (_walks[i].actorName == key)
			return static_cast<int>(i);
	return -1;
}

void ActorRuntime::clearWalkRecord(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	if (key == "all") {
		_walks.clear();
		return;
	}
	int index = findWalkRecord(key);
	if (index >= 0)
		_walks.remove_at(static_cast<uint32>(index));
}

void ActorRuntime::dispatchWalkComplete(CyberflixEngine &engine, const Common::String &name) {
	// Native movement service FUN_004250b0 clears the walk slot and dispatches
	// "<actor>, endwalk()" when queued walktostar/walktoxyz/walkonpath motion
	// finishes (also sent by the immediate-resolution path below so authored
	// locks such as B-70 Georgia's lockevents are always released).
	sendToActor(engine, name, "endwalk", Common::Array<Value>());
}

void ActorRuntime::dispatchTurnComplete(CyberflixEngine &engine, const Common::String &name) {
	// FUN_004250b0 dispatches "<actor>, endturn()" when the turning phase of a
	// walk (or a turntodeg record) reaches its target angle.
	sendToActor(engine, name, "endturn", Common::Array<Value>());
}

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

bool ActorRuntime::recoverExtraBaseActor(CyberflixEngine &engine, const Common::String &name) {
	if (!isExtraBaseActorName(name))
		return false;

	if (!findCastShared("extra.cst"))
		openCastFile(engine, "extra.cst");

	if (!findActorRef(name).actor)
		return false;

	debug(1, "Cyberflix: recovering extra base actor '%s' by opening extra.cst",
			name.c_str());
	return true;
}

bool ActorRuntime::recoverGeneratedExtraActor(CyberflixEngine &engine, const Common::String &name) {
	Common::String sourceName;
	Common::String where;
	if (!parseGeneratedExtraActorName(name, sourceName, where))
		return false;

	if (!findCastShared("extra.cst"))
		openCastFile(engine, "extra.cst");

	ActorRef source = findActorRef(sourceName);
	if (!source.actor)
		return false;

	// Heals saves made before actorinstance() existed: replay the same
	// EXTRA.CST setup path that originally creates and places the clone.
	Common::Array<Value> args;
	args.push_back(Value::makeString(where));
	debug(1, "Cyberflix: recovering generated extra actor '%s' via %s.setupactor('%s')",
			name.c_str(), sourceName.c_str(), where.c_str());
	sendToActor(engine, sourceName, "setupactor", args);
	return findActorRef(name).actor;
}

bool ActorRuntime::resolveActorStar(CyberflixEngine &engine, Cast::Actor &actor) {
	if (!engine._setRuntime.set() || !engine._setRuntime.set()->isOpen() ||
			!actor.setName.equalsIgnoreCase(engine._setRuntime.set()->setName()))
		return false;

	int16 x = 0, y = 0, z = 0;
	if (!engine._setRuntime.set()->starXYZ(actor.sceneName, x, y, z))
		return false;

	if (actor.x != x || actor.y != y || actor.z != z || actor.mode != 1) {
		actor.x = x;
		actor.y = y;
		actor.z = z;
		actor.mode = 1;
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
			Cast::ActorProjectionResult projected = _casts[c]->projectWorldActor(actor, camera, setName);
			if (!projected.valid)
				continue;
			draw.push_back(&actor);
			drawCast.push_back(_casts[c].get());
			depths.push_back(projected.depth);
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
			for (uint32 a = 0; a < _casts[i]->actorCount(); ++a) {
				const Common::String actorName = _casts[i]->actor(a).name;
				// Native FUN_004213c0 tears down actor walks and loops before
				// removing the CST actor record.
				stopWalk(actorName);
				engine._loopRuntime.stopLoop("actor", actorName);
			}
			debug(1, "Cyberflix: cast '%s' closed", key.c_str());
			_casts.remove_at(i);
			engine._propRuntime.setDirty(true);
			return;
		}
	}
	debug(1, "Cyberflix: closecastfile('%s'): cast not open", key.c_str());
}

void ActorRuntime::actorInstance(CyberflixEngine &engine, const Common::String &source, const Common::String &newName) {
	ActorRef ref = findActorRef(source);
	if (!ref.actor || !ref.cast) {
		warning("Cyberflix: actorinstance('%s', '%s'): source actor not found",
				source.c_str(), newName.c_str());
		return;
	}
	if (newName.size() > kMaxInstanceNameLength) {
		warning("Cyberflix: actorinstance('%s', '%s'): name too long",
				source.c_str(), newName.c_str());
		return;
	}
	if (findActorRef(newName).actor)
		return;

	if (ref.cast->addActorInstance(*ref.actor, newName)) {
		debug(1, "Cyberflix: actorinstance('%s', '%s')", source.c_str(), newName.c_str());
		refreshActorStarPositions(engine);
		engine._propRuntime.setDirty(true);
	}
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
		if (recoverExtraBaseActor(engine, actorName) || recoverGeneratedExtraActor(engine, actorName))
			ref = findActorRef(actorName);
	}
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
		if (recoverExtraBaseActor(engine, actorName) || recoverGeneratedExtraActor(engine, actorName))
			ref = findActorRef(actorName);
	}
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

bool ActorRuntime::getActorVisible(const Common::String &name) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorvisible('%s'): no such actor", name.c_str());
		return false;
	}
	return ref.actor->visible;
}

bool ActorRuntime::setActorVisible(CyberflixEngine &engine, const Common::String &name, bool visible) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorvisible('%s'): no such actor", name.c_str());
		return false;
	}
	if (ref.actor->visible != visible) {
		ref.actor->visible = visible;
		engine._propRuntime.setDirty(true);
	}
	return ref.actor->visible;
}

Common::String ActorRuntime::getActorSet(const Common::String &name) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorset('%s'): no such actor", name.c_str());
		return Common::String();
	}
	return ref.actor->setName;
}

Common::String ActorRuntime::setActorSet(CyberflixEngine &engine, const Common::String &name, const Common::String &newSet) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorset('%s'): no such actor", name.c_str());
		return Common::String();
	}
	Common::String key = newSet;
	key.toLowercase();
	if (ref.actor->setName != key) {
		ref.actor->setName = key;
		engine._propRuntime.setDirty(true);
	}
	return ref.actor->setName;
}

Common::String ActorRuntime::getActorStar(const Common::String &name) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorstar('%s'): no such actor", name.c_str());
		return Common::String();
	}
	return ref.actor->sceneName;
}

Common::String ActorRuntime::setActorStar(CyberflixEngine &engine, const Common::String &name, const Common::String &newStar) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorstar('%s'): no such actor", name.c_str());
		return Common::String();
	}
	Common::String key = newStar;
	key.toLowercase();
	if (ref.actor->sceneName != key || ref.actor->mode != 1) {
		ref.actor->sceneName = key;
		ref.actor->mode = 1;
		engine._propRuntime.setDirty(true);
	}
	resolveActorStar(engine, *ref.actor);
	return ref.actor->sceneName;
}

Common::String ActorRuntime::getActorPose(const Common::String &name) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorpose('%s'): no such actor", name.c_str());
		return Common::String();
	}
	return ref.actor->shapeName;
}

Common::String ActorRuntime::setActorPose(CyberflixEngine &engine, const Common::String &name, const Common::String &newPose) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorpose('%s'): no such actor", name.c_str());
		return Common::String();
	}
	Common::String key = newPose;
	key.toLowercase();
	if (ref.actor->shapeName != key) {
		ref.actor->shapeName = key;
		ref.actor->poseIndex = 0;
		engine._propRuntime.setDirty(true);
	}
	return ref.actor->shapeName;
}

void ActorRuntime::actorXYZ(CyberflixEngine &engine, const Common::String &name, int x, int y, int z) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorxyz('%s'): no such actor", name.c_str());
		return;
	}
	if (ref.actor->x != static_cast<int16>(x) || ref.actor->y != static_cast<int16>(y) ||
			ref.actor->z != static_cast<int16>(z) || ref.actor->mode != 1) {
		ref.actor->x = static_cast<int16>(x);
		ref.actor->y = static_cast<int16>(y);
		ref.actor->z = static_cast<int16>(z);
		ref.actor->mode = 1;
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

int ActorRuntime::getActorDeg(const Common::String &name) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actordeg('%s'): no such actor", name.c_str());
		return 0;
	}
	return ref.actor->angle;
}

int ActorRuntime::setActorDeg(CyberflixEngine &engine, const Common::String &name, int newDeg) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actordeg('%s'): no such actor", name.c_str());
		return 0;
	}
	if (ref.actor->angle != static_cast<int16>(newDeg & 0xff)) {
		ref.actor->angle = static_cast<int16>(newDeg & 0xff);
		engine._propRuntime.setDirty(true);
	}
	return ref.actor->angle;
}

int ActorRuntime::getActorDist(CyberflixEngine &engine, const Common::String &name) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actordist('%s'): no such actor", name.c_str());
		return 0;
	}

	if (ref.actor->mode == 0)
		return ref.actor->depth;

	CurrentWorldCameraResult camera = currentWorldCamera(engine.setRuntime());
	if (!camera.valid)
		return kOffCameraDepth;

	Cast::ActorProjectionResult projected = ref.cast->projectWorldActor(*ref.actor,
			camera.camera, engine._setRuntime.set()->setName());
	if (!projected.valid)
		return kOffCameraDepth;
	return projected.depth;
}

void ActorRuntime::setActorDist(CyberflixEngine &engine, const Common::String &name, int newDist) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actordist('%s'): no such actor", name.c_str());
		return;
	}

	if (ref.actor->mode == 0 && newDist < 0 && ref.actor->depth != static_cast<int16>(newDist)) {
		ref.actor->depth = static_cast<int16>(newDist);
		engine._propRuntime.setDirty(true);
	}
}

int ActorRuntime::getActorValue(const Common::String &name) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorvalue('%s'): no such actor", name.c_str());
		return 0;
	}
	return ref.actor->value;
}

int ActorRuntime::setActorValue(const Common::String &name, int newValue) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorvalue('%s'): no such actor", name.c_str());
		return 0;
	}
	ref.actor->value = newValue;
	return ref.actor->value;
}

Common::String ActorRuntime::getActorOwner(const Common::String &name) const {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorowner('%s'): no such actor", name.c_str());
		return Common::String();
	}
	return ref.actor->owner;
}

Common::String ActorRuntime::setActorOwner(const Common::String &name, const Common::String &newOwner) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: actorowner('%s'): no such actor", name.c_str());
		return Common::String();
	}
	Common::String key = newOwner;
	key.toLowercase();
	ref.actor->owner = key;
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

void ActorRuntime::turnToDeg(CyberflixEngine &engine, const Common::String &name, int deg) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: turntodeg('%s'): no such actor", name.c_str());
		return;
	}
	const int16 newAngle = static_cast<int16>(deg & 0xff);
	// Native FUN_004242f0 -> FUN_00424a60 queues a turn-only walk record
	// (kind 0) that the movement service rotates to the target angle before
	// dispatching endturn(). Animate in-set actors; off-set ones turn
	// instantly but still get the completion callback.
	clearWalkRecord(name);
	if (engine._setRuntime.set() && engine._setRuntime.set()->isOpen() &&
			ref.actor->setName.equalsIgnoreCase(engine._setRuntime.set()->setName())) {
		WalkRecord w;
		w.actorName = name;
		w.actorName.toLowercase();
		w.destName = ref.actor->sceneName;
		w.turnTarget = newAngle;
		w.turnOnly = true;
		_walks.push_back(w);
		return;
	}
	if (ref.actor->angle != newAngle) {
		ref.actor->angle = newAngle;
		engine._propRuntime.setDirty(true);
	}
	dispatchTurnComplete(engine, name);
}

// Integer sqrt for walk distances, like native FUN_0041b1a0.
static int32 walkDistance(int32 dx, int32 dy, int32 dz) {
	const double sum = static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
			static_cast<double>(dz) * dz;
	return static_cast<int32>(sqrt(sum));
}

// One turning-phase step, mirroring FUN_00426590: rotate `current` toward
// `target` by `step` in the shorter direction around the 256-unit circle,
// without overshooting the target.
static int16 stepAngleToward(int16 current, int16 target, int step) {
	const int cur = current & 0xff;
	const int tgt = target & 0xff;
	const int forward = (tgt - cur) & 0xff;
	if (forward == 0)
		return static_cast<int16>(tgt);
	if (forward <= 128)
		return static_cast<int16>((cur + MIN(step, forward)) & 0xff);
	return static_cast<int16>((cur - MIN(step, 256 - forward)) & 0xff);
}

// Build an animated walk record, mirroring FUN_00424ad0/FUN_00424be0: face the
// destination first, then cover the straight-line distance at the actor's
// speed. While the walk is live the actor leaves its star ("none") so the
// per-frame star snap (resolveActorStar) does not fight the interpolation;
// native does the same by parking a placeholder in the star field.
void ActorRuntime::queueAnimatedWalk(CyberflixEngine &engine, Cast::Actor &actor,
		const Common::String &name, const Common::String &dest,
		int16 destX, int16 destY, int16 destZ) {
	WalkRecord w;
	w.actorName = name;
	w.actorName.toLowercase();
	w.destName = dest;
	w.turnTarget = static_cast<int16>(engine.calcDeg(engine.makePoint(actor.x, actor.y),
			engine.makePoint(destX, destY)) & 0xff);
	w.startX = actor.x;
	w.startY = actor.y;
	w.startZ = actor.z;
	w.dx = actor.x - destX;
	w.dy = actor.y - destY;
	w.dz = actor.z - destZ;
	w.total = MAX<int32>(1, walkDistance(w.dx, w.dy, w.dz));
	actor.sceneName = "none";
	actor.mode = 1;
	_walks.push_back(w);
}

void ActorRuntime::walkToStar(CyberflixEngine &engine, const Common::String &name, const Common::String &star) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: walktostar('%s'): no such actor", name.c_str());
		return;
	}

	clearWalkRecord(name);
	// Native FUN_00424410 queues an animated record (FUN_00424ad0) when the
	// actor stands in the current set, otherwise a deferred record that waits
	// until the player reaches the actor's set. We animate the visible case;
	// for the deferred case resolve immediately instead so scripts polling
	// iswalk()/endwalk() never block on a set the player may not visit.
	int16 sx = 0, sy = 0, sz = 0;
	if (engine._setRuntime.set() && engine._setRuntime.set()->isOpen() &&
			ref.actor->setName.equalsIgnoreCase(engine._setRuntime.set()->setName()) &&
			engine._setRuntime.set()->starXYZ(star, sx, sy, sz)) {
		debug(1, "Cyberflix: walktostar('%s', '%s') queued dist %d", name.c_str(), star.c_str(),
				walkDistance(ref.actor->x - sx, ref.actor->y - sy, ref.actor->z - sz));
		queueAnimatedWalk(engine, *ref.actor, name, star, sx, sy, sz);
		return;
	}
	debug(1, "Cyberflix: walktostar('%s', '%s') resolved immediately (actor off-set)",
			name.c_str(), star.c_str());
	Common::String dest = star;
	setActorStar(engine, name, dest);
	dispatchWalkComplete(engine, name);
}

void ActorRuntime::walkOnPath(CyberflixEngine &engine, const Common::String &name,
		const Common::String &, const Common::String &dest) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: walkonpath('%s'): no such actor", name.c_str());
		return;
	}

	// Native FUN_00424640 walks the star path from the middle argument to the
	// destination (FUN_00424d00 path interpolation). Until path-following
	// exists, walk the straight line to the destination star; off-set actors
	// resolve immediately as in walkToStar.
	clearWalkRecord(name);
	int16 sx = 0, sy = 0, sz = 0;
	if (engine._setRuntime.set() && engine._setRuntime.set()->isOpen() &&
			ref.actor->setName.equalsIgnoreCase(engine._setRuntime.set()->setName()) &&
			engine._setRuntime.set()->starXYZ(dest, sx, sy, sz)) {
		debug(1, "Cyberflix: walkonpath('%s' -> '%s') queued as direct walk", name.c_str(), dest.c_str());
		queueAnimatedWalk(engine, *ref.actor, name, dest, sx, sy, sz);
		return;
	}
	debug(1, "Cyberflix: walkonpath('%s' -> '%s') resolved immediately (actor off-set)",
			name.c_str(), dest.c_str());
	setActorStar(engine, name, dest);
	dispatchWalkComplete(engine, name);
}

void ActorRuntime::walkToXYZ(CyberflixEngine &engine, const Common::String &name, int x, int y, int z) {
	ActorRef ref = findActorRef(name);
	if (!ref.actor) {
		warning("Cyberflix: walktoxyz('%s'): no such actor", name.c_str());
		return;
	}

	// Native FUN_00424540 -> FUN_00424be0 queues coordinate motion; the actor
	// ends the walk off-star ("none"), resting at the raw coordinates.
	clearWalkRecord(name);
	if (engine._setRuntime.set() && engine._setRuntime.set()->isOpen() &&
			ref.actor->setName.equalsIgnoreCase(engine._setRuntime.set()->setName())) {
		debug(1, "Cyberflix: walktoxyz('%s', %d, %d, %d) queued", name.c_str(), x, y, z);
		queueAnimatedWalk(engine, *ref.actor, name, Common::String(),
				static_cast<int16>(x), static_cast<int16>(y), static_cast<int16>(z));
		return;
	}
	debug(1, "Cyberflix: walktoxyz('%s', %d, %d, %d) resolved immediately (actor off-set)",
			name.c_str(), x, y, z);
	actorXYZ(engine, name, x, y, z);
	dispatchWalkComplete(engine, name);
}

void ActorRuntime::advanceActorPoses() {
	for (uint32 i = 0; i < _casts.size(); ++i)
		_casts[i]->advanceActorPoses();
}

void ActorRuntime::advanceWalks(CyberflixEngine &engine) {
	if (_walks.empty())
		return;
	// Freeze walks once the user is quitting. This service runs from
	// forceupdate() outside any script, so the VM's quit unwind cannot stop
	// it; without this check a queued walk would keep moving its actor (and
	// dispatching endturn/endwalk) while the application exits.
	if (engine.shouldQuit())
		return;

	// Advance records first, then dispatch endturn()/endwalk() afterwards:
	// those handlers run scripts that may queue or clear walks, which would
	// otherwise invalidate the iteration (native uses fixed slots).
	struct Completion {
		Common::String name;
		Common::String dest;
		bool walkDone;
	};
	Common::Array<Completion> completions;

	for (uint32 i = 0; i < _walks.size();) {
		WalkRecord &w = _walks[i];
		if (w.pause > 0) {
			++i;
			continue;
		}
		ActorRef ref = findActorRef(w.actorName);
		if (!ref.actor) {
			_walks.remove_at(i);
			continue;
		}

		if (w.turnTarget >= 0) {
			// Turning phase: rotate by the actor's turn rate per pass, then
			// notify. Native leaves a zero turn rate stuck; step at least 1
			// so scripts that forgot stdturn cannot hang the walk.
			const int step = MAX<int32>(1, ref.actor->turn);
			const int16 next = stepAngleToward(ref.actor->angle, w.turnTarget, step);
			if (ref.actor->angle != next) {
				ref.actor->angle = next;
				engine._propRuntime.setDirty(true);
			}
			if (next == w.turnTarget) {
				w.turnTarget = -1;
				Completion c;
				c.name = w.actorName;
				c.walkDone = false;
				completions.push_back(c);
				if (w.turnOnly) {
					_walks.remove_at(i);
					continue;
				}
			}
			++i;
			continue; // native walks only after the turn completes
		}

		// Motion phase: linear interpolation from start toward destination,
		// advancing by the actor's speed each pass (FUN_004250b0 case 1/2).
		w.progress = MIN(w.total, w.progress + MAX<int32>(1, ref.actor->speed));
		const int16 nx = static_cast<int16>(w.startX - (w.dx * w.progress) / w.total);
		const int16 ny = static_cast<int16>(w.startY - (w.dy * w.progress) / w.total);
		const int16 nz = static_cast<int16>(w.startZ - (w.dz * w.progress) / w.total);
		if (ref.actor->x != nx || ref.actor->y != ny || ref.actor->z != nz) {
			ref.actor->x = nx;
			ref.actor->y = ny;
			ref.actor->z = nz;
			engine._propRuntime.setDirty(true);
		}
		if (w.progress >= w.total) {
			Completion c;
			c.name = w.actorName;
			c.dest = w.destName;
			c.walkDone = true;
			completions.push_back(c);
			_walks.remove_at(i);
			continue;
		}
		++i;
	}

	for (uint32 i = 0; i < completions.size(); ++i) {
		if (!completions[i].walkDone) {
			dispatchTurnComplete(engine, completions[i].name);
		} else {
			// Arrival: assume the destination star (snaps any rounding error);
			// xyz walks end off-star at the raw coordinates like native.
			if (!completions[i].dest.empty())
				setActorStar(engine, completions[i].name, completions[i].dest);
			dispatchWalkComplete(engine, completions[i].name);
		}
	}
}

void ActorRuntime::stopWalk(const Common::String &name) {
	clearWalkRecord(name.empty() ? Common::String("all") : name);
}

void ActorRuntime::pauseWalk(const Common::String &name, int flag) {
	// FUN_00425590: bump or drop the pause nesting counter on the matching
	// walk record(s) ("all" or empty name touches every record); the movement
	// service skips records whose counter is above zero.
	const bool all = name.empty() || name.equalsIgnoreCase("all");
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _walks.size(); ++i) {
		if (!all && _walks[i].actorName != key)
			continue;
		if (flag)
			_walks[i].pause++;
		else
			_walks[i].pause = MAX(0, _walks[i].pause - 1);
	}
}

bool ActorRuntime::isWalk(const Common::String &name) const {
	return findWalkRecord(name) >= 0;
}

Common::String ActorRuntime::walkDest(const Common::String &name) const {
	int index = findWalkRecord(name);
	if (index < 0)
		return "None";
	return _walks[static_cast<uint32>(index)].destName;
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

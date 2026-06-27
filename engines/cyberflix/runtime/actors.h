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

#ifndef CYBERFLIX_RUNTIME_ACTORS_H
#define CYBERFLIX_RUNTIME_ACTORS_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

#include "cyberflix/cast.h"
#include "cyberflix/shop.h"

namespace Cyberflix {

class CyberflixEngine;
struct Value;

class ActorRuntime {
public:
	struct ActorRef {
		Common::SharedPtr<Cast> cast;
		Common::SharedPtr<Cast::Actor> actor;
	};

	static bool isGeneratedExtraActorName(const Common::String &name);

	Common::SharedPtr<Cast> findCastShared(const Common::String &name) const;
	ActorRef findActorRef(const Common::String &name) const;
	bool resolveActorStar(CyberflixEngine &engine, Cast::Actor &actor);
	void refreshActorStarPositions(CyberflixEngine &engine);
	void collectWorldActors(CyberflixEngine &engine, Common::Array<const Cast::Actor *> &draw,
			Common::Array<const Cast *> &drawCast, Common::Array<int16> &depths,
			const Shop::WorldCamera &camera) const;
	void openCastFile(CyberflixEngine &engine, const Common::String &name);
	void closeCastFile(CyberflixEngine &engine, const Common::String &name);
	void actorInstance(CyberflixEngine &engine, const Common::String &source, const Common::String &newName);
	void sendToCast(CyberflixEngine &engine, const Common::String &castName,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToCastFx(CyberflixEngine &engine, const Common::String &castName,
			const Common::String &message, const Common::Array<Value> &args);
	void sendToActor(CyberflixEngine &engine, const Common::String &actorName,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToActorFx(CyberflixEngine &engine, const Common::String &actorName,
			const Common::String &message, const Common::Array<Value> &args);
	int countActors() const;
	Common::String indexToActor(int index) const;
	bool getActorVisible(const Common::String &name) const;
	bool setActorVisible(CyberflixEngine &engine, const Common::String &name, bool visible);
	Common::String getActorSet(const Common::String &name) const;
	Common::String setActorSet(CyberflixEngine &engine, const Common::String &name, const Common::String &newSet);
	Common::String getActorStar(const Common::String &name) const;
	Common::String setActorStar(CyberflixEngine &engine, const Common::String &name, const Common::String &newStar);
	Common::String getActorPose(const Common::String &name) const;
	Common::String setActorPose(CyberflixEngine &engine, const Common::String &name, const Common::String &newPose);
	void actorXYZ(CyberflixEngine &engine, const Common::String &name, int x, int y, int z);
	int actorXYZ(CyberflixEngine &engine, const Common::String &name, int selector) const;
	int getActorDeg(const Common::String &name) const;
	int setActorDeg(CyberflixEngine &engine, const Common::String &name, int newDeg);
	int getActorDist(CyberflixEngine &engine, const Common::String &name) const;
	void setActorDist(CyberflixEngine &engine, const Common::String &name, int newDist);
	int getActorValue(const Common::String &name) const;
	int setActorValue(const Common::String &name, int newValue);
	Common::String getActorOwner(const Common::String &name) const;
	Common::String setActorOwner(const Common::String &name, const Common::String &newOwner);
	void actorZClip(CyberflixEngine &engine, const Common::String &name, int zClip);
	void actorSpeed(const Common::String &name, int speed);
	void actorScale(CyberflixEngine &engine, const Common::String &name, int scale);
	void actorTurn(const Common::String &name, int turn);
	void turnToDeg(CyberflixEngine &engine, const Common::String &name, int deg);
	void walkToStar(CyberflixEngine &engine, const Common::String &name, const Common::String &star);
	void walkOnPath(CyberflixEngine &engine, const Common::String &name, const Common::String &path,
			const Common::String &dest);
	void walkToXYZ(CyberflixEngine &engine, const Common::String &name, int x, int y, int z);
	void stopWalk(const Common::String &name);
	bool isWalk(const Common::String &name) const;
	Common::String walkDest(const Common::String &name) const;
	int starXYZ(CyberflixEngine &engine, const Common::String &name, int selector) const;

	Common::Array<Common::SharedPtr<Cast> > &casts() { return _casts; }
	const Common::Array<Common::SharedPtr<Cast> > &casts() const { return _casts; }

private:
	struct WalkRecord {
		Common::String actorName;
		Common::String destName;
	};

	int findWalkRecord(const Common::String &name) const;
	void clearWalkRecord(const Common::String &name);
	void dispatchWalkComplete(CyberflixEngine &engine, const Common::String &name);
	bool recoverExtraBaseActor(CyberflixEngine &engine, const Common::String &name);
	bool recoverGeneratedExtraActor(CyberflixEngine &engine, const Common::String &name);

	/** Open cast files and the global actor list they contribute. */
	Common::Array<Common::SharedPtr<Cast> > _casts;
	Common::Array<WalkRecord> _walks;
};

} // End of namespace Cyberflix

#endif

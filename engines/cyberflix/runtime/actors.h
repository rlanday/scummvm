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
	void pauseWalk(const Common::String &name, int flag);
	bool isWalk(const Common::String &name) const;

	/**
	 * Advance every queued walk/turn by one service pass, mirroring native
	 * FUN_00423a60 -> FUN_004250b0: a turning phase first (endturn() when the
	 * actor faces its target), then linear motion by the actor's speed until
	 * the total distance is covered (destination star + endwalk()).
	 */
	void advanceWalks(CyberflixEngine &engine);

	/** Step every actor's pose frame; run once per compositor pass like
	 *  PropRuntime::advancePropPoses. */
	void advanceActorPoses();
	Common::String walkDest(const Common::String &name) const;
	int starXYZ(CyberflixEngine &engine, const Common::String &name, int selector) const;

	Common::Array<Common::SharedPtr<Cast> > &casts() { return _casts; }
	const Common::Array<Common::SharedPtr<Cast> > &casts() const { return _casts; }

private:
	/**
	 * One queued walk/turn, mirroring a native 16-slot walk record
	 * (DAT_0045f540, built by FUN_00424ad0/FUN_00424be0/FUN_00424a60 and
	 * advanced once per forceupdate()/update() service pass FUN_00423a60 ->
	 * FUN_004250b0).
	 */
	struct WalkRecord {
		Common::String actorName;
		Common::String destName;    ///< Star assumed on arrival ("" for xyz walks).
		int16 turnTarget = -1;      ///< Walk-direction angle ([4]); -1 once turned.
		bool turnOnly = false;      ///< turntodeg record (native kind [2] == 0).
		int16 startX = 0;           ///< Position when the walk was queued ([6]..[8]).
		int16 startY = 0;
		int16 startZ = 0;
		int32 dx = 0;               ///< start - destination ([0xd]/[0xf]/[0x11]).
		int32 dy = 0;
		int32 dz = 0;
		int32 total = 1;            ///< Total walk distance ([0x13]), min 1.
		int32 progress = 0;         ///< Distance covered so far ([0xb]).
		int pause = 0;              ///< pausewalk() nesting counter ([1]).
	};

	int findWalkRecord(const Common::String &name) const;
	void queueAnimatedWalk(CyberflixEngine &engine, Cast::Actor &actor,
			const Common::String &name, const Common::String &dest,
			int16 destX, int16 destY, int16 destZ);
	void dispatchTurnComplete(CyberflixEngine &engine, const Common::String &name);
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

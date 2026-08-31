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

#include "cyberflix/cyberflix.h"

namespace CyberFlix {

void CyberFlixEngine::openCastFile(const Common::String &name) {
	actorRuntime().openCastFile(*this, name);
}

void CyberFlixEngine::closeCastFile(const Common::String &name) {
	actorRuntime().closeCastFile(*this, name);
}

void CyberFlixEngine::actorInstance(const Common::String &source, const Common::String &newName) {
	actorRuntime().actorInstance(*this, source, newName);
}

void CyberFlixEngine::sendToCast(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	actorRuntime().sendToCast(*this, castName, message, args);
}

Value CyberFlixEngine::sendToCastFx(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	return actorRuntime().sendToCastFx(*this, castName, message, args);
}

void CyberFlixEngine::sendToActor(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	actorRuntime().sendToActor(*this, actorName, message, args);
}

Value CyberFlixEngine::sendToActorFx(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	return actorRuntime().sendToActorFx(*this, actorName, message, args);
}

int CyberFlixEngine::countActors() {
	return actorRuntime().countActors();
}

Common::String CyberFlixEngine::indexToActor(int index) {
	return actorRuntime().indexToActor(index);
}

bool CyberFlixEngine::getActorVisible(const Common::String &name) {
	return actorRuntime().getActorVisible(name);
}

bool CyberFlixEngine::setActorVisible(const Common::String &name, bool visible) {
	return actorRuntime().setActorVisible(*this, name, visible);
}

Common::String CyberFlixEngine::getActorSet(const Common::String &name) {
	return actorRuntime().getActorSet(name);
}

Common::String CyberFlixEngine::setActorSet(const Common::String &name, const Common::String &newSet) {
	return actorRuntime().setActorSet(*this, name, newSet);
}

Common::String CyberFlixEngine::getActorStar(const Common::String &name) {
	return actorRuntime().getActorStar(name);
}

Common::String CyberFlixEngine::setActorStar(const Common::String &name, const Common::String &newStar) {
	return actorRuntime().setActorStar(*this, name, newStar);
}

Common::String CyberFlixEngine::getActorPose(const Common::String &name) {
	return actorRuntime().getActorPose(name);
}

Common::String CyberFlixEngine::setActorPose(const Common::String &name, const Common::String &newPose) {
	return actorRuntime().setActorPose(*this, name, newPose);
}

void CyberFlixEngine::actorXYZ(const Common::String &name, int x, int y, int z) {
	actorRuntime().actorXYZ(*this, name, x, y, z);
}

int CyberFlixEngine::actorXYZ(const Common::String &name, int selector) {
	return actorRuntime().actorXYZ(*this, name, selector);
}

int CyberFlixEngine::getActorDeg(const Common::String &name) {
	return actorRuntime().getActorDeg(name);
}

int CyberFlixEngine::setActorDeg(const Common::String &name, int newDeg) {
	return actorRuntime().setActorDeg(*this, name, newDeg);
}

int CyberFlixEngine::getActorDist(const Common::String &name) {
	return actorRuntime().getActorDist(*this, name);
}

void CyberFlixEngine::setActorDist(const Common::String &name, int newDist) {
	actorRuntime().setActorDist(*this, name, newDist);
}

int CyberFlixEngine::getActorValue(const Common::String &name) {
	return actorRuntime().getActorValue(name);
}

int CyberFlixEngine::setActorValue(const Common::String &name, int newValue) {
	return actorRuntime().setActorValue(name, newValue);
}

Common::String CyberFlixEngine::getActorOwner(const Common::String &name) {
	return actorRuntime().getActorOwner(name);
}

Common::String CyberFlixEngine::setActorOwner(const Common::String &name, const Common::String &newOwner) {
	return actorRuntime().setActorOwner(name, newOwner);
}

void CyberFlixEngine::actorZClip(const Common::String &name, int zClip) {
	actorRuntime().actorZClip(*this, name, zClip);
}

void CyberFlixEngine::actorSpeed(const Common::String &name, int speed) {
	actorRuntime().actorSpeed(name, speed);
}

void CyberFlixEngine::actorScale(const Common::String &name, int scale) {
	actorRuntime().actorScale(*this, name, scale);
}

void CyberFlixEngine::actorTurn(const Common::String &name, int turn) {
	actorRuntime().actorTurn(name, turn);
}

void CyberFlixEngine::turnToDeg(const Common::String &name, int deg) {
	actorRuntime().turnToDeg(*this, name, deg);
}

void CyberFlixEngine::walkToStar(const Common::String &name, const Common::String &star) {
	actorRuntime().walkToStar(*this, name, star);
}

void CyberFlixEngine::walkOnPath(const Common::String &name, const Common::String &path,
		const Common::String &dest) {
	actorRuntime().walkOnPath(*this, name, path, dest);
}

void CyberFlixEngine::walkToXYZ(const Common::String &name, int x, int y, int z) {
	actorRuntime().walkToXYZ(*this, name, x, y, z);
}

void CyberFlixEngine::stopWalk(const Common::String &name) {
	actorRuntime().stopWalk(name);
}

void CyberFlixEngine::pauseWalk(const Common::String &name, int flag) {
	actorRuntime().pauseWalk(name, flag);
}

bool CyberFlixEngine::actorExists(const Common::String &name) {
	return actorRuntime().findActorRef(name).actor != nullptr;
}

bool CyberFlixEngine::isWalk(const Common::String &name) {
	return actorRuntime().isWalk(name);
}

Common::String CyberFlixEngine::walkDest(const Common::String &name) {
	return actorRuntime().walkDest(name);
}

int CyberFlixEngine::starXYZ(const Common::String &name, int selector) {
	return actorRuntime().starXYZ(*this, name, selector);
}

} // End of namespace CyberFlix

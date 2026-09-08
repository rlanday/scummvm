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

#include "dreamfactory/dreamfactory.h"

namespace DreamFactory {

void DreamFactoryEngine::openCastFile(const Common::String &name) {
	actorRuntime().openCastFile(*this, name);
}

void DreamFactoryEngine::closeCastFile(const Common::String &name) {
	actorRuntime().closeCastFile(*this, name);
}

void DreamFactoryEngine::actorInstance(const Common::String &source, const Common::String &newName) {
	actorRuntime().actorInstance(*this, source, newName);
}

void DreamFactoryEngine::sendToCast(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	actorRuntime().sendToCast(*this, castName, message, args);
}

Value DreamFactoryEngine::sendToCastFx(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	return actorRuntime().sendToCastFx(*this, castName, message, args);
}

void DreamFactoryEngine::sendToActor(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	actorRuntime().sendToActor(*this, actorName, message, args);
}

Value DreamFactoryEngine::sendToActorFx(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	return actorRuntime().sendToActorFx(*this, actorName, message, args);
}

int DreamFactoryEngine::countActors() {
	return actorRuntime().countActors();
}

Common::String DreamFactoryEngine::indexToActor(int index) {
	return actorRuntime().indexToActor(index);
}

bool DreamFactoryEngine::getActorVisible(const Common::String &name) {
	return actorRuntime().getActorVisible(name);
}

bool DreamFactoryEngine::setActorVisible(const Common::String &name, bool visible) {
	return actorRuntime().setActorVisible(*this, name, visible);
}

Common::String DreamFactoryEngine::getActorSet(const Common::String &name) {
	return actorRuntime().getActorSet(name);
}

Common::String DreamFactoryEngine::setActorSet(const Common::String &name, const Common::String &newSet) {
	return actorRuntime().setActorSet(*this, name, newSet);
}

Common::String DreamFactoryEngine::getActorStar(const Common::String &name) {
	return actorRuntime().getActorStar(name);
}

Common::String DreamFactoryEngine::setActorStar(const Common::String &name, const Common::String &newStar) {
	return actorRuntime().setActorStar(*this, name, newStar);
}

Common::String DreamFactoryEngine::getActorPose(const Common::String &name) {
	return actorRuntime().getActorPose(name);
}

Common::String DreamFactoryEngine::setActorPose(const Common::String &name, const Common::String &newPose) {
	return actorRuntime().setActorPose(*this, name, newPose);
}

void DreamFactoryEngine::actorXYZ(const Common::String &name, int x, int y, int z) {
	actorRuntime().actorXYZ(*this, name, x, y, z);
}

int DreamFactoryEngine::actorXYZ(const Common::String &name, int selector) {
	return actorRuntime().actorXYZ(*this, name, selector);
}

int DreamFactoryEngine::getActorDeg(const Common::String &name) {
	return actorRuntime().getActorDeg(name);
}

int DreamFactoryEngine::setActorDeg(const Common::String &name, int newDeg) {
	return actorRuntime().setActorDeg(*this, name, newDeg);
}

int DreamFactoryEngine::getActorDist(const Common::String &name) {
	return actorRuntime().getActorDist(*this, name);
}

void DreamFactoryEngine::setActorDist(const Common::String &name, int newDist) {
	actorRuntime().setActorDist(*this, name, newDist);
}

int DreamFactoryEngine::getActorValue(const Common::String &name) {
	return actorRuntime().getActorValue(name);
}

int DreamFactoryEngine::setActorValue(const Common::String &name, int newValue) {
	return actorRuntime().setActorValue(name, newValue);
}

Common::String DreamFactoryEngine::getActorOwner(const Common::String &name) {
	return actorRuntime().getActorOwner(name);
}

Common::String DreamFactoryEngine::setActorOwner(const Common::String &name, const Common::String &newOwner) {
	return actorRuntime().setActorOwner(name, newOwner);
}

void DreamFactoryEngine::actorZClip(const Common::String &name, int zClip) {
	actorRuntime().actorZClip(*this, name, zClip);
}

void DreamFactoryEngine::actorSpeed(const Common::String &name, int speed) {
	actorRuntime().actorSpeed(name, speed);
}

void DreamFactoryEngine::actorScale(const Common::String &name, int scale) {
	actorRuntime().actorScale(*this, name, scale);
}

void DreamFactoryEngine::actorTurn(const Common::String &name, int turn) {
	actorRuntime().actorTurn(name, turn);
}

void DreamFactoryEngine::turnToDeg(const Common::String &name, int deg) {
	actorRuntime().turnToDeg(*this, name, deg);
}

void DreamFactoryEngine::walkToStar(const Common::String &name, const Common::String &star) {
	actorRuntime().walkToStar(*this, name, star);
}

void DreamFactoryEngine::walkOnPath(const Common::String &name, const Common::String &path,
		const Common::String &dest) {
	actorRuntime().walkOnPath(*this, name, path, dest);
}

void DreamFactoryEngine::walkToXYZ(const Common::String &name, int x, int y, int z) {
	actorRuntime().walkToXYZ(*this, name, x, y, z);
}

void DreamFactoryEngine::stopWalk(const Common::String &name) {
	actorRuntime().stopWalk(name);
}

void DreamFactoryEngine::pauseWalk(const Common::String &name, int flag) {
	actorRuntime().pauseWalk(name, flag);
}

bool DreamFactoryEngine::actorExists(const Common::String &name) {
	return actorRuntime().findActorRef(name).actor != nullptr;
}

bool DreamFactoryEngine::isWalk(const Common::String &name) {
	return actorRuntime().isWalk(name);
}

Common::String DreamFactoryEngine::walkDest(const Common::String &name) {
	return actorRuntime().walkDest(name);
}

int DreamFactoryEngine::starXYZ(const Common::String &name, int selector) {
	return actorRuntime().starXYZ(*this, name, selector);
}

} // End of namespace DreamFactory

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

namespace Cyberflix {

void CyberflixActorVMHost::openCastFile(const Common::String &name) {
	engine().actorRuntime().openCastFile(engine(), name);
}

void CyberflixActorVMHost::closeCastFile(const Common::String &name) {
	engine().actorRuntime().closeCastFile(engine(), name);
}

void CyberflixActorVMHost::actorInstance(const Common::String &source, const Common::String &newName) {
	engine().actorRuntime().actorInstance(engine(), source, newName);
}

void CyberflixActorVMHost::sendToCast(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	engine().actorRuntime().sendToCast(engine(), castName, message, args);
}

Value CyberflixActorVMHost::sendToCastFx(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	return engine().actorRuntime().sendToCastFx(engine(), castName, message, args);
}

void CyberflixActorVMHost::sendToActor(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	engine().actorRuntime().sendToActor(engine(), actorName, message, args);
}

Value CyberflixActorVMHost::sendToActorFx(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	return engine().actorRuntime().sendToActorFx(engine(), actorName, message, args);
}

int CyberflixActorVMHost::countActors() {
	return engine().actorRuntime().countActors();
}

Common::String CyberflixActorVMHost::indexToActor(int index) {
	return engine().actorRuntime().indexToActor(index);
}

bool CyberflixActorVMHost::getActorVisible(const Common::String &name) {
	return engine().actorRuntime().getActorVisible(name);
}

bool CyberflixActorVMHost::setActorVisible(const Common::String &name, bool visible) {
	return engine().actorRuntime().setActorVisible(engine(), name, visible);
}

Common::String CyberflixActorVMHost::getActorSet(const Common::String &name) {
	return engine().actorRuntime().getActorSet(name);
}

Common::String CyberflixActorVMHost::setActorSet(const Common::String &name, const Common::String &newSet) {
	return engine().actorRuntime().setActorSet(engine(), name, newSet);
}

Common::String CyberflixActorVMHost::getActorStar(const Common::String &name) {
	return engine().actorRuntime().getActorStar(name);
}

Common::String CyberflixActorVMHost::setActorStar(const Common::String &name, const Common::String &newStar) {
	return engine().actorRuntime().setActorStar(engine(), name, newStar);
}

Common::String CyberflixActorVMHost::getActorPose(const Common::String &name) {
	return engine().actorRuntime().getActorPose(name);
}

Common::String CyberflixActorVMHost::setActorPose(const Common::String &name, const Common::String &newPose) {
	return engine().actorRuntime().setActorPose(engine(), name, newPose);
}

void CyberflixActorVMHost::actorXYZ(const Common::String &name, int x, int y, int z) {
	engine().actorRuntime().actorXYZ(engine(), name, x, y, z);
}

int CyberflixActorVMHost::actorXYZ(const Common::String &name, int selector) {
	return engine().actorRuntime().actorXYZ(engine(), name, selector);
}

int CyberflixActorVMHost::getActorDeg(const Common::String &name) {
	return engine().actorRuntime().getActorDeg(name);
}

int CyberflixActorVMHost::setActorDeg(const Common::String &name, int newDeg) {
	return engine().actorRuntime().setActorDeg(engine(), name, newDeg);
}

int CyberflixActorVMHost::getActorDist(const Common::String &name) {
	return engine().actorRuntime().getActorDist(engine(), name);
}

void CyberflixActorVMHost::setActorDist(const Common::String &name, int newDist) {
	engine().actorRuntime().setActorDist(engine(), name, newDist);
}

int CyberflixActorVMHost::getActorValue(const Common::String &name) {
	return engine().actorRuntime().getActorValue(name);
}

int CyberflixActorVMHost::setActorValue(const Common::String &name, int newValue) {
	return engine().actorRuntime().setActorValue(name, newValue);
}

Common::String CyberflixActorVMHost::getActorOwner(const Common::String &name) {
	return engine().actorRuntime().getActorOwner(name);
}

Common::String CyberflixActorVMHost::setActorOwner(const Common::String &name, const Common::String &newOwner) {
	return engine().actorRuntime().setActorOwner(name, newOwner);
}

void CyberflixActorVMHost::actorZClip(const Common::String &name, int zClip) {
	engine().actorRuntime().actorZClip(engine(), name, zClip);
}

void CyberflixActorVMHost::actorSpeed(const Common::String &name, int speed) {
	engine().actorRuntime().actorSpeed(name, speed);
}

void CyberflixActorVMHost::actorScale(const Common::String &name, int scale) {
	engine().actorRuntime().actorScale(engine(), name, scale);
}

void CyberflixActorVMHost::actorTurn(const Common::String &name, int turn) {
	engine().actorRuntime().actorTurn(name, turn);
}

void CyberflixActorVMHost::turnToDeg(const Common::String &name, int deg) {
	engine().actorRuntime().turnToDeg(engine(), name, deg);
}

void CyberflixActorVMHost::walkToStar(const Common::String &name, const Common::String &star) {
	engine().actorRuntime().walkToStar(engine(), name, star);
}

void CyberflixActorVMHost::stopWalk(const Common::String &name) {
	engine().actorRuntime().stopWalk(name);
}

bool CyberflixActorVMHost::isWalk(const Common::String &name) {
	return engine().actorRuntime().isWalk(name);
}

Common::String CyberflixActorVMHost::walkDest(const Common::String &name) {
	return engine().actorRuntime().walkDest(name);
}

int CyberflixActorVMHost::starXYZ(const Common::String &name, int selector) {
	return engine().actorRuntime().starXYZ(engine(), name, selector);
}

} // End of namespace Cyberflix

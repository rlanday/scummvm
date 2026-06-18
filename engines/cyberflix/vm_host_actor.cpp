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

bool CyberflixActorVMHost::actorVisible(const Common::String &name, const bool *newVisible) {
	return engine().actorRuntime().actorVisible(engine(), name, newVisible);
}

Common::String CyberflixActorVMHost::actorSet(const Common::String &name, const Common::String *newSet) {
	return engine().actorRuntime().actorSet(engine(), name, newSet);
}

Common::String CyberflixActorVMHost::actorStar(const Common::String &name, const Common::String *newStar) {
	return engine().actorRuntime().actorStar(engine(), name, newStar);
}

Common::String CyberflixActorVMHost::actorPose(const Common::String &name, const Common::String *newPose) {
	return engine().actorRuntime().actorPose(engine(), name, newPose);
}

void CyberflixActorVMHost::actorXYZ(const Common::String &name, int x, int y, int z) {
	engine().actorRuntime().actorXYZ(engine(), name, x, y, z);
}

int CyberflixActorVMHost::actorXYZ(const Common::String &name, int selector) {
	return engine().actorRuntime().actorXYZ(engine(), name, selector);
}

int CyberflixActorVMHost::actorDeg(const Common::String &name, const int *newDeg) {
	return engine().actorRuntime().actorDeg(engine(), name, newDeg);
}

int CyberflixActorVMHost::actorValue(const Common::String &name, const int *newValue) {
	return engine().actorRuntime().actorValue(name, newValue);
}

Common::String CyberflixActorVMHost::actorOwner(const Common::String &name,
		const Common::String *newOwner) {
	return engine().actorRuntime().actorOwner(name, newOwner);
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

int CyberflixActorVMHost::starXYZ(const Common::String &name, int selector) {
	return engine().actorRuntime().starXYZ(engine(), name, selector);
}

} // End of namespace Cyberflix

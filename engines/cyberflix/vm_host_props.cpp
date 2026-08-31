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

void CyberFlixEngine::openShopFile(const Common::String &name) {
	propRuntime().openShopFile(*this, name);
}

void CyberFlixEngine::closeShopFile(const Common::String &name) {
	propRuntime().closeShopFile(*this, name);
}

void CyberFlixEngine::propInstance(const Common::String &source, const Common::String &newName) {
	propRuntime().propInstance(source, newName);
}

void CyberFlixEngine::sendToShop(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	propRuntime().sendToShop(*this, shopName, message, args);
}

Value CyberFlixEngine::sendToShopFx(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	return propRuntime().sendToShopFx(*this, shopName, message, args);
}

void CyberFlixEngine::sendToProp(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	propRuntime().sendToProp(*this, propName, message, args);
}

Value CyberFlixEngine::sendToPropFx(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	return propRuntime().sendToPropFx(*this, propName, message, args);
}

bool CyberFlixEngine::propExists(const Common::String &name) {
	return propRuntime().findProp(name) != nullptr;
}

int CyberFlixEngine::propXYZ(const Common::String &name, int selector) {
	return propRuntime().propXYZ(*this, name, selector);
}

bool CyberFlixEngine::propVisible(const Common::String &name) {
	return propRuntime().propVisible(name);
}

void CyberFlixEngine::propVisible(const Common::String &name, bool visible) {
	propRuntime().propVisible(name, visible);
}

Common::String CyberFlixEngine::propView(const Common::String &name) {
	return propRuntime().propView(name);
}

void CyberFlixEngine::propView(const Common::String &name, const Common::String &shape) {
	propRuntime().propView(name, shape);
}

int CyberFlixEngine::propXY(const Common::String &name, int selector) {
	return propRuntime().propXY(name, selector);
}

void CyberFlixEngine::setPropXY(const Common::String &name, int x, int y) {
	propRuntime().setPropXY(name, x, y);
}

void CyberFlixEngine::propSet(const Common::String &name, const Common::String &setName) {
	propRuntime().propSet(*this, name, setName);
}

void CyberFlixEngine::propXYZ(const Common::String &name, int x, int y, int z) {
	propRuntime().propXYZ(name, x, y, z);
}

Common::String CyberFlixEngine::getPropStar(const Common::String &name) {
	return propRuntime().getPropStar(name);
}

Common::String CyberFlixEngine::setPropStar(const Common::String &name, const Common::String &newStar) {
	return propRuntime().setPropStar(*this, name, newStar);
}

void CyberFlixEngine::propScale(const Common::String &name, int scale) {
	propRuntime().propScale(name, scale);
}

void CyberFlixEngine::propZClip(const Common::String &name, int dist) {
	propRuntime().propZClip(name, dist);
}

int CyberFlixEngine::getPropDist(const Common::String &name) {
	return propRuntime().getPropDist(*this, name);
}

void CyberFlixEngine::propDist(const Common::String &name, int dist) {
	propRuntime().propDist(name, dist);
}

int CyberFlixEngine::getPropDeg(const Common::String &name) {
	return propRuntime().getPropDeg(name);
}

int CyberFlixEngine::setPropDeg(const Common::String &name, int newDeg) {
	return propRuntime().setPropDeg(name, newDeg);
}

Common::String CyberFlixEngine::getPropOwner(const Common::String &name) {
	return propRuntime().getPropOwner(name);
}

Common::String CyberFlixEngine::setPropOwner(const Common::String &name, const Common::String &newOwner) {
	return propRuntime().setPropOwner(name, newOwner);
}

int CyberFlixEngine::getPropValue(const Common::String &name) {
	return propRuntime().getPropValue(name);
}

int CyberFlixEngine::setPropValue(const Common::String &name, int newValue) {
	return propRuntime().setPropValue(name, newValue);
}

int CyberFlixEngine::countProps() {
	return propRuntime().countProps();
}

Common::String CyberFlixEngine::indexToProp(int index) {
	return propRuntime().indexToProp(index);
}

bool CyberFlixEngine::pointInProp(const Common::String &name, int32 packedPoint) {
	return propRuntime().pointInProp(name, packedPoint);
}

} // End of namespace CyberFlix

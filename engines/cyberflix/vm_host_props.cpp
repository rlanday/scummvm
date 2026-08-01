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

void CyberflixEngine::openShopFile(const Common::String &name) {
	propRuntime().openShopFile(*this, name);
}

void CyberflixEngine::closeShopFile(const Common::String &name) {
	propRuntime().closeShopFile(*this, name);
}

void CyberflixEngine::propInstance(const Common::String &source, const Common::String &newName) {
	propRuntime().propInstance(source, newName);
}

void CyberflixEngine::sendToShop(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	propRuntime().sendToShop(*this, shopName, message, args);
}

Value CyberflixEngine::sendToShopFx(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	return propRuntime().sendToShopFx(*this, shopName, message, args);
}

void CyberflixEngine::sendToProp(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	propRuntime().sendToProp(*this, propName, message, args);
}

Value CyberflixEngine::sendToPropFx(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	return propRuntime().sendToPropFx(*this, propName, message, args);
}

bool CyberflixEngine::propExists(const Common::String &name) {
	return propRuntime().findProp(name) != nullptr;
}

int CyberflixEngine::propXYZ(const Common::String &name, int selector) {
	return propRuntime().propXYZ(*this, name, selector);
}

bool CyberflixEngine::propVisible(const Common::String &name) {
	return propRuntime().propVisible(name);
}

void CyberflixEngine::propVisible(const Common::String &name, bool visible) {
	propRuntime().propVisible(name, visible);
}

Common::String CyberflixEngine::propView(const Common::String &name) {
	return propRuntime().propView(name);
}

void CyberflixEngine::propView(const Common::String &name, const Common::String &shape) {
	propRuntime().propView(name, shape);
}

int CyberflixEngine::propXY(const Common::String &name, int selector) {
	return propRuntime().propXY(name, selector);
}

void CyberflixEngine::setPropXY(const Common::String &name, int x, int y) {
	propRuntime().setPropXY(name, x, y);
}

void CyberflixEngine::propSet(const Common::String &name, const Common::String &setName) {
	propRuntime().propSet(*this, name, setName);
}

void CyberflixEngine::propXYZ(const Common::String &name, int x, int y, int z) {
	propRuntime().propXYZ(name, x, y, z);
}

Common::String CyberflixEngine::getPropStar(const Common::String &name) {
	return propRuntime().getPropStar(name);
}

Common::String CyberflixEngine::setPropStar(const Common::String &name, const Common::String &newStar) {
	return propRuntime().setPropStar(*this, name, newStar);
}

void CyberflixEngine::propScale(const Common::String &name, int scale) {
	propRuntime().propScale(name, scale);
}

void CyberflixEngine::propZClip(const Common::String &name, int dist) {
	propRuntime().propZClip(name, dist);
}

int CyberflixEngine::getPropDist(const Common::String &name) {
	return propRuntime().getPropDist(*this, name);
}

void CyberflixEngine::propDist(const Common::String &name, int dist) {
	propRuntime().propDist(name, dist);
}

int CyberflixEngine::getPropDeg(const Common::String &name) {
	return propRuntime().getPropDeg(name);
}

int CyberflixEngine::setPropDeg(const Common::String &name, int newDeg) {
	return propRuntime().setPropDeg(name, newDeg);
}

Common::String CyberflixEngine::getPropOwner(const Common::String &name) {
	return propRuntime().getPropOwner(name);
}

Common::String CyberflixEngine::setPropOwner(const Common::String &name, const Common::String &newOwner) {
	return propRuntime().setPropOwner(name, newOwner);
}

int CyberflixEngine::getPropValue(const Common::String &name) {
	return propRuntime().getPropValue(name);
}

int CyberflixEngine::setPropValue(const Common::String &name, int newValue) {
	return propRuntime().setPropValue(name, newValue);
}

int CyberflixEngine::countProps() {
	return propRuntime().countProps();
}

Common::String CyberflixEngine::indexToProp(int index) {
	return propRuntime().indexToProp(index);
}

bool CyberflixEngine::pointInProp(const Common::String &name, int32 packedPoint) {
	return propRuntime().pointInProp(name, packedPoint);
}

} // End of namespace Cyberflix

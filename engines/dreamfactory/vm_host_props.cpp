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

void DreamFactoryEngine::openShopFile(const Common::String &name) {
	propRuntime().openShopFile(*this, name);
}

void DreamFactoryEngine::closeShopFile(const Common::String &name) {
	propRuntime().closeShopFile(*this, name);
}

void DreamFactoryEngine::propInstance(const Common::String &source, const Common::String &newName) {
	propRuntime().propInstance(source, newName);
}

void DreamFactoryEngine::sendToShop(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	propRuntime().sendToShop(*this, shopName, message, args);
}

Value DreamFactoryEngine::sendToShopFx(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	return propRuntime().sendToShopFx(*this, shopName, message, args);
}

void DreamFactoryEngine::sendToProp(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	propRuntime().sendToProp(*this, propName, message, args);
}

Value DreamFactoryEngine::sendToPropFx(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	return propRuntime().sendToPropFx(*this, propName, message, args);
}

bool DreamFactoryEngine::propExists(const Common::String &name) {
	return propRuntime().findProp(name) != nullptr;
}

int DreamFactoryEngine::propXYZ(const Common::String &name, int selector) {
	return propRuntime().propXYZ(*this, name, selector);
}

bool DreamFactoryEngine::propVisible(const Common::String &name) {
	return propRuntime().propVisible(name);
}

void DreamFactoryEngine::propVisible(const Common::String &name, bool visible) {
	propRuntime().propVisible(name, visible);
}

Common::String DreamFactoryEngine::propView(const Common::String &name) {
	return propRuntime().propView(name);
}

void DreamFactoryEngine::propView(const Common::String &name, const Common::String &shape) {
	propRuntime().propView(name, shape);
}

int DreamFactoryEngine::propXY(const Common::String &name, int selector) {
	return propRuntime().propXY(name, selector);
}

void DreamFactoryEngine::setPropXY(const Common::String &name, int x, int y) {
	propRuntime().setPropXY(name, x, y);
}

void DreamFactoryEngine::propSet(const Common::String &name, const Common::String &setName) {
	propRuntime().propSet(*this, name, setName);
}

void DreamFactoryEngine::propXYZ(const Common::String &name, int x, int y, int z) {
	propRuntime().propXYZ(name, x, y, z);
}

Common::String DreamFactoryEngine::getPropStar(const Common::String &name) {
	return propRuntime().getPropStar(name);
}

Common::String DreamFactoryEngine::setPropStar(const Common::String &name, const Common::String &newStar) {
	return propRuntime().setPropStar(*this, name, newStar);
}

void DreamFactoryEngine::propScale(const Common::String &name, int scale) {
	propRuntime().propScale(name, scale);
}

void DreamFactoryEngine::propZClip(const Common::String &name, int dist) {
	propRuntime().propZClip(name, dist);
}

int DreamFactoryEngine::getPropDist(const Common::String &name) {
	return propRuntime().getPropDist(*this, name);
}

void DreamFactoryEngine::propDist(const Common::String &name, int dist) {
	propRuntime().propDist(name, dist);
}

int DreamFactoryEngine::getPropDeg(const Common::String &name) {
	return propRuntime().getPropDeg(name);
}

int DreamFactoryEngine::setPropDeg(const Common::String &name, int newDeg) {
	return propRuntime().setPropDeg(name, newDeg);
}

Common::String DreamFactoryEngine::getPropOwner(const Common::String &name) {
	return propRuntime().getPropOwner(name);
}

Common::String DreamFactoryEngine::setPropOwner(const Common::String &name, const Common::String &newOwner) {
	return propRuntime().setPropOwner(name, newOwner);
}

int DreamFactoryEngine::getPropValue(const Common::String &name) {
	return propRuntime().getPropValue(name);
}

int DreamFactoryEngine::setPropValue(const Common::String &name, int newValue) {
	return propRuntime().setPropValue(name, newValue);
}

int DreamFactoryEngine::countProps() {
	return propRuntime().countProps();
}

Common::String DreamFactoryEngine::indexToProp(int index) {
	return propRuntime().indexToProp(index);
}

bool DreamFactoryEngine::pointInProp(const Common::String &name, int32 packedPoint) {
	return propRuntime().pointInProp(name, packedPoint);
}

} // End of namespace DreamFactory

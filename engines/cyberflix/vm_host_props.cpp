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

void CyberflixPropVMHost::openShopFile(const Common::String &name) {
	engine().propRuntime().openShopFile(engine(), name);
}

void CyberflixPropVMHost::closeShopFile(const Common::String &name) {
	engine().propRuntime().closeShopFile(engine(), name);
}

void CyberflixPropVMHost::propInstance(const Common::String &source, const Common::String &newName) {
	engine().propRuntime().propInstance(source, newName);
}

void CyberflixPropVMHost::sendToShop(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	engine().propRuntime().sendToShop(engine(), shopName, message, args);
}

Value CyberflixPropVMHost::sendToShopFx(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	return engine().propRuntime().sendToShopFx(engine(), shopName, message, args);
}

void CyberflixPropVMHost::sendToProp(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	engine().propRuntime().sendToProp(engine(), propName, message, args);
}

Value CyberflixPropVMHost::sendToPropFx(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	return engine().propRuntime().sendToPropFx(engine(), propName, message, args);
}

bool CyberflixPropVMHost::propVisible(const Common::String &name) {
	return engine().propRuntime().propVisible(name);
}

void CyberflixPropVMHost::propVisible(const Common::String &name, bool visible) {
	engine().propRuntime().propVisible(name, visible);
}

Common::String CyberflixPropVMHost::propView(const Common::String &name) {
	return engine().propRuntime().propView(name);
}

void CyberflixPropVMHost::propView(const Common::String &name, const Common::String &shape) {
	engine().propRuntime().propView(name, shape);
}

int CyberflixPropVMHost::propXY(const Common::String &name, int selector) {
	return engine().propRuntime().propXY(name, selector);
}

void CyberflixPropVMHost::setPropXY(const Common::String &name, int x, int y) {
	engine().propRuntime().setPropXY(name, x, y);
}

void CyberflixPropVMHost::propSet(const Common::String &name, const Common::String &setName) {
	engine().propRuntime().propSet(name, setName);
}

void CyberflixPropVMHost::propXYZ(const Common::String &name, int x, int y, int z) {
	engine().propRuntime().propXYZ(name, x, y, z);
}

void CyberflixPropVMHost::propScale(const Common::String &name, int scale) {
	engine().propRuntime().propScale(name, scale);
}

void CyberflixPropVMHost::propZClip(const Common::String &name, int dist) {
	engine().propRuntime().propZClip(name, dist);
}

void CyberflixPropVMHost::propDist(const Common::String &name, int dist) {
	engine().propRuntime().propDist(name, dist);
}

int CyberflixPropVMHost::propDeg(const Common::String &name, const int *newDeg) {
	return engine().propRuntime().propDeg(name, newDeg);
}

Common::String CyberflixPropVMHost::propOwner(const Common::String &name, const Common::String *newOwner) {
	return engine().propRuntime().propOwner(name, newOwner);
}

int CyberflixPropVMHost::propValue(const Common::String &name, const int *newValue) {
	return engine().propRuntime().propValue(name, newValue);
}

int CyberflixPropVMHost::countProps() {
	return engine().propRuntime().countProps();
}

Common::String CyberflixPropVMHost::indexToProp(int index) {
	return engine().propRuntime().indexToProp(index);
}

} // End of namespace Cyberflix

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

Common::String CyberflixPuppetVMHost::currentPuppet() {
	return engine().puppetRuntime().currentPuppet();
}

void CyberflixPuppetVMHost::openPuppetFile(const Common::String &name) {
	engine().puppetRuntime().openPuppetFile(name);
}

void CyberflixPuppetVMHost::closePuppetFile() {
	engine().puppetRuntime().closePuppetFile(engine());
}

void CyberflixPuppetVMHost::sendToPuppet(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	engine().puppetRuntime().sendToPuppet(engine(), puppetName, message, args);
}

Value CyberflixPuppetVMHost::sendToPuppetFx(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	return engine().puppetRuntime().sendToPuppetFx(engine(), puppetName, message, args);
}

void CyberflixPuppetVMHost::puppetScript(const Common::String &name) {
	engine().puppetRuntime().puppetScript(name);
}

void CyberflixPuppetVMHost::puppetClear() {
	engine().puppetRuntime().puppetClear(engine());
}

void CyberflixPuppetVMHost::puppetSpeak(const Common::String &name, int mode) {
	engine().puppetRuntime().puppetSpeak(engine(), name, mode);
}

void CyberflixPuppetVMHost::puppetBevel(const Common::String &name, int mode) {
	engine().puppetRuntime().puppetBevel(engine(), name, mode);
}

void CyberflixPuppetVMHost::puppetGrab(bool enabled) {
	engine().puppetRuntime().puppetGrab(enabled);
}

int CyberflixPuppetVMHost::puppetEvent(int timeout) {
	return engine().puppetRuntime().puppetEvent(engine(), timeout);
}

Common::String CyberflixPuppetVMHost::puppetBase(const Common::String *newBase) {
	return engine().puppetRuntime().puppetBase(newBase);
}

bool CyberflixPuppetVMHost::puppetVisible(const bool *newVisible) {
	return engine().puppetRuntime().puppetVisible(engine(), newVisible);
}

int CyberflixPuppetVMHost::puppetParam(int selector, const int *newValue) {
	return engine().puppetRuntime().puppetParam(selector, newValue);
}

int CyberflixPuppetVMHost::countPuppets() {
	return engine().puppetRuntime().countPuppets();
}

Common::String CyberflixPuppetVMHost::indexToPuppet(int index) {
	return engine().puppetRuntime().indexToPuppet(index);
}

} // End of namespace Cyberflix

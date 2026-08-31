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

Common::String CyberFlixEngine::currentPuppet() {
	return puppetRuntime().currentPuppet();
}

void CyberFlixEngine::openPuppetFile(const Common::String &name) {
	puppetRuntime().openPuppetFile(name);
}

void CyberFlixEngine::closePuppetFile() {
	puppetRuntime().closePuppetFile(*this);
}

void CyberFlixEngine::sendToPuppet(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	puppetRuntime().sendToPuppet(*this, puppetName, message, args);
}

Value CyberFlixEngine::sendToPuppetFx(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	return puppetRuntime().sendToPuppetFx(*this, puppetName, message, args);
}

void CyberFlixEngine::puppetScript(const Common::String &name) {
	puppetRuntime().puppetScript(name);
}

void CyberFlixEngine::puppetClear() {
	puppetRuntime().puppetClear(*this);
}

void CyberFlixEngine::puppetSpeak(const Common::String &name, int mode) {
	puppetRuntime().puppetSpeak(*this, name, mode);
}

void CyberFlixEngine::puppetBevel(const Common::String &name, int mode) {
	puppetRuntime().puppetBevel(*this, name, mode);
}

void CyberFlixEngine::puppetGrab(bool enabled) {
	puppetRuntime().puppetGrab(enabled);
}

int CyberFlixEngine::puppetEvent(int timeout) {
	return puppetRuntime().puppetEvent(*this, timeout);
}

Common::String CyberFlixEngine::getPuppetBase() {
	return puppetRuntime().getPuppetBase();
}

Common::String CyberFlixEngine::setPuppetBase(const Common::String &newBase) {
	return puppetRuntime().setPuppetBase(newBase);
}

bool CyberFlixEngine::getPuppetVisible() {
	return puppetRuntime().getPuppetVisible();
}

bool CyberFlixEngine::setPuppetVisible(bool visible) {
	return puppetRuntime().setPuppetVisible(*this, visible);
}

int CyberFlixEngine::getPuppetParam(int selector) {
	return puppetRuntime().getPuppetParam(selector);
}

int CyberFlixEngine::setPuppetParam(int selector, int newValue) {
	return puppetRuntime().setPuppetParam(selector, newValue);
}

int CyberFlixEngine::countPuppets() {
	return puppetRuntime().countPuppets();
}

Common::String CyberFlixEngine::indexToPuppet(int index) {
	return puppetRuntime().indexToPuppet(index);
}

} // End of namespace CyberFlix

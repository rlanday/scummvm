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

void CyberFlixEngine::openTrackFile(const Common::String &name) {
	audioRuntime().openTrackFile(name);
}

void CyberFlixEngine::closeTrackFile(const Common::String &name) {
	audioRuntime().closeTrackFile(name);
}

void CyberFlixEngine::playTheme(const Common::String &name) {
	audioRuntime().playTheme(*this, name);
}

void CyberFlixEngine::haltTheme() {
	audioRuntime().haltTheme(*this);
}

void CyberFlixEngine::playSound(const Common::String &name, int mode) {
	audioRuntime().playSound(*this, name, mode);
}

void CyberFlixEngine::playVoice(const Common::String &name) {
	audioRuntime().playVoice(*this, name);
}

void CyberFlixEngine::haltSound(int which) {
	audioRuntime().haltSound(*this, which);
}

void CyberFlixEngine::haltVoice() {
	audioRuntime().haltVoice(*this);
}

void CyberFlixEngine::themeVolume(const Common::String &name, int volume) {
	audioRuntime().themeVolume(*this, name, volume);
}

int CyberFlixEngine::getWaveVolume() {
	return audioRuntime().getWaveVolume(*this);
}

int CyberFlixEngine::setWaveVolume(int newLevel) {
	return audioRuntime().setWaveVolume(*this, newLevel);
}

int CyberFlixEngine::getSoundVolume(const Common::String &name) {
	return audioRuntime().getSoundVolume(*this, name);
}

int CyberFlixEngine::setSoundVolume(const Common::String &name, int newVolume) {
	return audioRuntime().setSoundVolume(*this, name, newVolume);
}

Common::String CyberFlixEngine::currentTheme(int which) {
	return audioRuntime().currentTheme(*this, which);
}

Common::String CyberFlixEngine::currentSound(int which) {
	return audioRuntime().currentSound(*this, which);
}

Common::String CyberFlixEngine::currentVoice() {
	return audioRuntime().currentVoice(*this);
}

bool CyberFlixEngine::voiceDone() {
	return audioRuntime().voiceDone(*this);
}

} // End of namespace CyberFlix

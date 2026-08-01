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

void CyberflixEngine::openTrackFile(const Common::String &name) {
	audioRuntime().openTrackFile(name);
}

void CyberflixEngine::closeTrackFile(const Common::String &name) {
	audioRuntime().closeTrackFile(name);
}

void CyberflixEngine::playTheme(const Common::String &name) {
	audioRuntime().playTheme(*this, name);
}

void CyberflixEngine::haltTheme() {
	audioRuntime().haltTheme(*this);
}

void CyberflixEngine::playSound(const Common::String &name, int mode) {
	audioRuntime().playSound(*this, name, mode);
}

void CyberflixEngine::playVoice(const Common::String &name) {
	audioRuntime().playVoice(*this, name);
}

void CyberflixEngine::haltSound(int which) {
	audioRuntime().haltSound(*this, which);
}

void CyberflixEngine::haltVoice() {
	audioRuntime().haltVoice(*this);
}

void CyberflixEngine::themeVolume(const Common::String &name, int volume) {
	audioRuntime().themeVolume(*this, name, volume);
}

int CyberflixEngine::getWaveVolume() {
	return audioRuntime().getWaveVolume(*this);
}

int CyberflixEngine::setWaveVolume(int newLevel) {
	return audioRuntime().setWaveVolume(*this, newLevel);
}

int CyberflixEngine::getSoundVolume(const Common::String &name) {
	return audioRuntime().getSoundVolume(*this, name);
}

int CyberflixEngine::setSoundVolume(const Common::String &name, int newVolume) {
	return audioRuntime().setSoundVolume(*this, name, newVolume);
}

Common::String CyberflixEngine::currentTheme(int which) {
	return audioRuntime().currentTheme(*this, which);
}

Common::String CyberflixEngine::currentSound(int which) {
	return audioRuntime().currentSound(*this, which);
}

Common::String CyberflixEngine::currentVoice() {
	return audioRuntime().currentVoice(*this);
}

bool CyberflixEngine::voiceDone() {
	return audioRuntime().voiceDone(*this);
}

} // End of namespace Cyberflix

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

void DreamFactoryEngine::openTrackFile(const Common::String &name) {
	audioRuntime().openTrackFile(name);
}

void DreamFactoryEngine::closeTrackFile(const Common::String &name) {
	audioRuntime().closeTrackFile(name);
}

void DreamFactoryEngine::playTheme(const Common::String &name) {
	audioRuntime().playTheme(*this, name);
}

void DreamFactoryEngine::haltTheme() {
	audioRuntime().haltTheme(*this);
}

void DreamFactoryEngine::playSound(const Common::String &name, int mode) {
	audioRuntime().playSound(*this, name, mode);
}

void DreamFactoryEngine::playVoice(const Common::String &name) {
	audioRuntime().playVoice(*this, name);
}

void DreamFactoryEngine::haltSound(int which) {
	audioRuntime().haltSound(*this, which);
}

void DreamFactoryEngine::haltVoice() {
	audioRuntime().haltVoice(*this);
}

void DreamFactoryEngine::themeVolume(const Common::String &name, int volume) {
	audioRuntime().themeVolume(*this, name, volume);
}

int DreamFactoryEngine::getWaveVolume() {
	return audioRuntime().getWaveVolume(*this);
}

int DreamFactoryEngine::setWaveVolume(int newLevel) {
	return audioRuntime().setWaveVolume(*this, newLevel);
}

int DreamFactoryEngine::getSoundVolume(const Common::String &name) {
	return audioRuntime().getSoundVolume(*this, name);
}

int DreamFactoryEngine::setSoundVolume(const Common::String &name, int newVolume) {
	return audioRuntime().setSoundVolume(*this, name, newVolume);
}

Common::String DreamFactoryEngine::currentTheme(int which) {
	return audioRuntime().currentTheme(*this, which);
}

Common::String DreamFactoryEngine::currentSound(int which) {
	return audioRuntime().currentSound(*this, which);
}

Common::String DreamFactoryEngine::currentVoice() {
	return audioRuntime().currentVoice(*this);
}

bool DreamFactoryEngine::voiceDone() {
	return audioRuntime().voiceDone(*this);
}

} // End of namespace DreamFactory

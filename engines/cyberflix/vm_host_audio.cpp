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

void CyberflixAudioVMHost::openTrackFile(const Common::String &name) {
	engine().audioRuntime().openTrackFile(name);
}

void CyberflixAudioVMHost::closeTrackFile(const Common::String &name) {
	engine().audioRuntime().closeTrackFile(name);
}

void CyberflixAudioVMHost::playTheme(const Common::String &name) {
	engine().audioRuntime().playTheme(engine(), name);
}

void CyberflixAudioVMHost::haltTheme() {
	engine().audioRuntime().haltTheme(engine());
}

void CyberflixAudioVMHost::playSound(const Common::String &name, int mode) {
	engine().audioRuntime().playSound(engine(), name, mode);
}

void CyberflixAudioVMHost::playVoice(const Common::String &name) {
	engine().audioRuntime().playVoice(engine(), name);
}

void CyberflixAudioVMHost::haltSound(int which) {
	engine().audioRuntime().haltSound(engine(), which);
}

void CyberflixAudioVMHost::haltVoice() {
	engine().audioRuntime().haltVoice(engine());
}

void CyberflixAudioVMHost::themeVolume(const Common::String &name, int volume) {
	engine().audioRuntime().themeVolume(engine(), name, volume);
}

int CyberflixAudioVMHost::waveVolume(const int *newLevel) {
	return engine().audioRuntime().waveVolume(engine(), newLevel);
}

int CyberflixAudioVMHost::soundVolume(const Common::String &name, const int *newVolume) {
	return engine().audioRuntime().soundVolume(engine(), name, newVolume);
}

Common::String CyberflixAudioVMHost::currentTheme(int which) {
	return engine().audioRuntime().currentTheme(engine(), which);
}

Common::String CyberflixAudioVMHost::currentSound(int which) {
	return engine().audioRuntime().currentSound(engine(), which);
}

Common::String CyberflixAudioVMHost::currentVoice() {
	return engine().audioRuntime().currentVoice(engine());
}

bool CyberflixAudioVMHost::voiceDone() {
	return engine().audioRuntime().voiceDone(engine());
}

} // End of namespace Cyberflix

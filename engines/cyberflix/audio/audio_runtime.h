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

#ifndef CYBERFLIX_AUDIO_AUDIO_RUNTIME_H
#define CYBERFLIX_AUDIO_AUDIO_RUNTIME_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

#include "audio/mixer.h"

namespace Cyberflix {

class CyberflixEngine;

class AudioRuntime {
public:
	/**
	 * A loaded .TRK track file (TI.EXE track record, FUN_00411cc0). Holds the
	 * raw container plus parsed theme/SFX cue directories.
	 */
	struct ThemeTrack {
		Common::String sourceName;
		Common::String name;
		Common::Array<byte> fileData;
		struct Cue {
			Common::String name;
			uint32 resId = 0;
			byte flags = 0;
			uint32 dataOffset = 0;
			uint32 length = 0;
			int volume = 255;
		};
		Common::Array<Cue> cues;
		Common::Array<Cue> sfxCues;
		Common::Array<uint16> playlist;
		uint32 loopIdx = 0;
		int volume = 255;
	};

	struct ThemeCueSpan {
		uint32 startSample;
		Common::String name;
	};

	struct SoundSlot {
		Audio::SoundHandle handle;
		Common::String cueName;
		uint32 resId = 0;
	};

	ThemeTrack *findTrack(const Common::String &name);
	Common::SharedPtr<ThemeTrack> findTrackRef(const Common::String &name);
	const ThemeTrack::Cue *findSfxCue(const Common::String &name, ThemeTrack **trackOut = nullptr);
	ThemeTrack::Cue *findMutableSfxCue(const Common::String &name, ThemeTrack **trackOut = nullptr);

	byte effectiveAudioVolume(int baseVolume) const;
	void applyLiveAudioVolumes(CyberflixEngine &engine);
	void prepareThemeSpans(const ThemeTrack &track);
	bool startThemeStream(CyberflixEngine &engine, const Common::SharedPtr<ThemeTrack> &track, uint32 startSample);
	bool playSoundCue(CyberflixEngine &engine, const Common::String &name, Audio::SoundHandle &handle,
			Common::String &currentCue, uint32 &currentResId);

	void openTrackFile(const Common::String &name);
	void closeTrackFile(const Common::String &name);
	void playTheme(CyberflixEngine &engine, const Common::String &name);
	void haltTheme(CyberflixEngine &engine);
	void playSound(CyberflixEngine &engine, const Common::String &name, int mode);
	void playVoice(CyberflixEngine &engine, const Common::String &name);
	void haltSound(CyberflixEngine &engine, int which);
	void haltVoice(CyberflixEngine &engine);
	void themeVolume(CyberflixEngine &engine, const Common::String &name, int volume);
	int waveVolume(CyberflixEngine &engine, const int *newLevel);
	int soundVolume(CyberflixEngine &engine, const Common::String &name, const int *newVolume);
	Common::String currentTheme(CyberflixEngine &engine, int which);
	Common::String currentSound(CyberflixEngine &engine, int which);
	Common::String currentVoice(CyberflixEngine &engine);
	bool voiceDone(CyberflixEngine &engine);

	Common::Array<Common::SharedPtr<ThemeTrack> > &tracks() { return _tracks; }
	const Common::Array<Common::SharedPtr<ThemeTrack> > &tracks() const { return _tracks; }
	void clearTracks() { _tracks.clear(); }
	int waveVolumeLevel() const { return _waveVolumeLevel; }
	void setWaveVolumeLevel(int level) { _waveVolumeLevel = CLIP(level, 0, 9); }

	const Common::String &themeTrackName() const { return _themeTrackName; }
	const Audio::SoundHandle &themeHandle() const { return _themeHandle; }
	uint32 themeStartSample() const { return _themeStartSample; }
	uint32 themeIntroSamples() const { return _themeIntroSamples; }
	uint32 themeLoopSamples() const { return _themeLoopSamples; }
	const Common::Array<ThemeCueSpan> &themeSpans() const { return _themeSpans; }

private:
	class ThemeAudioStream;

	Common::Array<Common::SharedPtr<ThemeTrack> > _tracks;
	Audio::SoundHandle _themeHandle;
	Common::String _themeTrackName;
	Common::Array<ThemeCueSpan> _themeSpans;
	uint32 _themeIntroSamples = 0;
	uint32 _themeLoopSamples = 0;
	uint32 _themeStartSample = 0;
	SoundSlot _soundSlots[2];
	SoundSlot _voiceSlot;
	int _waveVolumeLevel = 9;
};

} // End of namespace Cyberflix

#endif

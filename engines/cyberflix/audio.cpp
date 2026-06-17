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


#include "common/debug.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/path.h"
#include "common/util.h"

#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "audio/mixer.h"

#include "cyberflix/archive.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/sound.h"

#include <math.h>

namespace Cyberflix {

static const byte *engineBase(const Common::Array<byte> &fileData, const Archive::Resource &res) {
	if (res.empty || res.dataOffset < 4 || res.dataOffset > fileData.size())
		return nullptr;
	return fileData.begin() + res.dataOffset - 4;
}

// Read a Pascal string (1-byte length prefix) into a Common::String, bounded by
// the end of the file buffer.
static Common::String readPascalString(const byte *p, const Common::Array<byte> &fileData) {
	if (!p || p < fileData.begin() || p >= fileData.end())
		return Common::String();
	uint len = *p;
	const byte *s = p + 1;
	if (s + len > fileData.end())
		len = (uint)(fileData.end() - s);
	return Common::String((const char *)s, len);
}

CyberflixEngine::ThemeTrack *CyberflixEngine::findTrack(const Common::String &name) {
	Common::SharedPtr<ThemeTrack> track = findTrackRef(name);
	return track ? track.get() : nullptr;
}

Common::SharedPtr<CyberflixEngine::ThemeTrack> CyberflixEngine::findTrackRef(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint i = 0; i < _tracks.size(); ++i)
		if (_tracks[i]->name == key)
			return _tracks[i];
	return Common::SharedPtr<ThemeTrack>();
}

class CyberflixEngine::ThemeAudioStream : public Audio::AudioStream {
public:
	ThemeAudioStream(const Common::SharedPtr<ThemeTrack> &track, uint32 startSample) :
			_track(track), _loopIdx(track->loopIdx) {
		// Native playtheme() (FUN_00412250 -> FUN_0042f930/FUN_0042f960)
		// installs the theme cue chain on the DirectSound theme channel
		// (DAT_00460a88). FUN_0042f960 primes the DirectSound ring by calling the
		// servicer once, then FUN_0042ebb0 decodes later 0x400-byte ring blocks as
		// the sound buffer advances, following each cue's +0x18 next pointer and
		// looping from the final playlist entry back to loopIdx. Keep the already
		// loaded ThemeTrack alive like those locked native descriptors, and decode
		// one cbx block at a time so ScummVM does not block the main thread by
		// predecoding the full intro and loop before the music starts.
		for (uint i = 0; i < _track->playlist.size(); ++i) {
			CueData cueData;
			const uint16 cueIdx = _track->playlist[i];
			if (cueIdx >= 1 && cueIdx <= _track->cues.size()) {
				const ThemeTrack::Cue &cue = _track->cues[cueIdx - 1];
				if (cue.length && cue.dataOffset + cue.length <= _track->fileData.size()) {
					CbxAudioInfo info = getCbxAudioInfo(_track->fileData.begin() + cue.dataOffset, cue.length);
					if (info.blockSamples != 0 &&
							info.blockCount <= 0xffffffffU / info.blockSamples) {
						cueData.dataOffset = cue.dataOffset;
						cueData.length = cue.length;
						cueData.blockSamples = info.blockSamples;
						cueData.blockCount = info.blockCount;
						cueData.totalSamples = info.blockSamples * info.blockCount;
						if (cueData.blockSamples > _maxBlockSamples)
							_maxBlockSamples = cueData.blockSamples;
					}
				}
			}
			if (i < _loopIdx)
				_introSamples += cueData.totalSamples;
			else
				_loopSamples += cueData.totalSamples;
			_cues.push_back(cueData);
		}

		if (_loopIdx >= _cues.size())
			_loopIdx = _cues.empty() ? 0 : _cues.size() - 1;
		if (_maxBlockSamples != 0)
			_block.resize(_maxBlockSamples);
		seekToSample(startSample);
	}

	int readBuffer(int16 *buffer, const int numSamples) override {
		int produced = 0;
		while (produced < numSamples && !_finished) {
			if (_blockOffset >= _blockValid)
				decodeNextBlock();
			if (_finished)
				break;

			const uint32 n = MIN<uint32>(numSamples - produced, _blockValid - _blockOffset);
			for (uint32 i = 0; i < n; ++i)
				buffer[produced + i] = (int16)(((int)_block[_blockOffset + i] - 128) << 8);
			_blockOffset += n;
			produced += n;
		}
		return produced;
	}

	bool isStereo() const override { return false; }
	int getRate() const override { return kAudioSampleRate; }
	bool endOfData() const override { return _finished; }
	bool endOfStream() const override { return _finished; }

private:
	struct CueData {
		uint32 dataOffset = 0;
		uint32 length = 0;
		uint32 blockSamples = 0;
		uint32 blockCount = 0;
		uint32 totalSamples = 0;
	};

	void seekToSample(uint32 sample) {
		const uint32 totalSamples = _introSamples + _loopSamples;
		if (_cues.empty() || totalSamples == 0 || _maxBlockSamples == 0) {
			_finished = true;
			return;
		}
		if (sample >= _introSamples) {
			if (_loopSamples == 0) {
				_finished = true;
				return;
			}
			sample = _introSamples + (sample - _introSamples) % _loopSamples;
		}

		for (uint i = 0; i < _cues.size(); ++i) {
			const CueData &cue = _cues[i];
			if (sample < cue.totalSamples) {
				_cueIndex = i;
				_blockIndex = sample / cue.blockSamples;
				_blockOffset = sample % cue.blockSamples;
				_blockValid = 0;
				decodeCurrentBlock();
				return;
			}
			sample -= cue.totalSamples;
		}
		if (_loopSamples != 0) {
			_cueIndex = _loopIdx;
			_blockIndex = 0;
			_blockOffset = _blockValid = 0;
			decodeNextBlock();
		} else {
			_finished = true;
		}
	}

	void advanceCue() {
		if (_cues.empty() || (_loopSamples == 0 && _cueIndex + 1 >= _cues.size())) {
			_finished = true;
			return;
		}

		do {
			if (_cueIndex + 1 >= _cues.size())
				_cueIndex = _loopIdx;
			else
				++_cueIndex;
		} while (_cues[_cueIndex].totalSamples == 0 && !_finished && _cueIndex + 1 < _cues.size());

		if (_cues[_cueIndex].totalSamples == 0) {
			_finished = true;
			return;
		}
		_blockIndex = _blockOffset = _blockValid = 0;
	}

	void decodeCurrentBlock() {
		while (!_finished) {
			if (_cueIndex >= _cues.size()) {
				_finished = true;
				return;
			}
			const CueData &cue = _cues[_cueIndex];
			if (cue.totalSamples == 0 || _blockIndex >= cue.blockCount) {
				advanceCue();
				continue;
			}

			_blockValid = decodeCbxAudioBlock(_track->fileData.begin() + cue.dataOffset, cue.length,
					_blockIndex, _block.begin(), _block.size());
			++_blockIndex;
			if (_blockValid != 0)
				return;
			advanceCue();
		}
	}

	void decodeNextBlock() {
		_blockOffset = 0;
		decodeCurrentBlock();
	}

	Common::SharedPtr<ThemeTrack> _track;
	Common::Array<CueData> _cues;
	Common::Array<byte> _block;
	uint32 _loopIdx = 0;
	uint32 _introSamples = 0;
	uint32 _loopSamples = 0;
	uint32 _maxBlockSamples = 0;
	uint32 _cueIndex = 0;
	uint32 _blockIndex = 0;
	uint32 _blockOffset = 0;
	uint32 _blockValid = 0;
	bool _finished = false;
};

const CyberflixEngine::ThemeTrack::Cue *CyberflixEngine::findSfxCue(const Common::String &name,
		ThemeTrack **trackOut) {
	for (uint i = 0; i < _tracks.size(); ++i) {
		for (uint j = 0; j < _tracks[i]->sfxCues.size(); ++j) {
			if (_tracks[i]->sfxCues[j].name.equalsIgnoreCase(name)) {
				if (trackOut)
					*trackOut = _tracks[i].get();
				return &_tracks[i]->sfxCues[j];
			}
		}
	}
	return nullptr;
}

CyberflixEngine::ThemeTrack::Cue *CyberflixEngine::findMutableSfxCue(const Common::String &name,
		ThemeTrack **trackOut) {
	for (uint i = 0; i < _tracks.size(); ++i) {
		for (uint j = 0; j < _tracks[i]->sfxCues.size(); ++j) {
			if (_tracks[i]->sfxCues[j].name.equalsIgnoreCase(name)) {
				if (trackOut)
					*trackOut = _tracks[i].get();
				return &_tracks[i]->sfxCues[j];
			}
		}
	}
	return nullptr;
}

static byte nativeDirectSoundVolumeToMixerVolume(int volume) {
	volume = CLIP(volume, 0, 255);
	if (volume <= 0)
		return 0;
	if (volume >= 255)
		return Audio::Mixer::kMaxChannelVolume;

	// TI.EXE FUN_0042f100 converts cue volume to DirectSound attenuation as
	// -1000 * ln(255 / volume) millibels. ScummVM's mixer wants linear gain.
	const double dsMillibels = -1000.0 * log(255.0 / volume);
	const double linear = pow(10.0, dsMillibels / 2000.0);
	return (byte)CLIP((int)(linear * Audio::Mixer::kMaxChannelVolume + 0.5),
			0, (int)Audio::Mixer::kMaxChannelVolume);
}

byte CyberflixEngine::effectiveAudioVolume(int baseVolume) const {
	return nativeDirectSoundVolumeToMixerVolume(baseVolume) * CLIP(_waveVolumeLevel, 0, 9) / 9;
}

void CyberflixEngine::applyLiveAudioVolumes() {
	if (!_themeTrackName.empty() && _mixer->isSoundHandleActive(_themeHandle)) {
		ThemeTrack *track = findTrack(_themeTrackName);
		_mixer->setChannelVolume(_themeHandle,
				effectiveAudioVolume(track ? track->volume : 255));
	}

	for (uint i = 0; i < ARRAYSIZE(_soundSlots); ++i) {
		if (_mixer->isSoundHandleActive(_soundSlots[i].handle)) {
			const ThemeTrack::Cue *cue = findSfxCue(_soundSlots[i].cueName);
			_mixer->setChannelVolume(_soundSlots[i].handle,
					effectiveAudioVolume(cue ? cue->volume : 255));
		}
	}
	if (_mixer->isSoundHandleActive(_voiceSlot.handle)) {
		const ThemeTrack::Cue *cue = findSfxCue(_voiceSlot.cueName);
		_mixer->setChannelVolume(_voiceSlot.handle,
				effectiveAudioVolume(cue ? cue->volume : 255));
	}
}

void CyberflixEngine::prepareThemeSpans(const ThemeTrack &track) {
	_themeSpans.clear();
	_themeIntroSamples = _themeLoopSamples = 0;
	for (uint i = 0; i < track.playlist.size(); ++i) {
		const bool inLoop = (i >= track.loopIdx);
		const uint16 cueIdx = track.playlist[i];
		if (cueIdx < 1 || cueIdx > track.cues.size())
			continue;

		const ThemeTrack::Cue &cue = track.cues[cueIdx - 1];
		const uint32 start = inLoop ? _themeIntroSamples + _themeLoopSamples : _themeIntroSamples;
		ThemeCueSpan span;
		span.startSample = start;
		span.name = cue.name;
		_themeSpans.push_back(span);

		uint32 samples = 0;
		if (cue.length && cue.dataOffset + cue.length <= track.fileData.size()) {
			const CbxAudioInfo info = getCbxAudioInfo(track.fileData.begin() + cue.dataOffset, cue.length);
			if (info.blockSamples != 0 && info.blockCount <= 0xffffffffU / info.blockSamples)
				samples = info.blockSamples * info.blockCount;
		}
		if (inLoop)
			_themeLoopSamples += samples;
		else
			_themeIntroSamples += samples;
	}
}

bool CyberflixEngine::startThemeStream(const Common::SharedPtr<ThemeTrack> &track, uint32 startSample) {
	if (!track)
		return false;
	prepareThemeSpans(*track);
	if (_themeIntroSamples == 0 && _themeLoopSamples == 0)
		return false;

	Audio::AudioStream *stream = new ThemeAudioStream(track, startSample);
	if (stream->endOfStream()) {
		delete stream;
		return false;
	}
	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_themeHandle, stream);
	_mixer->setChannelVolume(_themeHandle, effectiveAudioVolume(track->volume));
	_themeTrackName = track->name;
	_themeStartSample = startSample;
	return true;
}

// opentrackfile('name.trk'): load and parse a track file, appending it to the
// open-track list (TI.EXE FUN_00411be0 -> parser FUN_00411cc0, list
// DAT_0046114c), including its theme and SFX cue directories.
//
// .TRK payload fields are read from the "record+8" base (the info dword is
// part of the master header there, unlike the MOV record+12 view): res0
// master header B: theme-table res id u32 @B+0x1c, pascal track name @B+0x24.
// Theme table T: loop index u32 @T+0, playlist length u16 @T+4, playlist
// u16[] @T+6 (1-based cue indices in play order), cue count u32 @T+0x10a, cue
// records @T+0x10e stride 0x1a { u32 ?, u32 resId @+4, pascal name @+0xa }.
// SFX table S: count u32 @S+4, records @S+8 stride 0x1a
// { flags @+0, u32 resId @+4, pascal name @+0xa }.
//
// Native parser FUN_00411cc0 immediately calls FUN_00430330 for every theme cue:
// FUN_00440b00 loads/caches the resource, FUN_0043f970 locks it, and the cue
// descriptor stores the payload pointer. playtheme() later consumes those
// descriptors, so disk/resource loading is intentionally front-loaded here.
// See files/audio-re-notes.md.
void CyberflixEngine::openTrackFile(const Common::String &name) {
	if (name.empty())
		return;

	Common::SharedPtr<ThemeTrack> track(new ThemeTrack());
	track->sourceName = name;
	track->sourceName.toLowercase();
	track->name = name;
	track->name.toLowercase();

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open track file '%s'", name.c_str());
		return;
	}
	uint32 size = (uint32)file.size();
	track->fileData.resize(size);
	if (file.read(track->fileData.begin(), size) != size) {
		warning("Cyberflix: could not read track file '%s'", name.c_str());
		return;
	}
	file.close();

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(track->fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid track container", name.c_str());
		return;
	}

	const byte *master = archive.getResourceCount()
			? engineBase(track->fileData, archive.getResource(0)) : nullptr;
	if (!master || master + 0x28 > track->fileData.end()) {
		warning("Cyberflix: track '%s' has no master header", name.c_str());
		return;
	}
	Common::String logicalName = readPascalString(master + 0x24, track->fileData);
	if (!logicalName.empty()) {
		track->name = logicalName;
		track->name.toLowercase();
	}
	uint32 themeTableId = READ_LE_UINT32(master + 0x1c);
	uint32 sfxTableId = READ_LE_UINT32(master + 0x20);
	const byte *tt = (themeTableId < archive.getResourceCount())
			? engineBase(track->fileData, archive.getResource(themeTableId)) : nullptr;
	if (!tt || tt + 0x10e > track->fileData.end()) {
		warning("Cyberflix: track '%s' has no theme table", name.c_str());
		return;
	}

	track->loopIdx = READ_LE_UINT32(tt);
	uint16 playlistLen = READ_LE_UINT16(tt + 4);
	for (uint i = 0; i < playlistLen && tt + 6 + 2 * i + 2 <= track->fileData.end(); ++i)
		track->playlist.push_back(READ_LE_UINT16(tt + 6 + 2 * i));
	// FUN_00411cc0 clamps the loop target into the playlist.
	if (!track->playlist.empty() && track->loopIdx >= track->playlist.size())
		track->loopIdx = track->playlist.size() - 1;

	uint32 themeCueCount = READ_LE_UINT32(tt + 0x10a);
	for (uint32 i = 0; i < themeCueCount; ++i) {
		const byte *rec = tt + 0x10e + 0x1a * i;
		if (rec + 0x1a > track->fileData.end())
			break;
		ThemeTrack::Cue cue;
		uint32 resId = READ_LE_UINT32(rec + 4);
		cue.resId = resId;
		cue.name = readPascalString(rec + 0xa, track->fileData);
		if (resId < archive.getResourceCount() && !archive.getResource(resId).empty) {
			cue.dataOffset = archive.getResource(resId).dataOffset;
			cue.length = archive.getResource(resId).length;
		}
		track->cues.push_back(cue);
	}

	const byte *st = (sfxTableId < archive.getResourceCount())
			? engineBase(track->fileData, archive.getResource(sfxTableId)) : nullptr;
	if (st && st + 8 <= track->fileData.end()) {
		uint32 sfxCueCount = READ_LE_UINT32(st + 4);
		for (uint32 i = 0; i < sfxCueCount; ++i) {
			const byte *rec = st + 8 + 0x1a * i;
			if (rec + 0x1a > track->fileData.end())
				break;
			ThemeTrack::Cue cue;
			cue.flags = rec[0];
			cue.resId = READ_LE_UINT32(rec + 4);
			cue.name = readPascalString(rec + 0xa, track->fileData);
			if (cue.resId < archive.getResourceCount() && !archive.getResource(cue.resId).empty) {
				cue.dataOffset = archive.getResource(cue.resId).dataOffset;
				cue.length = archive.getResource(cue.resId).length;
			}
			track->sfxCues.push_back(cue);
		}
	}

	_tracks.push_back(track);
	debug(1, "Cyberflix: track '%s' open as '%s' (%u theme cues, %u sfx cues, playlist %u, loop @%u)",
			name.c_str(), track->name.c_str(), (uint32)track->cues.size(),
			(uint32)track->sfxCues.size(), (uint32)track->playlist.size(), track->loopIdx);
}

// closetrackfile('name.trk'): remove the named track from the open list
// (TI.EXE FUN_00412070). Native playback already has locked cue descriptors in
// the DirectSound theme channel, so closing the track does not stop the active
// theme; our ThemeAudioStream likewise keeps a reference to the parsed track.
void CyberflixEngine::closeTrackFile(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint i = 0; i < _tracks.size(); ++i) {
		if (_tracks[i]->name == key) {
			_tracks.remove_at(i);
			return;
		}
	}
}

// playtheme('name.trk'): start the track's theme playlist on the theme
// channel, replacing whatever is playing (TI.EXE FUN_00412250 ->
// FUN_0042f930/FUN_0042f960). Native opentrackfile() has already loaded and
// locked each theme cue resource with FUN_00430330; playtheme() just installs
// that cue chain on the DirectSound theme channel. FUN_0042f960 primes the ring
// once, and FUN_0042ebb0 keeps decoding later cbx blocks as playback advances.
void CyberflixEngine::playTheme(const Common::String &name) {
	Common::SharedPtr<ThemeTrack> track = findTrackRef(name);
	if (!track) {
		warning("Cyberflix: playtheme('%s'): track not open", name.c_str());
		return;
	}

	_mixer->stopHandle(_themeHandle);
	_themeTrackName.clear();
	_themeSpans.clear();
	_themeIntroSamples = _themeLoopSamples = 0;
	_themeStartSample = 0;
	if (track->playlist.empty())
		return;

	if (!startThemeStream(track, 0))
		return;
	debug(1, "Cyberflix: playtheme '%s' (intro %u + loop %u samples, vol %d)",
			name.c_str(), _themeIntroSamples, _themeLoopSamples, track->volume);
}

// halttheme(): stop the theme channel (TI.EXE FUN_00412410 -> FUN_0042f690).
void CyberflixEngine::haltTheme() {
	_mixer->stopHandle(_themeHandle);
	_themeTrackName.clear();
	_themeSpans.clear();
	_themeIntroSamples = _themeLoopSamples = 0;
	_themeStartSample = 0;
}

bool CyberflixEngine::playSoundCue(const Common::String &name, Audio::SoundHandle &handle,
		Common::String &currentCue, uint32 &currentResId) {
	ThemeTrack *track = nullptr;
	const ThemeTrack::Cue *cue = findSfxCue(name, &track);
	if (!cue || !track || cue->length == 0 || cue->dataOffset + cue->length > track->fileData.size()) {
		warning("Cyberflix: sound cue '%s' not found", name.c_str());
		return false;
	}

	Common::Array<byte> pcm;
	decodeCbxAudio(track->fileData.begin() + cue->dataOffset, cue->length, pcm);
	if (pcm.empty())
		return false;

	byte *buf = (byte *)malloc(pcm.size());
	memcpy(buf, pcm.begin(), pcm.size());
	Audio::SeekableAudioStream *stream = Audio::makeRawStream(
			buf, pcm.size(), kAudioSampleRate, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
	_mixer->stopHandle(handle);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream);
	_mixer->setChannelVolume(handle, effectiveAudioVolume(cue->volume));
	currentCue = cue->name;
	currentResId = cue->resId;
	return true;
}

// singlesound/multiplesound/dualsound/bothsound: play a named SFX cue on the
// two normal sound slots. Slot selection follows FUN_0042fa80/FUN_0042fb20/
// FUN_0042fbc0/FUN_0042fc30, using the cue resource id as the native priority
// key when both slots are occupied.
void CyberflixEngine::playSound(const Common::String &name, int mode) {
	const ThemeTrack::Cue *cue = findSfxCue(name);
	if (!cue) {
		warning("Cyberflix: sound cue '%s' not found", name.c_str());
		return;
	}

	bool active0 = _mixer->isSoundHandleActive(_soundSlots[0].handle);
	bool active1 = _mixer->isSoundHandleActive(_soundSlots[1].handle);
	if (!active0) {
		_soundSlots[0].cueName.clear();
		_soundSlots[0].resId = 0;
	}
	if (!active1) {
		_soundSlots[1].cueName.clear();
		_soundSlots[1].resId = 0;
	}

	auto playSlot = [&](int slot) {
		playSoundCue(name, _soundSlots[slot].handle, _soundSlots[slot].cueName, _soundSlots[slot].resId);
	};

	switch (mode) {
	case 0: // singlesound
		if ((active0 && _soundSlots[0].resId == cue->resId) ||
				(active1 && _soundSlots[1].resId == cue->resId))
			return;
		if (!active0)
			playSlot(0);
		else if (!active1)
			playSlot(1);
		else if (_soundSlots[0].resId < cue->resId)
			playSlot(0);
		else if (_soundSlots[1].resId < cue->resId)
			playSlot(1);
		break;
	case 1: // multiplesound
		if (active0 && _soundSlots[0].resId == cue->resId)
			playSlot(0);
		else if (active1 && _soundSlots[1].resId == cue->resId)
			playSlot(1);
		else if (!active0)
			playSlot(0);
		else if (!active1)
			playSlot(1);
		else if (_soundSlots[0].resId < _soundSlots[1].resId) {
			if (_soundSlots[0].resId < cue->resId)
				playSlot(0);
		} else if (_soundSlots[1].resId < cue->resId) {
			playSlot(1);
		}
		break;
	case 2: // dualsound
		if (!active0 || _soundSlots[0].resId < cue->resId)
			playSlot(0);
		if (!active1 || _soundSlots[1].resId < cue->resId)
			playSlot(1);
		break;
	case 3: // bothsound
		playSlot(0);
		playSlot(1);
		break;
	}
}

// voicesound(name): play a named SFX cue on the dedicated voice slot.
void CyberflixEngine::playVoice(const Common::String &name) {
	playSoundCue(name, _voiceSlot.handle, _voiceSlot.cueName, _voiceSlot.resId);
}

// haltsound(which): which==1 stops slot 1, 2 stops slot 2, 3 stops both.
void CyberflixEngine::haltSound(int which) {
	if (which < 1 || which > 3) {
		warning("Cyberflix: haltsound(%d): invalid slot", which);
		return;
	}
	if (which == 1 || which == 3) {
		_mixer->stopHandle(_soundSlots[0].handle);
		_soundSlots[0].cueName.clear();
		_soundSlots[0].resId = 0;
	}
	if (which == 2 || which == 3) {
		_mixer->stopHandle(_soundSlots[1].handle);
		_soundSlots[1].cueName.clear();
		_soundSlots[1].resId = 0;
	}
}

void CyberflixEngine::haltVoice() {
	_mixer->stopHandle(_voiceSlot.handle);
	_voiceSlot.cueName.clear();
	_voiceSlot.resId = 0;
}

// themevol('name.trk', 0-255): set the volume of every cue of the named track
// and apply it live to a playing cue (TI.EXE FUN_004125c0 -> FUN_004300c0 ->
// IDirectSoundBuffer::SetVolume). The 0-255 scale matches the mixer's.
void CyberflixEngine::themeVolume(const Common::String &name, int volume) {
	ThemeTrack *track = findTrack(name);
	if (track)
		track->volume = CLIP(volume, 0, 255);
	Common::String key = name;
	key.toLowercase();
	if (key == _themeTrackName && _mixer->isSoundHandleActive(_themeHandle))
		_mixer->setChannelVolume(_themeHandle, effectiveAudioVolume(volume));
}

int CyberflixEngine::waveVolume(const int *newLevel) {
	if (newLevel) {
		_waveVolumeLevel = CLIP(*newLevel, 0, 9);
		applyLiveAudioVolumes();
	}
	return _waveVolumeLevel;
}

int CyberflixEngine::soundVolume(const Common::String &name, const int *newVolume) {
	ThemeTrack::Cue *cue = findMutableSfxCue(name);
	if (!cue) {
		ThemeTrack *track = findTrack(name);
		if (track) {
			if (newVolume)
				for (uint i = 0; i < track->sfxCues.size(); ++i)
					track->sfxCues[i].volume = CLIP(*newVolume, 0, 255);
			applyLiveAudioVolumes();
			return track->sfxCues.empty() ? track->volume : track->sfxCues[0].volume;
		}
		warning("Cyberflix: soundvol('%s'): cue/track not found", name.c_str());
		return 0;
	}

	if (newVolume) {
		cue->volume = CLIP(*newVolume, 0, 255);
		for (uint i = 0; i < ARRAYSIZE(_soundSlots); ++i)
			if (_soundSlots[i].cueName.equalsIgnoreCase(cue->name) &&
					_mixer->isSoundHandleActive(_soundSlots[i].handle))
				_mixer->setChannelVolume(_soundSlots[i].handle,
						effectiveAudioVolume(cue->volume));
		if (_voiceSlot.cueName.equalsIgnoreCase(cue->name) &&
				_mixer->isSoundHandleActive(_voiceSlot.handle))
			_mixer->setChannelVolume(_voiceSlot.handle,
					effectiveAudioVolume(cue->volume));
	}
	return cue->volume;
}

// currenttheme(which): which==1 -> the name of the cue now playing on the
// theme channel, which==2 -> its track file's name; 'none' when silent
// (TI.EXE FUN_00412f20). We map the channel's elapsed time onto the decoded
// cue spans, folding positions past the intro into the loop region.
Common::String CyberflixEngine::currentTheme(int which) {
	if (_themeTrackName.empty() || !_mixer->isSoundHandleActive(_themeHandle))
		return "none";
	if (which == 2)
		return _themeTrackName;
	// 8-bit mono at kAudioSampleRate: one sample per byte.
	uint32 sample = _themeStartSample + (uint32)((uint64)_mixer->getSoundElapsedTime(_themeHandle) *
			kAudioSampleRate / 1000);
	if (sample >= _themeIntroSamples && _themeLoopSamples)
		sample = _themeIntroSamples + (sample - _themeIntroSamples) % _themeLoopSamples;
	Common::String cueName = "none";
	for (uint i = 0; i < _themeSpans.size(); ++i) {
		if (_themeSpans[i].startSample <= sample)
			cueName = _themeSpans[i].name;
		else
			break;
	}
	return cueName;
}

// currentsound(which): query the two normal SFX slots. which==1/2 returns that
// slot; which==3 returns the active slot with the higher native cue resource id.
Common::String CyberflixEngine::currentSound(int which) {
	bool active0 = _mixer->isSoundHandleActive(_soundSlots[0].handle);
	bool active1 = _mixer->isSoundHandleActive(_soundSlots[1].handle);
	if (!active0) {
		_soundSlots[0].cueName.clear();
		_soundSlots[0].resId = 0;
	}
	if (!active1) {
		_soundSlots[1].cueName.clear();
		_soundSlots[1].resId = 0;
	}
	if (which == 1)
		return active0 && !_soundSlots[0].cueName.empty() ? _soundSlots[0].cueName : "None";
	if (which == 2)
		return active1 && !_soundSlots[1].cueName.empty() ? _soundSlots[1].cueName : "None";
	if (which == 3) {
		if (active0 && active1)
			return _soundSlots[1].resId < _soundSlots[0].resId ?
					_soundSlots[0].cueName : _soundSlots[1].cueName;
		if (active0 && !_soundSlots[0].cueName.empty())
			return _soundSlots[0].cueName;
		if (active1 && !_soundSlots[1].cueName.empty())
			return _soundSlots[1].cueName;
	}
	return "None";
}

Common::String CyberflixEngine::currentVoice() {
	if (_mixer->isSoundHandleActive(_voiceSlot.handle) && !_voiceSlot.cueName.empty())
		return _voiceSlot.cueName;
	_voiceSlot.cueName.clear();
	_voiceSlot.resId = 0;
	return "None";
}


} // End of namespace Cyberflix

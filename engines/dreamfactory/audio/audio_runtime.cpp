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
#include "common/ptr.h"
#include "common/util.h"

#include "audio/audiostream.h"
#include "audio/mixer.h"

#include "dreamfactory/archive.h"
#include "dreamfactory/debug.h"
#include "dreamfactory/audio_helpers.h"
#include "dreamfactory/dreamfactory.h"
#include "dreamfactory/resource_helpers.h"
#include "dreamfactory/audio/cbx_audio.h"

#include <math.h>

namespace DreamFactory {

static const uint32 kTrackMasterHeaderSize = 0x28;
static const uint32 kThemePlaylistOffset = 6;
static const uint32 kThemeCueCountOffset = 0x10a;
static const uint32 kThemeCueTableOffset = 0x10e;
static const uint32 kSfxCueTableOffset = 8;
static const uint32 kCueRecordSize = 0x1a;

AudioRuntime::ThemeTrack *AudioRuntime::findTrack(const Common::String &name) {
	Common::SharedPtr<ThemeTrack> track = findTrackRef(name);
	return track ? track.get() : nullptr;
}

Common::SharedPtr<AudioRuntime::ThemeTrack> AudioRuntime::findTrackRef(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (const Common::SharedPtr<ThemeTrack> &track : _tracks)
		if (track->name == key)
			return track;
	return Common::SharedPtr<ThemeTrack>();
}

class AudioRuntime::ThemeAudioStream : public Audio::AudioStream {
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
				if (cue.length && hasRange(_track->fileData.size(), cue.dataOffset, cue.length)) {
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
				buffer[produced + i] = static_cast<int16>((static_cast<int>(_block[_blockOffset + i]) - 128) * 256);
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

		// Find the next playable cue. At the end of a looping theme, continue
		// from loopIdx even when the final cue is empty. Try at most the number
		// of playlist entries so a loop made entirely of empty cues terminates.
		for (uint32 remaining = _cues.size(); remaining > 0; --remaining) {
			if (_cueIndex + 1 >= _cues.size()) {
				if (_loopSamples == 0) {
					_finished = true;
					return;
				}
				_cueIndex = _loopIdx;
			} else {
				++_cueIndex;
			}
			if (_cues[_cueIndex].totalSamples != 0) {
				_blockIndex = _blockOffset = _blockValid = 0;
				return;
			}
		}
		_finished = true;
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

AudioRuntime::SfxCueMatch AudioRuntime::findSfxCue(const Common::String &name) {
	for (const Common::SharedPtr<ThemeTrack> &track : _tracks) {
		for (uint cueIndex = 0; cueIndex < track->sfxCues.size(); ++cueIndex) {
			const ThemeTrack::Cue &cue = track->sfxCues[cueIndex];
			if (cue.name.equalsIgnoreCase(name)) {
				SfxCueMatch match;
				match.track = track;
				match.cueIndex = cueIndex;
				return match;
			}
		}
	}
	return SfxCueMatch();
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
	return static_cast<byte>(CLIP(static_cast<int>((linear * Audio::Mixer::kMaxChannelVolume + 0.5)),
			0, static_cast<int>(Audio::Mixer::kMaxChannelVolume)));
}

byte AudioRuntime::effectiveAudioVolume(int baseVolume) const {
	return nativeDirectSoundVolumeToMixerVolume(baseVolume) * CLIP(_waveVolumeLevel, 0, 9) / 9;
}

void AudioRuntime::applyLiveAudioVolumes(DreamFactoryEngine &engine) {
	if (!_themeTrackName.empty() && engine._mixer->isSoundHandleActive(_themeHandle)) {
		ThemeTrack *track = findTrack(_themeTrackName);
		engine._mixer->setChannelVolume(_themeHandle,
				effectiveAudioVolume(track ? track->volume : 255));
	}

	for (SoundSlot &slot : _soundSlots) {
		if (engine._mixer->isSoundHandleActive(slot.handle)) {
			const SfxCueMatch match = findSfxCue(slot.cueName);
			engine._mixer->setChannelVolume(slot.handle,
					effectiveAudioVolume(match.track ? match.track->sfxCues[match.cueIndex].volume : 255));
		}
	}
	if (engine._mixer->isSoundHandleActive(_voiceSlot.handle)) {
		const SfxCueMatch match = findSfxCue(_voiceSlot.cueName);
		engine._mixer->setChannelVolume(_voiceSlot.handle,
				effectiveAudioVolume(match.track ? match.track->sfxCues[match.cueIndex].volume : 255));
	}
}

void AudioRuntime::prepareThemeSpans(const ThemeTrack &track) {
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
		if (cue.length && hasRange(track.fileData.size(), cue.dataOffset, cue.length)) {
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

bool AudioRuntime::startThemeStream(DreamFactoryEngine &engine, const Common::SharedPtr<ThemeTrack> &track, uint32 startSample) {
	if (!track)
		return false;
	prepareThemeSpans(*track);
	if (_themeIntroSamples == 0 && _themeLoopSamples == 0)
		return false;

	Common::ScopedPtr<Audio::AudioStream> stream(new ThemeAudioStream(track, startSample));
	if (stream->endOfStream()) {
		return false;
	}
	engine._mixer->playStream(Audio::Mixer::kMusicSoundType, &_themeHandle, stream.release());
	engine._mixer->setChannelVolume(_themeHandle, effectiveAudioVolume(track->volume));
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
void AudioRuntime::openTrackFile(const Common::String &name) {
	if (name.empty())
		return;

	Common::SharedPtr<ThemeTrack> track(new ThemeTrack());
	track->sourceName = name;
	track->sourceName.toLowercase();
	track->name = name;
	track->name.toLowercase();

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("DreamFactory: could not open track file '%s'", name.c_str());
		return;
	}
	const int64 fileSize = file.size();
	if (fileSize <= 0 || fileSize > 0xffffffffLL) {
		warning("DreamFactory: invalid track file size for '%s'", name.c_str());
		return;
	}
	uint32 size = static_cast<uint32>(fileSize);
	track->fileData.resize(size);
	if (file.read(track->fileData.begin(), size) != size) {
		warning("DreamFactory: could not read track file '%s'", name.c_str());
		return;
	}
	file.close();

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(track->fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("DreamFactory: '%s' is not a valid track container", name.c_str());
		return;
	}

	const Archive::Resource *masterResource = archive.getResourceCount()
			? &archive.getResource(0) : nullptr;
	const ResourceView master = masterResource
			? resourceEngineView(track->fileData, *masterResource) : ResourceView();
	const byte *masterData = master.dataAt(0, kTrackMasterHeaderSize);
	if (!masterData) {
		warning("DreamFactory: track '%s' has no master header", name.c_str());
		return;
	}
	Common::String logicalName = master.readPascalString(0x24, true);
	if (!logicalName.empty()) {
		track->name = logicalName;
		track->name.toLowercase();
	}
	uint32 themeTableId = READ_LE_UINT32(masterData + 0x1c);
	uint32 sfxTableId = READ_LE_UINT32(masterData + 0x20);
	const Archive::Resource *themeTableResource = themeTableId < archive.getResourceCount()
			? &archive.getResource(themeTableId) : nullptr;
	const ResourceView themeTable = themeTableResource
			? resourceEngineView(track->fileData, *themeTableResource) : ResourceView();
	const byte *themeHeader = themeTable.dataAt(0, kThemeCueTableOffset);
	if (!themeHeader) {
		warning("DreamFactory: track '%s' has no theme table", name.c_str());
		return;
	}

	track->loopIdx = READ_LE_UINT32(themeHeader);
	const uint32 playlistLen = boundedRecordCount(READ_LE_UINT16(themeHeader + 4),
			kThemeCueCountOffset, kThemePlaylistOffset, 2);
	const RecordRange playlist(themeTable, kThemePlaylistOffset, playlistLen, 2);
	for (const ResourceView entry : playlist) {
		uint16 cueIndex;
		if (entry.readUint16LE(0, cueIndex))
			track->playlist.push_back(cueIndex);
	}
	// FUN_00411cc0 clamps the loop target into the playlist.
	if (!track->playlist.empty() && track->loopIdx >= track->playlist.size())
		track->loopIdx = track->playlist.size() - 1;

	const uint32 themeCueCount = boundedRecordCount(READ_LE_UINT32(themeHeader + kThemeCueCountOffset),
			themeTable.size(), kThemeCueTableOffset, kCueRecordSize);
	const RecordRange themeCues(themeTable, kThemeCueTableOffset, themeCueCount, kCueRecordSize);
	for (const ResourceView cueRecord : themeCues) {
		const byte *cueData = cueRecord.dataAt(0, kCueRecordSize);
		if (!cueData)
			continue;
		ThemeTrack::Cue cue;
		cue.resId = READ_LE_UINT32(cueData + 4);
		cue.name = cueRecord.readPascalString(0xa, true);
		if (cue.resId < archive.getResourceCount()) {
			const Archive::Resource &resource = archive.getResource(cue.resId);
			if (resourcePayloadView(track->fileData, resource).valid()) {
				cue.dataOffset = resource.dataOffset;
				cue.length = resource.length;
			}
		}
		track->cues.push_back(cue);
	}

	const Archive::Resource *sfxTableResource = sfxTableId < archive.getResourceCount()
			? &archive.getResource(sfxTableId) : nullptr;
	const ResourceView sfxTable = sfxTableResource
			? resourceEngineView(track->fileData, *sfxTableResource) : ResourceView();
	const byte *sfxHeader = sfxTable.dataAt(0, kSfxCueTableOffset);
	if (sfxHeader) {
		const uint32 sfxCueCount = boundedRecordCount(READ_LE_UINT32(sfxHeader + 4),
				sfxTable.size(), kSfxCueTableOffset, kCueRecordSize);
		const RecordRange sfxCues(sfxTable, kSfxCueTableOffset, sfxCueCount, kCueRecordSize);
		for (const ResourceView cueRecord : sfxCues) {
			const byte *cueData = cueRecord.dataAt(0, kCueRecordSize);
			if (!cueData)
				continue;
			ThemeTrack::Cue cue;
			cue.flags = cueData[0];
			cue.resId = READ_LE_UINT32(cueData + 4);
			cue.name = cueRecord.readPascalString(0xa, true);
			if (cue.resId < archive.getResourceCount()) {
				const Archive::Resource &resource = archive.getResource(cue.resId);
				if (resourcePayloadView(track->fileData, resource).valid()) {
					cue.dataOffset = resource.dataOffset;
					cue.length = resource.length;
				}
			}
			track->sfxCues.push_back(cue);
		}
	}

	_tracks.push_back(track);
	debugC(1, kDebugAudio, "DreamFactory: track '%s' open as '%s' (%u theme cues, %u sfx cues, playlist %u, loop @%u)",
			name.c_str(), track->name.c_str(), static_cast<uint32>(track->cues.size()),
			static_cast<uint32>(track->sfxCues.size()), static_cast<uint32>(track->playlist.size()), track->loopIdx);
}

// closetrackfile('name.trk'): remove the named track from the open list
// (TI.EXE FUN_00412070). Native playback already has locked cue descriptors in
// the DirectSound theme channel, so closing the track does not stop the active
// theme; our ThemeAudioStream likewise keeps a reference to the parsed track.
void AudioRuntime::closeTrackFile(const Common::String &name) {
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
void AudioRuntime::playTheme(DreamFactoryEngine &engine, const Common::String &name) {
	Common::SharedPtr<ThemeTrack> track = findTrackRef(name);
	if (!track) {
		warning("DreamFactory: playtheme('%s'): track not open", name.c_str());
		return;
	}

	engine._mixer->stopHandle(_themeHandle);
	_themeTrackName.clear();
	_themeSpans.clear();
	_themeIntroSamples = _themeLoopSamples = 0;
	_themeStartSample = 0;
	if (track->playlist.empty())
		return;

	if (!startThemeStream(engine, track, 0))
		return;
	debugC(1, kDebugAudio, "DreamFactory: playtheme '%s' (intro %u + loop %u samples, vol %d)",
			name.c_str(), _themeIntroSamples, _themeLoopSamples, track->volume);
}

// halttheme(): stop the theme channel (TI.EXE FUN_00412410 -> FUN_0042f690).
void AudioRuntime::haltTheme(DreamFactoryEngine &engine) {
	engine._mixer->stopHandle(_themeHandle);
	_themeTrackName.clear();
	_themeSpans.clear();
	_themeIntroSamples = _themeLoopSamples = 0;
	_themeStartSample = 0;
}

bool AudioRuntime::playSoundCue(DreamFactoryEngine &engine, const Common::String &name, Audio::SoundHandle &handle,
		Common::String &currentCue, uint32 &currentResId) {
	const SfxCueMatch match = findSfxCue(name);
	if (!match.track) {
		warning("DreamFactory: sound cue '%s' not found", name.c_str());
		return false;
	}
	const ThemeTrack::Cue &cue = match.track->sfxCues[match.cueIndex];
	if (cue.length == 0 || !hasRange(match.track->fileData.size(), cue.dataOffset, cue.length)) {
		warning("DreamFactory: sound cue '%s' has invalid audio data", name.c_str());
		return false;
	}

	Common::Array<byte> pcm;
	decodeCbxAudio(match.track->fileData.begin() + cue.dataOffset, cue.length, pcm);
	if (pcm.empty())
		return false;

	Audio::SeekableAudioStream *stream = makeOwnedRawPcmStream(pcm);
	if (!stream)
		return false;
	engine._mixer->stopHandle(handle);
	engine._mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream);
	engine._mixer->setChannelVolume(handle, effectiveAudioVolume(cue.volume));
	currentCue = cue.name;
	currentResId = cue.resId;
	return true;
}

// singlesound/multiplesound/dualsound/bothsound: play a named SFX cue on the
// two normal sound slots. Slot selection follows FUN_0042fa80/FUN_0042fb20/
// FUN_0042fbc0/FUN_0042fc30, using the cue resource id as the native priority
// key when both slots are occupied.
void AudioRuntime::playSound(DreamFactoryEngine &engine, const Common::String &name, int mode) {
	const SfxCueMatch match = findSfxCue(name);
	if (!match.track) {
		warning("DreamFactory: sound cue '%s' not found", name.c_str());
		return;
	}
	const ThemeTrack::Cue &cue = match.track->sfxCues[match.cueIndex];

	bool active0 = engine._mixer->isSoundHandleActive(_soundSlots[0].handle);
	bool active1 = engine._mixer->isSoundHandleActive(_soundSlots[1].handle);
	if (!active0) {
		_soundSlots[0].cueName.clear();
		_soundSlots[0].resId = 0;
	}
	if (!active1) {
		_soundSlots[1].cueName.clear();
		_soundSlots[1].resId = 0;
	}

	auto playSlot = [&](int slot) {
		playSoundCue(engine, name, _soundSlots[slot].handle, _soundSlots[slot].cueName, _soundSlots[slot].resId);
	};

	switch (mode) {
	case 0: // singlesound
		if ((active0 && _soundSlots[0].resId == cue.resId) ||
				(active1 && _soundSlots[1].resId == cue.resId))
			return;
		if (!active0)
			playSlot(0);
		else if (!active1)
			playSlot(1);
		else if (_soundSlots[0].resId < cue.resId)
			playSlot(0);
		else if (_soundSlots[1].resId < cue.resId)
			playSlot(1);
		break;
	case 1: // multiplesound
		if (active0 && _soundSlots[0].resId == cue.resId)
			playSlot(0);
		else if (active1 && _soundSlots[1].resId == cue.resId)
			playSlot(1);
		else if (!active0)
			playSlot(0);
		else if (!active1)
			playSlot(1);
		else if (_soundSlots[0].resId < _soundSlots[1].resId) {
			if (_soundSlots[0].resId < cue.resId)
				playSlot(0);
		} else if (_soundSlots[1].resId < cue.resId) {
			playSlot(1);
		}
		break;
	case 2: // dualsound
		if (!active0 || _soundSlots[0].resId < cue.resId)
			playSlot(0);
		if (!active1 || _soundSlots[1].resId < cue.resId)
			playSlot(1);
		break;
	case 3: // bothsound
		playSlot(0);
		playSlot(1);
		break;
	}
}

// voicesound(name): play a named SFX cue on the dedicated voice slot.
void AudioRuntime::playVoice(DreamFactoryEngine &engine, const Common::String &name) {
	playSoundCue(engine, name, _voiceSlot.handle, _voiceSlot.cueName, _voiceSlot.resId);
}

// haltsound(which): which==1 stops slot 1, 2 stops slot 2, 3 stops both.
void AudioRuntime::haltSound(DreamFactoryEngine &engine, int which) {
	if (which < 1 || which > 3) {
		warning("DreamFactory: haltsound(%d): invalid slot", which);
		return;
	}
	if (which == 1 || which == 3) {
		engine._mixer->stopHandle(_soundSlots[0].handle);
		_soundSlots[0].cueName.clear();
		_soundSlots[0].resId = 0;
	}
	if (which == 2 || which == 3) {
		engine._mixer->stopHandle(_soundSlots[1].handle);
		_soundSlots[1].cueName.clear();
		_soundSlots[1].resId = 0;
	}
}

void AudioRuntime::haltVoice(DreamFactoryEngine &engine) {
	engine._mixer->stopHandle(_voiceSlot.handle);
	_voiceSlot.cueName.clear();
	_voiceSlot.resId = 0;
}

// themevol('name.trk', 0-255): set the volume of every cue of the named track
// and apply it live to a playing cue (TI.EXE FUN_004125c0 -> FUN_004300c0 ->
// IDirectSoundBuffer::SetVolume). The 0-255 scale matches the mixer's.
void AudioRuntime::themeVolume(DreamFactoryEngine &engine, const Common::String &name, int volume) {
	ThemeTrack *track = findTrack(name);
	if (track)
		track->volume = CLIP(volume, 0, 255);
	Common::String key = name;
	key.toLowercase();
	if (key == _themeTrackName && engine._mixer->isSoundHandleActive(_themeHandle))
		engine._mixer->setChannelVolume(_themeHandle, effectiveAudioVolume(volume));
}

int AudioRuntime::getWaveVolume(DreamFactoryEngine &) {
	return _waveVolumeLevel;
}

int AudioRuntime::setWaveVolume(DreamFactoryEngine &engine, int newLevel) {
	_waveVolumeLevel = CLIP(newLevel, 0, 9);
	applyLiveAudioVolumes(engine);
	return _waveVolumeLevel;
}

int AudioRuntime::getSoundVolume(DreamFactoryEngine &, const Common::String &name) {
	const SfxCueMatch match = findSfxCue(name);
	if (!match.track) {
		ThemeTrack *track = findTrack(name);
		if (track)
			return track->sfxCues.empty() ? track->volume : track->sfxCues[0].volume;
		warning("DreamFactory: soundvol('%s'): cue/track not found", name.c_str());
		return 0;
	}

	return match.track->sfxCues[match.cueIndex].volume;
}

int AudioRuntime::setSoundVolume(DreamFactoryEngine &engine, const Common::String &name, int newVolume) {
	const SfxCueMatch match = findSfxCue(name);
	if (!match.track) {
		ThemeTrack *track = findTrack(name);
		if (track) {
			for (ThemeTrack::Cue &cue : track->sfxCues)
				cue.volume = CLIP(newVolume, 0, 255);
			applyLiveAudioVolumes(engine);
			return track->sfxCues.empty() ? track->volume : track->sfxCues[0].volume;
		}
		warning("DreamFactory: soundvol('%s'): cue/track not found", name.c_str());
		return 0;
	}

	ThemeTrack::Cue &cue = match.track->sfxCues[match.cueIndex];
	cue.volume = CLIP(newVolume, 0, 255);
	for (SoundSlot &slot : _soundSlots)
		if (slot.cueName.equalsIgnoreCase(cue.name) &&
				engine._mixer->isSoundHandleActive(slot.handle))
			engine._mixer->setChannelVolume(slot.handle,
					effectiveAudioVolume(cue.volume));
	if (_voiceSlot.cueName.equalsIgnoreCase(cue.name) &&
			engine._mixer->isSoundHandleActive(_voiceSlot.handle))
		engine._mixer->setChannelVolume(_voiceSlot.handle,
				effectiveAudioVolume(cue.volume));
	return cue.volume;
}

// currenttheme(which): which==1 -> the name of the cue now playing on the
// theme channel, which==2 -> its track file's name; 'none' when silent
// (TI.EXE FUN_00412f20). We map the channel's elapsed time onto the decoded
// cue spans, folding positions past the intro into the loop region.
Common::String AudioRuntime::currentTheme(DreamFactoryEngine &engine, int which) {
	if (_themeTrackName.empty() || !engine._mixer->isSoundHandleActive(_themeHandle))
		return "none";
	if (which == 2)
		return _themeTrackName;
	// 8-bit mono at kAudioSampleRate: one sample per byte.
	uint32 sample = _themeStartSample + static_cast<uint32>((static_cast<uint64>(engine._mixer->getSoundElapsedTime(_themeHandle)) *
			kAudioSampleRate / 1000));
	if (sample >= _themeIntroSamples && _themeLoopSamples)
		sample = _themeIntroSamples + (sample - _themeIntroSamples) % _themeLoopSamples;
	Common::String cueName = "none";
	for (const ThemeCueSpan &span : _themeSpans) {
		if (span.startSample <= sample)
			cueName = span.name;
		else
			break;
	}
	return cueName;
}

// currentsound(which): query the two normal SFX slots. which==1/2 returns that
// slot; which==3 returns the active slot with the higher native cue resource id.
Common::String AudioRuntime::currentSound(DreamFactoryEngine &engine, int which) {
	bool active0 = engine._mixer->isSoundHandleActive(_soundSlots[0].handle);
	bool active1 = engine._mixer->isSoundHandleActive(_soundSlots[1].handle);
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

Common::String AudioRuntime::currentVoice(DreamFactoryEngine &engine) {
	if (engine._mixer->isSoundHandleActive(_voiceSlot.handle) && !_voiceSlot.cueName.empty())
		return _voiceSlot.cueName;
	_voiceSlot.cueName.clear();
	_voiceSlot.resId = 0;
	return "None";
}

// voicedone() (0x4e81): true once the voicesound()/knock channel is idle. Native
// scripts busy-wait `while (not voicedone()) endwhile` on the asynchronous voice
// channel (e.g. HALLF2C Penny door: knock, wait for it to finish, then open the
// door and run the puppet). The voice plays on the mixer thread, so pump the
// engine here while it is still active: this lets the sound finish and keeps the
// software cursor responsive instead of spinning the VM to its step cap (which
// would abandon the rest of the handler and skip the conversation).
bool AudioRuntime::voiceDone(DreamFactoryEngine &engine) {
	if (engine._mixer->isSoundHandleActive(_voiceSlot.handle)) {
		engine.delayMillisWithCursorUpdates(10);
		if (engine._mixer->isSoundHandleActive(_voiceSlot.handle))
			return false;
	}
	return true;
}


} // End of namespace DreamFactory

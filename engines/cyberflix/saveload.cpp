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

#include "common/algorithm.h"
#include "common/error.h"
#include "common/memstream.h"
#include "common/savefile.h"
#include "common/stream.h"
#include "common/system.h"

#include "engines/metaengine.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/detection.h"
#include "cyberflix/set.h"
#include "cyberflix/shop.h"
#include "cyberflix/stage.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

enum {
	kCyberflixSaveVersion = 1
};

static void writeSaveString(Common::WriteStream &out, const Common::String &s) {
	out.writeUint32LE((uint32)s.size());
	if (!s.empty())
		out.write(s.c_str(), (uint32)s.size());
}

static void writeSaveData(Common::WriteStream &out, const Common::Array<byte> &data) {
	out.writeUint32LE((uint32)data.size());
	if (!data.empty())
		out.write(data.begin(), (uint32)data.size());
}

static void writeValue(Common::WriteStream &out, const Value &value) {
	out.writeUint32LE((uint32)value.type);
	out.writeSint32LE(value.intValue);
	writeSaveString(out, value.strValue);
}

static void writeChunk(Common::WriteStream &out, const char tag[4], Common::MemoryWriteStreamDynamic &payload) {
	out.write(tag, 4);
	out.writeUint32LE((uint32)payload.size());
	if (payload.size())
		out.write(payload.getData(), (uint32)payload.size());
}

static Common::String defaultSaveSignature(int gameType) {
	if (gameType == GType_Titanic)
		return "Titanic 1.0";
	return Common::String();
}

bool CyberflixEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	return (_stage && _stage->isOpen()) || (_set && _set->isOpen());
}

void CyberflixEngine::saveGame(const Common::String &signature) {
	Common::String oldSignature = _saveSignature;
	_saveSignature = signature;
	saveGameDialog();
	_saveSignature = oldSignature;
}

Common::Error CyberflixEngine::saveGameState(int slot, const Common::String &desc, bool isAutosave) {
	Common::OutSaveFile *out = _saveFileMan->openForSaving(getSaveStateName(slot));
	if (!out)
		return Common::Error(Common::kCreatingFileFailed, getSaveStateName(slot));
	Common::ScopedPtr<Common::OutSaveFile> saveFile(out);

	const Common::String signature = !_saveSignature.empty()
			? _saveSignature : defaultSaveSignature(getGameType());
	const uint32 now = _system->getMillis();

	saveFile->write("CFXS", 4);
	saveFile->writeUint32LE(kCyberflixSaveVersion);

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		writeSaveString(payload, signature);
		writeSaveString(payload, desc);
		writeSaveString(payload, getGameId());
		payload.writeUint32LE((uint32)getPlatform());
		payload.writeUint32LE((uint32)getLanguage());
		payload.writeUint32LE((uint32)getGameType());
		writeSaveString(payload, _activeCursor);
		writeSaveString(payload, _hitKind);
		payload.writeUint16LE(_actionFrameMask);

		writeSaveString(payload, _stage && _stage->isOpen() ? _stage->name() : Common::String());
		payload.writeSint32LE(_stageNode);
		writeSaveString(payload, _stage && _stage->isOpen()
				? _stage->nodeName((uint32)_stageNode) : Common::String());

		writeSaveString(payload, _set && _set->isOpen() ? _set->name() : Common::String());
		writeSaveString(payload, _set && _set->isOpen() ? _set->setName() : Common::String());
		payload.writeSint32LE(_setScene);
		writeSaveString(payload, (_set && _set->isOpen() && _setScene >= 0)
				? _set->sceneName((uint32)_setScene) : Common::String());
		payload.writeSint32LE(_setTable);
		payload.writeSint32LE(_setAngle);
		writeSaveString(payload, _setView);
		payload.writeByte(_setVisible ? 1 : 0);
		payload.writeUint32LE((uint32)_setTransitionType);
		payload.writeUint32LE(_setTransitionResource);
		payload.writeUint32LE(_setTransitionFrame);

		writeChunk(*saveFile, "HEAD", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		for (uint i = 0; i < ARRAYSIZE(_pathSlots); ++i)
			writeSaveString(payload, _pathSlots[i]);
		writeChunk(*saveFile, "PATH", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		payload.write(_screenClut, sizeof(_screenClut));
		for (uint i = 0; i < ARRAYSIZE(_paletteGamma); ++i)
			payload.writeDoubleLE(_paletteGamma[i]);
		writeChunk(*saveFile, "PAL ", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		payload.writeUint32LE((uint32)_shops.size());
		for (uint s = 0; s < _shops.size(); ++s) {
			const Shop &shop = *_shops[s];
			writeSaveString(payload, shop.name());
			payload.writeUint32LE(shop.propCount());
			for (uint p = 0; p < shop.propCount(); ++p) {
				const Shop::Prop &prop = shop.prop(p);
				writeSaveString(payload, prop.name);
				writeSaveString(payload, prop.setName);
				writeSaveString(payload, prop.sceneName);
				payload.writeUint32LE(prop.masterResId);
				payload.writeUint32LE(prop.scriptResId);
				payload.writeByte(prop.visible ? 1 : 0);
				payload.writeUint16LE(prop.mode);
				payload.writeSint16LE(prop.y);
				payload.writeSint16LE(prop.x);
				payload.writeSint16LE(prop.z);
				payload.writeSint16LE(prop.angle);
				payload.writeSint16LE(prop.depth);
				payload.writeSint32LE(prop.scale);
				payload.writeSint32LE(prop.zClip);
				payload.writeSint32LE(prop.value);
				writeSaveString(payload, prop.shapeName);
				writeSaveString(payload, prop.owner);
				payload.writeUint32LE((uint32)prop.shapes.size());
				for (uint i = 0; i < prop.shapes.size(); ++i) {
					payload.writeUint32LE(prop.shapes[i].resId);
					writeSaveString(payload, prop.shapes[i].name);
				}
			}
		}
		writeChunk(*saveFile, "SHOP", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		auto writeCueArray = [&](const Common::Array<ThemeTrack::Cue> &cues) {
			payload.writeUint32LE((uint32)cues.size());
			for (uint i = 0; i < cues.size(); ++i) {
				writeSaveString(payload, cues[i].name);
				payload.writeUint32LE(cues[i].resId);
				payload.writeByte(cues[i].flags);
				payload.writeUint32LE(cues[i].dataOffset);
				payload.writeUint32LE(cues[i].length);
			}
		};

		payload.writeUint32LE((uint32)_tracks.size());
		for (uint t = 0; t < _tracks.size(); ++t) {
			const ThemeTrack &track = *_tracks[t];
			writeSaveString(payload, track.sourceName);
			writeSaveString(payload, track.name);
			writeSaveData(payload, track.fileData);
			payload.writeUint32LE(track.loopIdx);
			payload.writeSint32LE(track.volume);
			payload.writeUint32LE((uint32)track.playlist.size());
			for (uint i = 0; i < track.playlist.size(); ++i)
				payload.writeUint16LE(track.playlist[i]);
			writeCueArray(track.cues);
			writeCueArray(track.sfxCues);
		}
		writeChunk(*saveFile, "TRAK", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		auto writeSoundSlot = [&](const SoundSlot &slot) {
			const bool active = _mixer->isSoundHandleActive(slot.handle);
			payload.writeByte(active ? 1 : 0);
			writeSaveString(payload, active ? slot.cueName : Common::String());
			payload.writeUint32LE(active ? slot.resId : 0);
			payload.writeUint32LE(active ? _mixer->getSoundElapsedTime(slot.handle) : 0);
		};

		const bool themeActive = !_themeTrackName.empty() && _mixer->isSoundHandleActive(_themeHandle);
		payload.writeByte(themeActive ? 1 : 0);
		writeSaveString(payload, themeActive ? _themeTrackName : Common::String());
		payload.writeUint32LE(themeActive ? _mixer->getSoundElapsedTime(_themeHandle) : 0);
		payload.writeUint32LE(_themeIntroSamples);
		payload.writeUint32LE(_themeLoopSamples);
		payload.writeUint32LE((uint32)_themeSpans.size());
		for (uint i = 0; i < _themeSpans.size(); ++i) {
			payload.writeUint32LE(_themeSpans[i].startSample);
			writeSaveString(payload, _themeSpans[i].name);
		}
		writeSoundSlot(_soundSlots[0]);
		writeSoundSlot(_soundSlots[1]);
		writeSoundSlot(_voiceSlot);

		writeChunk(*saveFile, "AUDI", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		const Common::HashMap<Common::String, Value> &vars = _vm.globalVars();
		Common::Array<Common::String> keys;
		for (Common::HashMap<Common::String, Value>::const_iterator it = vars.begin(); it != vars.end(); ++it)
			keys.push_back(it->_key);
		Common::sort(keys.begin(), keys.end());
		payload.writeUint32LE((uint32)keys.size());
		for (uint i = 0; i < keys.size(); ++i) {
			Common::HashMap<Common::String, Value>::const_iterator value = vars.find(keys[i]);
			writeSaveString(payload, keys[i]);
			writeValue(payload, value->_value);
		}
		writeChunk(*saveFile, "VARS", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		payload.writeByte(_loopsPaused ? 1 : 0);
		payload.writeUint32LE((uint32)_scheduledLoops.size());
		for (uint i = 0; i < _scheduledLoops.size(); ++i) {
			const ScheduledLoop &loop = _scheduledLoops[i];
			writeSaveString(payload, loop.kind);
			writeSaveString(payload, loop.target);
			writeSaveString(payload, loop.message);
			const uint32 remaining = ((int32)(loop.dueMillis - now) > 0) ? loop.dueMillis - now : 0;
			payload.writeUint32LE(remaining);
		}
		writeChunk(*saveFile, "LOOP", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		payload.writeByte(_cricketsPaused ? 1 : 0);
		payload.writeUint32LE((uint32)_crickets.size());
		for (uint i = 0; i < _crickets.size(); ++i) {
			writeSaveString(payload, _crickets[i].name);
			payload.writeByte(_crickets[i].paused ? 1 : 0);
		}
		writeChunk(*saveFile, "CRIK", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		payload.writeUint32LE(0); // Cast/actor objects are not modelled yet; native save bucket retained.
		writeChunk(*saveFile, "CAST", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		payload.writeUint32LE(0); // Puppet objects are not modelled yet; native save bucket retained.
		writeChunk(*saveFile, "PUPP", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		writeChunk(*saveFile, "END ", payload);
	}

	getMetaEngine()->appendExtendedSave(saveFile.get(), getTotalPlayTime() / 1000, desc, isAutosave);
	if (saveFile->err())
		return Common::Error(Common::kWritingFailed, getSaveStateName(slot));
	return Common::kNoError;
}

} // End of namespace Cyberflix

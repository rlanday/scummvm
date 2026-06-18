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
#include "common/translation.h"
#include "common/util.h"

#include "engines/metaengine.h"

#include "gui/message.h"
#include "gui/saveload.h"

#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "audio/mixer.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/detection.h"
#include "cyberflix/set.h"
#include "cyberflix/cast.h"
#include "cyberflix/shop.h"
#include "cyberflix/audio/cbx_audio.h"
#include "cyberflix/stage.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

typedef AudioRuntime::ThemeTrack ThemeTrack;

enum {
	kCyberflixSaveVersion = 1
};

static void writeSaveString(Common::WriteStream &out, const Common::String &s) {
	out.writeUint32LE(static_cast<uint32>(s.size()));
	if (!s.empty())
		out.write(s.c_str(), static_cast<uint32>(s.size()));
}

static void writeSaveData(Common::WriteStream &out, const Common::Array<byte> &data) {
	out.writeUint32LE(static_cast<uint32>(data.size()));
	if (!data.empty())
		out.write(data.begin(), static_cast<uint32>(data.size()));
}

static void writeValue(Common::WriteStream &out, const Value &value) {
	out.writeUint32LE(static_cast<uint32>(value.type));
	out.writeSint32LE(value.intValue);
	writeSaveString(out, value.strValue);
}

static bool readSaveString(Common::SeekableReadStream &in, int64 end, Common::String &s) {
	if (in.pos() + 4 > end)
		return false;
	uint32 len = in.readUint32LE();
	if (in.pos() + len > end)
		return false;
	s.clear();
	if (!len)
		return !in.err();
	Common::Array<char> buf;
	buf.resize(len);
	return in.read(buf.begin(), len) == len && (s = Common::String(buf.begin(), len), true);
}

static bool readSaveData(Common::SeekableReadStream &in, int64 end, Common::Array<byte> &data) {
	if (in.pos() + 4 > end)
		return false;
	uint32 len = in.readUint32LE();
	if (in.pos() + len > end)
		return false;
	data.clear();
	data.resize(len);
	if (!len)
		return !in.err();
	return in.read(data.begin(), len) == len;
}

static bool readValue(Common::SeekableReadStream &in, int64 end, Value &value) {
	if (in.pos() + 8 > end)
		return false;
	uint32 type = in.readUint32LE();
	if (type > Value::kBool)
		return false;
	value.type = static_cast<Value::Type>(type);
	value.intValue = in.readSint32LE();
	return readSaveString(in, end, value.strValue);
}

static bool readChunkHeader(Common::SeekableReadStream &in, char tag[5], int64 &end) {
	if (in.pos() + 8 > in.size())
		return false;
	if (in.read(tag, 4) != 4)
		return false;
	tag[4] = 0;
	uint32 size = in.readUint32LE();
	end = in.pos() + size;
	return end <= in.size();
}

static void writeChunk(Common::WriteStream &out, const char tag[4], Common::MemoryWriteStreamDynamic &payload) {
	out.write(tag, 4);
	out.writeUint32LE(static_cast<uint32>(payload.size()));
	if (payload.size())
		out.write(payload.getData(), static_cast<uint32>(payload.size()));
}

static Common::String defaultSaveSignature(int gameType) {
	if (gameType == GType_Titanic)
		return "Titanic 1.0";
	return Common::String();
}

static bool isLoadedReplacementStage(const Common::SharedPtr<Stage> &stage) {
	return stage && stage->isOpen() && !stage->name().equalsIgnoreCase("main.stg");
}

static bool isTourMode(const ScriptVM &vm) {
	Common::HashMap<Common::String, Value>::const_iterator it = vm.globalVars().find("tour");
	return it != vm.globalVars().end() &&
			(it->_value.type == Value::kBool || it->_value.type == Value::kInt) &&
			it->_value.intValue != 0;
}

struct HeaderState {
	bool seen = false;
	Common::String signature;
	Common::String description;
	Common::String gameId;
	uint32 platform = 0;
	uint32 language = 0;
	uint32 gameType = 0;
	Common::String activeCursor;
	Common::String hitKind;
	uint16 actionFrameMask = 0;
	Common::String stageName;
	int32 stageNode = 0;
	Common::String flatName;
	Common::String setFileName;
	Common::String setName;
	int32 setScene = -1;
	Common::String sceneName;
	int32 setTable = 0;
	int32 setAngle = 0;
	Common::String setView;
	bool setVisible = false;
	uint32 setTransitionType = 0;
	uint32 setTransitionResource = 0;
	uint32 setTransitionFrame = 0;
};

struct PropState {
	Common::String name;
	Common::String setName;
	Common::String sceneName;
	uint32 masterResId = 0;
	uint32 scriptResId = 0;
	bool visible = false;
	uint16 mode = 0;
	int16 y = 0;
	int16 x = 0;
	int16 z = 0;
	int16 angle = 0;
	int16 depth = 0;
	int32 scale = 1000;
	int32 zClip = 0;
	int32 value = 0;
	Common::String shapeName;
	Common::String owner;
};

struct ShopState {
	Common::String name;
	Common::Array<PropState> props;
};

struct ActorState {
	Common::String name;
	Common::String setName;
	Common::String sceneName;
	Common::String shapeName;
	Common::String owner;
	uint32 masterResId = 0;
	uint32 scriptResId = 0;
	bool visible = false;
	int16 x = 0;
	int16 y = 0;
	int16 z = 0;
	int16 angle = 0;
	int32 scale = 1000;
	int32 zClip = 0;
	int32 speed = 0;
	int32 turn = 0;
	int32 value = 0;
};

struct CastState {
	Common::String name;
	Common::Array<ActorState> actors;
};

struct CueState {
	Common::String name;
	uint32 resId = 0;
	byte flags = 0;
	uint32 dataOffset = 0;
	uint32 length = 0;
};

struct TrackState {
	Common::String sourceName;
	Common::String name;
	Common::Array<byte> fileData;
	Common::Array<uint16> playlist;
	Common::Array<CueState> cues;
	Common::Array<CueState> sfxCues;
	uint32 loopIdx = 0;
	int32 volume = 255;
};

struct ThemeSpanState {
	uint32 startSample = 0;
	Common::String name;
};

struct SoundSlotState {
	bool active = false;
	Common::String cueName;
	uint32 resId = 0;
	uint32 elapsedMillis = 0;
};

struct AudioState {
	bool seen = false;
	bool themeActive = false;
	Common::String themeTrack;
	uint32 themeElapsedMillis = 0;
	uint32 themeIntroSamples = 0;
	uint32 themeLoopSamples = 0;
	Common::Array<ThemeSpanState> themeSpans;
	SoundSlotState soundSlots[2];
	SoundSlotState voiceSlot;
};

struct RuntimeSettingsState {
	bool seen = false;
	int32 waveVolumeLevel = 9;
	bool keyAborts = false;
};

struct CueVolumeState {
	Common::String trackName;
	Common::String cueName;
	int32 volume = 255;
};

static bool parseHeaderChunk(Common::SeekableReadStream &in, int64 end, HeaderState &header) {
	header.seen = true;
	if (!readSaveString(in, end, header.signature) ||
			!readSaveString(in, end, header.description) ||
			!readSaveString(in, end, header.gameId) ||
			in.pos() + 18 > end)
		return false;
	header.platform = in.readUint32LE();
	header.language = in.readUint32LE();
	header.gameType = in.readUint32LE();
	if (!readSaveString(in, end, header.activeCursor) ||
			!readSaveString(in, end, header.hitKind) ||
			in.pos() + 2 > end)
		return false;
	header.actionFrameMask = in.readUint16LE();
	if (!readSaveString(in, end, header.stageName) ||
			in.pos() + 4 > end)
		return false;
	header.stageNode = in.readSint32LE();
	if (!readSaveString(in, end, header.flatName) ||
			!readSaveString(in, end, header.setFileName) ||
			!readSaveString(in, end, header.setName) ||
			in.pos() + 4 > end)
		return false;
	header.setScene = in.readSint32LE();
	if (!readSaveString(in, end, header.sceneName) ||
			in.pos() + 21 > end)
		return false;
	header.setTable = in.readSint32LE();
	header.setAngle = in.readSint32LE();
	if (!readSaveString(in, end, header.setView) ||
			in.pos() + 13 > end)
		return false;
	header.setVisible = in.readByte() != 0;
	header.setTransitionType = in.readUint32LE();
	header.setTransitionResource = in.readUint32LE();
	header.setTransitionFrame = in.readUint32LE();
	return !in.err();
}

static bool parsePathChunk(Common::SeekableReadStream &in, int64 end,
		Common::String (&pathSlots)[PathRuntime::kPathSlotCount]) {
	for (uint i = 0; i < PathRuntime::kPathSlotCount; ++i)
		if (!readSaveString(in, end, pathSlots[i]))
			return false;
	return !in.err();
}

static bool parsePaletteChunk(Common::SeekableReadStream &in, int64 end,
		byte (&screenClut)[256 * 3], double (&paletteGamma)[3]) {
	if (in.pos() + 256 * 3 + 3 * 8 > end)
		return false;
	if (in.read(screenClut, sizeof(screenClut)) != sizeof(screenClut))
		return false;
	for (uint i = 0; i < 3; ++i)
		paletteGamma[i] = in.readDoubleLE();
	return !in.err();
}

static bool parseShopChunk(Common::SeekableReadStream &in, int64 end, Common::Array<ShopState> &shops) {
	if (in.pos() + 4 > end)
		return false;
	uint32 shopCount = in.readUint32LE();
	shops.clear();
	for (uint32 s = 0; s < shopCount; ++s) {
		ShopState shop;
		if (!readSaveString(in, end, shop.name) || in.pos() + 4 > end)
			return false;
		uint32 propCount = in.readUint32LE();
		for (uint32 p = 0; p < propCount; ++p) {
			PropState prop;
			if (!readSaveString(in, end, prop.name) ||
					!readSaveString(in, end, prop.setName) ||
					!readSaveString(in, end, prop.sceneName) ||
					in.pos() + 35 > end)
				return false;
			prop.masterResId = in.readUint32LE();
			prop.scriptResId = in.readUint32LE();
			prop.visible = in.readByte() != 0;
			prop.mode = in.readUint16LE();
			prop.y = in.readSint16LE();
			prop.x = in.readSint16LE();
			prop.z = in.readSint16LE();
			prop.angle = in.readSint16LE();
			prop.depth = in.readSint16LE();
			prop.scale = in.readSint32LE();
			prop.zClip = in.readSint32LE();
			prop.value = in.readSint32LE();
			if (!readSaveString(in, end, prop.shapeName) ||
					!readSaveString(in, end, prop.owner) ||
					in.pos() + 4 > end)
				return false;
			uint32 shapeCount = in.readUint32LE();
			for (uint32 i = 0; i < shapeCount; ++i) {
				Common::String ignoredShapeName;
				if (in.pos() + 4 > end)
					return false;
				in.readUint32LE();
				if (!readSaveString(in, end, ignoredShapeName))
					return false;
			}
			shop.props.push_back(prop);
		}
		shops.push_back(shop);
	}
	return !in.err();
}

static bool parseCastChunk(Common::SeekableReadStream &in, int64 end, Common::Array<CastState> &casts) {
	if (in.pos() + 4 > end)
		return false;
	uint32 castCount = in.readUint32LE();
	casts.clear();
	for (uint32 c = 0; c < castCount; ++c) {
		CastState cast;
		if (!readSaveString(in, end, cast.name) || in.pos() + 4 > end)
			return false;
		uint32 actorCount = in.readUint32LE();
		for (uint32 a = 0; a < actorCount; ++a) {
			ActorState actor;
			if (!readSaveString(in, end, actor.name) ||
					!readSaveString(in, end, actor.setName) ||
					!readSaveString(in, end, actor.sceneName) ||
					!readSaveString(in, end, actor.shapeName) ||
					!readSaveString(in, end, actor.owner) ||
					in.pos() + 35 > end)
				return false;
			actor.masterResId = in.readUint32LE();
			actor.scriptResId = in.readUint32LE();
			actor.visible = in.readByte() != 0;
			actor.x = in.readSint16LE();
			actor.y = in.readSint16LE();
			actor.z = in.readSint16LE();
			actor.angle = in.readSint16LE();
			actor.scale = in.readSint32LE();
			actor.zClip = in.readSint32LE();
			actor.speed = in.readSint32LE();
			actor.turn = in.readSint32LE();
			actor.value = in.readSint32LE();
			cast.actors.push_back(actor);
		}
		casts.push_back(cast);
	}
	return !in.err();
}

static bool parseCueArray(Common::SeekableReadStream &in, int64 end, Common::Array<CueState> &cues) {
	if (in.pos() + 4 > end)
		return false;
	uint32 count = in.readUint32LE();
	cues.clear();
	for (uint32 i = 0; i < count; ++i) {
		CueState cue;
		if (!readSaveString(in, end, cue.name) || in.pos() + 17 > end)
			return false;
		cue.resId = in.readUint32LE();
		cue.flags = in.readByte();
		cue.dataOffset = in.readUint32LE();
		cue.length = in.readUint32LE();
		cues.push_back(cue);
	}
	return !in.err();
}

static bool parseTrackChunk(Common::SeekableReadStream &in, int64 end, Common::Array<TrackState> &tracks) {
	if (in.pos() + 4 > end)
		return false;
	uint32 count = in.readUint32LE();
	tracks.clear();
	for (uint32 t = 0; t < count; ++t) {
		TrackState track;
		if (!readSaveString(in, end, track.sourceName) ||
				!readSaveString(in, end, track.name) ||
				!readSaveData(in, end, track.fileData) ||
				in.pos() + 16 > end)
			return false;
		track.loopIdx = in.readUint32LE();
		track.volume = in.readSint32LE();
		uint32 playlistCount = in.readUint32LE();
		if (in.pos() + playlistCount * 2 > end)
			return false;
		for (uint32 i = 0; i < playlistCount; ++i)
			track.playlist.push_back(in.readUint16LE());
		if (!parseCueArray(in, end, track.cues) ||
				!parseCueArray(in, end, track.sfxCues))
			return false;
		tracks.push_back(track);
	}
	return !in.err();
}

static bool parseSoundSlot(Common::SeekableReadStream &in, int64 end, SoundSlotState &slot) {
	if (in.pos() + 1 > end)
		return false;
	slot.active = in.readByte() != 0;
	return readSaveString(in, end, slot.cueName) &&
			in.pos() + 8 <= end &&
			(slot.resId = in.readUint32LE(), slot.elapsedMillis = in.readUint32LE(), !in.err());
}

static bool parseAudioChunk(Common::SeekableReadStream &in, int64 end, AudioState &audio) {
	audio.seen = true;
	if (in.pos() + 1 > end)
		return false;
	audio.themeActive = in.readByte() != 0;
	if (!readSaveString(in, end, audio.themeTrack) || in.pos() + 16 > end)
		return false;
	audio.themeElapsedMillis = in.readUint32LE();
	audio.themeIntroSamples = in.readUint32LE();
	audio.themeLoopSamples = in.readUint32LE();
	uint32 spanCount = in.readUint32LE();
	audio.themeSpans.clear();
	for (uint32 i = 0; i < spanCount; ++i) {
		ThemeSpanState span;
		if (in.pos() + 4 > end)
			return false;
		span.startSample = in.readUint32LE();
		if (!readSaveString(in, end, span.name))
			return false;
		audio.themeSpans.push_back(span);
	}
	return parseSoundSlot(in, end, audio.soundSlots[0]) &&
			parseSoundSlot(in, end, audio.soundSlots[1]) &&
			parseSoundSlot(in, end, audio.voiceSlot);
}

static bool parseSettingsChunk(Common::SeekableReadStream &in, int64 end,
		RuntimeSettingsState &settings) {
	settings.seen = true;
	if (in.pos() + 5 > end)
		return false;
	settings.waveVolumeLevel = in.readSint32LE();
	settings.keyAborts = in.readByte() != 0;
	return !in.err();
}

static bool parseCueVolumeChunk(Common::SeekableReadStream &in, int64 end,
		Common::Array<CueVolumeState> &cueVolumes) {
	if (in.pos() + 4 > end)
		return false;
	uint32 count = in.readUint32LE();
	cueVolumes.clear();
	for (uint32 i = 0; i < count; ++i) {
		CueVolumeState cueVolume;
		if (!readSaveString(in, end, cueVolume.trackName) ||
				!readSaveString(in, end, cueVolume.cueName) ||
				in.pos() + 4 > end)
			return false;
		cueVolume.volume = in.readSint32LE();
		cueVolumes.push_back(cueVolume);
	}
	return !in.err();
}

static bool parseVarsChunk(Common::SeekableReadStream &in, int64 end,
		Common::HashMap<Common::String, Value> &vars) {
	if (in.pos() + 4 > end)
		return false;
	uint32 count = in.readUint32LE();
	vars.clear();
	for (uint32 i = 0; i < count; ++i) {
		Common::String key;
		Value value;
		if (!readSaveString(in, end, key) || !readValue(in, end, value))
			return false;
		vars[key] = value;
	}
	return !in.err();
}

static bool parseLoopChunk(Common::SeekableReadStream &in, int64 end,
		bool &loopsPaused, Common::Array<LoopRuntime::ScheduledLoop> &loops) {
	if (in.pos() + 5 > end)
		return false;
	loopsPaused = in.readByte() != 0;
	uint32 count = in.readUint32LE();
	loops.clear();
	for (uint32 i = 0; i < count; ++i) {
		LoopRuntime::ScheduledLoop loop;
		if (!readSaveString(in, end, loop.kind) ||
				!readSaveString(in, end, loop.target) ||
				!readSaveString(in, end, loop.message) ||
				in.pos() + 4 > end)
			return false;
		loop.kindId = LoopRuntime::scheduledLoopKind(loop.kind);
		loop.remainingPasses = static_cast<int32>(in.readUint32LE());
		loops.push_back(loop);
	}
	return !in.err();
}

static bool parseCricketChunk(Common::SeekableReadStream &in, int64 end,
		bool &cricketsPaused, Common::Array<LoopRuntime::CricketState> &crickets) {
	if (in.pos() + 5 > end)
		return false;
	cricketsPaused = in.readByte() != 0;
	uint32 count = in.readUint32LE();
	crickets.clear();
	for (uint32 i = 0; i < count; ++i) {
		LoopRuntime::CricketState cricket;
		if (!readSaveString(in, end, cricket.name) || in.pos() + 1 > end)
			return false;
		cricket.paused = in.readByte() != 0;
		crickets.push_back(cricket);
	}
	return !in.err();
}

static void restoreShopState(PropRuntime &propRuntime, const Common::Array<ShopState> &shopStates) {
	for (uint i = 0; i < shopStates.size(); ++i) {
		Common::SharedPtr<Shop> shop(new Shop());
		if (!shop->open(shopStates[i].name)) {
			warning("Cyberflix: load could not reopen shop '%s'", shopStates[i].name.c_str());
			continue;
		}
		for (uint p = 0; p < shopStates[i].props.size(); ++p) {
			const PropState &state = shopStates[i].props[p];
			Shop::Prop *prop = shop->findProp(state.name);
			if (!prop) {
				warning("Cyberflix: load shop '%s' missing prop '%s'",
						shopStates[i].name.c_str(), state.name.c_str());
				continue;
			}
			prop->setName = state.setName;
			prop->sceneName = state.sceneName;
			prop->visible = state.visible;
			prop->mode = state.mode;
			prop->y = state.y;
			prop->x = state.x;
			prop->z = state.z;
			prop->angle = state.angle;
			prop->depth = state.depth;
			prop->scale = state.scale;
			prop->zClip = state.zClip;
			prop->value = state.value;
			prop->shapeName = state.shapeName;
			prop->owner = state.owner;
		}
		propRuntime.shops().push_back(shop);
	}
}

static void restoreCastState(ActorRuntime &actorRuntime, const Common::Array<CastState> &castStates) {
	for (uint i = 0; i < castStates.size(); ++i) {
		Common::SharedPtr<Cast> cast(new Cast());
		if (!cast->open(castStates[i].name)) {
			warning("Cyberflix: load could not reopen cast '%s'", castStates[i].name.c_str());
			continue;
		}
		for (uint a = 0; a < castStates[i].actors.size(); ++a) {
			const ActorState &state = castStates[i].actors[a];
			Common::SharedPtr<Cast::Actor> actor = cast->findActor(state.name);
			if (!actor) {
				warning("Cyberflix: load cast '%s' missing actor '%s'",
						castStates[i].name.c_str(), state.name.c_str());
				continue;
			}
			actor->setName = state.setName;
			actor->sceneName = state.sceneName;
			actor->shapeName = state.shapeName;
			actor->owner = state.owner;
			actor->visible = state.visible;
			actor->x = state.x;
			actor->y = state.y;
			actor->z = state.z;
			actor->angle = state.angle;
			actor->scale = state.scale;
			actor->zClip = state.zClip;
			actor->speed = state.speed;
			actor->turn = state.turn;
			actor->value = state.value;
		}
		actorRuntime.casts().push_back(cast);
	}
}

static void restoreTitanicLegacyCastState(CyberflixEngine &engine) {
	// Early ScummVM CyberFlix saves wrote an empty CAST chunk. Native Titanic
	// keeps GANG.CST open after boot (`opencastfile("gang.cst")`, then
	// `sendtocast("gang.cst", initactors())`), and room scripts such as
	// GYM.SET's openset() assume sendtoactor("penny", ...) can find that global
	// actor table. Recreate that boot-time cast state only for those legacy saves;
	// modern saves with real CAST contents restore exact per-actor runtime fields.
	engine.actorRuntime().openCastFile(engine, "gang.cst");
	Common::Array<Value> noArgs;
	engine.actorRuntime().sendToCast(engine, "gang.cst", "initactors", noArgs);
}

static void restoreTrackState(AudioRuntime &audioRuntime, const Common::Array<TrackState> &trackStates) {
	for (uint i = 0; i < trackStates.size(); ++i) {
		Common::SharedPtr<ThemeTrack> track(new ThemeTrack());
		track->sourceName = trackStates[i].sourceName;
		track->name = trackStates[i].name;
		track->fileData = trackStates[i].fileData;
		track->playlist = trackStates[i].playlist;
		track->loopIdx = trackStates[i].loopIdx;
		track->volume = trackStates[i].volume;
		for (uint c = 0; c < trackStates[i].cues.size(); ++c) {
			ThemeTrack::Cue cue;
			cue.name = trackStates[i].cues[c].name;
			cue.resId = trackStates[i].cues[c].resId;
			cue.flags = trackStates[i].cues[c].flags;
			cue.dataOffset = trackStates[i].cues[c].dataOffset;
			cue.length = trackStates[i].cues[c].length;
			track->cues.push_back(cue);
		}
		for (uint c = 0; c < trackStates[i].sfxCues.size(); ++c) {
			ThemeTrack::Cue cue;
			cue.name = trackStates[i].sfxCues[c].name;
			cue.resId = trackStates[i].sfxCues[c].resId;
			cue.flags = trackStates[i].sfxCues[c].flags;
			cue.dataOffset = trackStates[i].sfxCues[c].dataOffset;
			cue.length = trackStates[i].sfxCues[c].length;
			track->sfxCues.push_back(cue);
		}
		audioRuntime.tracks().push_back(track);
	}
}

static void restoreCueVolumes(AudioRuntime &audioRuntime, const Common::Array<CueVolumeState> &cueVolumeStates) {
	for (uint i = 0; i < cueVolumeStates.size(); ++i) {
		for (uint t = 0; t < audioRuntime.tracks().size(); ++t) {
			if (!cueVolumeStates[i].trackName.empty() &&
					!audioRuntime.tracks()[t]->name.equalsIgnoreCase(cueVolumeStates[i].trackName) &&
					!audioRuntime.tracks()[t]->sourceName.equalsIgnoreCase(cueVolumeStates[i].trackName))
				continue;
			for (uint c = 0; c < audioRuntime.tracks()[t]->sfxCues.size(); ++c) {
				if (audioRuntime.tracks()[t]->sfxCues[c].name.equalsIgnoreCase(cueVolumeStates[i].cueName))
					audioRuntime.tracks()[t]->sfxCues[c].volume = CLIP(cueVolumeStates[i].volume, 0, 255);
			}
		}
	}
}

static void restoreLoopState(LoopRuntime &loopRuntime, bool loopsPaused,
		const Common::Array<LoopRuntime::ScheduledLoop> &loopStates,
		bool cricketsPaused, const Common::Array<LoopRuntime::CricketState> &cricketStates) {
	loopRuntime.setLoopsPaused(loopsPaused);
	for (uint i = 0; i < loopStates.size(); ++i)
		loopRuntime.restoreLoop(loopStates[i]);

	loopRuntime.setCricketsPaused(cricketsPaused);
	for (uint i = 0; i < cricketStates.size(); ++i)
		loopRuntime.restoreCricket(cricketStates[i]);
}

static void writeCueArray(Common::WriteStream &out, const Common::Array<ThemeTrack::Cue> &cues) {
	out.writeUint32LE(static_cast<uint32>(cues.size()));
	for (uint i = 0; i < cues.size(); ++i) {
		writeSaveString(out, cues[i].name);
		out.writeUint32LE(cues[i].resId);
		out.writeByte(cues[i].flags);
		out.writeUint32LE(cues[i].dataOffset);
		out.writeUint32LE(cues[i].length);
	}
}

static void writeInactiveSoundSlot(Common::WriteStream &out) {
	out.writeByte(0);
	writeSaveString(out, Common::String());
	out.writeUint32LE(0);
	out.writeUint32LE(0);
}

static void writeShopChunk(Common::WriteStream &out,
		const Common::Array<Common::SharedPtr<Shop> > &shops) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	payload.writeUint32LE(static_cast<uint32>(shops.size()));
	for (uint s = 0; s < shops.size(); ++s) {
		const Shop &shop = *shops[s];
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
			payload.writeUint32LE(static_cast<uint32>(prop.shapes.size()));
			for (uint i = 0; i < prop.shapes.size(); ++i) {
				payload.writeUint32LE(prop.shapes[i].resId);
				writeSaveString(payload, prop.shapes[i].name);
			}
		}
	}
	writeChunk(out, "SHOP", payload);
}

static void writeCastChunk(Common::WriteStream &out,
		const Common::Array<Common::SharedPtr<Cast> > &casts) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	payload.writeUint32LE(static_cast<uint32>(casts.size()));
	for (uint c = 0; c < casts.size(); ++c) {
		writeSaveString(payload, casts[c]->name());
		payload.writeUint32LE(casts[c]->actorCount());
		for (uint a = 0; a < casts[c]->actorCount(); ++a) {
			const Cast::Actor &actor = casts[c]->actor(a);
			writeSaveString(payload, actor.name);
			writeSaveString(payload, actor.setName);
			writeSaveString(payload, actor.sceneName);
			writeSaveString(payload, actor.shapeName);
			writeSaveString(payload, actor.owner);
			payload.writeUint32LE(actor.masterResId);
			payload.writeUint32LE(actor.scriptResId);
			payload.writeByte(actor.visible ? 1 : 0);
			payload.writeSint16LE(actor.x);
			payload.writeSint16LE(actor.y);
			payload.writeSint16LE(actor.z);
			payload.writeSint16LE(actor.angle);
			payload.writeSint32LE(actor.scale);
			payload.writeSint32LE(actor.zClip);
			payload.writeSint32LE(actor.speed);
			payload.writeSint32LE(actor.turn);
			payload.writeSint32LE(actor.value);
		}
	}
	writeChunk(out, "CAST", payload);
}

static void writeTrackChunk(Common::WriteStream &out,
		const Common::Array<Common::SharedPtr<ThemeTrack> > &tracks) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	payload.writeUint32LE(static_cast<uint32>(tracks.size()));
	for (uint t = 0; t < tracks.size(); ++t) {
		const ThemeTrack &track = *tracks[t];
		writeSaveString(payload, track.sourceName);
		writeSaveString(payload, track.name);
		writeSaveData(payload, track.fileData);
		payload.writeUint32LE(track.loopIdx);
		payload.writeSint32LE(track.volume);
		payload.writeUint32LE(static_cast<uint32>(track.playlist.size()));
		for (uint i = 0; i < track.playlist.size(); ++i)
			payload.writeUint16LE(track.playlist[i]);
		writeCueArray(payload, track.cues);
		writeCueArray(payload, track.sfxCues);
	}
	writeChunk(out, "TRAK", payload);
}

static void writeCueVolumeChunk(Common::WriteStream &out,
		const Common::Array<Common::SharedPtr<ThemeTrack> > &tracks) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	uint32 count = 0;
	for (uint t = 0; t < tracks.size(); ++t)
		count += tracks[t]->sfxCues.size();
	payload.writeUint32LE(count);
	for (uint t = 0; t < tracks.size(); ++t) {
		const ThemeTrack &track = *tracks[t];
		for (uint c = 0; c < track.sfxCues.size(); ++c) {
			writeSaveString(payload, track.name);
			writeSaveString(payload, track.sfxCues[c].name);
			payload.writeSint32LE(track.sfxCues[c].volume);
		}
	}
	writeChunk(out, "SVOL", payload);
}

static void writeAudioChunk(Common::WriteStream &out, const AudioRuntime &audioRuntime,
		Audio::Mixer *mixer) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	const bool themeActive = !audioRuntime.themeTrackName().empty() &&
			mixer->isSoundHandleActive(audioRuntime.themeHandle());
	const uint32 themeElapsedMillis = themeActive ?
			mixer->getSoundElapsedTime(audioRuntime.themeHandle()) +
			static_cast<uint32>((static_cast<uint64>(audioRuntime.themeStartSample()) * 1000 / kAudioSampleRate)) : 0;
	payload.writeByte(themeActive ? 1 : 0);
	writeSaveString(payload, themeActive ? audioRuntime.themeTrackName() : Common::String());
	payload.writeUint32LE(themeElapsedMillis);
	payload.writeUint32LE(audioRuntime.themeIntroSamples());
	payload.writeUint32LE(audioRuntime.themeLoopSamples());
	payload.writeUint32LE(static_cast<uint32>(audioRuntime.themeSpans().size()));
	for (uint i = 0; i < audioRuntime.themeSpans().size(); ++i) {
		payload.writeUint32LE(audioRuntime.themeSpans()[i].startSample);
		writeSaveString(payload, audioRuntime.themeSpans()[i].name);
	}
	// Keep the chunk fields stable, but match native persistence: live
	// one-shot SFX/voice channels are not part of the durable save graph.
	writeInactiveSoundSlot(payload);
	writeInactiveSoundSlot(payload);
	writeInactiveSoundSlot(payload);

	writeChunk(out, "AUDI", payload);
}

static void writeVarsChunk(Common::WriteStream &out, const Common::HashMap<Common::String, Value> &vars) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	Common::Array<Common::String> keys;
	for (Common::HashMap<Common::String, Value>::const_iterator it = vars.begin(); it != vars.end(); ++it)
		keys.push_back(it->_key);
	Common::sort(keys.begin(), keys.end());
	payload.writeUint32LE(static_cast<uint32>(keys.size()));
	for (uint i = 0; i < keys.size(); ++i) {
		Common::HashMap<Common::String, Value>::const_iterator value = vars.find(keys[i]);
		writeSaveString(payload, keys[i]);
		writeValue(payload, value->_value);
	}
	writeChunk(out, "VARS", payload);
}

static void writeLoopChunk(Common::WriteStream &out, const LoopRuntime &loopRuntime) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	const Common::Array<LoopRuntime::ScheduledLoop> &loops = loopRuntime.scheduledLoops();
	payload.writeByte(loopRuntime.loopsPaused() ? 1 : 0);
	payload.writeUint32LE(static_cast<uint32>(loops.size()));
	for (uint i = 0; i < loops.size(); ++i) {
		const LoopRuntime::ScheduledLoop &loop = loops[i];
		writeSaveString(payload, loop.kind);
		writeSaveString(payload, loop.target);
		writeSaveString(payload, loop.message);
		payload.writeUint32LE(loop.remainingPasses > 0 ? static_cast<uint32>(loop.remainingPasses): 0);
	}
	writeChunk(out, "LOOP", payload);
}

static void writeCricketChunk(Common::WriteStream &out, const LoopRuntime &loopRuntime) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	const Common::Array<LoopRuntime::CricketState> &crickets = loopRuntime.crickets();
	payload.writeByte(loopRuntime.cricketsPaused() ? 1 : 0);
	payload.writeUint32LE(static_cast<uint32>(crickets.size()));
	for (uint i = 0; i < crickets.size(); ++i) {
		writeSaveString(payload, crickets[i].name);
		payload.writeByte(crickets[i].paused ? 1 : 0);
	}
	writeChunk(out, "CRIK", payload);
}

static void writeEmptyCountChunk(Common::WriteStream &out, const char tag[4]) {
	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	payload.writeUint32LE(0);
	writeChunk(out, tag, payload);
}

bool CyberflixEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	if (isTourMode(_vm)) {
		if (msg)
			*msg = _("You can't save your game during the tour.");
		return false;
	}
	return (stageRuntime().stage() && stageRuntime().stage()->isOpen()) ||
			(setRuntime().set() && setRuntime().set()->isOpen());
}

bool CyberflixEngine::canSaveAutosaveCurrently() {
	return !isTourMode(_vm) && canSaveGameStateCurrently();
}

bool CyberflixEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	return true;
}

void CyberflixEngine::saveGame(const Common::String &signature) {
	Common::String oldSignature = _saveSignature;
	_saveSignature = signature;
	saveGameDialog();
	_saveSignature = oldSignature;
}

void CyberflixEngine::openGame(const Common::String &signature) {
	if (!canLoadGameStateCurrently()) {
		g_system->displayMessageOnOSD(_("Loading game is currently unavailable"));
		return;
	}

	Common::ScopedPtr<GUI::SaveLoadChooser> dialog(new GUI::SaveLoadChooser(false));
	int slotNum;
	{
		PauseToken pt = pauseEngine();
		slotNum = dialog->runModalWithCurrentTarget();
	}

	if (slotNum < 0)
		return;

	_pendingLoadSlot = slotNum;
	_pendingLoadSignature = signature;
}

bool CyberflixEngine::processPendingLoad() {
	if (_pendingLoadSlot < 0)
		return false;

	int slot = _pendingLoadSlot;
	Common::String signature = _pendingLoadSignature;
	_pendingLoadSlot = -1;
	_pendingLoadSignature.clear();

	Common::String oldSignature = _saveSignature;
	_saveSignature = signature;
	Common::Error loadError = loadGameState(slot);
	_saveSignature = oldSignature;

	if (loadError.getCode() != Common::kNoError) {
		GUI::MessageDialog errorDialog(loadError.getDesc());
		errorDialog.runModal();
		return false;
	}

	return true;
}

Common::Error CyberflixEngine::loadGameState(int slot) {
	Common::InSaveFile *inFile = _saveFileMan->openForLoading(getSaveStateName(slot));
	if (!inFile)
		return Common::Error(Common::kReadingFailed, getSaveStateName(slot));
	Common::ScopedPtr<Common::InSaveFile> saveFile(inFile);

	char magic[5] = {};
	if (saveFile->read(magic, 4) != 4 || memcmp(magic, "CFXS", 4))
		return Common::Error(Common::kReadingFailed, "Not a CyberFlix save");
	uint32 version = saveFile->readUint32LE();
	if (version != kCyberflixSaveVersion)
		return Common::Error(Common::kReadingFailed, "Unsupported CyberFlix save version");

	HeaderState header;
	Common::String pathSlots[PathRuntime::kPathSlotCount];
	byte savedClut[256 * 3] = {};
	double savedGamma[3] = { 0.65, 0.65, 0.65 };
	Common::Array<ShopState> shopStates;
	Common::Array<CastState> castStates;
	Common::Array<TrackState> trackStates;
	AudioState audioState;
	RuntimeSettingsState settingsState;
	Common::Array<CueVolumeState> cueVolumeStates;
	Common::HashMap<Common::String, Value> vars;
	bool varsSeen = false;
	bool loopsPaused = false;
	Common::Array<LoopRuntime::ScheduledLoop> loopStates;
	bool cricketsPaused = false;
	Common::Array<LoopRuntime::CricketState> cricketStates;
	bool sawEnd = false;

	while (!saveFile->eos() && !saveFile->err()) {
		char tag[5];
		int64 end = 0;
		if (!readChunkHeader(*saveFile, tag, end))
			return Common::Error(Common::kReadingFailed, "Corrupt CyberFlix save chunk");

		bool ok = true;
		if (!strcmp(tag, "HEAD")) {
			ok = parseHeaderChunk(*saveFile, end, header);
		} else if (!strcmp(tag, "PATH")) {
			ok = parsePathChunk(*saveFile, end, pathSlots);
		} else if (!strcmp(tag, "PAL ")) {
			ok = parsePaletteChunk(*saveFile, end, savedClut, savedGamma);
		} else if (!strcmp(tag, "SHOP")) {
			ok = parseShopChunk(*saveFile, end, shopStates);
		} else if (!strcmp(tag, "CAST")) {
			ok = parseCastChunk(*saveFile, end, castStates);
		} else if (!strcmp(tag, "TRAK")) {
			ok = parseTrackChunk(*saveFile, end, trackStates);
		} else if (!strcmp(tag, "AUDI")) {
			ok = parseAudioChunk(*saveFile, end, audioState);
		} else if (!strcmp(tag, "SETT")) {
			ok = parseSettingsChunk(*saveFile, end, settingsState);
		} else if (!strcmp(tag, "SVOL")) {
			ok = parseCueVolumeChunk(*saveFile, end, cueVolumeStates);
		} else if (!strcmp(tag, "VARS")) {
			ok = parseVarsChunk(*saveFile, end, vars);
			varsSeen = ok;
		} else if (!strcmp(tag, "LOOP")) {
			ok = parseLoopChunk(*saveFile, end, loopsPaused, loopStates);
		} else if (!strcmp(tag, "CRIK")) {
			ok = parseCricketChunk(*saveFile, end, cricketsPaused, cricketStates);
		} else if (!strcmp(tag, "PUPP")) {
			// Native save buckets retained for subsystems not modeled yet.
		} else if (!strcmp(tag, "END ")) {
			sawEnd = true;
		}

		if (!ok || saveFile->pos() > end)
			return Common::Error(Common::kReadingFailed, Common::String::format("Corrupt CyberFlix save chunk %.4s", tag));
		saveFile->seek(end);
		if (sawEnd)
			break;
	}

	if (!header.seen || !sawEnd)
		return Common::Error(Common::kReadingFailed, "Incomplete CyberFlix save");

	Common::String expectedSignature = !_saveSignature.empty()
			? _saveSignature : defaultSaveSignature(getGameType());
	if (!expectedSignature.empty() && !header.signature.equalsIgnoreCase(expectedSignature))
		return Common::Error(Common::kReadingFailed, "Save signature does not match this game");

	haltTheme();
	haltSound(3);
	haltVoice();
	_audioRuntime.clearTracks();
	_propRuntime.clear();
	_actorRuntime.casts().clear();
	_loopRuntime.clear();
	stageRuntime().reset();
	setRuntime().reset();
	_propRuntime.setDirty(false);

	for (uint i = 0; i < ARRAYSIZE(pathSlots); ++i) {
		_pathRuntime.setPathSlot(i, pathSlots[i]);
	}

	_paletteRuntime.setCurrent(savedClut);
	for (uint i = 0; i < 3; ++i)
		_paletteRuntime.setGamma(i, savedGamma[i]);
	// The palette lookup table is cached for fade performance; after loading
	// saved gamma values it must be rebuilt before the restored CLUT is applied.
	_paletteRuntime.markGammaTableDirty();

	if (varsSeen) {
		_vm.globalVars().clear();
		for (Common::HashMap<Common::String, Value>::const_iterator it = vars.begin(); it != vars.end(); ++it)
			_vm.globalVars()[it->_key] = it->_value;
	}

	restoreShopState(_propRuntime, shopStates);
	restoreCastState(_actorRuntime, castStates);
	restoreTrackState(_audioRuntime, trackStates);

	if (settingsState.seen) {
		_audioRuntime.setWaveVolumeLevel(settingsState.waveVolumeLevel);
		_keyAborts = settingsState.keyAborts;
	} else {
		_audioRuntime.setWaveVolumeLevel(9);
		_keyAborts = false;
	}

	restoreCueVolumes(_audioRuntime, cueVolumeStates);

	auto restoreTheme = [&](const Common::String &name, uint32 elapsedMillis) {
		Common::SharedPtr<ThemeTrack> track = _audioRuntime.findTrackRef(name);
		if (!track || track->playlist.empty())
			return;

		const uint32 startSample = static_cast<uint32>((static_cast<uint64>(elapsedMillis) * kAudioSampleRate / 1000));
		_audioRuntime.startThemeStream(*this, track, startSample);
	};

	if (audioState.seen) {
		if (audioState.themeActive)
			restoreTheme(audioState.themeTrack, audioState.themeElapsedMillis);
		// Native save/load restores open TRKs and loop/runtime state, but not
		// transient one-shot SFX or voice playback buffers.
	}

	restoreLoopState(_loopRuntime, loopsPaused, loopStates, cricketsPaused, cricketStates);

	StageRuntime::Snapshot stageSnapshot;
	stageSnapshot.stageName = header.stageName;
	stageSnapshot.node = header.stageNode;
	stageSnapshot.flatName = header.flatName;
	stageSnapshot.visible = !header.stageName.empty();
	stageRuntime().restoreSnapshot(stageSnapshot);

	SetRuntime::Snapshot setSnapshot;
	setSnapshot.fileName = header.setFileName;
	setSnapshot.setName = header.setName;
	setSnapshot.scene = header.setScene;
	setSnapshot.sceneName = header.sceneName;
	setSnapshot.table = header.setTable;
	setSnapshot.angle = header.setAngle;
	setSnapshot.view = header.setView;
	setSnapshot.visible = header.setVisible;
	setSnapshot.transitionType = header.setTransitionType <= kSetTransitionForward ?
			static_cast<SetTransitionType>(header.setTransitionType): kSetTransitionNone;
	setSnapshot.transitionResource = header.setTransitionResource;
	setSnapshot.transitionFrame = header.setTransitionFrame;
	setRuntime().restoreSnapshot(setSnapshot);

	if (castStates.empty() && getGameType() == GType_Titanic) {
		restoreTitanicLegacyCastState(*this);
		Common::Array<Value> noArgs;
		if (setRuntime().set() && setRuntime().set()->isOpen())
			dispatchSetMessage("openset", noArgs);
	}

	if (setRuntime().visible() && setRuntime().set() && setRuntime().set()->isOpen() && setRuntime().scene() >= 0 &&
			!isLoadedReplacementStage(stageRuntime().stage())) {
		setRuntime().renderSetScene(*this, setRuntime().scene(), setRuntime().table(), setRuntime().angle(), setRuntime().view());
	} else if (stageRuntime().stage() && stageRuntime().stage()->isOpen()) {
		stageRuntime().renderStageNode(*this, stageRuntime().node());
	} else {
		blackScreen();
	}

	programPalette(savedClut);
	_hitKind = header.hitKind;
	_actionFrameMask = header.actionFrameMask;
	_cursorRuntime.setActiveCursorName(header.activeCursor);
	if (!_cursorRuntime.activeCursor().empty())
		setGameCursor(_cursorRuntime.activeCursor());
	_system->updateScreen();

	return Common::kNoError;
}

Common::Error CyberflixEngine::saveGameState(int slot, const Common::String &desc, bool isAutosave) {
	Common::OutSaveFile *out = _saveFileMan->openForSaving(getSaveStateName(slot));
	if (!out)
		return Common::Error(Common::kCreatingFileFailed, getSaveStateName(slot));
	Common::ScopedPtr<Common::OutSaveFile> saveFile(out);

	const Common::String signature = !_saveSignature.empty()
			? _saveSignature : defaultSaveSignature(getGameType());

	saveFile->write("CFXS", 4);
	saveFile->writeUint32LE(kCyberflixSaveVersion);

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		writeSaveString(payload, signature);
		writeSaveString(payload, desc);
		writeSaveString(payload, getGameId());
		payload.writeUint32LE(static_cast<uint32>(getPlatform()));
		payload.writeUint32LE(static_cast<uint32>(getLanguage()));
		payload.writeUint32LE(static_cast<uint32>(getGameType()));
		writeSaveString(payload, _cursorRuntime.activeCursor());
		writeSaveString(payload, _hitKind);
		payload.writeUint16LE(_actionFrameMask);

		const StageRuntime::Snapshot stageSnapshot = stageRuntime().snapshot();
		writeSaveString(payload, stageSnapshot.stageName);
		payload.writeSint32LE(stageSnapshot.node);
		writeSaveString(payload, stageSnapshot.flatName);

		const SetRuntime::Snapshot setSnapshot = setRuntime().snapshot();
		writeSaveString(payload, setSnapshot.fileName);
		writeSaveString(payload, setSnapshot.setName);
		payload.writeSint32LE(setSnapshot.scene);
		writeSaveString(payload, setSnapshot.sceneName);
		payload.writeSint32LE(setSnapshot.table);
		payload.writeSint32LE(setSnapshot.angle);
		writeSaveString(payload, setSnapshot.view);
		payload.writeByte(setSnapshot.visible ? 1 : 0);
		payload.writeUint32LE(static_cast<uint32>(setSnapshot.transitionType));
		payload.writeUint32LE(setSnapshot.transitionResource);
		payload.writeUint32LE(setSnapshot.transitionFrame);

		writeChunk(*saveFile, "HEAD", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		for (uint i = 0; i < PathRuntime::kPathSlotCount; ++i)
			writeSaveString(payload, _pathRuntime.pathSlotValue(i));
		writeChunk(*saveFile, "PATH", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		byte currentClut[256 * 3];
		_paletteRuntime.copyCurrent(currentClut);
		payload.write(currentClut, sizeof(currentClut));
		for (uint i = 0; i < 3; ++i)
			payload.writeDoubleLE(_paletteRuntime.gamma(i));
		writeChunk(*saveFile, "PAL ", payload);
	}

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		payload.writeSint32LE(_audioRuntime.waveVolumeLevel());
		payload.writeByte(_keyAborts ? 1 : 0);
		writeChunk(*saveFile, "SETT", payload);
	}

	writeShopChunk(*saveFile, _propRuntime.shops());
	writeCastChunk(*saveFile, _actorRuntime.casts());
	writeTrackChunk(*saveFile, _audioRuntime.tracks());
	writeCueVolumeChunk(*saveFile, _audioRuntime.tracks());
	writeAudioChunk(*saveFile, _audioRuntime, _mixer);
	writeVarsChunk(*saveFile, _vm.globalVars());
	writeLoopChunk(*saveFile, _loopRuntime);
	writeCricketChunk(*saveFile, _loopRuntime);
	writeEmptyCountChunk(*saveFile, "PUPP"); // Puppet native save bucket retained until fully modeled.

	{
		Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
		writeChunk(*saveFile, "END ", payload);
	}

	getMetaEngine()->appendExtendedSave(saveFile.get(), getTotalPlayTime(), desc, isAutosave);
	if (saveFile->err())
		return Common::Error(Common::kWritingFailed, getSaveStateName(slot));
	return Common::kNoError;
}

} // End of namespace Cyberflix

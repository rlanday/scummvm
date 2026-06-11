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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/debug-channels.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/endian.h"
#include "common/memstream.h"
#include "common/system.h"
#include "common/util.h"

#include "engines/util.h"

#include "audio/audiostream.h"
#include "audio/mixer.h"
#include "audio/decoders/raw.h"

#include "common/formats/winexe_pe.h"

#include "graphics/cursorman.h"
#include "graphics/palette.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"
#include "graphics/wincursor.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/archive.h"
#include "cyberflix/console.h"
#include "cyberflix/image.h"
#include "cyberflix/script.h"
#include "cyberflix/sound.h"
#include "cyberflix/stage.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

// The runtime accesses every resource through a "record+8" data pointer (the
// info tag), which is four bytes before the payload that Archive exposes via
// dataOffset (== record+12). All master-header/table field offsets below are
// expressed in that record+8 frame, so we subtract 4 from dataOffset to get the
// engine's view. See files/decomp/movie-playback.md.
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

// Sample-add an 8-bit unsigned mono SFX buffer into the music track at the given
// sample offset, extending the track with silence (0x80) if needed and clamping.
static void mixSfx(Common::Array<byte> &track, const Common::Array<byte> &sfx, uint32 atSample) {
	if (sfx.empty())
		return;
	uint32 end = atSample + sfx.size();
	while (track.size() < end)
		track.push_back(0x80);
	for (uint32 i = 0; i < sfx.size(); ++i) {
		int v = ((int)track[atSample + i] - 0x80) + ((int)sfx[i] - 0x80);
		v = CLIP(v, -128, 127);
		track[atSample + i] = (byte)(v + 0x80);
	}
}

// A clickable region on an interactive movie frame. The original player reads
// these as 0x40-byte records starting at event chunk +0x446 (the count is
// (resourceLen - 0x446) / 0x40); FUN_0040d710 hit-tests the rect against the
// click point and runs the action. Field offsets within the record:
//   +0x00 u16 action (1=END, 2=GOTO target, 6=NEXT, 7=PREV),
//   +0x02 byte flags (bit0 => also require a per-pixel mask hit; our menu
//         buttons clear it so a rect hit is enough),
//   +0x08 rect {left, top, right, bottom} as int16 (see FUN_0041ac60),
//   +0x30 Pascal string = GOTO target frame name (for action 2).
struct MovieButton {
	uint16 action;
	int16 left, top, right, bottom;
	Common::String target;
	bool contains(int x, int y) const {
		return x >= left && x < right && y >= top && y < bottom;
	}
};

// Resolve a frame name (as used by GOTO buttons) to its index in the per-frame
// table, mirroring FUN_0040e050. Returns -1 if not found.
static int resolveFrameName(const Common::Array<Common::String> &names, const Common::String &target) {
	for (uint i = 0; i < names.size(); ++i)
		if (names[i].equalsIgnoreCase(target))
			return (int)i;
	return -1;
}


CyberflixEngine::CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _rnd("cyberflix"), _console(nullptr) {
}

CyberflixEngine::~CyberflixEngine() {
	// _console is owned by the debugger registered with the engine framework.
	// _exe, _cursorCache (SharedPtr values) and _stage free themselves.
}

int CyberflixEngine::getGameType() const {
	return _gameDescription->gameType;
}

const char *CyberflixEngine::getGameId() const {
	return _gameDescription->desc.gameId;
}

Common::Language CyberflixEngine::getLanguage() const {
	return _gameDescription->desc.language;
}

Common::Platform CyberflixEngine::getPlatform() const {
	return _gameDescription->desc.platform;
}

bool CyberflixEngine::hasFeature(EngineFeature f) const {
	return (f == kSupportsReturnToLauncher);
}

// The cursor bitmaps are copyrighted game art, so they are loaded at runtime
// from the player's own TI.EXE rather than shipped with ScummVM. TI.EXE is the
// CyberFlix "Bicycle" runtime; in an installed game it lives under INSTALL/BINX
// (or INSTALL/BIN). It is a Win32 PE whose RT_GROUP_CURSOR resources are named
// CURS.ARROW, CURS.HAND, CURS.GOUP, ... (see files/decomp/movie-playback.md).
Common::PEResources *CyberflixEngine::gameExe() {
	if (_exeTried)
		return _exe.get();
	_exeTried = true;

	const Common::FSNode gameDir(ConfMan.getPath("path"));
	static const char *const candidates[][3] = {
		{ "INSTALL", "BINX", "TI.EXE" },
		{ "INSTALL", "BIN", "TI.EXE" }
	};
	for (uint c = 0; c < ARRAYSIZE(candidates); ++c) {
		Common::FSNode node = gameDir.getChild(candidates[c][0])
				.getChild(candidates[c][1]).getChild(candidates[c][2]);
		if (!node.exists())
			continue;
		Common::SeekableReadStream *stream = node.createReadStream();
		if (!stream)
			continue;
		Common::ScopedPtr<Common::PEResources> exe(new Common::PEResources());
		if (exe->loadFromEXE(stream, DisposeAfterUse::YES)) {
			_exe.reset(exe.release());
			return _exe.get();
		}
		// exe (and the stream it owns) is freed as it goes out of scope.
	}
	warning("Cyberflix: could not locate TI.EXE for cursor resources");
	return nullptr;
}

bool CyberflixEngine::setGameCursor(const Common::String &name) {
	if (_activeCursor == name && _cursorCache.contains(name))
		return true;

	Common::SharedPtr<Graphics::WinCursorGroup> group;
	if (_cursorCache.contains(name)) {
		group = _cursorCache[name];
	} else {
		Common::PEResources *exe = gameExe();
		if (!exe)
			return false;
		group = Common::SharedPtr<Graphics::WinCursorGroup>(
				Graphics::WinCursorGroup::createCursorGroup(exe, Common::WinResourceID(name)));
		_cursorCache[name] = group; // cache even null to avoid re-parsing
	}
	if (!group || group->cursors.empty())
		return false;

	CursorMan.replaceCursor(group->cursors[0].cursor);
	_activeCursor = name;
	return true;
}

// openstagefile(name): open a DATA/*.STG deck. The boot script calls this for
// MAIN.STG just before sendtostage(0). Mirrors TI.EXE FUN_004090b0 (which parses
// via FUN_00409150). See files/decomp/stage-notes.md.
void CyberflixEngine::openStageFile(const Common::String &name) {
	if (name.empty())
		return;
	Common::ScopedPtr<Stage> stage(new Stage());
	if (!stage->open(name))
		return;
	_stage.reset(stage.release());
	debug(1, "Cyberflix: stage '%s' open (%u nodes)", name.c_str(), _stage->nodeCount());
}

// sendtostage(node): navigate to a node of the open stage and render it. Mirrors
// TI.EXE FUN_0040ad60 -> FUN_0040b180 (the node renderer); per-node enter scripts
// (interactivity/hotspots) land in a later phase.
void CyberflixEngine::sendToStage(int node) {
	renderStageNode(node);
}

void CyberflixEngine::renderStageNode(int node) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtostage(%d) with no stage open", node);
		return;
	}

	FrameImage frame;
	if (!_stage->renderNode((uint32)node, frame))
		return;

	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	if (_stage->loadStagePalette(rgb))
		_system->getPaletteManager()->setPalette(rgb, 0, 256);

	Graphics::Surface *screen = _system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	int x0 = (kScreenWidth - frame.width) / 2;
	int y0 = (kScreenHeight - frame.height) / 2;
	for (int y = 0; y < frame.height; ++y) {
		for (int x = 0; x < frame.width; ++x) {
			int sx = x0 + x, sy = y0 + y;
			if (sx >= 0 && sy >= 0 && sx < kScreenWidth && sy < kScreenHeight)
				*((byte *)screen->getBasePtr(sx, sy)) = frame.pixels[(uint)y * frame.width + x];
		}
	}
	_system->unlockScreen();

	// Show the default arrow over the rendered node until per-node hotspot
	// hit-testing (directional cursors) is implemented.
	if (setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
	_system->updateScreen();

	debug(1, "Cyberflix: rendered stage '%s' node %d (%ux%u)",
			_stage->name().c_str(), node, frame.width, frame.height);
}

Common::Error CyberflixEngine::run() {
	// The original is a 640x480 8-bit palettised WinG title.
	initGraphics(kScreenWidth, kScreenHeight);

	_console = new Console(this);
	setDebugger(_console);

	// Assets live in the DATA subdirectory of the installed game; the intro and
	// other full-screen movies live alongside it in MOVIES.
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "data");
	SearchMan.addSubDirectoryMatching(gameDataDir, "movies");

	// Clear to black before the boot script paints anything.
	byte palette[3 * 256];
	memset(palette, 0, sizeof(palette));
	_system->getPaletteManager()->setPalette(palette, 0, 256);

	Graphics::Surface *screen = _system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	_system->unlockScreen();
	_system->updateScreen();

	// Drive the boot script. BOOTFILE holds the master header plus two script
	// resources (info tag 0x0FA1); the first is the boot/intro script that, once
	// its CD check is excised, runs engine setup and plays the intro movies.
	Common::File bootFile;
	if (!bootFile.open("BOOTFILE")) {
		warning("Cyberflix: could not locate DATA/BOOTFILE");
		return Common::kNoGameDataFoundError;
	}

	Archive boot;
	if (!boot.open(bootFile.readStream(bootFile.size()), "BOOTFILE")) {
		warning("Cyberflix: BOOTFILE present but failed LPPALPPA validation");
		return Common::kUnknownError;
	}

	int bootScriptIndex = -1;
	for (uint32 i = 0; i < boot.getResourceCount(); ++i) {
		const Archive::Resource &res = boot.getResource(i);
		if (!res.empty && res.info == Script::kScriptInfoTag) {
			bootScriptIndex = (int)i;
			break;
		}
	}
	if (bootScriptIndex < 0) {
		warning("Cyberflix: BOOTFILE has no script resource");
		return Common::kUnknownError;
	}

	Common::SeekableReadStream *scriptStream =
			boot.createReadStreamForResource((uint32)bootScriptIndex);
	Script bootScript;
	bool parsed = scriptStream && bootScript.parse(scriptStream);
	delete scriptStream;
	if (!parsed) {
		warning("Cyberflix: failed to parse boot script resource %d", bootScriptIndex);
		return Common::kUnknownError;
	}

	if (!exciseBootCdCheck(bootScript))
		warning("Cyberflix: boot script CD check not found; running unmodified");

	ScriptVM vm;
	vm.setHost(this);
	vm.runProgram(bootScript);

	// Minimal event loop so the window stays responsive after the boot script
	// returns, until the main interactive loop lands.
	Common::Event event;
	while (!shouldQuit()) {
		while (_eventMan->pollEvent(event)) {
			// Input handling will be wired into the script VM in a later phase.
		}
		_system->updateScreen();
		_system->delayMillis(10);
	}

	return Common::kNoError;
}

bool CyberflixEngine::exciseBootCdCheck(Script &script) {
	const uint32 n = script.getInstructionCount();

	// The CD presence check compares a path against the "titanic1:" CD volume
	// literal. Locate that literal, then the if-block that encloses it.
	int literal = -1;
	for (uint32 i = 0; i < n; ++i) {
		uint16 op = script.getInstruction(i).opcode;
		if (op == Script::kOpPush3 || op == Script::kOpPush4 || op == Script::kOpPushSym) {
			if (script.getSelfRelString(i).equalsIgnoreCase("titanic1:")) {
				literal = (int)i;
				break;
			}
		}
	}
	if (literal < 0)
		return false;

	// Walk back to the nearest enclosing kOpIf (the literal sits in its
	// condition), balancing any nested if/endif pairs in between.
	int ifIndex = -1;
	int depth = 0;
	for (int i = literal - 1; i >= 0; --i) {
		uint16 op = script.getInstruction((uint32)i).opcode;
		if (op == Script::kOpEndIf) {
			++depth;
		} else if (op == Script::kOpIf) {
			if (depth == 0) {
				ifIndex = i;
				break;
			}
			--depth;
		}
	}
	if (ifIndex < 0)
		return false;

	int endIfIndex = script.findMatchingEndIf((uint32)ifIndex);
	if (endIfIndex < 0)
		return false;

	script.neutralizeRange((uint32)ifIndex, (uint32)endIfIndex);
	debug(0, "Cyberflix: excised boot CD check (instructions %d..%d)",
			ifIndex, endIfIndex);
	return true;
}

void CyberflixEngine::playMovie(const Common::String &name) {
	if (name.empty())
		return;

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open movie '%s'", name.c_str());
		return;
	}

	uint32 size = (uint32)file.size();
	// fileData is declared before archive so it outlives it: the archive owns a
	// stream that points into this buffer, and the movie loop below reads
	// payloads through raw pointers into it.
	Common::Array<byte> fileData(size);
	if (file.read(fileData.begin(), size) != size) {
		warning("Cyberflix: could not read movie '%s'", name.c_str());
		return;
	}
	file.close();

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid movie container", name.c_str());
		return;
	}

	// Full-screen frames are the resources whose info word's high half is 0x0200;
	// that word doubles as the frame's {uint16 H, uint16 W} header (W is always
	// 512, H is the low half: 264 for LOGO video, 384 for the PLAYMODE menu), so
	// the decoder source begins four bytes before the payload (see image.h). This
	// linear scan is only a fallback frame order; when the movie has a master
	// header we drive playback from its authoritative per-frame table below.
	Common::Array<uint32> frames;
	for (uint32 i = 0; i < archive.getResourceCount(); ++i) {
		const Archive::Resource &res = archive.getResource(i);
		if (!res.empty && (res.info >> 16) == kFrameInfoHigh && res.dataOffset >= 4)
			frames.push_back(i);
	}

	// Build the soundtrack. A linear movie's master header (info==0x40000)
	// references two cue tables: a MUSIC table (the continuous score, played from
	// t=0) and an SFX/event table (named one-shots triggered by individual video
	// frames). The video runs at a fixed 20 fps (50 ms/frame, from the scaled
	// timer FUN_00405130 * 0.06 and the masterHdr[+0x1c]=3 floor), so 318 frames
	// == 15.9 s == the music duration: video and music are co-terminous. We
	// therefore decode the MUSIC cues into one track and MIX each frame-triggered
	// SFX into it at that frame's time (frame f -> f/frameCount of the track).
	// This reproduces the original A/V sync (e.g. LOGO.MOV's gunshots land on the
	// "INCORPORATED" frame). See files/decomp/movie-playback.md.
	//
	// NB: do NOT concatenate the SFX resources onto the music track. Doing so
	// lengthened the track (so frames played too slowly) and made the effects
	// sound at their concatenation offset instead of their trigger frame.
	Common::Array<byte> pcmBuf;
	// Cumulative start time (ms) of each video frame; last entry is the total
	// duration. Built from the per-frame event chunks below. Empty => no usable
	// master header, in which case the frame loop falls back to a fixed cadence.
	Common::Array<uint32> frameStartMs;
	// Per-frame table, captured from the master header: the video resource id to
	// composite (event record +0xc) and the navigation command of its event
	// chunk (event chunk +0: 6 = NEXT, 1 = HOLD/wait). When populated this is the
	// authoritative playback order; the kFrameInfoHigh scan above is the fallback.
	Common::Array<uint32> pfVideoRes;
	Common::Array<uint16> pfNavCmd;
	// Per-frame name (event record +0x1a, Pascal) used to resolve GOTO targets,
	// and the per-frame interactive button table (empty => non-interactive frame
	// that obeys its nav command; non-empty => the player holds the frame and
	// waits for a click, as the main menu does).
	Common::Array<Common::String> pfName;
	Common::Array<Common::Array<MovieButton> > pfButtons;
	// Per-frame hold duration in ms (event chunk +2 in scaled timer units,
	// floored by masterHdr[+0x1c]). Used to pace interactive movies frame by
	// frame (the menu and its pressed-button frames), independent of the audio
	// timeline that paces linear movies.
	Common::Array<uint32> pfHoldMs;

	int masterIdx = -1;
	for (uint32 i = 0; i < archive.getResourceCount(); ++i) {
		if (!archive.getResource(i).empty && archive.getResource(i).info == kMasterHeaderInfoTag) {
			masterIdx = (int)i;
			break;
		}
	}
	if (masterIdx < 0) {
		warning("Cyberflix: movie '%s' has no master header; playing without audio", name.c_str());
	} else {
		const byte *hdr = engineBase(fileData, archive.getResource(masterIdx));
		// Guard the per-frame table extent before trusting the header layout.
		if (hdr && hdr + 0x87c <= fileData.end()) {
			uint32 musicTableIdx = READ_LE_UINT32(hdr + 0x64); // masterHdr[0x19]
			uint32 sfxTableIdx   = READ_LE_UINT32(hdr + 0x60); // masterHdr[0x18]
			uint32 pfCount       = READ_LE_UINT32(hdr + 0x878);
			const byte *pfTable  = hdr + 0x87c; // per-frame records, stride 0x2a
			// Minimum per-frame hold, in the scaled timer's units (FUN_00405130
			// returns timeGetTime * 0.06, so 1 unit == 1000/60 ms). For LOGO this
			// floor is 3 units == 50 ms == 20 fps.
			uint32 frameFloorUnits = READ_LE_UINT32(hdr + 0x1c);
			if (frameFloorUnits == 0)
				frameFloorUnits = 3;

			// 1. MUSIC track: decode each music-table cue's 22050 Hz resource in
			//    order. (Skip non-22050 cues such as the silent 11025 Hz pad.)
			if (musicTableIdx < archive.getResourceCount()) {
				const byte *mt = engineBase(fileData, archive.getResource(musicTableIdx));
				if (mt && mt + 0x10e <= fileData.end()) {
					uint32 mc = READ_LE_UINT32(mt + 0x10a);
					for (uint32 e = 0; e < mc; ++e) {
						const byte *ent = mt + 0x10e + e * 0x1a;
						if (ent + 0x1a > fileData.end())
							break;
						uint32 rid = READ_LE_UINT32(ent + 4);
						if (rid >= archive.getResourceCount())
							continue;
						const Archive::Resource &r = archive.getResource(rid);
						if (r.empty || r.info != kAudioResourceInfoTag || r.dataOffset < 4)
							continue;
						const byte *payload = fileData.begin() + r.dataOffset;
						if (READ_LE_UINT32(payload + 0x18) != kAudioRate22050)
							continue;
						decodeCbxAudio(payload, r.length, pcmBuf);
					}
				}
			}

			// 2. Per-frame timeline + SFX. Walk the per-frame table: each frame's
			//    event chunk (info==0x6 resource at record[+0x10]) gives its hold
			//    duration at engine offset +2 (floored by frameFloorUnits) and an
			//    optional cue NAME at +0x12. We accumulate the real start time of
			//    every frame, and for each named cue we look it up in the SFX
			//    table and MIX that effect into the music track at the frame's
			//    time (sample-add). The two LOGO frames that hold 333/500 ms make
			//    the video timeline (~16.6 s) slightly longer than the music
			//    (~15.9 s); the trailing fade plays over silence, as in the
			//    original.
			const byte *st = (sfxTableIdx < archive.getResourceCount())
					? engineBase(fileData, archive.getResource(sfxTableIdx)) : nullptr;
			uint32 sfxCount = (st && st + 8 <= fileData.end()) ? READ_LE_UINT32(st + 4) : 0;
			uint32 cumMs = 0;
			for (uint32 f = 0; f < pfCount; ++f) {
				const byte *rec = pfTable + f * 0x2a;
				if (rec + 0x2a > fileData.end())
					break;
				const byte *eb = nullptr;
				uint32 eventId = READ_LE_UINT32(rec + 0x10);
				uint32 ebLen = 0;
				if (eventId < archive.getResourceCount()) {
					eb = engineBase(fileData, archive.getResource(eventId));
					ebLen = archive.getResource(eventId).length;
				}

				frameStartMs.push_back(cumMs);
				pfVideoRes.push_back(READ_LE_UINT32(rec + 0xc));
				pfNavCmd.push_back((eb && eb + 2 <= fileData.end())
						? READ_LE_UINT16(eb) : 6 /* default NEXT */);
				pfName.push_back(readPascalString(rec + 0x1a, fileData));

				// Interactive buttons: any bytes past the 0x446-byte event-chunk
				// base are 0x40-byte button records (the original reads the count
				// from the parser's allocated struct at +0x221; the raw resource
				// only stores as many records as fit past the base, so the size
				// delta is the reliable count).
				Common::Array<MovieButton> buttons;
				uint32 btnCount = (eb && ebLen > 0x446) ? (ebLen - 0x446) / 0x40 : 0;
				for (uint32 b = 0; b < btnCount; ++b) {
					const byte *br = eb + 0x446 + b * 0x40;
					if (br + 0x40 > fileData.end())
						break;
					MovieButton mb;
					mb.action = READ_LE_UINT16(br);
					mb.left   = (int16)READ_LE_UINT16(br + 8);
					mb.top    = (int16)READ_LE_UINT16(br + 10);
					mb.right  = (int16)READ_LE_UINT16(br + 12);
					mb.bottom = (int16)READ_LE_UINT16(br + 14);
					mb.target = readPascalString(br + 0x30, fileData);
					buttons.push_back(mb);
				}
				pfButtons.push_back(buttons);

				// Mix this frame's SFX (if it names a cue and we have a track).
				if (eb && !pcmBuf.empty()) {
					Common::String cue = readPascalString(eb + 0x12, fileData);
					if (!cue.empty()) {
						uint32 sfxResId = (uint32)-1;
						for (uint32 e = 0; e < sfxCount; ++e) {
							const byte *ent = st + 8 + e * 0x2a;
							if (ent + 0x2a > fileData.end())
								break;
							if (readPascalString(ent + 0xa, fileData) == cue) {
								sfxResId = READ_LE_UINT32(ent + 4);
								break;
							}
						}
						if (sfxResId < archive.getResourceCount()) {
							const Archive::Resource &sr = archive.getResource(sfxResId);
							if (!sr.empty && sr.info == kAudioResourceInfoTag && sr.dataOffset >= 4) {
								Common::Array<byte> sfx;
								decodeCbxAudio(fileData.begin() + sr.dataOffset, sr.length, sfx);
								uint32 atSample = (uint32)((uint64)cumMs * kAudioSampleRate / 1000);
								mixSfx(pcmBuf, sfx, atSample);
							}
						}
					}
				}

				// Advance the timeline by this frame's hold (scaled units -> ms).
				uint32 units = frameFloorUnits;
				if (eb && eb + 6 <= fileData.end()) {
					uint32 d = READ_LE_UINT32(eb + 2);
					if (d > units)
						units = d;
				}
				uint32 holdMs = (uint32)((uint64)units * 1000 / 60);
				pfHoldMs.push_back(holdMs);
				cumMs += holdMs;
			}
			frameStartMs.push_back(cumMs); // total movie duration
		}
	}

	byte *pcm = nullptr;
	uint32 pcmLen = pcmBuf.size();
	if (pcmLen) {
		pcm = (byte *)malloc(pcmLen);
		if (pcm)
			memcpy(pcm, pcmBuf.begin(), pcmLen);
		else
			pcmLen = 0;
	}

	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	if (loadPalette(fileData.begin(), size, rgb))
		_system->getPaletteManager()->setPalette(rgb, 0, 256);

	// Composite frames in order into a persistent surface (frames are
	// inter-coded) and present them on the movie's own timeline. ESC/quit skips.
	//
	// There is NO stored frames-per-second field. Each frame carries its own
	// hold time in its event chunk (offset +2, floored by masterHdr[+0x1c]),
	// expressed in the scaled-timer units returned by TI.EXE FUN_00405130
	// (timeGetTime * 0.06, i.e. 1 unit == 1000/60 ms). We precomputed the
	// cumulative start time of every frame into frameStartMs above, so the video
	// is paced by that timeline against a wall clock - NOT slaved to the audio.
	// The soundtrack (music with SFX mixed in at their frame times) is fired on
	// the mixer and runs concurrently; both advance in real time so they stay in
	// step. The video timeline can be slightly longer than the music (the two
	// long-hold LOGO frames push it to ~16.6 s vs ~15.9 s of music), so the final
	// fade is no longer cut short. With no usable timeline we fall back to a
	// fixed cadence.
	FrameSequence seq;
	const uint32 kFallbackFrameDelayMs = 66; // ~15 fps when there is no frame timeline
	bool skip = false;
	Common::Event event;

	// A movie is interactive if any frame carries buttons (the main menu). Such
	// movies loop their soundtrack while they wait for the user; linear movies
	// (the logo) play their track once.
	bool hasInteractive = false;
	for (uint i = 0; i < pfButtons.size(); ++i)
		if (!pfButtons[i].empty()) {
			hasInteractive = true;
			break;
		}

	// The original movie player shows the Windows arrow cursor while an
	// interactive frame (the menu) is up and hides it during linear playback
	// (FUN_004051b0/FUN_00405210, gated by movie flag bit 0x10). Mirror that:
	// the arrow is decoded on demand from the user's TI.EXE.
	if (hasInteractive && setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
	else
		CursorMan.showMouse(false);

	Audio::SoundHandle audioHandle;
	if (pcm && pcmLen) {
		Audio::SeekableAudioStream *stream = Audio::makeRawStream(
				pcm, pcmLen, kAudioSampleRate, Audio::FLAG_UNSIGNED,
				DisposeAfterUse::YES);
		if (hasInteractive) {
			Audio::AudioStream *loop = new Audio::LoopingAudioStream(stream, 0);
			_mixer->playStream(Audio::Mixer::kSFXSoundType, &audioHandle, loop);
		} else {
			_mixer->playStream(Audio::Mixer::kSFXSoundType, &audioHandle, stream);
		}
	} else {
		free(pcm);
		pcm = nullptr;
	}

	debug(0, "Cyberflix: movie '%s' frames=%u audioBytes=%u audioMs=%u",
			name.c_str(), pfVideoRes.empty() ? frames.size() : pfVideoRes.size(), pcmLen,
			pcm ? (uint32)((uint64)pcmLen * 1000 / kAudioSampleRate) : 0);

	// Composite frames in order into a persistent surface (frames are
	// inter-coded) and present them. ESC/quit skips.
	//
	// There is NO stored frames-per-second field. Each frame carries its own
	// hold time in its event chunk (offset +2, floored by masterHdr[+0x1c]),
	// expressed in the scaled-timer units returned by TI.EXE FUN_00405130
	// (timeGetTime * 0.06, i.e. 1 unit == 1000/60 ms). We precompute the
	// cumulative start time of every frame into frameStartMs above.
	//
	// SYNC: the SFX (e.g. LOGO.MOV's gunshots) are mixed into the soundtrack at
	// their exact frame time, so they are locked to the music sample-for-sample.
	// To keep the *picture* locked to those sounds even when frame decoding/blit
	// lags, we clock the video off the real audio position (the mixer's elapsed
	// time) rather than a free-running wall clock, and DROP the present of any
	// frame whose slot has already passed (still decoding it, since frames are
	// inter-coded). This mirrors the original's adaptive frame-drop in
	// FUN_0040e8b0. Once the music ends (the video timeline can run ~0.75 s
	// longer than the music, e.g. LOGO's trailing fade) we continue on the wall
	// clock so the fade still plays out.
	const bool usePF = !pfVideoRes.empty();
	const uint32 frameCount = usePF ? pfVideoRes.size() : frames.size();
	if (frameCount == 0)
		warning("Cyberflix: movie '%s' has no frames to show", name.c_str());

	const uint32 wallStartMs = _system->getMillis();
	uint32 fi = 0;
	while (fi < frameCount && !shouldQuit() && !skip) {
		uint32 resIdx = usePF ? pfVideoRes[fi] : frames[fi];
		if (resIdx >= archive.getResourceCount())
			break;
		const Archive::Resource &res = archive.getResource(resIdx);
		if (res.empty || res.dataOffset < 4 ||
				seq.applyFrame(fileData.begin() + res.dataOffset - 4, res.length + 4) == 0) {
			warning("Cyberflix: movie '%s' frame %u failed to decode", name.c_str(), fi);
			break;
		}

		const bool interactive = usePF && fi < pfButtons.size() && !pfButtons[fi].empty();

		// Current playback clock: real audio position while the track plays,
		// else elapsed wall time (covers the post-music fade and silent movies).
		uint32 nowMs = (pcm && _mixer->isSoundHandleActive(audioHandle))
				? _mixer->getSoundElapsedTime(audioHandle)
				: (_system->getMillis() - wallStartMs);
		uint32 frameEndMs = (fi + 1 < frameStartMs.size())
				? frameStartMs[fi + 1] : (fi + 1) * kFallbackFrameDelayMs;

		// Drop the present of a late linear frame to let the picture catch up to
		// the audio; always present in interactive movies. The original player
		// blits every frame (FUN_0040e8b0) before running the nav/button
		// interpreter (FUN_0040d710), so the pressed-button ("squished") frames
		// reached by a click are always shown. Frame-drop is a sync aid for the
		// long linear movies (the logo) only.
		bool present = hasInteractive || nowMs < frameEndMs || fi + 1 >= frameCount;

		const byte *pixels = seq.pixels();
		int w = seq.width(), h = seq.height();
		int x0 = (kScreenWidth - w) / 2;
		int y0 = (kScreenHeight - h) / 2;
		if (present) {
			Graphics::Surface *screen = _system->lockScreen();
			for (int y = 0; y < h; ++y) {
				int sy = y0 + y;
				if (sy < 0 || sy >= kScreenHeight)
					continue;
				for (int x = 0; x < w; ++x) {
					int sx = x0 + x;
					if (sx >= 0 && sx < kScreenWidth)
						*((byte *)screen->getBasePtr(sx, sy)) = pixels[(uint)y * w + x];
				}
			}
			_system->unlockScreen();
			_system->updateScreen();
		}

		if (interactive) {
			// Interactive frame (the main menu): the original player suppresses
			// the frame's nav command and waits on the button table
			// (FUN_0040d710). Hold here, looping the soundtrack, until the user
			// clicks a button or quits. A click inside a button rect runs its
			// action: GOTO jumps to the named frame, NEXT/PREV step, END (and any
			// click on an action-1 button) returns from the movie.
			int32 nextFi = -1;
			while (nextFi < 0 && !shouldQuit() && !skip) {
				while (_eventMan->pollEvent(event)) {
					if (event.type == Common::EVENT_KEYDOWN &&
							event.kbd.keycode == Common::KEYCODE_ESCAPE) {
						skip = true;
					} else if (event.type == Common::EVENT_LBUTTONDOWN) {
						int fx = event.mouse.x - x0;
						int fy = event.mouse.y - y0;
						for (uint b = 0; b < pfButtons[fi].size(); ++b) {
							const MovieButton &mb = pfButtons[fi][b];
							if (!mb.contains(fx, fy))
								continue;
							if (mb.action == 2) { // GOTO target frame
								int idx = resolveFrameName(pfName, mb.target);
								nextFi = (idx >= 0) ? idx : (int32)fi;
							} else if (mb.action == 6) { // NEXT
								nextFi = (fi + 1 < frameCount) ? (int32)(fi + 1) : (int32)fi;
							} else if (mb.action == 7) { // PREV
								nextFi = (fi > 0) ? (int32)(fi - 1) : 0;
							} else { // END / unsupported -> leave the movie
								skip = true;
							}
							break;
						}
					}
				}
				if (nextFi < 0 && !skip) {
					// Composite the cursor at its new position and keep the
					// window live. ScummVM draws the mouse during updateScreen,
					// so without this the cursor would appear frozen.
					_system->updateScreen();
					_system->delayMillis(10);
				}
			}
			if (nextFi >= 0)
				fi = (uint32)nextFi;
			continue;
		}

		uint16 nav = usePF ? pfNavCmd[fi] : 6;

		// Interactive movies (the menu and its pressed-button frames) are paced
		// frame by frame off a local wall clock by each frame's own authored
		// hold, NOT the global audio timeline (a click jumps around the frame
		// table, so cumulative audio time is meaningless here). This is what
		// makes the "squished" pressed-button frame visible for its hold before
		// the menu returns.
		if (hasInteractive) {
			uint32 holdMs = (fi < pfHoldMs.size()) ? pfHoldMs[fi] : kFallbackFrameDelayMs;
			uint32 holdStart = _system->getMillis();
			while (!shouldQuit() && !skip) {
				while (_eventMan->pollEvent(event)) {
					if (event.type == Common::EVENT_KEYDOWN &&
							event.kbd.keycode == Common::KEYCODE_ESCAPE)
						skip = true;
				}
				if (_system->getMillis() - holdStart >= holdMs)
					break;
				_system->updateScreen();
				_system->delayMillis(5);
			}
			if (nav == 1)
				break; // END: leave this (pressed) frame on screen and return
			++fi;
			continue;
		}

		if (nav == 1) {
			// END: nav cmd 1 is the last-frame marker. The original player
			// (FUN_0040ca80: when FUN_0040d710 returns 1 -> goto cleanup) shows
			// this frame and then RETURNS from playback, leaving it on screen.
			break;
		}

		// NEXT (cmd 6, and any not-yet-implemented command): wait until this
		// frame's authored end time on the playback clock, then advance.
		for (;;) {
			while (_eventMan->pollEvent(event)) {
				if (event.type == Common::EVENT_KEYDOWN &&
						event.kbd.keycode == Common::KEYCODE_ESCAPE)
					skip = true;
			}
			if (shouldQuit() || skip)
				break;
			uint32 t = (pcm && _mixer->isSoundHandleActive(audioHandle))
					? _mixer->getSoundElapsedTime(audioHandle)
					: (_system->getMillis() - wallStartMs);
			if (t >= frameEndMs)
				break;
			_system->delayMillis(5);
		}
		++fi;
	}

	_mixer->stopHandle(audioHandle);

	// Leave the cursor hidden when we hand control back; the next interactive
	// movie/node re-shows it.
	CursorMan.showMouse(false);
}

} // End of namespace Cyberflix

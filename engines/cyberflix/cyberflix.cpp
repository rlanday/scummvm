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
#include "common/memstream.h"
#include "common/system.h"

#include "engines/util.h"

#include "graphics/palette.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/archive.h"
#include "cyberflix/console.h"
#include "cyberflix/image.h"
#include "cyberflix/script.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

CyberflixEngine::CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _rnd("cyberflix"), _console(nullptr) {
}

CyberflixEngine::~CyberflixEngine() {
	// _console is owned by the debugger registered with the engine framework.
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
	byte *fileData = (byte *)malloc(size);
	if (!fileData || file.read(fileData, size) != size) {
		warning("Cyberflix: could not read movie '%s'", name.c_str());
		free(fileData);
		return;
	}
	file.close();

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(fileData, size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid movie container", name.c_str());
		free(fileData);
		return;
	}

	// Video frames are the resources tagged kFrameInfoTag; that tag doubles as
	// the frame's {uint16 H, uint16 P} header, so the decoder source begins four
	// bytes before the payload (see image.h / Console::cmdShowMovie).
	Common::Array<uint32> frames;
	for (uint32 i = 0; i < archive.getResourceCount(); ++i) {
		const Archive::Resource &res = archive.getResource(i);
		if (!res.empty && res.info == kFrameInfoTag && res.dataOffset >= 4)
			frames.push_back(i);
	}
	if (frames.empty()) {
		warning("Cyberflix: movie '%s' has no video frames", name.c_str());
		free(fileData);
		return;
	}

	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	if (loadPalette(fileData, size, rgb))
		_system->getPaletteManager()->setPalette(rgb, 0, 256);

	// Composite frames in order into a persistent surface (frames are
	// inter-coded) and present each at a fixed cadence. ESC or quit skips ahead.
	//
	// The original has NO stored frames-per-second field: its player (TI.EXE
	// fcn.0042eb00 / the wait loop at 0x0043e1cc) is audio-clocked. It advances
	// one video frame per consumed audio chunk, servicing the sound double
	// buffer on a 20 ms poll quantum with only a 4000 ms hang watchdog as the
	// other time constant. The true cadence is therefore audioSampleRate /
	// samplesPerChunk.
	//
	// Both fall out of the MOV audio format, reversed from TI.EXE and the
	// container: the DirectSound output WAVEFORMATEX built at 0x0042e870 is
	// 22050 Hz / 16-bit / stereo, and each video frame is paired with one
	// audio chunk (info tag 0x6) carrying ~1024 8-bit mono samples. That gives
	// 22050 / 1024 ~= 21.5 fps (~46 ms). Until the audio chunks are decoded and
	// playback is truly audio-locked, pace video with this derived cadence.
	FrameSequence seq;
	const uint32 frameDelayMs = 46; // 22050 Hz / ~1024 samples per frame ~= 21.5 fps.
	bool skip = false;
	Common::Event event;

	for (uint32 f = 0; f < frames.size() && !shouldQuit() && !skip; ++f) {
		const Archive::Resource &res = archive.getResource(frames[f]);
		if (seq.applyFrame(fileData + res.dataOffset - 4, res.length + 4) == 0) {
			warning("Cyberflix: movie '%s' frame %u failed to decode", name.c_str(), f);
			break;
		}

		const byte *pixels = seq.pixels();
		int w = seq.width(), h = seq.height();

		int x0 = (kScreenWidth - w) / 2;
		int y0 = (kScreenHeight - h) / 2;
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

		uint32 until = _system->getMillis() + frameDelayMs;
		while (_system->getMillis() < until) {
			while (_eventMan->pollEvent(event)) {
				if (event.type == Common::EVENT_KEYDOWN &&
						event.kbd.keycode == Common::KEYCODE_ESCAPE)
					skip = true;
			}
			if (shouldQuit() || skip)
				break;
			_system->delayMillis(10);
		}
	}

	free(fileData);
}

} // End of namespace Cyberflix

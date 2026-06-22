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
#include "common/events.h"
#include "common/system.h"
#include "common/util.h"

#include "gui/message.h"

#include "cyberflix/console.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/script.h"

namespace Cyberflix {

static const double kDefaultPaletteGamma = 0.65;
static const double kPaletteGammaUp = 1.05;
static const double kPaletteGammaDown = 0.9523809523809523;
static const double kPaletteGammaMin = 0.15;
static const double kPaletteGammaMax = 2.5;

bool CyberflixEngine::exciseBootCdCheck(Script &script) {
	const uint32 n = script.getInstructionCount();

	// The CD presence check compares a path against the "titanic1:" CD volume
	// literal. Locate that literal, then the if-block that encloses it.
	int literal = -1;
	for (uint32 i = 0; i < n; ++i) {
		uint16 op = script.getInstruction(i).opcode;
		if (op == Script::kOpPush3 || op == Script::kOpPush4 || op == Script::kOpPushSym) {
			if (script.getSelfRelString(i).equalsIgnoreCase("titanic1:")) {
				literal = static_cast<int>(i);
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
		uint16 op = script.getInstruction(static_cast<uint32>(i)).opcode;
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

	int endIfIndex = script.findMatchingEndIf(static_cast<uint32>(ifIndex));
	if (endIfIndex < 0)
		return false;

	script.neutralizeRange(static_cast<uint32>(ifIndex), static_cast<uint32>(endIfIndex));
	debug(0, "Cyberflix: excised boot CD check (instructions %d..%d)",
			ifIndex, endIfIndex);
	return true;
}

uint32 CyberflixEngine::handleMovieHotkeys(const Common::Event &event, bool skippable,
		const Audio::SoundHandle &audioHandle, bool &skip) {
	if (event.type != Common::EVENT_KEYDOWN)
		return 0;

	if (handleGlobalKey(event))
		return 0;

	const Common::KeyCode kc = event.kbd.keycode;
	const bool ctrl = (event.kbd.flags & Common::KBD_CTRL) != 0;

	// SKIP. TI.EXE's WndProc (FUN_00403690) forces VK_ESCAPE into a '.' event
	// carrying the modifier word 0x1fa0; the movie key handler (FUN_0040e430,
	// case 0x2e/'.', 0x51/'Q', 0x71/'q') then aborts playback, but only when the
	// master header flags byte (+0x18) has bit 0 set. Ctrl is what sets 0x1fa0
	// for the letter keys, so the faithful combos are Esc, Ctrl+Q and Ctrl+period.
	if (kc == Common::KEYCODE_ESCAPE ||
			(ctrl && (kc == Common::KEYCODE_q || kc == Common::KEYCODE_PERIOD))) {
		if (skippable)
			skip = true;
		return 0;
	}

	// PAUSE / RESUME. FUN_0040e430 case 0x54/0x74 ('T'/'t', gated by the 0x1fa0
	// modifier) toggles the pause latch DAT_0045ef88 (FUN_0042f930 pauses the
	// audio, FUN_0042f690 resumes). We pause the soundtrack handle and freeze the
	// picture until Ctrl+T again (or quit), returning the elapsed paused time so
	// the caller can shift its wall clock.
	if (ctrl && kc == Common::KEYCODE_t) {
		const uint32 pauseStart = _system->getMillis();
		_mixer->pauseHandle(audioHandle, true);
		bool paused = true;
		Common::Event e2;
		while (paused && !shouldQuit()) {
			while (_eventMan->pollEvent(e2)) {
				if (e2.type == Common::EVENT_KEYDOWN &&
						(e2.kbd.flags & Common::KBD_CTRL) && e2.kbd.keycode == Common::KEYCODE_t)
					paused = false;
				else if (e2.type == Common::EVENT_KEYDOWN &&
						e2.kbd.keycode == Common::KEYCODE_ESCAPE && skippable) {
					skip = true;
					paused = false;
				}
			}
			_system->updateScreen();
			_system->delayMillis(10);
		}
		_mixer->pauseHandle(audioHandle, false);
		return _system->getMillis() - pauseStart;
	}

	// Open the ScummVM debug console (a development aid, not an original
	// shortcut): backquote or Ctrl+D.
	if (kc == Common::KEYCODE_BACKQUOTE || (ctrl && kc == Common::KEYCODE_d))
		_console->attach();

	// NOT IMPLEMENTED (no faithful analog in our pre-mixed, single-stream audio):
	//
	// * Ctrl+0..9 audio channel/level select. FUN_0040e430 case 0x30..0x39 ->
	//   FUN_0042f620(n) -> FUN_0042f630: DAT_00460a54 = (long)n + DAT_00460a38,
	//   then invokes the DirectSound mixer callback (*DAT_00460ab8)(). To
	//   implement: reproduce that mixer object so the index has a target; with a
	//   single pre-mixed stream there is nothing to address.

	return 0;
}

bool CyberflixEngine::handleGlobalKey(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return false;

	const Common::KeyCode kc = event.kbd.keycode;
	bool handled = true;
	switch (kc) {
	case Common::KEYCODE_F1:
		for (int i = 0; i < 3; ++i)
			_paletteRuntime.gamma(i) *= kPaletteGammaDown;
		break;
	case Common::KEYCODE_F2:
		for (int i = 0; i < 3; ++i)
			_paletteRuntime.gamma(i) *= kPaletteGammaUp;
		break;
	case Common::KEYCODE_F3:
		_paletteRuntime.gamma(0) *= kPaletteGammaDown;
		break;
	case Common::KEYCODE_F4:
		_paletteRuntime.gamma(0) *= kPaletteGammaUp;
		break;
	case Common::KEYCODE_F5:
		_paletteRuntime.gamma(1) *= kPaletteGammaDown;
		break;
	case Common::KEYCODE_F6:
		_paletteRuntime.gamma(1) *= kPaletteGammaUp;
		break;
	case Common::KEYCODE_F7:
		_paletteRuntime.gamma(2) *= kPaletteGammaDown;
		break;
	case Common::KEYCODE_F8:
		_paletteRuntime.gamma(2) *= kPaletteGammaUp;
		break;
	case Common::KEYCODE_F9:
		for (int i = 0; i < 3; ++i)
			_paletteRuntime.gamma(i) = kDefaultPaletteGamma;
		break;
	case Common::KEYCODE_F12:
		showAboutDialog();
		return true;
	default:
		handled = false;
		break;
	}

	if (!handled)
		return false;

	for (int i = 0; i < 3; ++i)
		_paletteRuntime.setGamma(i, CLIP(_paletteRuntime.gamma(i), kPaletteGammaMin, kPaletteGammaMax));

	Palette rgb = {};
	_paletteRuntime.copyCurrent(rgb);
	programPalette(rgb);
	_system->updateScreen();
	return true;
}

void CyberflixEngine::showAboutDialog() {
	// Faithful reproduction of TI.EXE FUN_00404120's "About" MessageBox (format
	// string @0x00457380, build stamp @0x004574d0). The original also appends
	// live OS/heap/joystick/audio lines; under ScummVM we show the static engine
	// identification (the meaningful, stable part) plus a ScummVM host note.
	GUI::MessageDialog dialog(
			"DreamFactory v4.0\n"
			"(C) Copyright 1993-1996 CyberFlix, Inc.\n"
			"All rights reserved.\n"
			"\n"
			"Windows RT4 engine, DirectX version\n"
			"Compiled Mar 10 1997 at 15:52:38\n"
			"\n"
			"Running under ScummVM");
	dialog.runModal();
}

bool CyberflixEngine::questionDialog(const Common::String &message) {
	Common::String text = message;
	if (text == "Are you sure you want to quit? You can't save your game during the tour.")
		text = "Are you sure you want to quit? You can't save your game during the tour, but you can still use OPEN to load another saved game.";
	GUI::MessageDialog dialog(text, "Yes", "No");
	return dialog.runModal() == GUI::kMessageOK;
}

void CyberflixEngine::requestQuit() {
	Engine::quitGame();
}

} // End of namespace Cyberflix

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
#include "common/debug.h"
#include "common/events.h"
#include "common/system.h"
#include "common/util.h"

#include "graphics/font.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "gui/message.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/puppet.h"
#include "cyberflix/set.h"
#include "cyberflix/stage.h"

#include <math.h>

namespace Cyberflix {

// Resolve a clut name the way TI.EXE's registry lookup does (FUN_004470b0):
// the built-in names "black"/"current", and "set"/"stage"/"puppet" which alias
// the palette embedded in the currently open file of that kind. Named cluts
// registered by scripts land later.
bool CyberflixEngine::resolveClut(const Common::String &name, Palette &rgb) {
	Common::String key = name;
	key.toLowercase();
	rgb.fill(0);
	if (key == "black" || key.empty())
		return true;
	if (key == "current") {
		_paletteRuntime.copyCurrent(rgb);
		return true;
	}
	if (key == "set")
		return _setRuntime.set() && _setRuntime.set()->isOpen() && _setRuntime.set()->loadSetPalette(rgb);
	if (key == "stage")
		return _stageRuntime.stage() && _stageRuntime.stage()->isOpen() && _stageRuntime.stage()->loadStagePalette(rgb);
	if (key == "puppet") {
		if (!_puppetRuntime.loadPalette(rgb))
			return false;
		if (_puppetRuntime.grabEnabled()) {
			Palette backdrop = {};
			bool haveBackdrop = false;
			if (_setRuntime.set() && _setRuntime.set()->isOpen())
				haveBackdrop = _setRuntime.set()->loadSetPalette(backdrop);
			else if (_stageRuntime.stage() && _stageRuntime.stage()->isOpen())
				haveBackdrop = _stageRuntime.stage()->loadStagePalette(backdrop);
			if (haveBackdrop) {
				const int16 *params = _puppetRuntime.params();
				int first = CLIP<int>(params[0], 0, 256);
				int last = CLIP<int>(params[1], 0, 256);
				if (last > first)
					Common::copy(backdrop.begin() + first * kPaletteChannelCount,
							backdrop.begin() + last * kPaletteChannelCount,
							rgb.begin() + first * kPaletteChannelCount);
			}
		}
		return true;
	}
	warning("Cyberflix: clut '%s' not resolvable yet", name.c_str());
	return false;
}

// Program the hardware palette and mirror it as the "current" CLUT
// TI.EXE DAT_0045f3c8 programmed by FUN_004010f0). The original forces
// entry 0 to black and 255 to white; the game palettes already obey that.
//
// The runtime never programs the clut values directly: FUN_00401170 maps
// every component through a per-channel gamma curve built by FUN_00401220,
// table[i] = trunc(pow(i / 255.0, gamma) * 255.0), with the gamma globals
// statically initialized to 0.65 (TI.EXE .data 0x457040/48/50) and runtime
// adjustable from F1-F8 (steps *1.05 / *0.952381, clamped 0.15..2.5,
// FUN_00403bf0 from the message pump FUN_00403690; F9 resets to 0.65).
// 0.65 < 1 brightens the mid-tones considerably, which is why the original
// renders noticeably lighter than the raw clut colors. The current CLUT stays
// pre-gamma like DAT_0045f3c8 (fades interpolate raw cluts and re-apply the
// curve every step, matching FUN_0041ba80 -> FUN_004010f0).
void CyberflixEngine::updatePaletteGammaTable() {
	// Fades call programPalette() once per 60 Hz step. Gamma changes only via
	// F1-F9, so cache the expensive pow() lookup table and reuse it across fade
	// steps instead of rebuilding it for every palette update.
	_paletteRuntime.updateGammaTable();
}

void CyberflixEngine::programPalette(const Palette &rgb) {
	_paletteRuntime.setCurrent(rgb);
	updatePaletteGammaTable();

	Palette hw;
	for (int i = 0; i < kPaletteColorCount; ++i) {
		hw[i * kPaletteChannelCount + 0] = _paletteRuntime.gammaMapped(0, rgb[i * kPaletteChannelCount + 0]);
		hw[i * kPaletteChannelCount + 1] = _paletteRuntime.gammaMapped(1, rgb[i * kPaletteChannelCount + 1]);
		hw[i * kPaletteChannelCount + 2] = _paletteRuntime.gammaMapped(2, rgb[i * kPaletteChannelCount + 2]);
	}
	_system->getPaletteManager()->setPalette(hw.data(), 0, kPaletteColorCount);
}

// clut(name): snap the hardware palette to the named clut instantly
// (FUN_00446500 -> FUN_0041ba80). Pixels are untouched, so clut('black')
// makes whatever is (or gets) painted invisible until a fade reveals it.
void CyberflixEngine::setClut(const Common::String &name) {
	Palette rgb = {};
	if (!resolveClut(name, rgb))
		return;
	programPalette(rgb);
	_system->updateScreen();
	debug(1, "Cyberflix: clut('%s')", name.c_str());
}

// blackscreen() (FUN_00446b80): fill the window with black pixels via a GDI
// rect fill in the original. The palette is not touched.
void CyberflixEngine::blackScreen() {
	Graphics::Surface *screen = _system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	_system->unlockScreen();
	_system->updateScreen();
	debug(1, "Cyberflix: blackscreen()");
}

void CyberflixEngine::message(const Common::String &text) {
	// TI.EXE message() (dispatch B FUN_00444c60 case 0 -> FUN_00446240) only
	// evaluates its argument expression (FUN_00419cf0) and releases the
	// resulting temporary string slot (FUN_00419c40); it never draws anything
	// or opens a modal box. Several scripts (e.g. MAP.STG) leave debug
	// message() calls like "Map 1" that the shipping game silently ignores, so
	// mirror that by debug-logging only rather than popping a GUI dialog.
	debug(1, "Cyberflix message: %s", text.c_str());
}

// TI.EXE notedialog() (dispatch B FUN_00444c60 case 0x4e -> FUN_004461e0)
// evaluates its argument and shows a real modal note box through FUN_00408fc0:
// MessageBoxA(NULL, text, title, 0x2040) = MB_OK | MB_ICONINFORMATION |
// MB_TASKMODAL. Unlike message(), this is user-visible. The CTL.STG SAVE/OPEN
// button scripts call it (gated by `if (tour)`) to show the authored warnings
// "Sorry, you can't save/open a saved game during the tour.", so mirror the
// native single-OK modal with a GUI::MessageDialog.
void CyberflixEngine::noteDialog(const Common::String &text) {
	debug(1, "Cyberflix notedialog: %s", text.c_str());
	GUI::MessageDialog dialog(text);
	dialog.runModal();
}

void CyberflixEngine::flushEvents() {
	_eventMan->purgeMouseEvents();
	_eventMan->purgeKeyboardEvents();
}

void CyberflixEngine::drawString(const Common::String &text, int32 packedPoint, int color, int size) {
	const Graphics::Font *font = puppetRuntime().textFont(size);
	if (!font)
		return;

	const int16 x = static_cast<int16>(packedPoint >> 16);
	const int16 baselineY = static_cast<int16>(packedPoint & 0xffff);
	if (x >= kScreenWidth || baselineY >= kScreenHeight)
		return;

	Graphics::Surface *screen = _system->lockScreen();
	font->drawString(screen, text, x, baselineY - font->getFontAscent(),
			kScreenWidth - x, static_cast<uint32>(CLIP(color, 0, 255)));
	_system->unlockScreen();
	_system->updateScreen();
}

// blacktoscreen(target, n) / screentoblack(target, n): palette-only fade
// between black and the target clut, one interpolation step per 60 Hz tick
// (FUN_0041b3f0 / FUN_0041b3a0 stepping FUN_0041b200 against the scaled timer).
// Verified: TI.EXE's blacktoscreen (FUN_00446b00 -> FUN_004470b0 ->
// FUN_0041b3f0) only resolves the target CLUT and interpolates the palette; it
// does NOT re-render props. Scripts redraw first via visualeffect(plain, 0).
void CyberflixEngine::fadePalette(const Common::String &target, int steps, bool toBlack) {
	Palette to = {};
	if (!resolveClut(target, to))
		return;
	if (steps < 1)
		steps = 1;

	Palette from = {};
	if (toBlack) {
		from = to;
		to.fill(0);
	}

	debug(1, "Cyberflix: %s('%s', %d)", toBlack ? "screentoblack" : "blacktoscreen",
			target.c_str(), steps);
	fadePaletteSteps(from, to, steps);
}

bool CyberflixEngine::paletteIsBlack() const {
	return _paletteRuntime.isBlack();
}

void CyberflixEngine::fadePaletteSteps(const Palette &from, const Palette &to, int steps) {
	if (steps < 1)
		steps = 1;
	uint32 startMs = _system->getMillis();
	bool reachedFinalStep = false;
	for (int s = 1; s <= steps && !shouldQuit(); ++s) {
		Palette cur;
		for (int i = 0; i < kPaletteByteCount; ++i)
			cur[i] = static_cast<byte>((from[i] + (static_cast<int>(to[i]) - static_cast<int>(from[i])) * s / steps));
		programPalette(cur);
		_system->updateScreen();
		if (s == steps)
			reachedFinalStep = true;
		// One step per 60 Hz tick of the original's scaled timer.
		uint32 deadline = startMs + static_cast<uint32>((static_cast<uint64>(s) * 1000 / 60));
		uint32 now = _system->getMillis();
		if (now < deadline)
			_system->delayMillis(deadline - now);
		Common::Event event;
		while (_eventMan->pollEvent(event))
			; // keep the window live; fades are not skippable in the original
	}
	if (!reachedFinalStep) {
		// Preserve the old "finish at target palette" behavior if the loop exits
		// early, but avoid a duplicate final update when the last timed step
		// already displayed the target palette.
		programPalette(to);
		_system->updateScreen();
	}
}

// visualeffect(effect, dur) (FUN_00446400): mark a full-screen dirty rect
// (FUN_00441ce0(2)), run the compositor (FUN_00423a60), then apply the chosen
// visual transition to the full-screen backing buffer (FUN_004439c0). The boot
// scripts use plain (0x5dce) before blacktoscreen('set'/'stage') so the pixels
// are already redrawn while the palette is black.
void CyberflixEngine::setVisualEffect(uint16 effect, int duration) {
	if (duration < 1)
		duration = 1;
	else if (duration > 1000)
		duration = 1000;

	propRuntime().refreshPropsIfDirty(*this);
	if (_puppetRuntime.isVisible()) {
		puppetRuntime().renderCurrentFrame(*this, false);
	} else if (_setRuntime.visible() && _setRuntime.set() && _setRuntime.set()->isOpen() && _setRuntime.scene() >= 0) {
		if (_setRuntime.transitionType() != kSetTransitionNone)
			setRuntime().advanceSetTransition(*this);
		else
			setRuntime().renderSetScene(*this, _setRuntime.scene(), _setRuntime.table(), _setRuntime.angle(), _setRuntime.view());
	} else if (_stageRuntime.stage() && _stageRuntime.stage()->isOpen()) {
		// visualeffect() reaches the same update/compositor path as forceupdate();
		// it repaints pixels but native cursor state remains script-controlled.
		stageRuntime().renderStageNode(*this, _stageRuntime.node(), false);
		_propRuntime.setDirty(false);
	} else {
		_system->updateScreen();
	}

	debug(1, "Cyberflix: visualeffect(%#x, %d)", effect, duration);
}

} // End of namespace Cyberflix

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
	_deferredInputEvents.clear();
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

	// When a stage is open, draw into the retained node frame so the text
	// survives background recomposites (the native engine draws into a single
	// retained backing bitmap). Writing into the live screen instead would be
	// overwritten by the next renderStageNode, causing the W/A/D direction-key
	// labels on ctl.stg to flash and disappear.
	FrameImage *backing = nullptr;
	if (_stageRuntime.stage() && _stageRuntime.stage()->isOpen() && _stageRuntime.nodeFrameValid())
		backing = &_stageRuntime.nodeFrameData();

	if (backing && !backing->pixels.empty()) {
		Graphics::Surface surf;
		surf.setPixels(backing->pixels.begin());
		surf.w = backing->width;
		surf.h = backing->height;
		surf.pitch = backing->width;
		surf.format = Graphics::PixelFormat::createFormatCLUT8();
		font->drawString(&surf, text, x, baselineY - font->getFontAscent(),
				kScreenWidth - x, static_cast<uint32>(CLIP(color, 0, 255)));
		// Mark props dirty so the next compositor pass presents the text.
		_propRuntime.setDirty(true);
	} else {
		Graphics::Surface *screen = _system->lockScreen();
		font->drawString(screen, text, x, baselineY - font->getFontAscent(),
				kScreenWidth - x, static_cast<uint32>(CLIP(color, 0, 255)));
		_system->unlockScreen();
		_system->updateScreen();
	}
}

int CyberflixEngine::stringWidth(const Common::String &text, int fontId, int size) {
	(void)fontId; // Titanic's recovered scripts use font selector 0 for this text path.

	const Graphics::Font *font = puppetRuntime().textFont(size);
	if (!font)
		return 0;

	return font->getStringWidth(text);
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

// mixclut(a, b, first, last, weight) (FUN_00446570): resolve both cluts,
// blend entries first..last of a toward b by weight/255 (all three ints
// clamped to 0..255), and program the mix as the current palette. A14.SET's
// lights-out state uses ("set", "black", 0, 127, 240): the room's colors go
// nearly black while the interface half of the palette stays untouched.
void CyberflixEngine::mixClut(const Common::String &nameA, const Common::String &nameB,
		int first, int last, int weight) {
	Palette a = {};
	Palette b = {};
	if (!resolveClut(nameA, a) || !resolveClut(nameB, b)) {
		warning("Cyberflix: mixclut('%s', '%s'): clut not found", nameA.c_str(), nameB.c_str());
		return;
	}
	first = CLIP(first, 0, static_cast<int>(kPaletteLastColor));
	last = CLIP(last, 0, static_cast<int>(kPaletteLastColor));
	weight = CLIP(weight, 0, 255);
	debug(1, "Cyberflix: mixclut('%s', '%s', %d, %d, %d)",
			nameA.c_str(), nameB.c_str(), first, last, weight);
	for (int i = first; i <= last; ++i) {
		for (int c = 0; c < kPaletteChannelCount; ++c) {
			const uint off = Palette::colorOffset(static_cast<uint>(i)) + static_cast<uint>(c);
			a[off] = static_cast<byte>(a[off] +
					(static_cast<int>(b[off]) - static_cast<int>(a[off])) * weight / 255);
		}
	}
	programPalette(a);
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
		while (_eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
				requestQuit();
		}
	}
	if (!reachedFinalStep) {
		// Preserve the old "finish at target palette" behavior if the loop exits
		// early, but avoid a duplicate final update when the last timed step
		// already displayed the target palette.
		programPalette(to);
		_system->updateScreen();
	}
}

// The four directional wipes are the only members of the 0x5dc1..0x5dd5 effect
// family that are implemented; the rest still present the image directly.
static bool isWipeEffect(uint16 effect) {
	return effect == Script::kEffectWipeDown || effect == Script::kEffectWipeUp ||
			effect == Script::kEffectWipeRight || effect == Script::kEffectWipeLeft;
}

// Copy the visible screen into @p out, which the caller must free().
bool CyberflixEngine::captureScreen(Graphics::Surface &out) {
	Graphics::Surface *screen = _system->lockScreen();
	if (!screen) {
		_system->unlockScreen();
		return false;
	}
	out.copyFrom(*screen);
	_system->unlockScreen();
	return true;
}

// Blit one band of @p image to the screen surface without presenting it.
void CyberflixEngine::blitScreenBand(const Graphics::Surface &image, const Common::Rect &band) {
	Graphics::Surface *screen = _system->lockScreen();
	if (screen)
		screen->copyRectToSurface(image, band.left, band.top, band);
	_system->unlockScreen();
}

// Reveal @p incoming, which the compositor has already finished, by copying it
// to the visible screen one band at a time (wipedown FUN_00444270, wipeup
// FUN_00444300, wiperight FUN_00444390, wipeleft FUN_00444430). The screen must
// still be showing the outgoing image: the native wipes never touch it, they
// just march bands of the new image over it from the backing buffer via
// FUN_00444b60, then blit the whole rect once at the end.
//
// Each wipe carries a rect whose two packed dwords are (y, x) pairs — y in the
// low half, x in the high half, the same order the save format uses for props.
// wipedown/wipeup step the y halves, wiperight/wipeleft the x halves; left and
// up start at the far edge and count down, right and down start at the near
// edge and count up.
void CyberflixEngine::runWipe(const Graphics::Surface &incoming, uint16 effect, int steps) {
	if (steps < 1)
		steps = 1;
	const bool horizontal = (effect == Script::kEffectWipeLeft || effect == Script::kEffectWipeRight);
	const bool descending = (effect == Script::kEffectWipeLeft || effect == Script::kEffectWipeUp);
	const int extent = horizontal ? kScreenWidth : kScreenHeight;
	// FUN_00444430: band = (x1 - x0) / steps + 1. The +1 makes the bands overrun
	// the rect slightly, which the final full blit tidies up.
	const int band = extent / steps + 1;
	int pos = descending ? extent - band : 0;

	const uint32 startMs = _system->getMillis();
	for (int s = 0; s < steps && !shouldQuit(); ++s) {
		const int lo = CLIP(pos, 0, extent);
		const int hi = CLIP(pos + band, 0, extent);
		if (hi > lo) {
			blitScreenBand(incoming, horizontal ? Common::Rect(lo, 0, hi, kScreenHeight)
					: Common::Rect(0, lo, kScreenWidth, hi));
			_system->updateScreen();
		}
		pos += descending ? -band : band;

		// FUN_00444b40(start, i) busy-waits until FUN_00405130() >= start + i, and
		// that clock is timeGetTime() scaled by the 0.06 double at .rdata
		// 0x00456038 — i.e. 1/60 s ticks, not milliseconds. So it is one band per
		// tick, and visualeffect(wipeleft, 30) runs for 30/60 s.
		const uint32 deadline = startMs + static_cast<uint32>(static_cast<uint64>(s) * 1000 / 60);
		const uint32 now = _system->getMillis();
		if (now < deadline)
			_system->delayMillis(deadline - now);
		Common::Event event;
		while (_eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
				requestQuit();
		}
	}

	// Trailing FUN_00444b60(param_1, param_1): the complete rect, so any band
	// rounding or an early exit still lands on the finished image.
	blitScreenBand(incoming, Common::Rect(kScreenWidth, kScreenHeight));
	_system->updateScreen();
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

	// gotoflat() only queues a full-screen dirty rect (queueStageNodeRedraw), so
	// the incoming flat is still uncomposited here and the screen holds the
	// outgoing one. It has to be captured before refreshPropsIfDirty() below,
	// which repaints that queued rect with the new flat and presents it — after
	// that point there is no outgoing image left for a wipe to reveal over.
	Graphics::Surface outgoing;
	const bool wiping = isWipeEffect(effect) && captureScreen(outgoing);

	propRuntime().refreshPropsIfDirty(*this, false, !wiping);
	if (_puppetRuntime.isVisible()) {
		puppetRuntime().renderCurrentFrame(*this, false);
	} else if (_setRuntime.visible() && _setRuntime.set() && _setRuntime.set()->isOpen() && _setRuntime.scene() >= 0) {
		if (_setRuntime.transitionType() != kSetTransitionNone)
			setRuntime().advanceSetTransition(*this);
		else
			setRuntime().renderSetScene(*this, _setRuntime.scene(), _setRuntime.table(), _setRuntime.angle(), _setRuntime.view());
		// Room-change scripts hide the expensive destination setup behind a
		// black palette. BOOTFILE changeset(), for example, does:
		//
		//   screentoblack() -> opensetfile()/actor setup -> visualeffect(plain)
		//   -> blacktoscreen("set")
		//
		// In TI.EXE, visualeffect() is the handoff point between "build the new
		// room" and "reveal the new room": it runs the SET compositor and copies
		// one complete finished image to the visible surface while the hardware
		// palette is still black. blacktoscreen("set") then only interpolates the
		// palette from black to the SET CLUT; it does not redraw the panorama,
		// re-run actor scripts, or load actors during the fade.
		//
		// ScummVM's SET compositor writes that same finished image into the locked
		// screen surface, but normally leaves the backend upload pending so a burst
		// of script-driven repaints can collapse to one updateScreen(). If we keep
		// that optimization across visualeffect(), the final Grand Staircase image
		// is still pending when blacktoscreen() starts. The first non-black fade
		// step then becomes both the palette change and the first backend upload of
		// a room that contains many newly-created EXTRA.CST actors. On slow/heavy
		// actor compositions this makes the setup work visible as apparent
		// one-by-one actor pop-in: the player is watching the destination room get
		// presented instead of watching a palette fade over an already-presented
		// destination room.
		//
		// Presenting here fixes the ordering without inventing new game behavior.
		// All actor setup and compositing has already happened; this only uploads
		// the completed pixels while palette index colors are black. Afterward the
		// pending flag is clear, so blacktoscreen() has no SET image change left to
		// reveal and can remain the native palette-only fade.
		if (setRuntime().presentPendingScreenUpdate(*this))
			_cursorPresentationDirty = false;
	} else if (_stageRuntime.stage() && _stageRuntime.stage()->isOpen()) {
		// visualeffect() reaches the same update/compositor path as forceupdate();
		// it repaints pixels but native cursor state remains script-controlled.
		// A wipe reveals the freshly composited node over the outgoing image, so
		// the outgoing pixels have to be captured before the node is rendered.
		// NAREND.STG's epilogue slides use wipeleft between narration stills.
		// Native composites into a backing buffer and only the wipe copies it
		// forward, so the finished image must not be presented up front or it
		// would flash whole for a frame before the wipe ran.
		stageRuntime().renderStageNode(*this, _stageRuntime.node(), false, !wiping);
		if (wiping) {
			Graphics::Surface incoming;
			if (captureScreen(incoming)) {
				// Put the outgoing image back in the screen surface. The backend
				// is still showing it, so this is invisible; it just gives the
				// wipe the same starting state the native one relies on.
				blitScreenBand(outgoing, Common::Rect(kScreenWidth, kScreenHeight));
				runWipe(incoming, effect, duration);
				incoming.free();
			} else {
				_system->updateScreen(); // capture failed: present what we have
			}
		}
		_propRuntime.setDirty(false);
	} else {
		_system->updateScreen();
	}

	if (wiping)
		outgoing.free();

	const char *effectName = Script::methodName(effect);
	debug(1, "Cyberflix: visualeffect(%s, %d)%s", effectName ? effectName : "?", duration,
			(effect != Script::kEffectPlain && !isWipeEffect(effect)) ? " [not implemented, presented plain]" : "");
}

} // End of namespace Cyberflix

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

#include "common/archive.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/path.h"
#include "common/system.h"
#include "common/util.h"

#include "graphics/cursorman.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#ifdef USE_FREETYPE2
#include "graphics/fonts/ttf.h"
#endif
#include "graphics/surface.h"

#include "cyberflix/audio_helpers.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/puppet.h"
#include "cyberflix/runtime/graphics_helpers.h"
#include "cyberflix/set.h"
#include "cyberflix/stage.h"

namespace Cyberflix {

Common::String CyberflixEngine::currentPuppet() {
	return (_puppet && _puppet->isOpen()) ? _puppet->sourceName() : Common::String("none");
}

// ---- Puppet subsystem (TI.EXE FUN_004473c0 and friends) --------------------
// RE notes: files/decomp/stage-notes.md. This models the verified PUP archive
// lifetime, script table, and palette state used by C73 Smethels. The native
// compositor branch (FUN_00448a60), speech runner (FUN_00447ce0/FUN_00448b60),
// and bevel queue/event path (FUN_00447b30/FUN_00449370/FUN_00449e40).

void CyberflixEngine::openPuppetFile(const Common::String &name) {
	if (name.empty())
		return;
	if (_puppet && _puppet->isOpen()) {
		warning("Cyberflix: openpuppetfile('%s'): puppet already open", name.c_str());
		return;
	}

	Common::String key = name;
	key.toLowercase();
	Common::SharedPtr<Puppet> puppet(new Puppet());
	if (!puppet->open(key))
		return;

	_puppet = puppet;
	_puppetVisible = true; // FUN_00447470 sets DAT_00461202 = 1.
	_puppetCurrentAction.clear();
	_puppetCurrentFrame = 0;
	_puppetBevels.clear();
	debug(1, "Cyberflix: puppet '%s' open", _puppet->sourceName().c_str());
}

void CyberflixEngine::closePuppetFile() {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: closepuppetfile(): no puppet open");
		return;
	}
	debug(1, "Cyberflix: puppet '%s' closed", _puppet->sourceName().c_str());
	_mixer->stopHandle(_puppetSpeechHandle);
	_puppet.reset();
	_puppetVisible = false;
	_puppetBase.clear();
	_puppetCurrentAction.clear();
	_puppetCurrentFrame = 0;
	_puppetBevels.clear();
}

void CyberflixEngine::sendToPuppet(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtopuppet('%s') -> %s(%u args)", puppetName.c_str(),
			message.c_str(), args.size());
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: sendtopuppet('%s'): no puppet open", puppetName.c_str());
		return;
	}

	Common::SharedPtr<Script> script = _puppet->scriptByName(puppetName);
	if (!script) {
		warning("Cyberflix: sendtopuppet('%s'): no such puppet script", puppetName.c_str());
		return;
	}

	// PUP script lookups are immutable while the puppet file is open. The hot
	// Smethels path repeatedly dispatches one-scope puppet messages, so use the
	// fixed helper instead of building a transient scope-chain array each time.
	dispatchWithScopes(script.get(), nullptr, _puppet->sourceName(), Common::String(),
			message, args, "puppet");
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToPuppetFx(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtopuppetfx('%s') -> %s(%u args)", puppetName.c_str(),
			message.c_str(), args.size());
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: sendtopuppetfx('%s'): no puppet open", puppetName.c_str());
		return Value();
	}

	Common::SharedPtr<Script> script = _puppet->scriptByName(puppetName);
	if (!script) {
		warning("Cyberflix: sendtopuppetfx('%s'): no such puppet script", puppetName.c_str());
		return Value();
	}

	return dispatchWithScopesValue(script.get(), nullptr, _puppet->sourceName(),
			Common::String(), message, args, "puppetfx");
}

void CyberflixEngine::puppetScript(const Common::String &name) {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: puppetscript('%s'): no puppet open", name.c_str());
		return;
	}
	if (!_puppet->scriptByName(name)) {
		warning("Cyberflix: puppetscript('%s'): no such puppet script", name.c_str());
		return;
	}
	debug(1, "Cyberflix: puppetscript('%s')", name.c_str());
}

void CyberflixEngine::puppetClear() {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: puppetclear(): no puppet open");
		return;
	}
	_puppetBevels.clear();
	renderCurrentPuppetFrame(true);
	debug(1, "Cyberflix: puppetclear()");
}

void CyberflixEngine::puppetSpeak(const Common::String &name, int mode) {
	if (!_puppet || !_puppet->isOpen() || !_puppetVisible) {
		warning("Cyberflix: puppetspeak('%s'): no visible puppet", name.c_str());
		return;
	}
	const Puppet::ActionEntry *action = _puppet->actionByName(name);
	if (!action) {
		warning("Cyberflix: puppetspeak('%s'): no such action", name.c_str());
		return;
	}
	debug(1, "Cyberflix: puppetspeak('%s', %d)", name.c_str(), mode);
	playPuppetAction(*action);
}

void CyberflixEngine::puppetBevel(const Common::String &name, int mode) {
	if (!_puppet || !_puppet->isOpen() || !_puppetVisible) {
		warning("Cyberflix: puppetbevel('%s'): no visible puppet", name.c_str());
		return;
	}
	PuppetBevelOption option;
	option.text = name;
	option.id = mode;
	const int top = kScreenHeight + ((int)_puppetBevels.size() - 5) * 24;
	option.rect = Common::Rect(0, top, kScreenWidth, top + 24);
	_puppetBevels.push_back(option);
	renderPuppetBevels(true);
	debug(1, "Cyberflix: puppetbevel('%s', %d)", name.c_str(), mode);
}

void CyberflixEngine::puppetGrab(bool enabled) {
	_puppetGrab = enabled;
	debug(1, "Cyberflix: puppetgrab(%s)", enabled ? "true" : "false");
}

int CyberflixEngine::puppetEvent(int timeout) {
	if (!_puppet || !_puppet->isOpen() || !_puppetVisible) {
		warning("Cyberflix: puppetevent(%d): no visible puppet", timeout);
		return -1;
	}
	renderCurrentPuppetFrame(true);
	if (_puppetBevels.empty())
		return -1;

	setGameCursor("CURS.ARROW");
	CursorMan.showMouse(true);
	int hoverState = -1;
	const uint32 start = _system->getMillis();
	Common::Event event;
	for (;;) {
		if (shouldQuit())
			return -1;
		if (timeout >= 0 && _system->getMillis() - start >= (uint32)timeout)
			return -1;

		Common::Point mouse = _eventMan->getMousePos();
		int hover = 0;
		for (uint i = 0; i < _puppetBevels.size(); ++i) {
			if (_puppetBevels[i].rect.contains(mouse)) {
				hover = 1;
				break;
			}
		}
		if (hover != hoverState) {
			setGameCursor(hover ? "CURS131" : "CURS.ARROW");
			hoverState = hover;
		}

		while (_eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN &&
					event.kbd.keycode == Common::KEYCODE_ESCAPE)
				return -1;
			if (event.type != Common::EVENT_LBUTTONDOWN)
				continue;
			mouse = _eventMan->getMousePos();
			for (uint i = 0; i < _puppetBevels.size(); ++i) {
				if (!_puppetBevels[i].rect.contains(mouse))
					continue;
				const int id = _puppetBevels[i].id;
				debug(1, "Cyberflix: puppetevent(%d) click (%d,%d) -> %d",
						timeout, mouse.x, mouse.y, id);
				_puppetBevels.clear();
				renderCurrentPuppetFrame(true);
				return id;
			}
		}
		_system->updateScreen();
		_system->delayMillis(10);
	}
}

Common::String CyberflixEngine::puppetBase(const Common::String *newBase) {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: puppetbase(): no puppet open");
		return Common::String();
	}
	if (newBase) {
		if (newBase->size() > 31) {
			warning("Cyberflix: puppetbase('%s'): name too long", newBase->c_str());
			return _puppetBase;
		}
		_puppetBase = *newBase;
		_puppetBase.toLowercase();
		_puppetCurrentAction = _puppetBase;
		_puppetCurrentFrame = 0;
		debug(1, "Cyberflix: puppetbase('%s')", _puppetBase.c_str());
	}
	return _puppetBase;
}

bool CyberflixEngine::puppetVisible(const bool *newVisible) {
	if (!_puppet || !_puppet->isOpen())
		return false;
	if (newVisible) {
		_puppetVisible = *newVisible;
		if (_puppetVisible)
			renderCurrentPuppetFrame(true);
		debug(1, "Cyberflix: puppetvisible(%s)", _puppetVisible ? "true" : "false");
	}
	return _puppetVisible;
}

const Puppet::ActionEntry *CyberflixEngine::currentPuppetAction() const {
	if (!_puppet || !_puppet->isOpen())
		return nullptr;
	if (!_puppetCurrentAction.empty()) {
		if (const Puppet::ActionEntry *action = _puppet->actionByName(_puppetCurrentAction))
			return action;
	}
	if (!_puppetBase.empty()) {
		if (const Puppet::ActionEntry *action = _puppet->actionByName(_puppetBase))
			return action;
	}
	return _puppet->actionAt(0);
}

static void copyPuppetGrabBackdropToScreen(const Common::Array<byte> &backdrop,
		Graphics::Surface &screen) {
	if (backdrop.size() != kScreenWidth * kScreenHeight)
		return;
	copyFramePixelsToScreen(screen, backdrop.begin(), kScreenWidth, kScreenHeight, 0, 0);
}

bool CyberflixEngine::capturePuppetGrabBackdrop(Common::Array<byte> &backdrop) {
	if (!_puppetGrab) {
		backdrop.clear();
		return false;
	}

	backdrop.resize(kScreenWidth * kScreenHeight);
	memset(backdrop.begin(), 0, backdrop.size());

	if (_setVisible && _set && _set->isOpen() && _setScene >= 0) {
		// TI.EXE FUN_00449150 copies from the retained SET backing surface
		// (0x486770). ScummVM keeps that same surface in _setFrameSequence, so
		// reuse it for puppetgrab instead of replaying the compressed panorama
		// once per puppet action. Fall back to rendering only if a save/load or
		// startup edge case reaches here before the retained surface exists.
		FrameImage frame;
		const byte *pixels = nullptr;
		uint16 frameWidth = 0;
		uint16 frameHeight = 0;
		if (!_setFrameSequence.empty()) {
			pixels = _setFrameSequence.pixels();
			frameWidth = _setFrameSequence.width();
			frameHeight = _setFrameSequence.height();
		} else if (_set->renderScene((uint32)_setScene, (uint32)_setTable,
				(uint32)_setAngle, _setFrameSequence, frame)) {
			pixels = frame.pixels.begin();
			frameWidth = frame.width;
			frameHeight = frame.height;
		}
		if (pixels) {
			// The native grab does not include stage/inventory-bar composite
			// pixels or SHOP props; it copies just the current SET backing rect.
			const int x0 = _set->viewLeft();
			const int y0 = _set->viewTop();
			int left = x0;
			int srcX = 0;
			int width = frameWidth;
			if (left < 0) {
				srcX = -left;
				width -= srcX;
				left = 0;
			}
			if (left + width > kScreenWidth)
				width = kScreenWidth - left;
			if (width > 0) {
				for (int y = 0; y < frameHeight; ++y) {
					const int sy = y0 + y;
					if (sy >= 0 && sy < kScreenHeight) {
						memcpy(backdrop.begin() + (uint)sy * kScreenWidth + left,
								pixels + (uint)y * frameWidth + srcX,
								width);
					}
				}
			}
		}
		return true;
	}

	if (_stage && _stage->isOpen()) {
		FrameImage frame;
		if (_stage->renderNode((uint32)_stageNode, frame)) {
			const int width = MIN<int>(frame.width, kScreenWidth);
			const int height = MIN<int>(frame.height, kScreenHeight);
			for (int y = 0; y < height; ++y) {
				memcpy(backdrop.begin() + (uint)y * kScreenWidth,
						frame.pixels.begin() + (uint)y * frame.width, width);
			}
		}
	}

	return true;
}

bool CyberflixEngine::paintPuppetGrabBackdrop(Graphics::Surface &screen,
		const Common::Array<byte> *cachedBackdrop) {
	if (!_puppetGrab)
		return false;

	Common::Array<byte> freshBackdrop;
	const Common::Array<byte> *backdrop = cachedBackdrop;
	if (!backdrop) {
		if (!capturePuppetGrabBackdrop(freshBackdrop))
			return false;
		backdrop = &freshBackdrop;
	}

	copyPuppetGrabBackdropToScreen(*backdrop, screen);
	return true;
}

bool CyberflixEngine::renderPuppetFrame(const Puppet::ActionEntry &action,
		uint32 frameIndex, bool present, const Common::Array<byte> *cachedBackdrop) {
	Graphics::Surface *screen = _system->lockScreen();
	const bool backdropPainted = paintPuppetGrabBackdrop(*screen, cachedBackdrop);
	if (!backdropPainted)
		screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	const bool drew = _puppet->renderActionFrame(action, frameIndex, *screen, _puppetGrab);
	_system->unlockScreen();
	renderPuppetBevels(false);
	if (present)
		_system->updateScreen();
	return drew;
}

bool CyberflixEngine::renderCurrentPuppetFrame(bool present) {
	const Puppet::ActionEntry *action = currentPuppetAction();
	if (!action)
		return false;
	return renderPuppetFrame(*action, _puppetCurrentFrame, present);
}

const Graphics::Font *CyberflixEngine::textFont(int size) {
#ifdef USE_FREETYPE2
	const bool antialiasing = ConfMan.hasKey(CYBERFLIX_OPTION_FONT_ANTIALIASING) &&
			ConfMan.getBool(CYBERFLIX_OPTION_FONT_ANTIALIASING);
	if (_nativeTextFont && _nativeTextFontSize == size &&
			_nativeTextFontAntialiasing == antialiasing)
		return _nativeTextFont.get();

	_nativeTextFont.reset();
	_nativeTextFontSize = size;
	_nativeTextFontAntialiasing = antialiasing;

	const Graphics::TTFRenderMode renderMode = antialiasing
			? Graphics::kTTFRenderModeLight
			: Graphics::kTTFRenderModeMonochrome;

	static const char *const arialNames[] = {
		"arial.ttf",
		"Arial.ttf",
		"ARIAL.TTF",
		nullptr
	};

	// TI.EXE creates "Arial" with CreateFontA(). For the closest match, put
	// Microsoft's Arial.ttf directly in the configured game root, or set this
	// target's ScummVM Extra Path to a directory containing Arial.ttf. Otherwise
	// we use the bundled Liberation Sans fallback, as other ScummVM engines do
	// for Arial-like text.
	for (const char *const *name = arialNames; *name; ++name) {
		Common::SeekableReadStream *stream = SearchMan.createReadStreamForMember(
				Common::Path(*name, Common::Path::kNoSeparator));
		if (!stream)
			continue;
		_nativeTextFont.reset(Graphics::loadTTFFont(stream, DisposeAfterUse::YES,
				size, Graphics::kTTFSizeModeCharacter, 0, 0, renderMode));
		if (_nativeTextFont)
			return _nativeTextFont.get();
	}

	_nativeTextFont.reset(Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf",
			size, Graphics::kTTFSizeModeCharacter, 0, 0, renderMode));
	if (_nativeTextFont)
		return _nativeTextFont.get();
#endif

	const Graphics::Font *font = size >= 12
			? FontMan.getFontByUsage(Graphics::FontManager::kGUIFont)
			: FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
	return font;
}

void CyberflixEngine::renderPuppetBevels(bool present) {
	if (!_puppet || !_puppet->isOpen())
		return;

	Graphics::Surface *screen = _system->lockScreen();
	// FUN_00449370 always draws the PUP master bevel backdrop before checking
	// the queued bevel count, so Smethels' text-row panel stays visible even
	// during speech beats that offer no clickable choices.
	_puppet->renderBevelBackdrop(*screen, kScreenHeight, kScreenWidth);

	const Graphics::Font *font = textFont(_puppetParams[5]);
	if (font) {
		for (uint i = 0; i < _puppetBevels.size(); ++i) {
			const Common::Rect &rect = _puppetBevels[i].rect;
			Common::Rect clipped = rect;
			clipped.clip(Common::Rect(kScreenWidth, kScreenHeight));
			if (clipped.isEmpty())
				continue;
			const int baselineY = rect.top + 0x10;
			const int x = rect.left + _puppetParams[9];
			font->drawString(screen, _puppetBevels[i].text, x, baselineY - font->getFontAscent(),
					kScreenWidth - x, (uint32)CLIP<int>(_puppetParams[2], 0, 255));
		}
	}
	_system->unlockScreen();
	if (present)
		_system->updateScreen();
}

void CyberflixEngine::playPuppetAction(const Puppet::ActionEntry &action) {
	_puppetCurrentAction = action.name;
	_puppetCurrentFrame = 0;
	_puppetBevels.clear();
	_mixer->stopHandle(_puppetSpeechHandle);

	Common::Array<byte> pcm;
	_puppet->decodeActionAudio(action, pcm);
	if (!pcm.empty()) {
		Audio::SeekableAudioStream *stream = makeOwnedRawPcmStream(pcm);
		if (stream) {
			_mixer->playStream(Audio::Mixer::kSpeechSoundType, &_puppetSpeechHandle, stream);
			_mixer->setChannelVolume(_puppetSpeechHandle, effectiveAudioVolume(255));
		}
	}

	const uint32 frameCount = action.frameCount ? action.frameCount : 1;
	uint32 lastFrame = (uint32)-1;
	uint32 lastPresentedFrame = (uint32)-1;
	const uint32 wallStart = _system->getMillis();
	// Puppet speech redraws animation frames at native 30 fps against the same
	// puppetgrab backdrop. Cache that grabbed SET/STG image once per action so
	// each frame only restores a memory copy instead of re-decoding the room
	// background and locking/unlocking the backend twice.
	Common::Array<byte> grabBackdrop;
	const Common::Array<byte> *cachedBackdrop =
			capturePuppetGrabBackdrop(grabBackdrop) ? &grabBackdrop : nullptr;
	Common::Event event;
	const uint32 kPuppetActionPollCapMs = 33; // one native 30 fps puppet frame
	for (;;) {
		if (shouldQuit())
			break;
		uint32 elapsed = _mixer->isSoundHandleActive(_puppetSpeechHandle)
				? _mixer->getSoundElapsedTime(_puppetSpeechHandle)
				: (_system->getMillis() - wallStart);
		uint32 frame = (uint32)((uint64)elapsed * 60 / 1000 / 2);
		if (frame >= frameCount)
			frame = frameCount - 1;
		if (frame != lastFrame) {
			_puppetCurrentFrame = frame;
			if (lastPresentedFrame == (uint32)-1 ||
					!_puppet->actionFramesVisuallyEqual(action, lastPresentedFrame, frame, _puppetGrab)) {
				renderPuppetFrame(action, frame, true, cachedBackdrop);
				lastPresentedFrame = frame;
			}
			lastFrame = frame;
		}
		bool aborted = false;
		while (_eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN &&
					event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				_mixer->stopHandle(_puppetSpeechHandle);
				aborted = true;
				break;
			}
		}
		if (aborted)
			break;
		if (!_mixer->isSoundHandleActive(_puppetSpeechHandle)) {
			if (pcm.empty() && frame + 1 < frameCount) {
				const uint32 nextFrameMs = (uint32)(((uint64)frame + 1) * 1000 + 29) / 30;
				// Silent puppet actions are clocked from wall time at the same
				// 30 fps cadence as speech. Sleep toward the next frame boundary
				// instead of waking the backend event pump several times per
				// frame; there is no cursor tracking during action playback.
				_system->delayMillis(nextFrameMs > elapsed ?
						MIN<uint32>(nextFrameMs - elapsed, kPuppetActionPollCapMs) : 1);
				continue;
			}
			break;
		}
		const uint32 nextFrameMs = (uint32)(((uint64)frame + 1) * 1000 + 29) / 30;
		// Speech playback is frame-clocked from the mixer at native 30 fps.
		// Poll Esc at that same cadence, avoiding extra SDL/Cocoa event-pump
		// wakeups between frames when the picture cannot change.
		_system->delayMillis(nextFrameMs > elapsed ?
				MIN<uint32>(nextFrameMs - elapsed, kPuppetActionPollCapMs) : 1);
	}
	_puppetCurrentFrame = frameCount - 1;
	// If playback timing already presented the final visual frame, avoid one
	// redundant backend swap at the end of the action.
	if (lastPresentedFrame == (uint32)-1 ||
			!_puppet->actionFramesVisuallyEqual(action, lastPresentedFrame, _puppetCurrentFrame, _puppetGrab))
		renderPuppetFrame(action, _puppetCurrentFrame, true, cachedBackdrop);
}

int CyberflixEngine::puppetParam(int selector, const int *newValue) {
	if (selector < 1 || selector > (int)ARRAYSIZE(_puppetParams)) {
		warning("Cyberflix: puppetparam(%d): selector out of range", selector);
		return 0;
	}
	int16 &slot = _puppetParams[selector - 1];
	if (newValue) {
		slot = (int16)*newValue;
		debug(1, "Cyberflix: puppetparam(%d, %d)", selector, *newValue);
	}
	return slot;
}

int CyberflixEngine::countPuppets() {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: countpuppets(): no puppet open");
		return 0;
	}
	return (int)_puppet->scriptCount();
}

Common::String CyberflixEngine::indexToPuppet(int index) {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: indextopuppet(%d): no puppet open", index);
		return Common::String();
	}
	if (index < 1 || (uint32)index > _puppet->scriptCount()) {
		warning("Cyberflix: indextopuppet(%d): index out of range", index);
		return Common::String();
	}
	return _puppet->scriptName((uint32)index - 1);
}

} // End of namespace Cyberflix

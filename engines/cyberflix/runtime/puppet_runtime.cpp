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
#include "common/archive.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/path.h"
#include "common/ptr.h"
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
#include "cyberflix/runtime/puppet_runtime.h"
#include "cyberflix/set.h"
#include "cyberflix/stage.h"

namespace Cyberflix {

PuppetRuntime::~PuppetRuntime() {
}

Common::String PuppetRuntime::currentPuppet() const {
	return isOpen() ? _puppet->sourceName() : Common::String("none");
}

bool PuppetRuntime::loadPalette(Palette &rgb) const {
	return isOpen() && _puppet->loadPuppetPalette(rgb);
}

void PuppetRuntime::open(const Common::SharedPtr<Puppet> &puppet) {
	_puppet = puppet;
	_visible = true; // FUN_00447470 sets DAT_00461202 = 1.
	_currentAction.clear();
	_currentFrame = 0;
	_bevels.clear();
}

void PuppetRuntime::close(Audio::Mixer *mixer) {
	if (mixer)
		mixer->stopHandle(_speechHandle);
	_puppet.reset();
	_visible = false;
	_base.clear();
	_currentAction.clear();
	_currentFrame = 0;
	_bevels.clear();
}

// ---- Puppet subsystem (TI.EXE FUN_004473c0 and friends) --------------------
// This models the verified PUP archive
// lifetime, script table, and palette state used by C73 Smethels. The native
// compositor branch (FUN_00448a60), speech runner (FUN_00447ce0/FUN_00448b60),
// and bevel queue/event path (FUN_00447b30/FUN_00449370/FUN_00449e40).

void PuppetRuntime::openPuppetFile(const Common::String &name) {
	if (name.empty())
		return;
	if (isOpen()) {
		warning("Cyberflix: openpuppetfile('%s'): puppet already open", name.c_str());
		return;
	}

	Common::String key = name;
	key.toLowercase();
	Common::SharedPtr<Puppet> puppet(new Puppet());
	if (!puppet->open(key))
		return;

	open(puppet);
	debug(1, "Cyberflix: puppet '%s' open", puppet->sourceName().c_str());
}

void PuppetRuntime::closePuppetFile(CyberflixEngine &engine) {
	if (!isOpen()) {
		warning("Cyberflix: closepuppetfile(): no puppet open");
		return;
	}
	debug(1, "Cyberflix: puppet '%s' closed", _puppet->sourceName().c_str());
	close(engine._mixer);
}

void PuppetRuntime::sendToPuppet(CyberflixEngine &engine, const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtopuppet('%s') -> %s(%u args)", puppetName.c_str(),
			message.c_str(), args.size());
	if (!isOpen()) {
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
	engine.dispatchWithScopes(script.get(), nullptr, _puppet->sourceName(), Common::String(),
			message, args, "puppet");
	engine.propRuntime().refreshPropsIfDirty(engine);
}

Value PuppetRuntime::sendToPuppetFx(CyberflixEngine &engine, const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtopuppetfx('%s') -> %s(%u args)", puppetName.c_str(),
			message.c_str(), args.size());
	if (!isOpen()) {
		warning("Cyberflix: sendtopuppetfx('%s'): no puppet open", puppetName.c_str());
		return Value();
	}

	Common::SharedPtr<Script> script = _puppet->scriptByName(puppetName);
	if (!script) {
		warning("Cyberflix: sendtopuppetfx('%s'): no such puppet script", puppetName.c_str());
		return Value();
	}

	return engine.dispatchWithScopesValue(script.get(), nullptr, _puppet->sourceName(),
			Common::String(), message, args, "puppetfx");
}

void PuppetRuntime::puppetScript(const Common::String &name) {
	if (!isOpen()) {
		warning("Cyberflix: puppetscript('%s'): no puppet open", name.c_str());
		return;
	}
	if (!_puppet->scriptByName(name)) {
		warning("Cyberflix: puppetscript('%s'): no such puppet script", name.c_str());
		return;
	}
	debug(1, "Cyberflix: puppetscript('%s')", name.c_str());
}

void PuppetRuntime::puppetClear(CyberflixEngine &engine) {
	if (!isOpen()) {
		warning("Cyberflix: puppetclear(): no puppet open");
		return;
	}
	_bevels.clear();
	renderCurrentFrame(engine, true);
	debug(1, "Cyberflix: puppetclear()");
}

void PuppetRuntime::puppetSpeak(CyberflixEngine &engine, const Common::String &name, int mode) {
	if (!isVisible()) {
		warning("Cyberflix: puppetspeak('%s'): no visible puppet", name.c_str());
		return;
	}
	const Puppet::ActionEntry *action = _puppet->actionByName(name);
	if (!action) {
		warning("Cyberflix: puppetspeak('%s'): no such action", name.c_str());
		return;
	}
	debug(1, "Cyberflix: puppetspeak('%s', %d)", name.c_str(), mode);
	playAction(engine, *action);
}

void PuppetRuntime::puppetBevel(CyberflixEngine &engine, const Common::String &name, int mode) {
	if (!isVisible()) {
		warning("Cyberflix: puppetbevel('%s'): no visible puppet", name.c_str());
		return;
	}
	BevelOption option;
	option.text = name;
	option.id = mode;
	const int top = kScreenHeight + (static_cast<int>(_bevels.size()) - 5) * 24;
	option.rect = Common::Rect(0, top, kScreenWidth, top + 24);
	_bevels.push_back(option);
	renderBevels(engine, true);
	debug(1, "Cyberflix: puppetbevel('%s', %d)", name.c_str(), mode);
}

void PuppetRuntime::puppetGrab(bool enabled) {
	_grab = enabled;
	debug(1, "Cyberflix: puppetgrab(%s)", enabled ? "true" : "false");
}

int PuppetRuntime::puppetEvent(CyberflixEngine &engine, int timeout) {
	if (!isVisible()) {
		warning("Cyberflix: puppetevent(%d): no visible puppet", timeout);
		return -1;
	}
	renderCurrentFrame(engine, true);
	if (_bevels.empty())
		return -1;

	engine.setGameCursor("CURS.ARROW");
	CursorMan.showMouse(true);
	int hoverState = -1;
	const uint32 start = engine._system->getMillis();
	Common::Event event;
	for (;;) {
		if (engine.shouldQuit())
			return -1;
		if (timeout >= 0 && engine._system->getMillis() - start >= static_cast<uint32>(timeout))
			return -1;

		Common::Point mouse = engine._eventMan->getMousePos();
		int hover = 0;
		for (uint i = 0; i < _bevels.size(); ++i) {
			if (_bevels[i].rect.contains(mouse)) {
				hover = 1;
				break;
			}
		}
		if (hover != hoverState) {
			engine.setGameCursor(hover ? "CURS131" : "CURS.ARROW");
			hoverState = hover;
		}

		bool cursorDirty = false;
		while (engine._eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				engine.requestQuit();
				return -1;
			}
			if (event.type == Common::EVENT_MOUSEMOVE) {
				cursorDirty = true;
				continue;
			}
			if (event.type == Common::EVENT_KEYDOWN &&
					event.kbd.keycode == Common::KEYCODE_ESCAPE)
				return -1;
			if (event.type != Common::EVENT_LBUTTONDOWN)
				continue;
			mouse = engine._eventMan->getMousePos();
			for (uint i = 0; i < _bevels.size(); ++i) {
				if (!_bevels[i].rect.contains(mouse))
					continue;
				const int id = _bevels[i].id;
				debug(1, "Cyberflix: puppetevent(%d) click (%d,%d) -> %d",
						timeout, mouse.x, mouse.y, id);
				_bevels.clear();
				renderCurrentFrame(engine, true);
				return id;
			}
		}
		if (cursorDirty)
			engine._cursorPresentationDirty = true;
		engine.presentCursorIfDirty();
		engine.delayMillisWithCursorUpdates(10);
	}
}

Common::String PuppetRuntime::getPuppetBase() const {
	if (!isOpen()) {
		warning("Cyberflix: puppetbase(): no puppet open");
		return Common::String();
	}
	return _base;
}

Common::String PuppetRuntime::setPuppetBase(const Common::String &newBase) {
	if (!isOpen()) {
		warning("Cyberflix: puppetbase(): no puppet open");
		return Common::String();
	}
	if (newBase.size() > 31) {
		warning("Cyberflix: puppetbase('%s'): name too long", newBase.c_str());
		return _base;
	}
	_base = newBase;
	_base.toLowercase();
	_currentAction = _base;
	_currentFrame = 0;
	debug(1, "Cyberflix: puppetbase('%s')", _base.c_str());
	return _base;
}

bool PuppetRuntime::getPuppetVisible() const {
	if (!isOpen())
		return false;
	return _visible;
}

bool PuppetRuntime::setPuppetVisible(CyberflixEngine &engine, bool visible) {
	if (!isOpen())
		return false;
	_visible = visible;
	if (_visible)
		renderCurrentFrame(engine, true);
	debug(1, "Cyberflix: puppetvisible(%s)", _visible ? "true" : "false");
	return _visible;
}

const Puppet::ActionEntry *PuppetRuntime::currentActionEntry() const {
	if (!isOpen())
		return nullptr;
	if (!_currentAction.empty()) {
		if (const Puppet::ActionEntry *action = _puppet->actionByName(_currentAction))
			return action;
	}
	if (!_base.empty()) {
		if (const Puppet::ActionEntry *action = _puppet->actionByName(_base))
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

bool PuppetRuntime::captureGrabBackdrop(CyberflixEngine &engine, Common::Array<byte> &backdrop) {
	if (!_grab) {
		backdrop.clear();
		return false;
	}

	backdrop.resize(kScreenWidth * kScreenHeight);
	Common::fill(backdrop.begin(), backdrop.end(), 0);

	SetRuntime &setRuntime = engine._setRuntime;
	if (setRuntime.visible() && setRuntime.set() && setRuntime.set()->isOpen() && setRuntime.scene() >= 0) {
		// TI.EXE FUN_00449150 copies from the retained SET backing surface
		// (0x486770). ScummVM keeps that same surface in SetRuntime, so
		// reuse it for puppetgrab instead of replaying the compressed panorama
		// once per puppet action. Fall back to rendering only if a save/load or
		// startup edge case reaches here before the retained surface exists.
		FrameImage frame;
		const byte *pixels = nullptr;
		uint16 frameWidth = 0;
		uint16 frameHeight = 0;
		if (!setRuntime.frameSequence().empty()) {
			pixels = setRuntime.frameSequence().pixels();
			frameWidth = setRuntime.frameSequence().width();
			frameHeight = setRuntime.frameSequence().height();
		} else if (setRuntime.set()->renderScene(static_cast<uint32>(setRuntime.scene()), static_cast<uint32>(setRuntime.table()),
				static_cast<uint32>(setRuntime.angle()), setRuntime.frameSequence(), frame)) {
			pixels = frame.pixels.begin();
			frameWidth = frame.width;
			frameHeight = frame.height;
		}
		if (pixels) {
			// The native grab does not include stage/inventory-bar composite
			// pixels or SHOP props; it copies just the current SET backing rect.
			const int x0 = setRuntime.set()->viewLeft();
			const int y0 = setRuntime.set()->viewTop();
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
						memcpy(backdrop.begin() + static_cast<uint>(sy) * kScreenWidth + left,
								pixels + static_cast<uint>(y) * frameWidth + srcX,
								width);
					}
				}
			}
		}
		return true;
	}

	if (engine._stageRuntime.stage() && engine._stageRuntime.stage()->isOpen()) {
		FrameImage frame;
		if (engine._stageRuntime.stage()->renderNode(static_cast<uint32>(engine._stageRuntime.node()), frame)) {
			const int width = MIN<int>(frame.width, kScreenWidth);
			const int height = MIN<int>(frame.height, kScreenHeight);
			for (int y = 0; y < height; ++y) {
				memcpy(backdrop.begin() + static_cast<uint>(y) * kScreenWidth,
						frame.pixels.begin() + static_cast<uint>(y) * frame.width, width);
			}
		}
	}

	return true;
}

bool PuppetRuntime::paintGrabBackdrop(CyberflixEngine &engine, Graphics::Surface &screen,
		const Common::Array<byte> *cachedBackdrop) {
	if (!_grab)
		return false;

	Common::Array<byte> freshBackdrop;
	const Common::Array<byte> *backdrop = cachedBackdrop;
	if (!backdrop) {
		if (!captureGrabBackdrop(engine, freshBackdrop))
			return false;
		backdrop = &freshBackdrop;
	}

	copyPuppetGrabBackdropToScreen(*backdrop, screen);
	return true;
}

bool PuppetRuntime::renderFrame(CyberflixEngine &engine, const Puppet::ActionEntry &action,
		uint32 frameIndex, bool present, const Common::Array<byte> *cachedBackdrop) {
	Graphics::Surface *screen = engine._system->lockScreen();
	const bool backdropPainted = paintGrabBackdrop(engine, *screen, cachedBackdrop);
	if (!backdropPainted)
		screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	const bool drew = _puppet->renderActionFrame(action, frameIndex, *screen, _grab);
	engine._system->unlockScreen();
	renderBevels(engine, false);
	if (present)
		engine._system->updateScreen();
	return drew;
}

bool PuppetRuntime::renderCurrentFrame(CyberflixEngine &engine, bool present) {
	const Puppet::ActionEntry *action = currentActionEntry();
	if (!action)
		return false;
	return renderFrame(engine, *action, _currentFrame, present);
}

const Graphics::Font *PuppetRuntime::textFont(int size) {
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
		Common::ScopedPtr<Common::SeekableReadStream> stream(SearchMan.createReadStreamForMember(
				Common::Path(*name, Common::Path::kNoSeparator)));
		if (!stream)
			continue;
		_nativeTextFont.reset(Graphics::loadTTFFont(stream.get(), DisposeAfterUse::YES,
				size, Graphics::kTTFSizeModeCharacter, 0, 0, renderMode));
		if (_nativeTextFont) {
			stream.release();
			return _nativeTextFont.get();
		}
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

void PuppetRuntime::renderBevels(CyberflixEngine &engine, bool present) {
	if (!isOpen())
		return;

	Graphics::Surface *screen = engine._system->lockScreen();
	// FUN_00449370 always draws the PUP master bevel backdrop before checking
	// the queued bevel count, so Smethels' text-row panel stays visible even
	// during speech beats that offer no clickable choices.
	_puppet->renderBevelBackdrop(*screen, kScreenHeight, kScreenWidth);

	const Graphics::Font *font = textFont(_params[5]);
	if (font) {
		for (uint i = 0; i < _bevels.size(); ++i) {
			const Common::Rect &rect = _bevels[i].rect;
			Common::Rect clipped = rect;
			clipped.clip(Common::Rect(kScreenWidth, kScreenHeight));
			if (clipped.isEmpty())
				continue;
			const int baselineY = rect.top + 0x10;
			const int x = rect.left + _params[9];
			font->drawString(screen, _bevels[i].text, x, baselineY - font->getFontAscent(),
					kScreenWidth - x, static_cast<uint32>(CLIP<int>(_params[2], 0, 255)));
		}
	}
	engine._system->unlockScreen();
	if (present)
		engine._system->updateScreen();
}

void PuppetRuntime::playAction(CyberflixEngine &engine, const Puppet::ActionEntry &action) {
	_currentAction = action.name;
	_currentFrame = 0;
	_bevels.clear();
	engine._mixer->stopHandle(_speechHandle);

	Common::Array<byte> pcm;
	_puppet->decodeActionAudio(action, pcm);
	if (!pcm.empty()) {
		Audio::SeekableAudioStream *stream = makeOwnedRawPcmStream(pcm);
		if (stream) {
			engine._mixer->playStream(Audio::Mixer::kSpeechSoundType, &_speechHandle, stream);
			engine._mixer->setChannelVolume(_speechHandle, engine._audioRuntime.effectiveAudioVolume(255));
		}
	}

	const uint32 frameCount = action.frameCount ? action.frameCount : 1;
	uint32 lastFrame = static_cast<uint32>(-1);
	uint32 lastPresentedFrame = static_cast<uint32>(-1);
	const uint32 wallStart = engine._system->getMillis();
	// Puppet speech redraws animation frames at native 30 fps against the same
	// puppetgrab backdrop. Cache that grabbed SET/STG image once per action so
	// each frame only restores a memory copy instead of re-decoding the room
	// background and locking/unlocking the backend twice.
	Common::Array<byte> grabBackdrop;
	const Common::Array<byte> *cachedBackdrop =
			captureGrabBackdrop(engine, grabBackdrop) ? &grabBackdrop : nullptr;
	Common::Event event;
	const uint32 kPuppetActionPollCapMs = 33; // one native 30 fps puppet frame
	for (;;) {
		if (engine.shouldQuit())
			break;
		uint32 elapsed = engine._mixer->isSoundHandleActive(_speechHandle)
				? engine._mixer->getSoundElapsedTime(_speechHandle)
				: (engine._system->getMillis() - wallStart);
		uint32 frame = static_cast<uint32>((static_cast<uint64>(elapsed) * 60 / 1000 / 2));
		if (frame >= frameCount)
			frame = frameCount - 1;
		if (frame != lastFrame) {
			_currentFrame = frame;
			if (lastPresentedFrame == static_cast<uint32>(-1) ||
					!_puppet->actionFramesVisuallyEqual(action, lastPresentedFrame, frame, _grab)) {
				renderFrame(engine, action, frame, true, cachedBackdrop);
				lastPresentedFrame = frame;
			}
			lastFrame = frame;
		}
		bool aborted = false;
		bool cursorDirty = false;
		while (engine._eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				engine.requestQuit();
				aborted = true;
				break;
			}
			if (event.type == Common::EVENT_MOUSEMOVE) {
				cursorDirty = true;
				continue;
			}
			if (event.type == Common::EVENT_KEYDOWN &&
					event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				engine._mixer->stopHandle(_speechHandle);
				aborted = true;
				break;
			}
		}
		if (aborted)
			break;
		if (cursorDirty)
			engine._cursorPresentationDirty = true;
		engine.presentCursorIfDirty();
		if (!engine._mixer->isSoundHandleActive(_speechHandle)) {
			if (pcm.empty() && frame + 1 < frameCount) {
				const uint32 nextFrameMs = static_cast<uint32>(((static_cast<uint64>(frame) + 1) * 1000 + 29)) / 30;
				// Silent puppet actions are clocked from wall time at the same
				// 30 fps cadence as speech. Sleep toward the next frame boundary
				// while keeping ScummVM's software cursor responsive between
				// puppet animation frames.
				engine.delayMillisWithCursorUpdates(nextFrameMs > elapsed ?
						MIN<uint32>(nextFrameMs - elapsed, kPuppetActionPollCapMs) : 1);
				continue;
			}
			break;
		}
		const uint32 nextFrameMs = static_cast<uint32>(((static_cast<uint64>(frame) + 1) * 1000 + 29)) / 30;
		// Speech playback is frame-clocked from the mixer at native 30 fps.
		// The picture only changes on those frames, but the software cursor must
		// still be pumped/presented between them to approximate native Win32
		// cursor movement.
		engine.delayMillisWithCursorUpdates(nextFrameMs > elapsed ?
				MIN<uint32>(nextFrameMs - elapsed, kPuppetActionPollCapMs) : 1);
	}
	_currentFrame = frameCount - 1;
	// If playback timing already presented the final visual frame, avoid one
	// redundant backend swap at the end of the action.
	if (lastPresentedFrame == static_cast<uint32>(-1) ||
			!_puppet->actionFramesVisuallyEqual(action, lastPresentedFrame, _currentFrame, _grab))
		renderFrame(engine, action, _currentFrame, true, cachedBackdrop);
}

int PuppetRuntime::getPuppetParam(int selector) const {
	if (selector < 1 || selector > 10) {
		warning("Cyberflix: puppetparam(%d): selector out of range", selector);
		return 0;
	}
	return _params[selector - 1];
}

int PuppetRuntime::setPuppetParam(int selector, int newValue) {
	if (selector < 1 || selector > 10) {
		warning("Cyberflix: puppetparam(%d): selector out of range", selector);
		return 0;
	}
	int16 &slot = _params[selector - 1];
	slot = static_cast<int16>(newValue);
	debug(1, "Cyberflix: puppetparam(%d, %d)", selector, newValue);
	return slot;
}

int PuppetRuntime::countPuppets() const {
	if (!isOpen()) {
		warning("Cyberflix: countpuppets(): no puppet open");
		return 0;
	}
	return static_cast<int>(_puppet->scriptCount());
}

Common::String PuppetRuntime::indexToPuppet(int index) const {
	if (!isOpen()) {
		warning("Cyberflix: indextopuppet(%d): no puppet open", index);
		return Common::String();
	}
	if (index < 1 || static_cast<uint32>(index) > _puppet->scriptCount()) {
		warning("Cyberflix: indextopuppet(%d): index out of range", index);
		return Common::String();
	}
	return _puppet->scriptName(static_cast<uint32>(index) - 1);
}

} // End of namespace Cyberflix

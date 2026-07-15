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

#ifndef CYBERFLIX_RUNTIME_PUPPET_RUNTIME_H
#define CYBERFLIX_RUNTIME_PUPPET_RUNTIME_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/rect.h"
#include "common/str.h"

#include "audio/mixer.h"

#include "cyberflix/puppet.h"
#include "cyberflix/vm.h"

namespace Graphics {
class Font;
}

namespace Cyberflix {

class CyberflixEngine;

class PuppetRuntime {
public:
	struct BevelOption {
		Common::String text;
		int id = 0;
		Common::Rect rect;
	};

	~PuppetRuntime();

	bool isOpen() const { return _puppet && _puppet->isOpen(); }
	bool isVisible() const { return isOpen() && _visible; }
	Common::String currentPuppet() const;
	bool loadPalette(Palette &rgb) const;

	bool grabEnabled() const { return _grab; }
	const int16 *params() const { return _params; }

	void openPuppetFile(const Common::String &name);
	void closePuppetFile(CyberflixEngine &engine);
	void sendToPuppet(CyberflixEngine &engine, const Common::String &puppetName,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToPuppetFx(CyberflixEngine &engine, const Common::String &puppetName,
			const Common::String &message, const Common::Array<Value> &args);
	void puppetScript(const Common::String &name);
	void puppetClear(CyberflixEngine &engine);
	void puppetSpeak(CyberflixEngine &engine, const Common::String &name, int mode);
	void puppetBevel(CyberflixEngine &engine, const Common::String &name, int mode);
	void puppetGrab(bool enabled);
	int puppetEvent(CyberflixEngine &engine, int timeout);
	Common::String getPuppetBase() const;
	Common::String setPuppetBase(const Common::String &newBase);
	bool getPuppetVisible() const;
	bool setPuppetVisible(CyberflixEngine &engine, bool visible);
	bool renderCurrentFrame(CyberflixEngine &engine, bool present);
	const Graphics::Font *textFont(int size);
	int getPuppetParam(int selector) const;
	int setPuppetParam(int selector, int newValue);
	int countPuppets() const;
	Common::String indexToPuppet(int index) const;

private:
	void open(const Common::SharedPtr<Puppet> &puppet);
	void close(Audio::Mixer *mixer);

	const Puppet::ActionEntry *currentActionEntry() const;
	bool captureGrabBackdrop(CyberflixEngine &engine, Common::Array<byte> &backdrop);
	bool paintGrabBackdrop(CyberflixEngine &engine, Graphics::Surface &screen,
			const Common::Array<byte> *cachedBackdrop);
	bool renderFrame(CyberflixEngine &engine, const Puppet::ActionEntry &action,
			uint32 frameIndex, bool present,
			const Common::Array<byte> *cachedBackdrop = nullptr);
	void renderBevels(CyberflixEngine &engine, bool present);
	void playAction(CyberflixEngine &engine, const Puppet::ActionEntry &action);

	/**
	 * Re-play the most recently spoken line, mirroring native FUN_00449e40's
	 * click-on-character path: a left-click that misses every bevel but lands
	 * in the puppet display area re-runs the last puppetspeak action. Returns
	 * true if a line was replayed (so the caller re-enters its input loop).
	 */
	bool replayLastSpokenAction(CyberflixEngine &engine);

	/** Currently open puppet archive (TI.EXE DAT_00461200 cluster). */
	Common::SharedPtr<Puppet> _puppet;
	Common::String _base;
	Common::String _currentAction;
	uint32 _currentFrame = 0;
	bool _visible = false;
	bool _grab = false;
	int16 _params[10] = { 0, 0x80, 0xfa, 0xfb, 0x378, 0x0c, 0, 0, 2, 8 };
	Audio::SoundHandle _speechHandle;
	Common::Array<BevelOption> _bevels;
	/**
	 * Ring of recently spoken action names (native DAT_00486790, count
	 * DAT_00486cb4, capped at 3). puppetspeak pushes the action name (skipping a
	 * duplicate of the immediately-prior entry); a click on the character
	 * replays the most recent.
	 */
	Common::Array<Common::String> _spokenActions;
	Common::ScopedPtr<Graphics::Font> _nativeTextFont;
	int _nativeTextFontSize = 0;
};

} // End of namespace Cyberflix

#endif

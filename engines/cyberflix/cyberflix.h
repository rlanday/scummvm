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

#ifndef CYBERFLIX_CYBERFLIX_H
#define CYBERFLIX_CYBERFLIX_H

#include "common/random.h"
#include "common/error.h"
#include "common/ptr.h"
#include "common/rect.h"

#include "engines/engine.h"

#include "audio/mixer.h"

#include "cyberflix/audio/audio_runtime.h"
#include "cyberflix/cast.h"
#include "cyberflix/detection.h"
#include "cyberflix/image.h"
#include "cyberflix/puppet.h"
#include "cyberflix/runtime/actors.h"
#include "cyberflix/runtime/cursor.h"
#include "cyberflix/runtime/loops.h"
#include "cyberflix/runtime/movie.h"
#include "cyberflix/runtime/palette.h"
#include "cyberflix/runtime/paths.h"
#include "cyberflix/runtime/props.h"
#include "cyberflix/runtime/puppet_runtime.h"
#include "cyberflix/runtime/set_runtime.h"
#include "cyberflix/runtime/stage_runtime.h"
#include "cyberflix/runtime/timing.h"
#include "cyberflix/shop.h"
#include "cyberflix/vm_host.h"

namespace Cyberflix {

class Console;
class Script;
class Stage;
class Set;
}

namespace Common {
struct Event;
}

namespace Audio {
class SoundHandle;
}

namespace Graphics {
class Font;
}

namespace Cyberflix {

// The game renders into a 512x384, 8-bit palettised framebuffer (the menu and
// in-game node images are full 512x384; the LOGO movie's frames are 512x264 and
// sit letterboxed within it).
enum {
	kScreenWidth = 512,
	kScreenHeight = 384
};

class CyberflixEngine : public Engine, public VMHost,
		public CyberflixMovieVMHost, public CyberflixStageSetVMHost,
		public CyberflixPuppetVMHost, public CyberflixAudioVMHost,
		public CyberflixActorVMHost, public CyberflixPropVMHost {
public:
	CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc);
	~CyberflixEngine() override;

	Common::Error run() override;

	AudioRuntime &audioRuntime() { return _audioRuntime; }
	const AudioRuntime &audioRuntime() const { return _audioRuntime; }
	ActorRuntime &actorRuntime() { return _actorRuntime; }
	const ActorRuntime &actorRuntime() const { return _actorRuntime; }
	CursorRuntime &cursorRuntime() { return _cursorRuntime; }
	const CursorRuntime &cursorRuntime() const { return _cursorRuntime; }
	LoopRuntime &loopRuntime() { return _loopRuntime; }
	const LoopRuntime &loopRuntime() const { return _loopRuntime; }
	MovieRuntime &movieRuntime() { return _movieRuntime; }
	const MovieRuntime &movieRuntime() const { return _movieRuntime; }
	PathRuntime &pathRuntime() { return _pathRuntime; }
	const PathRuntime &pathRuntime() const { return _pathRuntime; }
	PropRuntime &propRuntime() { return _propRuntime; }
	const PropRuntime &propRuntime() const { return _propRuntime; }
	PuppetRuntime &puppetRuntime() { return _puppetRuntime; }
	const PuppetRuntime &puppetRuntime() const { return _puppetRuntime; }
	SetRuntime &setRuntime() { return _setRuntime; }
	const SetRuntime &setRuntime() const { return _setRuntime; }
	StageRuntime &stageRuntime() { return _stageRuntime; }
	const StageRuntime &stageRuntime() const { return _stageRuntime; }

	bool hasFeature(EngineFeature f) const override;
	Common::Error loadGameState(int slot) override;
	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveAutosaveCurrently() override;

	int getGameType() const;
	const char *getGameId() const;
	Common::Language getLanguage() const;
	Common::Platform getPlatform() const;

	// VMHost methods with engine-owned logic.
	bool actionFrame(int n) override;
	int randomNumber(int n) override;
	int getFrameRate() override;
	int setFrameRate(int newRate) override;
	void sendToBoot(const Common::String &message, const Common::Array<Value> &args) override;
	Value sendToBootFx(const Common::String &message, const Common::Array<Value> &args) override;
	Value sendToSetFx(const Common::String &message, const Common::Array<Value> &args) override;
	void setClut(const Common::String &name) override;
	void blackScreen() override;
	void forceUpdate() override;
	void message(const Common::String &text) override;
	void noteDialog(const Common::String &text) override;
	void flushEvents() override;
	void drawString(const Common::String &text, int32 packedPoint, int color, int size) override;
	void fadePalette(const Common::String &target, int steps, bool toBlack) override;
	void setVisualEffect(uint16 effect, int duration) override;
	void makeLoop(const Common::String &kind, const Common::String &target,
			const Common::String &message, int delay) override;
	void stopLoop(const Common::String &kind, const Common::String &target) override;
	void pauseLoop(const Common::String &kind, bool paused) override;
	void makeCricket(const Common::String &name) override;
	void stopCricket(const Common::String &name) override;
	void pauseCricket(const Common::String &kind, bool paused) override;
	bool keyAborts(const Common::String *resource, const Common::String *key,
			const bool *enabled) override;
	bool optionKey() override;
	Common::String getPathSlot(int slot) override;
	Common::String setPathSlot(int slot, const Common::String &newPath) override;
	Common::String getCurrentCD() override;
	Common::String setCurrentCD(const Common::String &requested) override;
	bool pointInButton(const Common::String &flat,
			const Common::String &button, int32 packedPoint) override;
	bool pointInPainting(const Common::String &scene, const Common::String &view,
			const Common::String &painting, int32 packedPoint) override;
	Common::String hitTest(int32 packedPoint) override;
	Common::String hitTestResult() override;
	int32 mousePoint() override;
	int32 makePoint(int x, int y) override;
	bool buttonDown() override;
	bool stillDown() override;
	int tick() override;
	int frameCounter() override;
	int calcDeg(int32 a, int32 b) override;
	int calcVectX(int deg, int dist) override;
	int calcVectY(int deg, int dist) override;
	int calcDist(int32 a, int32 b) override;
	int calcMod(int a, int b) override;
	void setCursorResource(const Common::String &resourceName) override;
	void saveGame(const Common::String &signature) override;
	void openGame(const Common::String &signature) override;
	bool questionDialog(const Common::String &message) override;
	void requestQuit() override;

private:
	friend class AudioRuntime;
	friend class ActorRuntime;
	friend class LoopRuntime;
	friend class MovieRuntime;
	friend class PropRuntime;
	friend class PuppetRuntime;
	friend class SetRuntime;
	friend class StageRuntime;

	/**
	 * Special-case the boot script: excise its CD presence check so the game
	 * can be run from an installed directory. The check is the if-block guarded
	 * by the "titanic1:" path literal; replacing it with no-op padding removes
	 * the notedialog/quit it would otherwise reach. Returns true if patched.
	 */
	static bool exciseBootCdCheck(Script &script);

	/**
	 * Install the named mouse cursor, decoding it on demand from the user's
	 * copy of TI.EXE. The cursor bitmaps are copyrighted game assets, so they
	 * are never embedded in ScummVM: they are read at runtime from the game's
	 * PE executable (RT_GROUP_CURSOR resources named CURS.ARROW, CURS.HAND, ...).
	 * The PEResources handle and
	 * decoded cursor groups are cached for reuse. Returns true on success.
	 */
	bool setGameCursor(const Common::String &name);
	bool pollInputStateEvents();
	bool pumpCursorMotionEvents();
	void reassertCursorVisibility();
	void presentCursorIfDirty();
	bool delayMillisWithCursorUpdates(uint32 delayMillis);
	void debugCargoPaintingTimer();

	/**
	 * Process the global/movie keyboard shortcuts that the original handles
	 * during playback, mirroring TI.EXE's WndProc (FUN_00403690) and movie key
	 * handler (FUN_0040e430): Esc / Ctrl+Q / Ctrl+. skip (when @p skippable),
	 * Ctrl+T pause/resume, F12 the About dialog, and backquote/Ctrl+D the debug
	 * console. Sets @p skip when the movie should be aborted. Returns the number
	 * of milliseconds spent paused, so wall-clock callers can shift their time
	 * references. @p audioHandle is the movie soundtrack handle (paused/resumed).
	 */
	uint32 handleMovieHotkeys(const Common::Event &event, bool skippable,
			const Audio::SoundHandle &audioHandle, bool &skip);

	/** Show the original's F12 "About" dialog (TI.EXE FUN_00404120). */
	void showAboutDialog();

	/** Handle the original's global WndProc key actions (F1-F9, F12). */
	bool handleGlobalKey(const Common::Event &event);

	const CyberflixGameDescription *_gameDescription;
	Common::RandomSource _rnd;
	Console *_console; ///< Owned by the engine framework's debugger, not by us.

	CursorRuntime _cursorRuntime;
	bool _cursorPresentationDirty = false;
	uint32 _lastCursorDebugLogMillis = 0;

	StageRuntime _stageRuntime;
	SetRuntime _setRuntime;

	/** Kind recorded by the last hittest, read back by result() — mirrors the
	 *  TI.EXE global DAT_00461298. */
	Common::String _hitKind;

	PropRuntime _propRuntime;

	ActorRuntime _actorRuntime;

	PuppetRuntime _puppetRuntime;
	/**
	 * Dispatch `message(args)` with temporary scope-chain entries pushed on
	 * the VM (newest searched first), mirroring the original's per-dispatch
	 * chains. @p self / @p targetProp set the 0xfba/0xfbb context atoms.
	 */
	void dispatchWithScopes(const Script *scope1, const Script *scope2,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext = "shop/prop");
	Value dispatchWithScopesValue(const Script *scope1, const Script *scope2,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext);
	Value dispatchWithThreeScopesValue(const Script *scope1, const Script *scope2,
			const Script *scope3, const Common::String &self,
			const Common::String &targetProp, const Common::String &message,
			const Common::Array<Value> &args, const char *debugContext);
	void dispatchWithScopeChain(const Common::Array<const Script *> &scopes,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext);
	Value dispatchWithScopeChainValue(const Common::Array<const Script *> &scopes,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext);
	Value dispatchWithScopeChainContextsValue(const Common::Array<const Script *> &scopes,
			const Common::Array<Common::String> &scopeSelf,
			const Common::Array<Common::String> &scopeProp,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext);
	void dispatchSetMessage(const Common::String &message, const Common::Array<Value> &args);
	Value dispatchSetMessageValue(const Common::String &message, const Common::Array<Value> &args);
	void dispatchSceneMessage(uint32 scene, const Common::String &message,
			const Common::Array<Value> &args);
	bool closeCurrentSceneForNavigation();
	bool processPendingLoad();

	/**
	 * The script VM driving the boot/stage scripts, with BOOTFILE res2
	 * registered as the global function library (TI.EXE keeps the equivalent
	 * scope chain around DAT_0045f010 / the boot resources).
	 */
	ScriptVM _vm;
	Common::ScopedPtr<Script> _globalLib;  ///< BOOTFILE res2 (function library).
	Common::ScopedPtr<Script> _bootScript; ///< BOOTFILE res1 (boot + handlers).

	/**
	 * Action-frame bitmask, mirroring TI.EXE DAT_0046112a: cleared by each
	 * playmovie (FUN_00446f80), bit 0/1 ORed in by the player main loop when
	 * the presented frame matches the master header cue-name field at +0x40 /
	 * +0x50 (FUN_0043b800 callers at 0x0040d19a/0x0040d1af). actionframe(n)
	 * (0x4e73, FUN_004362c0) tests bit n-1.
	 */
	uint16 _actionFrameMask = 0;

	AudioRuntime _audioRuntime;
	bool _keyAborts = false;  ///< Global keyaborts() getter state.

	LoopRuntime _loopRuntime;
	MovieRuntime _movieRuntime;
	void processScheduledLoops();
	FramePacingRuntime _framePacingRuntime;
	int _frameCounter = 0;
	int _lastCargoPaintingTimerLogBucket = -1;
	bool _cargoPaintingTimerExpiredLogged = false;

	PathRuntime _pathRuntime;

	/**
	 * Resolve the named CLUT to 256 RGB triplets, mirroring TI.EXE's clut
	 * registry (FUN_004470b0): "black" = all black; "set"/"stage" = the
	 * palette embedded in the currently open set/stage file; "current" = the
	 * hardware palette mirror. Returns false if unresolvable.
	 */
	bool resolveClut(const Common::String &name, Palette &rgb);

	/**
	 * Program the hardware palette and remember it as the "current" clut
	 * (TI.EXE DAT_0045f3c8 + FUN_004010f0). All engine palette
	 * writes funnel through here so fades always start from the true state.
	 */
	void programPalette(const Palette &rgb);
	void updatePaletteGammaTable();

	/** Linear palette fade @p from -> @p to, one step per 60 Hz tick of the
	 *  original's scaled timer (TI.EXE FUN_0041b200 step loop). */
	void fadePaletteSteps(const Palette &from, const Palette &to, int steps);

	/** True if the hardware palette is currently all black (post clut('black')
	 *  or a fade-out): renders must then leave the palette untouched so the
	 *  paint stays invisible until the next fade-in reveals it. */
	bool paletteIsBlack() const;

	PaletteRuntime _paletteRuntime;

	Common::String _saveSignature; ///< Current savegame()/opengame() argument while a ScummVM dialog is active.
	int _pendingLoadSlot = -1;
	Common::String _pendingLoadSignature;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_CYBERFLIX_H

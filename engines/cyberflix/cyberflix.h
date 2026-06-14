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
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/ptr.h"

#include "engines/engine.h"

#include "audio/mixer.h"

#include "cyberflix/detection.h"
#include "cyberflix/image.h"
#include "cyberflix/shop.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

class Console;
class Script;
class Stage;
class Set;
}

namespace Common {
class PEResources;
struct Event;
}

namespace Audio {
class SoundHandle;
}

namespace Graphics {
struct WinCursorGroup;
}

namespace Cyberflix {

// The game renders into a 512x384, 8-bit palettised framebuffer (the menu and
// in-game node images are full 512x384; the LOGO movie's frames are 512x264 and
// sit letterboxed within it).
enum {
	kScreenWidth = 512,
	kScreenHeight = 384
};

class CyberflixEngine : public Engine, public VMHost {
public:
	CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc);
	~CyberflixEngine() override;

	Common::Error run() override;

	bool hasFeature(EngineFeature f) const override;

	int getGameType() const;
	const char *getGameId() const;
	Common::Language getLanguage() const;
	Common::Platform getPlatform() const;

	// VMHost
	void playMovie(const Common::String &name) override;
	void openStageFile(const Common::String &name) override;
	void closeStageFile() override;
	void gotoFlat(const Value &flat) override;
	Common::String currentStage() override;
	Common::String currentFlat() override;
	void sendToStage(const Common::String &message, const Common::Array<Value> &args) override;
	void sendToFlat(const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args) override;
	void sendToButton(const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args) override;
	void openSetFile(const Common::String &name,
			const Common::String &scene = Common::String(),
			const Common::String &view = Common::String()) override;
	void closeSetFile() override;
	Common::String currentSet() override;
	Common::String currentView() override;
	Common::String currentScene(const Common::String *target) override;
	bool setVisible(const bool *newVisible) override;
	Common::String currentPuppet() override;
	void sendToScene(const Common::String &scene, const Common::String &message = Common::String(),
			const Common::Array<Value> &args = Common::Array<Value>()) override;
	void sendToPainting(const Common::String &scene, const Common::String &view,
			const Common::String &painting, const Common::String &message,
			const Common::Array<Value> &args) override;
	int countPaintings(const Common::String &scene, const Common::String &view) override;
	Common::String indexToPainting(const Common::String &scene,
			const Common::String &view, int index) override;
	bool roadAhead(const Common::String &scene, const Common::String &view) override;
	bool actionFrame(int n) override;
	void setClut(const Common::String &name) override;
	void blackScreen() override;
	void forceUpdate() override;
	void fadePalette(const Common::String &target, int steps, bool toBlack) override;
	void setVisualEffect(uint16 effect, int duration) override;
	void makeLoop(const Common::String &kind, const Common::String &target,
			const Common::String &message, int delay) override;
	void stopLoop(const Common::String &kind, const Common::String &target) override;
	void pauseLoop(const Common::String &kind, bool paused) override;
	void makeCricket(const Common::String &name) override;
	void stopCricket(const Common::String &name) override;
	void pauseCricket(const Common::String &kind, bool paused) override;
	void openTrackFile(const Common::String &name) override;
	void closeTrackFile(const Common::String &name) override;
	void playTheme(const Common::String &name) override;
	void haltTheme() override;
	void playSound(const Common::String &name, int mode) override;
	void playVoice(const Common::String &name) override;
	void haltSound(int which) override;
	void haltVoice() override;
	void themeVolume(const Common::String &name, int volume) override;
	Common::String currentTheme(int which) override;
	Common::String currentSound(int which) override;
	Common::String currentVoice() override;
	Common::String pathSlot(int slot, const Common::String *newPath) override;
	void openShopFile(const Common::String &name) override;
	void closeShopFile(const Common::String &name) override;
	void sendToShop(const Common::String &shop, const Common::String &message,
			const Common::Array<Value> &args) override;
	void sendToProp(const Common::String &prop, const Common::String &message,
			const Common::Array<Value> &args) override;
	bool propVisible(const Common::String &name) override;
	void propVisible(const Common::String &name, bool visible) override;
	Common::String propView(const Common::String &name) override;
	void propView(const Common::String &name, const Common::String &shape) override;
	void propXY(const Common::String &name, int x, int y) override;
	void propDist(const Common::String &name, int dist) override;
	void propDeg(const Common::String &name, int deg) override;
	Common::String propOwner(const Common::String &name, const Common::String *newOwner) override;
	int propValue(const Common::String &name, const int *newValue) override;
	int countProps() override;
	Common::String indexToProp(int index) override;
	Common::String hitTest(int32 packedPoint) override;
	Common::String hitTestResult() override;
	int32 mousePoint() override;
	void setCursorResource(const Common::String &resourceName) override;

private:
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
	 * PE executable (RT_GROUP_CURSOR resources named CURS.ARROW, CURS.HAND, ...
	 * documented in files/decomp/movie-playback.md). The PEResources handle and
	 * decoded cursor groups are cached for reuse. Returns true on success.
	 */
	bool setGameCursor(const Common::String &name);

	/** Lazily open the game's TI.EXE for resource access. Returns nullptr if
	 *  it cannot be found (the game can still run without a custom cursor). */
	Common::PEResources *gameExe();

	/**
	 * Render node @p node of the currently open stage to the screen: decode its
	 * background frame (compositing from the nearest keyframe), apply the stage
	 * palette and show the navigation cursor. Mirrors TI.EXE FUN_0040b180.
	 */
	void renderStageNode(int node);

	/**
	 * Render the current angle of scene @p scene of the currently open set to the
	 * screen: replay the panorama frames up to the camera angle (cold-start buffer
	 * state), apply the set palette and show the navigation cursor. Mirrors the
	 * background-paint half of TI.EXE FUN_00431200 (sendtoscene). @p angle is the
	 * panorama index; the heading-to-view selection (FUN_00442b70 / FUN_00426250)
	 * lands with panorama navigation.
	 */
	void renderSetScene(int scene, int table, int angle,
			const Common::String &view = Common::String());
	void renderSetScene(int scene, int angle) { renderSetScene(scene, _setTable, angle); }
	/** Composite a decoded SET background frame with the stage shell and props. */
	void displaySetFrame(const FrameImage &frame);
	/** Run the verified SET navigation actions used by currentscene(). */
	void navigateSet(const Common::String &action);
	/** Advance an active SET transition by one native frame. */
	void advanceSetTransition();

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

	Common::ScopedPtr<Common::PEResources> _exe;
	bool _exeTried = false;
	Common::HashMap<Common::String, Common::SharedPtr<Graphics::WinCursorGroup> > _cursorCache;
	Common::String _activeCursor;

	Common::SharedPtr<Stage> _stage; ///< Currently open stage (DATA/*.STG), or null.
	Common::ScopedPtr<Set> _set;     ///< Currently open set (DATA/*.SET), or null.
	enum SetTransitionType {
		kSetTransitionNone,
		kSetTransitionTurn,
		kSetTransitionForward
	};
	int _setScene = -1;              ///< Active scene index within _set, or -1.
	int _setTable = 0;               ///< Active panorama table: 0 = +0x0a/right, 1 = +0x0e/left.
	int _setAngle = 0;               ///< Active panorama angle within _setScene.
	Common::String _setView;         ///< Active view name in _setScene (DAT_004611dc).
	SetTransitionType _setTransitionType = kSetTransitionNone;
	uint32 _setTransitionResource = 0; ///< Active forward transition resource id.
	uint32 _setTransitionFrame = 0;    ///< Active forward transition frame index.
	FrameSequence _setFrameSequence; ///< Retained SET background buffer (TI.EXE 0x486770).
	bool _setVisible = false;        ///< TI.EXE DAT_00461182, read by setvisible().
	int _stageNode = 0;              ///< Current stage node (TI.EXE DAT_00461160).

	/** Kind recorded by the last hittest, read back by result() — mirrors the
	 *  TI.EXE global DAT_00461298. */
	Common::String _hitKind;

	/**
	 * Open shops (DATA/ .SHP files), in openshopfile order. The original keeps
	 * ONE global prop array across all shops (DAT_0046113c/DAT_00461140), so
	 * countprops/indextoprop and the by-name prop lookups span every open
	 * shop here, in open order.
	 */
	Common::Array<Common::SharedPtr<Shop> > _shops;
	bool _propsDirty = false; ///< Prop state changed; re-render before idling.

	/** Find an open shop by (case-insensitive) file name, or nullptr. */
	Shop *findShop(const Common::String &name);
	/** Find a prop by name across all open shops (global array semantics).
	 *  Optionally returns the owning shop. */
	Shop::Prop *findProp(const Common::String &name, Shop **shopOut = nullptr);
	/**
	 * Collect the visible screen-space props in paint order (most negative
	 * depth first, stable) — the same display-item list the compositor builds
	 * (FUN_0042bb90 / FUN_004434f0). Shared by renderSetScene (paints in list
	 * order, deepest first) and hitTest (probes it backwards, topmost first,
	 * like FUN_004430f0).
	 */
	void collectScreenProps(Common::Array<const Shop::Prop *> &draw,
			Common::Array<const Shop *> &drawShop);
	/**
	 * Dispatch `message(args)` with temporary scope-chain entries pushed on
	 * the VM (newest searched first), mirroring the original's per-dispatch
	 * chains. @p self / @p targetProp set the 0xfba/0xfbb context atoms.
	 */
	void dispatchWithScopes(const Script *scope1, const Script *scope2,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args);
	void dispatchWithScopeChain(const Common::Array<const Script *> &scopes,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext);
	void dispatchSetMessage(const Common::String &message, const Common::Array<Value> &args);
	void dispatchSceneMessage(uint32 scene, const Common::String &message,
			const Common::Array<Value> &args);
	bool closeCurrentSceneForNavigation();
	/** Repaint the current set scene if prop state changed (post-dispatch). */
	void refreshPropsIfDirty();

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
	 * (0x4e73, FUN_004362c0) tests bit n-1. See decomp/movie-playback.md.
	 */
	uint16 _actionFrameMask = 0;

	/**
	 * A loaded .TRK track file (TI.EXE track record, FUN_00411cc0). Holds the
	 * raw container plus parsed theme/SFX cue directories: the theme playlist
	 * (play order, 1-based cue indices) and the loop index (playlist position
	 * the chain loops back to after the last entry, so the leading cues play
	 * once and the tail repeats forever), plus named SFX cues for sound/voice
	 * builtins. See files/audio-re-notes.md.
	 */
	struct ThemeTrack {
		Common::String name;          ///< Lowercased file name ('bedrad1.trk').
		Common::Array<byte> fileData; ///< Whole container; cue payloads point in.
		struct Cue {
			Common::String name;      ///< Cue label ('prelude.01').
			uint32 resId = 0;         ///< Archive resource id (native cue priority key).
			byte flags = 0;           ///< SFX flags byte; theme cues leave this zero.
			uint32 dataOffset = 0;    ///< Absolute payload offset in fileData.
			uint32 length = 0;        ///< Payload length.
		};
		Common::Array<Cue> cues;        ///< Theme cue directory.
		Common::Array<Cue> sfxCues;     ///< SFX cue directory.
		Common::Array<uint16> playlist; ///< Play order, 1-based indices into cues.
		uint32 loopIdx = 0;             ///< Playlist index of the loop target.
		int volume = 255;               ///< themevol() setting (0-255).
	};

	/** Open track files, in opentrackfile order (TI.EXE list DAT_0046114c). */
	Common::Array<Common::SharedPtr<ThemeTrack> > _tracks;

	/** Find an open track by (case-insensitive) file name, or nullptr. */
	ThemeTrack *findTrack(const Common::String &name);
	const ThemeTrack::Cue *findSfxCue(const Common::String &name, ThemeTrack **trackOut = nullptr);
	bool playSoundCue(const Common::String &name, Audio::SoundHandle &handle,
			Common::String &currentCue, uint32 &currentResId);

	Audio::SoundHandle _themeHandle;   ///< Theme channel (TI.EXE DAT_00460a88).
	Common::String _themeTrackName;    ///< Track of the playing theme, or empty.

	/** Cue boundaries of the playing theme, for currenttheme(1). */
	struct ThemeCueSpan {
		uint32 startSample;
		Common::String name;
	};
	Common::Array<ThemeCueSpan> _themeSpans;
	uint32 _themeIntroSamples = 0; ///< Samples before the loop region.
	uint32 _themeLoopSamples = 0;  ///< Length of the looping region.

	struct SoundSlot {
		Audio::SoundHandle handle;
		Common::String cueName;
		uint32 resId = 0;
	};
	SoundSlot _soundSlots[2]; ///< Normal sound slots (TI.EXE DAT_00460a58/70).
	SoundSlot _voiceSlot;     ///< Voice slot (TI.EXE DAT_00460aa0).

	struct ScheduledLoop {
		Common::String kind;
		Common::String target;
		Common::String message;
		uint32 dueMillis = 0;
	};
	Common::Array<ScheduledLoop> _scheduledLoops;
	bool _loopsPaused = false;
	void processScheduledLoops();

	Common::String _pathSlots[9];        ///< Native path slots 0..8 (FUN_00438450).
	Common::String _pathSlotArchives[9]; ///< SearchMan archive names for slots 1..8.
	void registerPathSlotDirectory(int slot);

	/**
	 * Resolve the named CLUT to 256 RGB triplets, mirroring TI.EXE's clut
	 * registry (FUN_004470b0): "black" = all black; "set"/"stage" = the
	 * palette embedded in the currently open set/stage file; "current" = the
	 * hardware palette mirror (_screenClut). Returns false if unresolvable.
	 */
	bool resolveClut(const Common::String &name, byte (&rgb)[256 * 3]);

	/**
	 * Program the hardware palette and remember it in _screenClut (the
	 * "current" clut, TI.EXE DAT_0045f3c8 + FUN_004010f0). All engine palette
	 * writes funnel through here so fades always start from the true state.
	 */
	void programPalette(const byte (&rgb)[256 * 3]);

	/** Linear palette fade @p from -> @p to, one step per 60 Hz tick of the
	 *  original's scaled timer (TI.EXE FUN_0041b200 step loop). */
	void fadePaletteSteps(const byte (&from)[256 * 3], const byte (&to)[256 * 3], int steps);

	/** True if the hardware palette is currently all black (post clut('black')
	 *  or a fade-out): renders must then leave the palette untouched so the
	 *  paint stays invisible until the next fade-in reveals it. */
	bool paletteIsBlack() const;

	/**
	 * Hardware palette mirror — what is on screen right now. Boot starts black,
	 * matching run()'s initial clear. Fades interpolate from/to this so that a
	 * set/stage render performed while the palette is black stays invisible
	 * until blacktoscreen() reveals it (the original's transition model).
	 */
	byte _screenClut[256 * 3] = {};
	double _paletteGamma[3] = { 0.65, 0.65, 0.65 };
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_CYBERFLIX_H

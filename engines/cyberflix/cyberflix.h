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

#include "cyberflix/cast.h"
#include "cyberflix/detection.h"
#include "cyberflix/image.h"
#include "cyberflix/puppet.h"
#include "cyberflix/runtime/actors.h"
#include "cyberflix/runtime/cursor.h"
#include "cyberflix/runtime/loops.h"
#include "cyberflix/runtime/paths.h"
#include "cyberflix/runtime/timing.h"
#include "cyberflix/shop.h"
#include "cyberflix/vm.h"

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

class CyberflixEngine : public Engine, public VMHost {
public:
	CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc);
	~CyberflixEngine() override;

	Common::Error run() override;

	bool hasFeature(EngineFeature f) const override;
	Common::Error loadGameState(int slot) override;
	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;

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
	bool stageVisible(const bool *newVisible) override;
	Common::String currentFlat() override;
	void sendToStage(const Common::String &message, const Common::Array<Value> &args) override;
	Value sendToStageFx(const Common::String &message, const Common::Array<Value> &args) override;
	void sendToBoot(const Common::String &message, const Common::Array<Value> &args) override;
	Value sendToBootFx(const Common::String &message, const Common::Array<Value> &args) override;
	void sendToFlat(const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args) override;
	Value sendToFlatFx(const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args) override;
	void sendToButton(const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args) override;
	Value sendToButtonFx(const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args) override;
	void openSetFile(const Common::String &name,
			const Common::String &scene = Common::String(),
			const Common::String &view = Common::String()) override;
	void closeSetFile() override;
	Common::String currentSet() override;
	Value sendToSetFx(const Common::String &message, const Common::Array<Value> &args) override;
	Common::String currentView() override;
	Common::String currentScene(const Common::String *target) override;
	bool setVisible(const bool *newVisible) override;
	Common::String currentPuppet() override;
	void openPuppetFile(const Common::String &name) override;
	void closePuppetFile() override;
	void sendToPuppet(const Common::String &puppet, const Common::String &message,
			const Common::Array<Value> &args) override;
	Value sendToPuppetFx(const Common::String &puppet, const Common::String &message,
			const Common::Array<Value> &args) override;
	void puppetScript(const Common::String &name) override;
	void puppetClear() override;
	void puppetSpeak(const Common::String &name, int mode) override;
	void puppetBevel(const Common::String &name, int mode) override;
	void puppetGrab(bool enabled) override;
	int puppetEvent(int timeout) override;
	Common::String puppetBase(const Common::String *newBase) override;
	bool puppetVisible(const bool *newVisible) override;
	int puppetParam(int selector, const int *newValue) override;
	int countPuppets() override;
	Common::String indexToPuppet(int index) override;
	void sendToScene(const Common::String &scene, const Common::String &message = Common::String(),
			const Common::Array<Value> &args = Common::Array<Value>()) override;
	Value sendToSceneFx(const Common::String &scene, const Common::String &message,
			const Common::Array<Value> &args) override;
	void sendToPainting(const Common::String &scene, const Common::String &view,
			const Common::String &painting, const Common::String &message,
			const Common::Array<Value> &args) override;
	Value sendToPaintingFx(const Common::String &scene, const Common::String &view,
			const Common::String &painting, const Common::String &message,
			const Common::Array<Value> &args) override;
	int countPaintings(const Common::String &scene, const Common::String &view) override;
	Common::String indexToPainting(const Common::String &scene,
			const Common::String &view, int index) override;
	bool roadAhead(const Common::String &scene, const Common::String &view) override;
	bool actionFrame(int n) override;
	int randomNumber(int n) override;
	int frameRate(const int *newRate) override;
	void setClut(const Common::String &name) override;
	void blackScreen() override;
	void forceUpdate() override;
	void message(const Common::String &text) override;
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
	void openTrackFile(const Common::String &name) override;
	void closeTrackFile(const Common::String &name) override;
	void playTheme(const Common::String &name) override;
	void haltTheme() override;
	void playSound(const Common::String &name, int mode) override;
	void playVoice(const Common::String &name) override;
	void haltSound(int which) override;
	void haltVoice() override;
	void themeVolume(const Common::String &name, int volume) override;
	int waveVolume(const int *newLevel) override;
	int soundVolume(const Common::String &name, const int *newVolume) override;
	Common::String currentTheme(int which) override;
	Common::String currentSound(int which) override;
	Common::String currentVoice() override;
	bool keyAborts(const Common::String *resource, const Common::String *key,
			const bool *enabled) override;
	bool optionKey() override;
	Common::String pathSlot(int slot, const Common::String *newPath) override;
	Common::String currentCD(const Common::String *requested) override;
	void openCastFile(const Common::String &name) override;
	void closeCastFile(const Common::String &name) override;
	void sendToCast(const Common::String &cast, const Common::String &message,
			const Common::Array<Value> &args) override;
	Value sendToCastFx(const Common::String &cast, const Common::String &message,
			const Common::Array<Value> &args) override;
	void sendToActor(const Common::String &actor, const Common::String &message,
			const Common::Array<Value> &args) override;
	Value sendToActorFx(const Common::String &actor, const Common::String &message,
			const Common::Array<Value> &args) override;
	int countActors() override;
	Common::String indexToActor(int index) override;
	bool actorVisible(const Common::String &name, const bool *newVisible) override;
	Common::String actorSet(const Common::String &name, const Common::String *newSet) override;
	Common::String actorStar(const Common::String &name, const Common::String *newStar) override;
	Common::String actorPose(const Common::String &name, const Common::String *newPose) override;
	void actorXYZ(const Common::String &name, int x, int y, int z) override;
	int actorXYZ(const Common::String &name, int selector) override;
	int actorDeg(const Common::String &name, const int *newDeg) override;
	int actorValue(const Common::String &name, const int *newValue) override;
	Common::String actorOwner(const Common::String &name, const Common::String *newOwner) override;
	void actorZClip(const Common::String &name, int zClip) override;
	void actorSpeed(const Common::String &name, int speed) override;
	void actorScale(const Common::String &name, int scale) override;
	void actorTurn(const Common::String &name, int turn) override;
	int starXYZ(const Common::String &name, int selector) override;
	void openShopFile(const Common::String &name) override;
	void closeShopFile(const Common::String &name) override;
	void propInstance(const Common::String &source, const Common::String &newName) override;
	void sendToShop(const Common::String &shop, const Common::String &message,
			const Common::Array<Value> &args) override;
	Value sendToShopFx(const Common::String &shop, const Common::String &message,
			const Common::Array<Value> &args) override;
	void sendToProp(const Common::String &prop, const Common::String &message,
			const Common::Array<Value> &args) override;
	Value sendToPropFx(const Common::String &prop, const Common::String &message,
			const Common::Array<Value> &args) override;
	bool propVisible(const Common::String &name) override;
	void propVisible(const Common::String &name, bool visible) override;
	Common::String propView(const Common::String &name) override;
	void propView(const Common::String &name, const Common::String &shape) override;
	void propSet(const Common::String &name, const Common::String &setName) override;
	void propXYZ(const Common::String &name, int x, int y, int z) override;
	int propXY(const Common::String &name, int selector) override;
	void setPropXY(const Common::String &name, int x, int y) override;
	void propScale(const Common::String &name, int scale) override;
	void propZClip(const Common::String &name, int dist) override;
	void propDist(const Common::String &name, int dist) override;
	int propDeg(const Common::String &name, const int *newDeg) override;
	Common::String propOwner(const Common::String &name, const Common::String *newOwner) override;
	int propValue(const Common::String &name, const int *newValue) override;
	int countProps() override;
	Common::String indexToProp(int index) override;
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
	int calcDeg(int32 a, int32 b) override;
	int calcMod(int a, int b) override;
	void setCursorResource(const Common::String &resourceName) override;
	void saveGame(const Common::String &signature) override;
	void openGame(const Common::String &signature) override;
	bool questionDialog(const Common::String &message) override;
	void requestQuit() override;

private:
	friend class ActorRuntime;
	friend class LoopRuntime;

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
	bool pumpCursorMotionEvents();

	/**
	 * Render node @p node of the currently open stage to the screen: decode its
	 * background frame (compositing from the nearest keyframe), apply the stage
	 * palette and optionally show the navigation cursor. Direct native stage-node
	 * renders (FUN_0040b180) show CURS.ARROW, while forceupdate/compositor
	 * repaints choose the cursor separately through BOOTFILE idle hittest.
	 */
	void renderStageNode(int node, bool resetCursor = true);

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
	/** Present retained SET pixels directly, avoiding a full-frame copy per transition frame. */
	void displaySetFrame(const FrameSequence &frame);
	void displaySetFramePixels(const byte *pixels, uint16 width, uint16 height);
	/** Upload composited pixels only when displaySetFramePixels() actually changed them. */
	bool presentPendingScreenUpdate();
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

	CursorRuntime _cursorRuntime;

	Common::SharedPtr<Stage> _stage; ///< Currently open stage (DATA/*.STG), or null.
	bool _stageVisible = false;      ///< DAT_00461156, queried by stagevisible().
	Common::ScopedPtr<Set> _set;     ///< Currently open set (DATA/*.SET), or null.
	FrameImage _stageShellFrame;     ///< Cached STG node 0 backing under SET viewports.
	bool _stageShellFrameValid = false;
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
	bool _screenUpdatePending = false; ///< Avoid redundant OpenGL uploads when forceupdate() did not draw.

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
	Common::Array<Common::Rect> _dirtyRects; ///< Native-style dirty screen rectangles for prop changes.

	ActorRuntime _actorRuntime;
	void refreshActorStarPositions();

	/** Currently open puppet archive (TI.EXE DAT_00461200 cluster). */
	Common::SharedPtr<Puppet> _puppet;
	Common::String _puppetBase;
	Common::String _puppetCurrentAction;
	uint32 _puppetCurrentFrame = 0;
	bool _puppetVisible = false;
	bool _puppetGrab = false;
	int16 _puppetParams[10] = { 0, 0x80, 0xfa, 0xfb, 0x378, 0x0c, 0, 0, 2, 8 };
	Audio::SoundHandle _puppetSpeechHandle;
	struct PuppetBevelOption {
		Common::String text;
		int id = 0;
		Common::Rect rect;
	};
	Common::Array<PuppetBevelOption> _puppetBevels;
	Common::ScopedPtr<Graphics::Font> _nativeTextFont;
	int _nativeTextFontSize = 0;
	bool _nativeTextFontAntialiasing = false;
	const Puppet::ActionEntry *currentPuppetAction() const;
	const Graphics::Font *textFont(int size);
	bool capturePuppetGrabBackdrop(Common::Array<byte> &backdrop);
	bool paintPuppetGrabBackdrop(Graphics::Surface &screen, const Common::Array<byte> *cachedBackdrop);
	bool renderPuppetFrame(const Puppet::ActionEntry &action, uint32 frameIndex,
			bool present, const Common::Array<byte> *cachedBackdrop = nullptr);
	bool renderCurrentPuppetFrame(bool present);
	void renderPuppetBevels(bool present);
	void playPuppetAction(const Puppet::ActionEntry &action);

	/** Find an open shop by (case-insensitive) file name, or nullptr. */
	Shop *findShop(const Common::String &name);
	Common::SharedPtr<Shop> findShopShared(const Common::String &name);
	/** Find a prop by name across all open shops (global array semantics).
	 *  Optionally returns the owning shop. */
	Shop::Prop *findProp(const Common::String &name, Shop **shopOut = nullptr);
	Common::SharedPtr<Shop> findPropOwnerShared(const Common::String &name, Shop::Prop **propOut);
	/**
	 * Collect the visible screen-space props in paint order (most negative
	 * depth first, stable) — the same display-item list the compositor builds
	 * (FUN_0042bb90 / FUN_004434f0). Shared by renderSetScene (paints in list
	 * order, deepest first) and hitTest (probes it backwards, topmost first,
	 * like FUN_004430f0).
	 */
	void collectScreenProps(Common::Array<const Shop::Prop *> &draw,
			Common::Array<const Shop *> &drawShop);
	void advancePropPoses();
	bool hasAnimatedScreenProps() const;
	void collectWorldProps(Common::Array<const Shop::Prop *> &draw,
			Common::Array<const Shop *> &drawShop, Common::Array<int16> &depths,
			const Shop::WorldCamera &camera);
	void collectWorldActors(Common::Array<const Cast::Actor *> &draw,
			Common::Array<const Cast *> &drawCast, Common::Array<int16> &depths,
			const Shop::WorldCamera &camera);
	bool screenPropRect(const Shop &shop, const Shop::Prop &prop, Common::Rect &rect) const;
	void queueDirtyRect(const Common::Rect &rect);
	void markPropDirty(const Shop &shop, const Shop::Prop &prop, const Common::Rect *oldRect);
	void markShopDirty(const Shop &shop);
	void repaintDirtyStageRects();
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
	void dispatchSetMessage(const Common::String &message, const Common::Array<Value> &args);
	Value dispatchSetMessageValue(const Common::String &message, const Common::Array<Value> &args);
	void dispatchSceneMessage(uint32 scene, const Common::String &message,
			const Common::Array<Value> &args);
	bool closeCurrentSceneForNavigation();
	/** Repaint the current set scene if prop state changed (post-dispatch). */
	void refreshPropsIfDirty();
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
		Common::String sourceName;    ///< Lowercased file name requested by opentrackfile().
		Common::String name;          ///< Lowercased logical track name from master.
		Common::Array<byte> fileData; ///< Whole container; cue payloads point in.
		struct Cue {
			Common::String name;      ///< Cue label ('prelude.01').
			uint32 resId = 0;         ///< Archive resource id (native cue priority key).
			byte flags = 0;           ///< SFX flags byte; theme cues leave this zero.
			uint32 dataOffset = 0;    ///< Absolute payload offset in fileData.
			uint32 length = 0;        ///< Payload length.
			int volume = 255;         ///< soundvol() per-cue volume (0-255).
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
	Common::SharedPtr<ThemeTrack> findTrackRef(const Common::String &name);
	ThemeTrack *findTrack(const Common::String &name);
	const ThemeTrack::Cue *findSfxCue(const Common::String &name, ThemeTrack **trackOut = nullptr);
	ThemeTrack::Cue *findMutableSfxCue(const Common::String &name, ThemeTrack **trackOut = nullptr);
	byte effectiveAudioVolume(int baseVolume) const;
	void applyLiveAudioVolumes();
	void prepareThemeSpans(const ThemeTrack &track);
	bool startThemeStream(const Common::SharedPtr<ThemeTrack> &track, uint32 startSample);
	bool playSoundCue(const Common::String &name, Audio::SoundHandle &handle,
			Common::String &currentCue, uint32 &currentResId);
	void clearStageShellFrame();
	const FrameImage *stageShellFrame();

	class ThemeAudioStream;
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
	uint32 _themeStartSample = 0;  ///< Virtual sample offset used after restoring a theme mid-stream.

	struct SoundSlot {
		Audio::SoundHandle handle;
		Common::String cueName;
		uint32 resId = 0;
	};
	SoundSlot _soundSlots[2]; ///< Normal sound slots (TI.EXE DAT_00460a58/70).
	SoundSlot _voiceSlot;     ///< Voice slot (TI.EXE DAT_00460aa0).
	int _waveVolumeLevel = 9; ///< Global wave volume level, native scale 0..9.
	bool _keyAborts = false;  ///< Global keyaborts() getter state.

	LoopRuntime _loopRuntime;
	void processScheduledLoops();
	FramePacingRuntime _framePacingRuntime;

	PathRuntime _pathRuntime;

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
	void updatePaletteGammaTable();

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
	byte _paletteGammaTable[3][256] = {};
	bool _paletteGammaTableDirty = true;

	Common::String _saveSignature; ///< Current savegame()/opengame() argument while a ScummVM dialog is active.
	int _pendingLoadSlot = -1;
	Common::String _pendingLoadSignature;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_CYBERFLIX_H

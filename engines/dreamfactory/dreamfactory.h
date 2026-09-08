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

#ifndef DREAMFACTORY_DREAMFACTORY_H
#define DREAMFACTORY_DREAMFACTORY_H

#include "common/random.h"
#include "common/error.h"
#include "common/events.h"
#include "common/ptr.h"
#include "common/queue.h"
#include "common/rect.h"

#include "engines/engine.h"

#include "audio/mixer.h"

#include "dreamfactory/audio/audio_runtime.h"
#include "dreamfactory/cast.h"
#include "dreamfactory/detection.h"
#include "dreamfactory/image.h"
#include "dreamfactory/puppet.h"
#include "dreamfactory/runtime/actors.h"
#include "dreamfactory/runtime/cursor.h"
#include "dreamfactory/runtime/loops.h"
#include "dreamfactory/runtime/movie.h"
#include "dreamfactory/runtime/palette.h"
#include "dreamfactory/runtime/paths.h"
#include "dreamfactory/runtime/props.h"
#include "dreamfactory/runtime/puppet_runtime.h"
#include "dreamfactory/runtime/set_runtime.h"
#include "dreamfactory/runtime/stage_runtime.h"
#include "dreamfactory/runtime/timing.h"
#include "dreamfactory/shop.h"
#include "dreamfactory/vm.h"

namespace DreamFactory {

class GameSupport;
class Script;
class Stage;
class Set;
}

namespace Audio {
class SoundHandle;
}

namespace Graphics {
class Font;
class ManagedSurface;
}

namespace DreamFactory {

// The game renders into a 512x384, 8-bit palettised framebuffer (the menu and
// in-game node images are full 512x384; the LOGO movie's frames are 512x264 and
// sit letterboxed within it).
enum {
	kScreenWidth = 512,
	kScreenHeight = 384
};

/**
 * Connects ScummVM's engine lifecycle to the DreamFactory script VM.
 * Owns the runtime subsystems and coordinates script dispatch, input,
 * presentation and save restoration. GameSupport supplies per-title policy.
 *
 * Header layout: ScummVM entry points and subsystem accessors come first,
 * followed by the script-builtin interface, then private coordination/state.
 * Many public methods retain the original script vocabulary rather than
 * ScummVM terminology. A stage is a .STG container holding a collection of
 * named 2D screens called flats, plus stage-wide script behavior. Each flat
 * has a background image, button hotspots and optional scripts of its own;
 * the STG parser calls these flat records "nodes". Opening a stage loads the
 * collection; gotoFlat() selects a screen within it. currentStage() identifies
 * the open stage, while currentFlat() identifies the selected screen.
 * For example, Titanic's MAP.STG contains flats named "Map 1" through "Map 8".
 * With that stage open, gotoFlat("Map 2") selects the second map screen and
 * currentFlat() returns "Map 2"; selecting "Map 3" stays within MAP.STG.
 *
 * A set instead supplies world scenes/views; shops and casts supply props
 * and actors; a puppet is an animated talking-head character used for dialogue.
 *
 * The large builtin interface is called directly by ScriptVM. Subsystem
 * forwarders live in vm_host_*.cpp; their behavior lives in runtime/ and audio/.
 * Comments here describe caller-visible contracts; native implementation
 * details and reverse-engineering evidence belong beside those implementations.
 */
class DreamFactoryEngine : public Engine {
public:
	/** Both arguments are borrowed and must outlive the engine; @p syst must not be null. */
	DreamFactoryEngine(OSystem *syst, const DreamFactoryGameDescription &gameDesc);
	~DreamFactoryEngine() override;

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
	GameSupport &gameSupport() { return *_gameSupport; }
	const GameSupport &gameSupport() const { return *_gameSupport; }
	ScriptVM &scriptVM() { return _vm; }
	const ScriptVM &scriptVM() const { return _vm; }

	bool hasFeature(EngineFeature f) const override;
	/**
	 * ScummVM's slot-based load entry point: restore without a file chooser,
	 * unlike the script-facing openGame(). Both use this engine's save format.
	 * Restore a save, or queue it if the VM is executing. In the latter case,
	 * success means queued, not restored: the main loop performs the load once
	 * scripts have unwound, so their live resources are not destroyed mid-call.
	 */
	Common::Error loadGameState(int slot) override;
	/**
	 * ScummVM's slot-based save entry point: write the current state directly,
	 * without a dialog. The script-facing saveGame() opens a chooser which
	 * ultimately calls this method with the selected slot and description.
	 * Writes the script-supplied signature, or the title's default when none
	 * is supplied, plus ScummVM metadata including the autosave flag.
	 */
	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveAutosaveCurrently() override;

	int getGameType() const;
	const char *getGameId() const;
	Common::Language getLanguage() const;
	Common::Platform getPlatform() const;

	// --- Script-builtin bindings with engine-owned logic ------------------
	// (implemented in dreamfactory.cpp / runtime/system.cpp / saveload.cpp).
	/**
	 * actionframe(n): whether movie playback reached authored action cue 1 or 2.
	 * These are named frame markers from the movie header, not frame numbers
	 * or puppet actions. The flag stays set after playback passes the marker
	 * (and after playback ends); querying it does not clear it. A new nonempty
	 * playMovie() call clears both flags, but chained movies share the flags.
	 * Returns false for indices other than 1/2 or a cue not yet reached.
	 */
	bool actionFrame(int n);
	/**
	 * random(n): pseudorandom integer in 1..n inclusive, or 0 when @p n < 1.
	 * For example, randomNumber(6) returns 1 through 6, not 0 through 5.
	 * Uses the engine's ScummVM RandomSource for recorder/replay integration;
	 * preserves the native range, not the original generator's exact sequence.
	 */
	int randomNumber(int n);
	/**
	 * framerate(): requested interval between forceUpdate() compositor passes,
	 * in 60 Hz ticks, NOT measured frames per second. Default 3 is about 20
	 * passes/second when work fits the budget. Movies and puppet speech have
	 * independent playback clocks; cursor-only presents do not count as passes.
	 */
	int getFrameRate();
	/**
	 * framerate(n): clamp @p newRate to 0..60 and return the stored interval.
	 * Larger values slow compositor passes (2 ~ 30/s, 3 ~ 20/s, 6 ~ 10/s).
	 * Zero disables this pacing wait, not rendering. Time spent doing work
	 * since the previous forceUpdate() completed counts toward the interval.
	 * For example, with a 50 ms interval, 30 ms of work leaves about 20 ms to
	 * wait. If the work takes 70 ms, no wait is added, but the pass is already
	 * late: this setting cannot make rendering faster than the machine allows.
	 */
	int setFrameRate(int newRate);
	/**
	 * sendtoboot(message(args)): synchronously call a named handler in the
	 * persistent boot script (BOOTFILE res1), falling back to the GLOBAL
	 * function library (res2) if absent or passed on with `pass`. This calls
	 * an existing handler; it does not reload BOOTFILE or restart the game.
	 * @p message is the handler name and @p args contains evaluated arguments.
	 * Temporarily replaces the caller's dispatch chain, with self="bootfile"
	 * and an empty targetProp, then restores the previous chain/context.
	 * Discards the handler's return value and refreshes dirty props afterwards.
	 * Warns and does nothing if the boot script has not been loaded.
	 */
	void sendToBoot(const Common::String &message, const Common::Array<Value> &args);
	/**
	 * Value-returning sendtobootfx(): uses the same boot/GLOBAL lookup and
	 * temporary context as sendToBoot(). The handler can still have side
	 * effects; Fx does not mean a visual effect or a side-effect-free call.
	 * Unlike sendToBoot(), does not refresh dirty props after dispatch.
	 * Returns a default-constructed Value if the boot script is unavailable.
	 */
	Value sendToBootFx(const Common::String &message, const Common::Array<Value> &args);
	/**
	 * sendtosetfx(message(args)): synchronously call a named handler in the open
	 * SET's set-wide script, then GLOBAL (BOOTFILE res2) if absent or passed on
	 * with `pass`. Scene/painting scripts and boot handlers are not searched.
	 * @p message is the handler name; @p args contains evaluated arguments.
	 * Temporarily replaces the dispatch chain with self set to the SET's
	 * embedded name and targetProp empty, then restores the caller's context.
	 * Returns the handler's value; side effects are allowed, but this wrapper
	 * does not refresh dirty props. Returns a default-constructed Value if no
	 * SET is open or @p message is empty. Does not open or switch SETs.
	 */
	Value sendToSetFx(const Common::String &message, const Common::Array<Value> &args);
	/**
	 * sendtoset(message(args)): same dispatch as sendToSetFx(), discarding the
	 * return value. Does nothing if no SET is open or @p message is empty.
	 * Unlike sendToBoot(), neither SET variant refreshes dirty props afterwards.
	 */
	void sendToSet(const Common::String &message, const Common::Array<Value> &args);
	void setClut(const Common::String &name);
	void blackScreen();
	/**
	 * Explicitly run a game update from the calling script, rather than wait
	 * for the normal event loop to trigger one. This lets a script advance
	 * animation and movement while it remains in its own loop.
	 * "Force" does not mean bypass frame pacing or always upload new pixels.
	 *
	 * Advance walks, scheduled callbacks and animation, increment frame(), then
	 * composite/present as needed and wait for the framerate() pacing deadline.
	 * This advances game state, unlike a cursor-only updateScreen() call.
	 */
	void forceUpdate();
	bool hostQuitRequested() { return shouldQuit(); }
	/** Script diagnostic text: debug-log only, with no visible dialog. See noteDialog(). */
	void message(const Common::String &text);
	/**
	 * delay(ticks): wait in 60 Hz time units, not animation frames (60 ~ 1 s).
	 * Nonpositive values return immediately. Keeps cursor motion/quit responsive
	 * and defers other input; does not call forceUpdate() or advance frame().
	 * Returns early on quit. This is not a global pause of mixer audio.
	 */
	void delayTicks(int ticks);
	/** Show a modal informational dialog with an OK button; script execution waits for dismissal. */
	void noteDialog(const Common::String &text);
	/**
	 * Discard deferred input and purge queued backend mouse/keyboard events.
	 * Used to prevent old clicks/keypresses reaching the next interaction;
	 * does not dispatch them or reset script state, and is not an audio flush.
	 */
	void flushEvents();
	/**
	 * Draw text at a packed screen point whose y coordinate is the baseline.
	 * @p color is a palette index; @p size selects the puppet text font size.
	 * With a cached stage background, draws there for the next compositor pass
	 * so text survives later redraws; otherwise draws/presents directly on screen.
	 * Missing font means nothing is drawn.
	 */
	void drawString(const Common::String &text, int32 packedPoint, int color, int size);
	/** Text width in pixels at @p size; returns 0 without a font. @p fontId is currently ignored. */
	int stringWidth(const Common::String &text, int fontId, int size);
	void fadePalette(const Common::String &target, int steps, bool toBlack);
	/** mixclut(a, b, first, last, weight) (0x2f32 FUN_00446570): program the
	 *  palette with clut @p a blended toward clut @p b by weight/255 over
	 *  entries first..last; entries outside the range stay at @p a. A-14's
	 *  lights-out uses ("set", "black", 0, 127, 240), which darkens the room
	 *  colors but leaves the interface half of the palette lit. */
	void mixClut(const Common::String &nameA, const Common::String &nameB,
			int first, int last, int weight);
	/**
	 * visualeffect(effect, duration): composite the current game state and
	 * present it now, optionally revealing a stage flat with a directional
	 * wipe. Despite the name, this performs the update synchronously; it does
	 * not merely store an effect for a later draw.
	 * @p effect is a Script::kEffect* code. Plain presents without a transition;
	 * wipeleft/right/up/down are implemented for stage flats. Other effects
	 * fall back to plain presentation; SET/puppet paths do not run these wipes.
	 * @p duration is clamped to 1..1000 wipe steps, each paced at one 60 Hz tick
	 * (30 steps ~ 0.5 s), not milliseconds. Plain ignores the duration.
	 * Does not itself fade the palette: scripts can prepare a new room while
	 * the palette is black, then reveal the finished image with blacktoscreen().
	 */
	void setVisualEffect(uint16 effect, int duration);

	/** Copy the visible screen into @p out. */
	bool captureScreen(Graphics::ManagedSurface &out);

	/** Blit one band of @p image into the screen surface without presenting. */
	void blitScreenBand(const Graphics::Surface &image, const Common::Rect &band);

	/** Reveal the already-composited @p incoming with a directional wipe. */
	void runWipe(const Graphics::Surface &incoming, uint16 effect, int steps);
	/**
	 * Schedule a script message with no arguments. Despite the name, a "loop"
	 * is a one-shot callback; its handler must call makeLoop() again to repeat.
	 * @p kind selects scene, flat, stage, prop, shop or actor dispatch, and
	 * @p target names the recipient (stage dispatch uses the open stage).
	 * Replaces pending callbacks using stopLoop(kind, target), so an empty
	 * target replaces all callbacks of that kind. Matching ignores case.
	 * @p delay counts scheduler passes in forceUpdate(), not milliseconds or
	 * 60 Hz ticks. Both 0 and 1 fire on the next eligible pass, never inline.
	 * Callbacks registered by a handler wait until a later pass; nested
	 * forceUpdate() calls during dispatch do not advance these countdowns.
	 */
	void makeLoop(const Common::String &kind, const Common::String &target,
			const Common::String &message, int delay);
	/**
	 * Cancel pending script callbacks matching kind and target, ignoring case.
	 * An empty target matches all recipients of that kind; kind "all" cancels
	 * every pending callback regardless of target. Does not interrupt a handler
	 * already running or stop audio/animation playback.
	 */
	void stopLoop(const Common::String &kind, const Common::String &target);
	/**
	 * With kind "all", freeze (true) or resume (false) pending loop countdowns
	 * on subsequent scheduler passes. Resuming preserves the remaining delays;
	 * it does not restart them. Currently other kinds have no effect.
	 */
	void pauseLoop(const Common::String &kind, bool paused);
	/**
	 * Register a named sound cue as a "cricket" and request SFX playback using
	 * multiplesound channel selection. This is a sound registration, not a
	 * script callback like makeLoop(). Currently incomplete: there is no
	 * automatic repeat scheduler, and playback is requested even when paused.
	 * Re-registering a name reuses its entry and requests playback again.
	 */
	void makeCricket(const Common::String &name);
	/** Remove a cricket registration, or all for "all"; does not stop playing audio. */
	void stopCricket(const Common::String &name);
	/**
	 * Record a cricket's pause flag, or update all entries and the default for
	 * future entries when @p kind is "all". Currently does not pause mixer audio
	 * or affect scheduling; these flags are retained in save state only.
	 */
	void pauseCricket(const Common::String &kind, bool paused);
	/**
	 * Get or set the script's keyaborts flag: a null @p enabled queries it;
	 * otherwise store the supplied value and return the new state.
	 * Currently incomplete: resource and key are ignored, and the saved flag
	 * is not consulted by input/playback code, so it does not enable or disable
	 * keyboard interruption of playback.
	 */
	bool keyAborts(const Common::String *resource, const Common::String *key,
			const bool *enabled);
	/**
	 * Test Shift in the event manager's current modifier state, without polling
	 * for new events. Despite the name, this is not Alt/Option: the original
	 * Windows optionkey() and shiftkey() both test VK_SHIFT.
	 */
	bool optionKey();
	/** Test Shift using the same modifier-state query as optionKey(). */
	bool shiftKey();
	Common::String getPathSlot(int slot);
	Common::String setPathSlot(int slot, const Common::String &newPath);
	Common::String getCurrentCD();
	Common::String setCurrentCD(const Common::String &requested);
	/**
	 * Test a named rectangular hotspot ("button") on a flat in the open STG.
	 * A flat is a 2D screen/node; its buttons can have scripts, but need not
	 * look like GUI push buttons. An empty @p flat selects the current node.
	 * @p packedPoint is a screen point from mousePoint()/makePoint(). Left/top
	 * edges are included, right/bottom excluded. Returns false for missing
	 * stage/flat/button data. Tests geometry only: does not dispatch a click,
	 * check visibility/occlusion, or update hitTestResult().
	 */
	bool pointInButton(const Common::String &flat,
			const Common::String &button, int32 packedPoint);
	/**
	 * Test a named rectangular hotspot ("painting") in a SET scene's named
	 * view. A painting is a view-specific interaction region with an optional
	 * script, not necessarily a picture hanging on a wall or a movable prop.
	 * @p packedPoint uses screen coordinates, not world x/y/z. Uses the same
	 * half-open rectangle rule as pointInButton(); missing set/scene/view/painting
	 * data returns false. Tests only the requested region, not the topmost hit,
	 * and does not dispatch a message or update hitTestResult().
	 */
	bool pointInPainting(const Common::String &scene, const Common::String &view,
			const Common::String &painting, int32 packedPoint);
	Common::String hitTest(int32 packedPoint);
	Common::String hitTestResult();
	/** Current mouse position in the packed format returned by makePoint(). */
	int32 mousePoint();
	/**
	 * Store two signed 16-bit coordinates in one script integer: x occupies
	 * bits 16..31 and y occupies bits 0..15. For example, (100, 200) becomes
	 * 0x006400c8. This is what "packed point" means in the script interface.
	 * When used as a screen point, coordinates refer to the logical 512x384
	 * game image: origin at top left, x increasing rightward and y downward,
	 * independent of host-window scaling. Packing itself does not clip to the
	 * screen or distinguish screen coordinates from world coordinates.
	 */
	int32 makePoint(int x, int y);
	/** Poll live input and report the left button, preserving clicks for script dispatch. */
	bool buttonDown();
	/** Like buttonDown(), but true if either the left or right button is held. */
	bool stillDown();
	/** Backend time expressed in 60 Hz ticks, independent of rendered frames. */
	int tick();
	/** Script frame counter advanced by forceUpdate(), not by every idle redraw. */
	int frameCounter();
	/**
	 * Direction from packed point @p a to @p b, in signed angle units -128..127.
	 * A full turn is 256 units: 0 points along +x, 64 along +y. On screen,
	 * where y increases downward, positive angles therefore turn clockwise.
	 */
	int calcDeg(int32 a, int32 b);
	/**
	 * X component of a vector of length @p dist at heading @p deg. Despite its
	 * name, @p deg uses 256 units per turn, wrapping to its low eight bits.
	 * Uses cosine scaled by 16384, then truncates the scaled product toward zero.
	 * The intermediate product must fit in int; overflow is not currently guarded.
	 */
	int calcVectX(int deg, int dist);
	/** Y component, using sine with the same units, truncation and limits as calcVectX(). */
	int calcVectY(int deg, int dist);
	/** Euclidean distance between packed points, truncated to an integer. */
	int calcDist(int32 a, int32 b);
	/**
	 * Signed remainder: a nonzero result has the sign of @p a, not necessarily
	 * a positive mathematical modulo (e.g. calcMod(-5, 3) is -2).
	 * Returns zero for a zero divisor. INT_MIN % -1 is not currently guarded.
	 */
	int calcMod(int a, int b);
	void setCursorResource(const Common::String &resourceName);
	/**
	 * Script savegame(signature): show ScummVM's save chooser, then write the
	 * selected slot through saveGameState(). Cancelling writes nothing.
	 * @p signature is a game-identifying string stored in the save header,
	 * not a filename or slot; an empty string uses the title's default.
	 */
	void saveGame(const Common::String &signature);
	/**
	 * Script opengame(signature): check whether loading is allowed, show
	 * ScummVM's load chooser, then queue the selected slot. Cancelling does
	 * nothing. The main loop calls loadGameState() after scripts unwind;
	 * this method does not replace resources beneath the calling script.
	 * @p signature must match the saved header string, ignoring case; an empty
	 * string selects the title's default. Checked when loading, not by the
	 * chooser. This reads the same saves as ScummVM's own load entry points,
	 * not a separate original-game save format.
	 */
	void openGame(const Common::String &signature);
	/** Show a modal Yes/No question; returns true only for Yes. */
	bool questionDialog(const Common::String &message);
	/** Request engine shutdown for the event/playback loops to observe; does not terminate the process. */
	void requestQuit();

	// --- Script-builtin bindings: subsystem forwarders ---------------------
	// The ScriptVM calls all of these directly (vm/builtins.cpp, vm/vm.cpp);
	// each mirrors one script builtin. The bodies are one-line forwarders into
	// the runtime subsystems, grouped by implementing file.

	// Movie playback (vm_host_movie.cpp).
	/** playmovie('name.mov'): play a MOVIES/ basename, e.g. "logo.mov". */
	void playMovie(const Common::String &name);

	// Stage, set, scene and painting navigation (vm_host_set_stage.cpp).
	/** Open the stage file @p name (a DATA/ basename, e.g. "main.stg"). */
	void openStageFile(const Common::String &name);
	/** Close the current stage file (closestagefile, TI.EXE opcode 0x2f1d). */
	void closeStageFile();
	/** gotoflat(name|index) (0x2f1e): switch to a 1-based stage node or node name. */
	void gotoFlat(const Value &flat);
	/** currentstage() (0x4e50): open stage name, or native "None". */
	Common::String currentStage();
	/** stagevisible([flag]) (0x3e88): current stage visibility flag. */
	bool getStageVisible();
	bool setStageVisible(bool visible);
	/**
	 * currentflat() (0x4e46): name of the selected flat in the open stage.
	 * A flat is a named 2D screen with a background image and button hotspots;
	 * the STG parser calls its record a "node". Returns the flat's name, not
	 * the stage file name or a numeric index; returns "None" if no stage is open.
	 */
	Common::String currentFlat();
	/** countflats() (0x4e43 FUN_00409980): node count of the open stage. */
	int countFlats();
	/** indextoflat(i) (0x4e44 FUN_004099e0): name of the 1-based flat i, or "None". */
	Common::String indexToFlat(int index);
	/** flattoindex(name) (0x4e45 FUN_00409d70): 1-based index of the flat, or 0. */
	int flatToIndex(const Common::String &name);
	/**
	 * Deliver the message call `message(args)` to the open stage's script. The
	 * original sendtostage (TI.EXE FUN_0040ad80) passes the message UNevaluated
	 * and dispatches it against the stage script's definitions with the global
	 * library as fallback scope; the ...Fx variants return the handler's value.
	 * sendtoflat (0x2f25) prepends the flat/node script to that chain, and
	 * sendtobutton (0x2f24) the button script in front of both.
	 */
	void sendToStage(const Common::String &message, const Common::Array<Value> &args);
	Value sendToStageFx(const Common::String &message, const Common::Array<Value> &args);
	void sendToFlat(const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args);
	Value sendToFlatFx(const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args);
	void sendToButton(const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToButtonFx(const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args);
	/**
	 * Open the set file @p name. @p scene / @p view optionally name the scene
	 * and view to make current (the opensetfile optional args; TI.EXE
	 * FUN_00430690). Empty = default to the set's first scene.
	 */
	void openSetFile(const Common::String &name,
			const Common::String &scene = Common::String(),
			const Common::String &view = Common::String());
	/** Close the open set file (closesetfile, TI.EXE opcode 0x2f01). */
	void closeSetFile();
	/** Name of the open set (currentset, 0x4e55), or "none". */
	Common::String currentSet();
	/** currentview([name]) (0x3e8b): current SET view name, "Moving", or "none";
	 *  with a name, switch the current scene to that view. */
	Common::String getCurrentView();
	Common::String setCurrentView(const Common::String &target);
	/** currentdeg() (0x3e9f FUN_00431d50): current SET heading, or -1. */
	int currentDeg();
	/** currentscene([name|left|right|strait]) (0x3e9d): no arg reads the current
	 *  SET scene name; an arg switches scene or starts the native navigation
	 *  action named by BOOTFILE's keydown fallback. */
	Common::String getCurrentScene();
	Common::String setCurrentScene(const Common::String &target);
	/** setvisible([flag]) (0x3e87): with no args, read the open-set visibility
	 *  flag (TI.EXE FUN_00431ca0 / DAT_00461182); with an arg, set it
	 *  (FUN_004318d0) and invalidate/redraw the set. */
	bool getSetVisible();
	bool setSetVisible(bool visible);
	/** Dispatch a scene message. The native sendtoscene() does not switch the
	 *  current rendered scene; current scene changes are driven by currentscene()
	 *  / SET navigation. */
	void sendToScene(const Common::String &scene,
			const Common::String &message = Common::String(),
			const Common::Array<Value> &args = Common::Array<Value>());
	Value sendToSceneFx(const Common::String &scene, const Common::String &message,
			const Common::Array<Value> &args);
	/** sendtopainting(scene, view, painting, message(args)) (0x2f22): dispatch
	 *  against [painting script, scene script, set script, BOOTFILE res2]. */
	void sendToPainting(const Common::String &scene, const Common::String &view,
			const Common::String &painting, const Common::String &message,
			const Common::Array<Value> &args);
	Value sendToPaintingFx(const Common::String &scene, const Common::String &view,
			const Common::String &painting, const Common::String &message,
			const Common::Array<Value> &args);
	/** countpaintings(scene, view) (0x4e32): SET painting records in the view. */
	int countPaintings(const Common::String &scene, const Common::String &view);
	/** indextopainting(scene, view, index) (0x4e36): native 1-based painting lookup. */
	Common::String indexToPainting(const Common::String &scene,
			const Common::String &view, int index);
	/** roadahead(scene, view) (0x4e94): whether the view has a forward transition. */
	bool roadAhead(const Common::String &scene, const Common::String &view);
	/** cameraxyz(selector) (0x4e5f FUN_00437870): selector 1/2/3 = x/y/z,
	 *  4 = packed x/y point for the active SET camera. */
	int cameraXYZ(int selector);
	/** playerxyz(selector) (0x4e60 FUN_00437950): selector 1/2/3 = x/y/z,
	 *  4 = packed x/y point for the active SET player point. */
	int playerXYZ(int selector);
	/** camerahi([z]) (0x3ea3): script-settable world-projection base height
	 *  DAT_0046119a (getter FUN_00436170, setter FUN_00446190). BOOTFILE's
	 *  adjustcamera() drives it on every set open. */
	int getCameraHi() const { return _cameraHiValue; }
	int setCameraHi(int z);

	/**
	 * @name Talking-head dialogue
	 * Forwarded by vm_host_puppet.cpp to PuppetRuntime.
	 * A PUP file supplies named animation/speech actions and scripts. Speaking
	 * plays one of those recorded actions; it does not synthesize the supplied
	 * name as text. A "bevel" is a clickable dialogue-choice row, not an image
	 * filter: its text and return ID are supplied by the script.
	 *
	 * Typical sequence, with illustrative action names and choice text:
	 * @code
	 * puppetSpeak("greeting", 0);      // Character performs a recorded action.
	 * puppetClear();                   // Remove any previous dialogue choices.
	 * puppetBevel("Who are you?", 1);   // Display choice text with ID 1.
	 * puppetBevel("Goodbye.", 2);       // Display choice text with ID 2.
	 * int choice = puppetEvent(-1);    // Wait for ID 1/2, or -1 on cancellation.
	 * @endcode
	 * The puppet must already be open and visible. The calling script decides
	 * which response action/message to run for the returned choice ID.
	 */
	/// @{
	/** currentpuppet() (0x4e51), or "none" when no puppet file is open. */
	Common::String currentPuppet();
	/** Open a .PUP talking-head container; refuses to replace an already open puppet. */
	void openPuppetFile(const Common::String &name);
	/** Stop puppet speech and release the current puppet and its dialogue state. */
	void closePuppetFile();
	/** Dispatch to the named script inside the open PUP, not to a separate PUP file. */
	void sendToPuppet(const Common::String &puppet, const Common::String &message,
			const Common::Array<Value> &args);
	/** Value-returning counterpart of sendToPuppet(). */
	Value sendToPuppetFx(const Common::String &puppet, const Common::String &message,
			const Common::Array<Value> &args);
	/** Currently validates a script name only; does not execute it or change active state. */
	void puppetScript(const Common::String &name);
	/** Clear queued dialogue choices and their highlight, then redraw the open puppet. */
	void puppetClear();
	/**
	 * Play the named action in the visible puppet, including its animation and
	 * speech audio. Waits for playback while processing input; Escape can abort.
	 * Remembers the action for click-to-repeat and preserves dialogue choices.
	 * @p mode is the optional script argument (default 0); currently only logged,
	 * with no effect on playback. @p name is an action name, not dialogue text.
	 */
	void puppetSpeak(const Common::String &name, int mode);
	/**
	 * Append and draw a clickable dialogue-choice row for the visible puppet.
	 * @p name is the displayed text; @p mode is its script-supplied choice ID,
	 * returned by puppetEvent(), not a rendering mode or array index. The VM
	 * supplies ID 0 when the optional second argument is omitted.
	 */
	void puppetBevel(const Common::String &name, int mode);
	/** Enable composition over a captured scene backdrop during puppet rendering. */
	void puppetGrab(bool enabled);
	/**
	 * Wait for a dialogue choice and return its puppetBevel() ID. @p timeout
	 * is in milliseconds; negative means no deadline. Returns -1 on timeout,
	 * Escape, quit, no visible puppet or no choices. A selected row stays
	 * highlighted until cleared; clicking the character can replay speech.
	 */
	int puppetEvent(int timeout);
	/** Current base-action name, or an empty string when no puppet is open. */
	Common::String getPuppetBase();
	/** Set the base/current action to frame 0; return its lowercased name (maximum 31 bytes). */
	Common::String setPuppetBase(const Common::String &newBase);
	bool getPuppetVisible();
	bool setPuppetVisible(bool visible);
	/** Read one of ten signed 16-bit puppet parameters (selectors 1..10); invalid selector returns 0. */
	int getPuppetParam(int selector);
	/** Store @p newValue narrowed to int16 and return it; invalid selector returns 0. */
	int setPuppetParam(int selector, int newValue);
	/** Number of named scripts in the open PUP, not the number of open PUP files. */
	int countPuppets();
	/** Script name at a 1-based index, or empty if out of range or no PUP is open. */
	Common::String indexToPuppet(int index);
	/// @}

	// Audio: theme/SFX/voice channels (vm_host_audio.cpp).
	/** opentrackfile('name.trk'): load a .TRK track file (TI.EXE FUN_00411be0). */
	void openTrackFile(const Common::String &name);
	/** closetrackfile('name.trk'): unload a track file (TI.EXE FUN_00412070). */
	void closeTrackFile(const Common::String &name);
	/** playtheme('name.trk'): start the track's theme playlist (intro cues once,
	 *  then loop from the loop index forever) on the theme channel, replacing any
	 *  current theme (TI.EXE FUN_00412250). */
	void playTheme(const Common::String &name);
	/** halttheme(): stop the theme channel (TI.EXE FUN_00412410). */
	void haltTheme();
	/** singlesound/multiplesound/dualsound/bothsound(name): play a named SFX cue
	 *  on the two normal sound channels (TI.EXE FUN_004122d0..00412390). */
	void playSound(const Common::String &name, int mode);
	/** voicesound(name): play a named SFX cue on the voice channel (FUN_004123d0). */
	void playVoice(const Common::String &name);
	/** haltsound(which): which 1/2 stop normal sound slot, 3 stops both. */
	void haltSound(int which);
	/** haltvoice(): stop the voice channel. */
	void haltVoice();
	/** themevol('name.trk', 0-255): set the track's theme volume (FUN_004125c0). */
	void themeVolume(const Common::String &name, int volume);
	/** wavevolume([0..9]) (0x3ea1): get or set the global wave volume level. */
	int getWaveVolume();
	int setWaveVolume(int newLevel);
	/** soundvol(name[, 0..255]) (0x3ea8): get or set a named SFX cue volume. */
	int getSoundVolume(const Common::String &name);
	int setSoundVolume(const Common::String &name, int newVolume);
	/** currenttheme(which): which==1 -> playing cue name, which==2 -> its track
	 *  file name; 'none' when silent (TI.EXE FUN_00412f20). */
	Common::String currentTheme(int which);
	/** currentsound(which): which==1/2/3 query active SFX sound slots; "None"
	 *  when silent (TI.EXE FUN_00412e60). */
	Common::String currentSound(int which);
	/** currentvoice(): active voice cue name, or "None" when silent. */
	Common::String currentVoice();
	/** voicedone(): true once the voicesound() channel has finished playing. */
	bool voiceDone();

	// Cast/actor subsystem (vm_host_actor.cpp).
	// An actor's star is a named world-space placement point in a SET. Its
	// pose is a named visual shape in its CST, potentially an animation with
	// several frames and viewing directions. These select where it is and
	// what it looks like, respectively; neither is a talking-head PUP action.
	/** opencastfile('name.cst') (0x2eed FUN_0041f1c0): load actor records. */
	void openCastFile(const Common::String &name);
	/** closecastfile('name.cst') (0x2eee FUN_004211b0): remove a cast and actors. */
	void closeCastFile(const Common::String &name);
	/** actorinstance(source, newName) (0x2f2b FUN_0041f6a0): clone a runtime actor record. */
	void actorInstance(const Common::String &source, const Common::String &newName);
	/** sendtocast('file.cst', message(args)): dispatch against the cast script;
	 *  sendtoactor prepends the actor's own script to the chain. */
	void sendToCast(const Common::String &cast, const Common::String &message,
			const Common::Array<Value> &args);
	Value sendToCastFx(const Common::String &cast, const Common::String &message,
			const Common::Array<Value> &args);
	void sendToActor(const Common::String &actor, const Common::String &message,
			const Common::Array<Value> &args);
	Value sendToActorFx(const Common::String &actor, const Common::String &message,
			const Common::Array<Value> &args);
	/** countactors() / indextoactor(i): native global actor list. */
	int countActors();
	Common::String indexToActor(int index);
	bool getActorVisible(const Common::String &name);
	bool setActorVisible(const Common::String &name, bool visible);
	Common::String getActorSet(const Common::String &name);
	Common::String setActorSet(const Common::String &name, const Common::String &newSet);
	/** Named placement point stored in the actor's sceneName field; empty if actor is missing. */
	Common::String getActorStar(const Common::String &name);
	/**
	 * Store the lowercased star name and select world-space placement. If the
	 * actor's SET is open and contains that star, copy its x/y/z coordinates.
	 * Otherwise retain the name without changing coordinates. Does not walk
	 * the actor there or open another SET. Returns the stored name, or empty
	 * if the actor is missing.
	 */
	Common::String setActorStar(const Common::String &name, const Common::String &newStar);
	/** Named visual shape (shapeName), not its current animation-frame index. */
	Common::String getActorPose(const Common::String &name);
	/**
	 * Select a lowercased shape name; changing it resets poseIndex to frame 0
	 * and requests a redraw. This stores the name without validating that the
	 * shape exists. Returns the stored name, or empty if the actor is missing.
	 */
	Common::String setActorPose(const Common::String &name, const Common::String &newPose);
	void actorXYZ(const Common::String &name, int x, int y, int z);
	int actorXYZ(const Common::String &name, int selector);
	int getActorDeg(const Common::String &name);
	int setActorDeg(const Common::String &name, int newDeg);
	int getActorDist(const Common::String &name);
	void setActorDist(const Common::String &name, int newDist);
	int getActorValue(const Common::String &name);
	int setActorValue(const Common::String &name, int newValue);
	Common::String getActorOwner(const Common::String &name);
	Common::String setActorOwner(const Common::String &name, const Common::String &newOwner);
	void actorZClip(const Common::String &name, int zClip);
	void actorSpeed(const Common::String &name, int speed);
	void actorScale(const Common::String &name, int scale);
	void actorTurn(const Common::String &name, int turn);
	void turnToDeg(const Common::String &name, int deg);
	void walkToStar(const Common::String &name, const Common::String &star);
	void walkOnPath(const Common::String &name, const Common::String &path, const Common::String &dest);
	void walkToXYZ(const Common::String &name, int x, int y, int z);
	void stopWalk(const Common::String &name);
	/** pausewalk(actor, flag) (0x3eb0 FUN_00446ea0 -> FUN_00425590): pause or
	 *  resume a queued walk record. */
	void pauseWalk(const Common::String &name, int flag);
	/** actorexists(name) (0x4e37 FUN_0041fb20): whether the actor lookup
	 *  (FUN_004225b0) succeeds. */
	bool actorExists(const Common::String &name);
	bool isWalk(const Common::String &name);
	Common::String walkDest(const Common::String &name);
	int starXYZ(const Common::String &name, int selector);

	// Shop/prop subsystem (vm_host_props.cpp).
	/** openshopfile('name.shp'): load a .SHP prop container and dispatch its
	 *  openshop()/openprop() messages (TI.EXE 0x2f18 FUN_00428450). */
	void openShopFile(const Common::String &name);
	/** closeshopfile('name.shp'): remove the shop and its props (0x2f19). */
	void closeShopFile(const Common::String &name);
	void propInstance(const Common::String &source, const Common::String &newName);
	/** sendtoshop('file.shp', message(args)) (0x2f1b FUN_0042b2b0): dispatch
	 *  against [shop script, BOOTFILE res2]; sendtoprop (0x2f17 FUN_0042ae80)
	 *  against [prop script, shop script, BOOTFILE res2]. */
	void sendToShop(const Common::String &shop, const Common::String &message,
			const Common::Array<Value> &args);
	Value sendToShopFx(const Common::String &shop, const Common::String &message,
			const Common::Array<Value> &args);
	void sendToProp(const Common::String &prop, const Common::String &message,
			const Common::Array<Value> &args);
	Value sendToPropFx(const Common::String &prop, const Common::String &message,
			const Common::Array<Value> &args);
	/** propexists(name) (0x4e38 FUN_00428fc0): whether the prop lookup succeeds. */
	bool propExists(const Common::String &name);
	/** propvisible(name[, flag]) (0x3e8f FUN_00429dc0/FUN_00429d00). */
	bool propVisible(const Common::String &name);
	void propVisible(const Common::String &name, bool visible);
	/** propview(name[, shape]) (0x3e99 FUN_004294a0/FUN_004293a0): current or
	 *  selected shape name. */
	Common::String propView(const Common::String &name);
	void propView(const Common::String &name, const Common::String &shape);
	/** propset(name, set) (0x3e9b FUN_00428c20): assign SET placement and
	 *  switch back to world/SET-space mode. */
	void propSet(const Common::String &name, const Common::String &setName);
	/** propxyz(name, x, y, z) (0x3e91 FUN_0042a140): world/SET-space placement;
	 *  the selector getter (FUN_0042a250) reads 1=x, 2=y, 3=z, 4=packed point. */
	void propXYZ(const Common::String &name, int x, int y, int z);
	int propXYZ(const Common::String &name, int selector);
	/** propstar(name[, star]) (0x3e94 FUN_00429320/FUN_004291f0): get or set
	 *  the prop's SET star, a named 3D placement point in the current .SET room. */
	Common::String getPropStar(const Common::String &name);
	Common::String setPropStar(const Common::String &name, const Common::String &newStar);
	/** propxy(name, selector) (0x3e92 FUN_0042a450): selector 1 = x, 2 = y,
	 *  3 = packed point; the setter (FUN_0042a370) is screen-space placement. */
	int propXY(const Common::String &name, int selector);
	void setPropXY(const Common::String &name, int x, int y);
	/** propscale(name, scale) (0x3e9c FUN_00429870): non-negative world scale. */
	void propScale(const Common::String &name, int scale);
	/** propzclip(name, dist) (0x3eb4 FUN_00428ea0): world z clip distance. */
	void propZClip(const Common::String &name, int dist);
	/** propdist(name[, d]) (0x3e8d FUN_00429670/FUN_004295c0): screen-space
	 *  stacking depth, or projected world depth for SET-space props. */
	int getPropDist(const Common::String &name);
	void propDist(const Common::String &name, int dist);
	/** propdeg(name[, deg]) (0x3e90 FUN_00429730/FUN_00429520): 0..255 view angle. */
	int getPropDeg(const Common::String &name);
	int setPropDeg(const Common::String &name, int newDeg);
	/** propowner(name[, owner]) (0x3ea0 FUN_00428d40): the player is "frank". */
	Common::String getPropOwner(const Common::String &name);
	Common::String setPropOwner(const Common::String &name, const Common::String &newOwner);
	/** propvalue(name[, value]) (0x3eaa FUN_004290d0/FUN_00428e00): stored int. */
	int getPropValue(const Common::String &name);
	int setPropValue(const Common::String &name, int newValue);
	/** countprops() (0x4e3f FUN_0042b4f0): total props across ALL open shops. */
	int countProps();
	/** indextoprop(i) (0x4e40 FUN_0042b550): 1-based global index -> name. */
	Common::String indexToProp(int index);
	/** pointinprop(prop, point) (0x4e48 FUN_00434af1): named screen prop
	 *  containment through the current cel opacity mask. */
	bool pointInProp(const Common::String &name, int32 packedPoint);

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
	 * Install the named mouse cursor, decoding it on demand from the user's
	 * game executable. The cursor bitmaps are copyrighted game assets, so they
	 * are never embedded in ScummVM: they are read at runtime from the game's
	 * PE executable (RT_GROUP_CURSOR resources named CURS.ARROW, CURS.HAND, ...).
	 * The PEResources handle and decoded cursor groups are cached for reuse.
	 * Returns true on success.
	 */
	bool setGameCursor(const Common::String &name);
	/** Drain deferred input before backend events; script/modal consumers must use this order. */
	bool pollScriptEvent(Common::Event &event);
	/**
	 * Pump backend events so buttonDown()/stillDown() can read updated button
	 * state, including while a script is waiting for a press or release.
	 * Consume mouse motion and refresh the cursor, but queue button events,
	 * keystrokes and other non-quit events for later pollScriptEvent() calls:
	 * testing whether a button is held must not swallow the click or a key
	 * that a script event handler still needs to receive.
	 * Stop after the first left/right button event so a queued press and release
	 * are not both consumed before the caller can observe the pressed state.
	 * Return false on quit or return-to-launcher; do not defer those requests.
	 */
	bool pollInputStateEvents();
	/** Handle motion/quit and defer other events; returns whether the mouse position changed. */
	bool pumpCursorMotionEvents();
	void reassertCursorVisibility();
	/** Present pending cursor motion without re-rendering the scene. */
	void presentCursorIfDirty();
	/** Wait while pumping cursor input; returns whether cursor presentation occurred, not completion. */
	bool delayMillisWithCursorUpdates(uint32 delayMillis);

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

	/** Borrowed from the metaengine, which destroys the description after the engine. */
	const DreamFactoryGameDescription &_gameDescription;
	/** Must precede _pathRuntime: that runtime borrows this per-title policy. */
	Common::ScopedPtr<GameSupport> _gameSupport;
	Common::RandomSource _rnd;

	CursorRuntime _cursorRuntime;
	bool _cursorPresentationDirty = false;
	/** Input preserved by nested polling helpers for the next script/modal consumer. */
	Common::Queue<Common::Event> _deferredInputEvents;
	/** True when the last pollScriptEvent() came from _deferredInputEvents
	 *  rather than fresh from the backend. Diagnostic only: dispatch time and
	 *  arrival time differ by however long a script held the main loop. */
	bool _lastScriptEventWasDeferred = false;
	/** Log a mouse-button event at the moment it is queued, so a -d 1 trace
	 *  shows backend arrival times and not just dispatch times. */
	void noteDeferredInputEvent(const Common::Event &event);

	StageRuntime _stageRuntime;
	SetRuntime _setRuntime;

	/** Kind recorded by the last hittest, read back by result() — mirrors the
	 *  TI.EXE global DAT_00461298. */
	Common::String _hitKind;

	PropRuntime _propRuntime;

	ActorRuntime _actorRuntime;

	PuppetRuntime _puppetRuntime;
	/**
	 * Dispatch a script handler call using an ordered list of scripts to search.
	 * A dispatch "scope" is a script containing named handler definitions, not
	 * a C++ scope or a variable table. Search is case-insensitive; the first
	 * matching handler runs. Its `pass` statement requests the next match in
	 * the chain, rather than broadcasting the message to every script.
	 *
	 * Typical lookup orders (leftmost first; GLOBAL is BOOTFILE res2):
	 * - prop -> shop -> GLOBAL
	 * - actor -> cast -> GLOBAL
	 * - button -> flat -> stage -> GLOBAL
	 * - painting -> scene -> set -> GLOBAL
	 *
	 * Each message uses the recipient's search list, not the calling script's.
	 * For example, if the boot mousedown handler sends "mousedown" to a prop,
	 * search only that prop, its shop and GLOBAL. If none handles the message,
	 * return to the boot handler; do not search BOOTFILE res1 and accidentally
	 * call the same boot mousedown handler again.
	 *
	 * Sending a message waits for its handler to finish before the caller
	 * continues. During that call, the VM uses the recipient's search list and
	 * self/target names. Afterwards it restores the caller's list and names.
	 * If a handler sends another message, the same save/restore happens for
	 * that inner call, so each caller resumes with its own lookup context.
	 *
	 * This overload searches @p scope1, @p scope2, then GLOBAL, skipping null
	 * scripts. @p self / @p targetProp supply the names read by the VM context
	 * atoms 0xfba/0xfbb, separate from handler parameters in @p args. Scripts
	 * are borrowed: callers must keep them alive throughout execution, even
	 * if the handler closes or replaces the resource from which they came.
	 */
	void dispatchWithScopes(const Script *scope1, const Script *scope2,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext = "shop/prop");
	/** Same lookup/context rules as dispatchWithScopes(), but returns the handler's value. */
	Value dispatchWithScopesValue(const Script *scope1, const Script *scope2,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext);
	/** Search scope1, scope2, scope3, then GLOBAL; null entries are skipped. */
	Value dispatchWithThreeScopesValue(const Script *scope1, const Script *scope2,
			const Script *scope3, const Common::String &self,
			const Common::String &targetProp, const Common::String &message,
			const Common::Array<Value> &args, const char *debugContext);
	/** Search @p scopes in array order, then GLOBAL, with the same context for each. */
	Value dispatchWithScopeChainValue(const Common::Array<const Script *> &scopes,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext);
	/**
	 * Like dispatchWithScopeChainValue(), but entries can specify their own
	 * context. For button dispatch, the button script sees the button as self;
	 * a handler found in the flat script sees the flat as self while retaining
	 * the button as targetProp. GLOBAL uses the supplied dispatch context.
	 * See ScriptVM::LibraryScope for the empty-context inheritance rule.
	 */
	Value dispatchWithScopeChainContextsValue(const Common::Array<ScriptVM::LibraryScope> &scopes,
			const Common::String &self, const Common::String &targetProp,
			const Common::String &message, const Common::Array<Value> &args,
			const char *debugContext);
	void dispatchSetMessage(const Common::String &message, const Common::Array<Value> &args);
	Value dispatchSetMessageValue(const Common::String &message, const Common::Array<Value> &args);
	void dispatchSceneMessage(uint32 scene, const Common::String &message,
			const Common::Array<Value> &args);
	bool closeCurrentSceneForNavigation();
	/** Perform a queued load at the main-loop safe point; true only if restoration succeeds. */
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

	int16 _cameraHiValue = 0; ///< camerahi (0x3ea3) script value, mirrors DAT_0046119a.

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
	int _pendingLoadSlot = -1; ///< Queued save slot; -1 means no load is pending.
	Common::String _pendingLoadSignature; ///< Queued opengame() signature; empty for framework loads.
};

} // End of namespace DreamFactory

#endif // DREAMFACTORY_DREAMFACTORY_H

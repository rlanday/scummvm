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

#ifndef CYBERFLIX_VM_H
#define CYBERFLIX_VM_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

#include "cyberflix/script.h"

namespace Cyberflix {

/**
 * Tagged value held on the CyberFlix VM operand stack.
 *
 * The precise runtime value model of the "Bicycle" VM is still being recovered
 * from TI.EXE; this is the structural skeleton the
 * confirmed opcodes operate on and will be refined as the evaluator at
 * 0x00419cf0 is mapped. Keep changes additive so the disassembler/trace stays
 * usable while semantics are filled in.
 */
struct Value {
	enum Type {
		kInt = 0,    ///< Integer constant (pushInt / arithmetic result).
		kSymbol,     ///< Reference to a symbol in the script string pool.
		kString,     ///< String constant from the pool.
		kBool        ///< Boolean (result of comparison/logical operators).
	};

	Type type;
	int32 intValue;
	Common::String strValue;

	Value() : type(kInt), intValue(0) {}
	static Value makeInt(int32 v) { Value r; r.type = kInt; r.intValue = v; return r; }
	static Value makeBool(bool v) { Value r; r.type = kBool; r.intValue = v ? 1 : 0; return r; }
	static Value makeSymbol(const Common::String &s) { Value r; r.type = kSymbol; r.strValue = s; return r; }
	static Value makeString(const Common::String &s) { Value r; r.type = kString; r.strValue = s; return r; }

	Common::String toString() const;
};

/**
 * Host interface through which the VM drives engine subsystems (video, audio,
 * navigation, ...). The interpreter itself is engine-agnostic and only knows
 * how to evaluate scripts; effectful builtins are forwarded here so the engine
 * can realise them. A null host (the default) makes every effectful builtin a
 * logged no-op, which keeps the VM usable as a standalone reverse-engineering
 * harness.
 */
class VMMovieHost {
public:
	virtual ~VMMovieHost() {}

	/** Play the movie named @p name (a MOVIES/ basename, e.g. "logo.mov"). */
	virtual void playMovie(const Common::String &name) = 0;
};

class VMNavigationHost {
public:
	virtual ~VMNavigationHost() {}

	/** Open the stage file @p name (a DATA/ basename, e.g. "main.stg"). */
	virtual void openStageFile(const Common::String &name) {}

	/** Close the current stage file (closestagefile, TI.EXE opcode 0x2f1d). */
	virtual void closeStageFile() {}

	/** gotoflat(name|index) (0x2f1e): switch to a 1-based stage node or node name. */
	virtual void gotoFlat(const Value &flat) {}

	/** currentstage() (0x4e50): open stage name, or native "None". */
	virtual Common::String currentStage() { return "None"; }

	/** stagevisible([flag]) (0x3e88): current stage visibility flag. */
	virtual bool getStageVisible() { return false; }
	virtual bool setStageVisible(bool visible) { return visible; }

	/** currentflat() (0x4e46): current stage node name, or native "None". */
	virtual Common::String currentFlat() { return "None"; }

	/**
	 * Deliver the message call `message(args)` to the open stage's script. The
	 * original sendtostage (TI.EXE FUN_0040ad80) passes the message UNevaluated
	 * and dispatches it against the stage script's definitions with the global
	 * library as fallback scope. The engine realises that by calling back into
	 * ScriptVM::callFunction over its registered libraries.
	 */
	virtual void sendToStage(const Common::String &message, const Common::Array<Value> &args) {}
	virtual Value sendToStageFx(const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/**
	 * sendtoboot(message(args)) (0x2f31, FUN_00439080/FUN_004390a0): dispatch
	 * against [BOOTFILE res1, BOOTFILE res2].
	 */
	virtual void sendToBoot(const Common::String &message, const Common::Array<Value> &args) {}
	virtual Value sendToBootFx(const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/**
	 * sendtoflat(flat, message(args)) (0x2f25): dispatch against
	 * [flat/node script, stage script, BOOTFILE res2].
	 */
	virtual void sendToFlat(const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args) {}
	virtual Value sendToFlatFx(const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/**
	 * sendtobutton(flat, button, message(args)) (0x2f24): dispatch against
	 * [button script, flat/node script, stage script, BOOTFILE res2].
	 */
	virtual void sendToButton(const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args) {}
	virtual Value sendToButtonFx(const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args) { return Value(); }

	/**
	 * Open the set file @p name. @p scene / @p view optionally name the scene
	 * and view to make current (the opensetfile optional args; TI.EXE
	 * FUN_00430690). Empty = default to the set's first scene.
	 */
	virtual void openSetFile(const Common::String &name,
			const Common::String &scene = Common::String(),
			const Common::String &view = Common::String()) {}

	/** Close the open set file (closesetfile, TI.EXE opcode 0x2f01). */
	virtual void closeSetFile() {}

	/** Name of the open set (currentset, 0x4e55), or "none". */
	virtual Common::String currentSet() { return "none"; }
	virtual Value sendToSetFx(const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/**
	 * currentview([name]) (0x3e8b): current SET view name, "Moving", or "none";
	 * with a name, switch the current scene to that view.
	 */
	virtual Common::String getCurrentView() { return "none"; }
	virtual Common::String setCurrentView(const Common::String &target) { return target; }

	/** currentdeg() (0x3e9f FUN_00431d50): current SET heading, or -1. */
	virtual int currentDeg() { return -1; }

	/**
	 * currentscene([name|left|right|strait]) (0x3e9d): no arg reads the current
	 * SET scene name; an arg switches scene or starts the native navigation
	 * action named by BOOTFILE's keydown fallback.
	 */
	virtual Common::String getCurrentScene() { return "none"; }
	virtual Common::String setCurrentScene(const Common::String &target) { return target; }

	/**
	 * setvisible([flag]) (0x3e87): with no args, read the open-set visibility
	 * flag (TI.EXE FUN_00431ca0 / DAT_00461182); with an arg, set it
	 * (FUN_004318d0) and invalidate/redraw the set.
	 */
	virtual bool getSetVisible() { return false; }
	virtual bool setSetVisible(bool visible) { return visible; }

	/** currentpuppet() (0x4e51), or "none" while the puppet subsystem is absent. */
	virtual Common::String currentPuppet() { return "none"; }

	virtual void openPuppetFile(const Common::String &name) {}
	virtual void closePuppetFile() {}
	virtual void sendToPuppet(const Common::String &puppet, const Common::String &message,
			const Common::Array<Value> &args) {}
	virtual Value sendToPuppetFx(const Common::String &puppet, const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }
	virtual void puppetScript(const Common::String &name) {}
	virtual void puppetClear() {}
	virtual void puppetSpeak(const Common::String &name, int mode) {}
	virtual void puppetBevel(const Common::String &name, int mode) {}
	virtual void puppetGrab(bool enabled) {}
	virtual int puppetEvent(int timeout) { return -1; }
	virtual Common::String getPuppetBase() { return Common::String(); }
	virtual Common::String setPuppetBase(const Common::String &newBase) { return newBase; }
	virtual bool getPuppetVisible() { return false; }
	virtual bool setPuppetVisible(bool visible) { return visible; }
	virtual int getPuppetParam(int selector) { return 0; }
	virtual int setPuppetParam(int selector, int newValue) { return newValue; }
	virtual int countPuppets() { return 0; }
	virtual Common::String indexToPuppet(int index) { return Common::String(); }

	/** Dispatch a scene message. The native sendtoscene() does not switch the
	 *  current rendered scene; current scene changes are driven by currentscene()
	 *  / SET navigation. */
	virtual void sendToScene(const Common::String &scene,
			const Common::String &message = Common::String(),
			const Common::Array<Value> &args = Common::Array<Value>()) {}
	virtual Value sendToSceneFx(const Common::String &scene, const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/**
	 * sendtopainting(scene, view, painting, message(args)) (0x2f22): dispatch
	 * against [painting script, scene script, set script, BOOTFILE res2].
	 */
	virtual void sendToPainting(const Common::String &scene, const Common::String &view,
			const Common::String &painting, const Common::String &message,
			const Common::Array<Value> &args) {}
	virtual Value sendToPaintingFx(const Common::String &scene, const Common::String &view,
			const Common::String &painting, const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/** countpaintings(scene, view) (0x4e32): SET painting records in the view. */
	virtual int countPaintings(const Common::String &scene, const Common::String &view) { return 0; }

	/** indextopainting(scene, view, index) (0x4e36): native 1-based painting lookup. */
	virtual Common::String indexToPainting(const Common::String &scene,
			const Common::String &view, int index) { return Common::String(); }

	/** roadahead(scene, view) (0x4e94): whether the view has a forward transition. */
	virtual bool roadAhead(const Common::String &scene, const Common::String &view) { return false; }

	/** cameraxyz(selector) (0x4e5f FUN_00437870): selector 1/2/3 = x/y/z,
	 *  4 = packed x/y point for the active SET camera. */
	virtual int cameraXYZ(int selector) { return 0; }

	/** playerxyz(selector) (0x4e60 FUN_00437950): selector 1/2/3 = x/y/z,
	 *  4 = packed x/y point for the active SET player point. */
	virtual int playerXYZ(int selector) { return 0; }
};

class VMSystemHost {
public:
	virtual ~VMSystemHost() {}

	/**
	 * actionframe(n): true if the last movie ended on action frame @p n —
	 * i.e. which menu button the user clicked (TI.EXE 0x4e73 FUN_00435026).
	 */
	virtual bool actionFrame(int n) { return false; }

	/** random(n) (0x4e21): script-visible range is 1..n. */
	virtual int randomNumber(int n) { return 0; }

	/** framerate([n]) (0x3e96): native compositor pacing in 60 Hz timer units. */
	virtual int getFrameRate() { return 3; }
	virtual int setFrameRate(int newRate) { return newRate; }

	/** frame() (0x4e6a FUN_00435a30): native compositor pass counter. */
	virtual int frameCounter() { return 0; }

	/**
	 * clut(name): snap the hardware palette to the named CLUT instantly
	 * (TI.EXE 0x2f06 FUN_00446500 -> FUN_0041ba80). Built-in names include
	 * "black", and "set"/"stage"/"puppet" resolve to the palette embedded in
	 * the currently open file of that kind. Pixels are untouched.
	 */
	virtual void setClut(const Common::String &name) {}

	/** blackscreen() (0x2f13 FUN_00446b80): fill the window with black
	 *  pixels (a GDI rect fill in the original). Palette untouched. */
	virtual void blackScreen() {}

	/**
	 * forceupdate() (0x2f14 -> FUN_00446910 -> FUN_00423a60): process the
	 * display queues and present the current compositor state immediately.
	 */
	virtual void forceUpdate() {}

	/** message(text) (0x2ee1): copy/log a script diagnostic message. */
	virtual void message(const Common::String &text) {}

	/**
	 * notedialog(text) (0x2f2f -> FUN_004461e0 -> FUN_00408fc0): show a modal
	 * note dialog with an OK button. The original calls MessageBoxA with flags
	 * 0x2040 (MB_OK | MB_ICONINFORMATION | MB_TASKMODAL). Unlike message(),
	 * which only writes the debug-console line, notedialog() is user-visible:
	 * the CTL.STG SAVE/OPEN button scripts use it (gated by `if (tour)`) to warn
	 * "Sorry, you can't save/open a saved game during the tour."
	 */
	virtual void noteDialog(const Common::String &text) {}

	/** flushevents() (0x2f29): purge queued input events. */
	virtual void flushEvents() {}

	/** drawstring(text, point, color, size) (0x2f30): draw text to the current port. */
	virtual void drawString(const Common::String &text, int32 packedPoint, int color, int size) {}

	/**
	 * blacktoscreen(target, n) / screentoblack(target, n) (0x2f11/0x2f12,
	 * FUN_00446b00/FUN_00446a80 -> FUN_0041b3f0/FUN_0041b3a0): fade the
	 * hardware palette between black and the target CLUT, one interpolation
	 * step per 60 Hz tick over @p n steps. The pixels are already on screen;
	 * only the palette moves.
	 */
	virtual void fadePalette(const Common::String &target, int steps, bool toBlack) {}

	/** visualeffect(effect, dur) (0x2ee9 FUN_00446400): set the global default
	 *  transition for subsequent redraws ('plain' = opcode 0x5dce). */
	virtual void setVisualEffect(uint16 effect, int duration) {}

	/** makeloop(kind, target, message, delay): schedule a native loop callback. */
	virtual void makeLoop(const Common::String &kind, const Common::String &target,
			const Common::String &message, int delay) {}

	/** stoploop(kind[, target]): remove scheduled loop callbacks. */
	virtual void stopLoop(const Common::String &kind, const Common::String &target) {}

	/** pauseloop(kind, paused): pause or resume scheduled loop callbacks. */
	virtual void pauseLoop(const Common::String &kind, bool paused) {}

	/** makecricket(name, ...): start a positional/ambient SFX cue. */
	virtual void makeCricket(const Common::String &name) {}

	/** stopcricket(name): stop a native cricket ambient cue. */
	virtual void stopCricket(const Common::String &name) {}

	/** pausecricket(kind, paused): pause or resume cricket ambient cues. */
	virtual void pauseCricket(const Common::String &kind, bool paused) {}
};

class VMAudioHost {
public:
	virtual ~VMAudioHost() {}

	/** opentrackfile('name.trk'): load a .TRK track file (TI.EXE FUN_00411be0). */
	virtual void openTrackFile(const Common::String &name) {}

	/** closetrackfile('name.trk'): unload a track file (TI.EXE FUN_00412070). */
	virtual void closeTrackFile(const Common::String &name) {}

	/**
	 * playtheme('name.trk'): start the track's theme playlist (intro cues once,
	 * then loop from the loop index forever) on the theme channel, replacing any
	 * current theme (TI.EXE FUN_00412250).
	 */
	virtual void playTheme(const Common::String &name) {}

	/** halttheme(): stop the theme channel (TI.EXE FUN_00412410). */
	virtual void haltTheme() {}

	/**
	 * singlesound/multiplesound/dualsound/bothsound(name): play a named SFX cue
	 * on the two normal sound channels (TI.EXE FUN_004122d0..00412390).
	 */
	virtual void playSound(const Common::String &name, int mode) {}

	/** voicesound(name): play a named SFX cue on the voice channel (FUN_004123d0). */
	virtual void playVoice(const Common::String &name) {}

	/** haltsound(which): which 1/2 stop normal sound slot, 3 stops both. */
	virtual void haltSound(int which) {}

	/** haltvoice(): stop the voice channel. */
	virtual void haltVoice() {}

	/** themevol('name.trk', 0-255): set the track's theme volume (FUN_004125c0). */
	virtual void themeVolume(const Common::String &name, int volume) {}

	/** wavevolume([0..9]) (0x3ea1): get or set the global wave volume level. */
	virtual int getWaveVolume() { return 9; }
	virtual int setWaveVolume(int newLevel) { return newLevel; }

	/** soundvol(name[, 0..255]) (0x3ea8): get or set a named SFX cue volume. */
	virtual int getSoundVolume(const Common::String &name) { return 255; }
	virtual int setSoundVolume(const Common::String &name, int newVolume) { return newVolume; }

	/**
	 * currenttheme(which): which==1 -> playing cue name, which==2 -> its track
	 * file name; 'none' when silent (TI.EXE FUN_00412f20).
	 */
	virtual Common::String currentTheme(int which) { return "none"; }

	/**
	 * currentsound(which): which==1/2/3 query active SFX sound slots; "None"
	 * when silent (TI.EXE FUN_00412e60).
	 */
	virtual Common::String currentSound(int) { return "None"; }

	/** currentvoice(): active voice cue name, or "None" when silent. */
	virtual Common::String currentVoice() { return "None"; }

	/** voicedone(): true once the voicesound() channel has finished playing. */
	virtual bool voiceDone() { return true; }
};

class VMInputHost {
public:
	virtual ~VMInputHost() {}

	/** keyaborts([res, key, flag]) (0x3ead): key-abort table toggle/global state. */
	virtual bool keyAborts(const Common::String *resource, const Common::String *key,
			const bool *enabled) { return false; }

	/** optionkey() (0x4e5a): true while the native Option/Shift key is down. */
	virtual bool optionKey() { return false; }

	/** path(slot[, value]) (0x3e89 FUN_004462a0): get or set a native path slot. */
	virtual Common::String getPathSlot(int slot) { return Common::String(); }
	virtual Common::String setPathSlot(int slot, const Common::String &newPath) { return newPath; }

	/** currentcd([name]) (0x3ea2 FUN_00439df0/FUN_0043a290): switch/query current CD label. */
	virtual Common::String getCurrentCD() { return Common::String(); }
	virtual Common::String setCurrentCD(const Common::String &requested) { return requested; }
};

class VMActorHost {
public:
	virtual ~VMActorHost() {}

	/** opencastfile('name.cst') (0x2eed FUN_0041f1c0): load actor records. */
	virtual void openCastFile(const Common::String &name) {}

	/** closecastfile('name.cst') (0x2eee FUN_004211b0): remove a cast and actors. */
	virtual void closeCastFile(const Common::String &name) {}

	/** sendtocast('file.cst', message(args)): dispatch against the cast script. */
	virtual void sendToCast(const Common::String &cast, const Common::String &message,
			const Common::Array<Value> &args) {}
	virtual Value sendToCastFx(const Common::String &cast, const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/** sendtoactor(actor, message(args)): dispatch [actor script, cast script]. */
	virtual void sendToActor(const Common::String &actor, const Common::String &message,
			const Common::Array<Value> &args) {}
	virtual Value sendToActorFx(const Common::String &actor, const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/** countactors()/ indextoactor(i): native global actor list. */
	virtual int countActors() { return 0; }
	virtual Common::String indexToActor(int index) { return Common::String(); }

	virtual bool getActorVisible(const Common::String &name) { return false; }
	virtual bool setActorVisible(const Common::String &name, bool visible) { return visible; }
	virtual Common::String getActorSet(const Common::String &name) { return Common::String(); }
	virtual Common::String setActorSet(const Common::String &name, const Common::String &newSet) { return newSet; }
	virtual Common::String getActorStar(const Common::String &name) { return Common::String(); }
	virtual Common::String setActorStar(const Common::String &name, const Common::String &newStar) { return newStar; }
	virtual Common::String getActorPose(const Common::String &name) { return Common::String(); }
	virtual Common::String setActorPose(const Common::String &name, const Common::String &newPose) { return newPose; }
	virtual void actorXYZ(const Common::String &name, int x, int y, int z) {}
	virtual int actorXYZ(const Common::String &name, int selector) { return 0; }
	virtual int getActorDeg(const Common::String &name) { return 0; }
	virtual int setActorDeg(const Common::String &name, int newDeg) { return newDeg; }
	virtual int getActorDist(const Common::String &name) { return 0; }
	virtual void setActorDist(const Common::String &name, int newDist) {}
	virtual int getActorValue(const Common::String &name) { return 0; }
	virtual int setActorValue(const Common::String &name, int newValue) { return newValue; }
	virtual Common::String getActorOwner(const Common::String &name) { return Common::String(); }
	virtual Common::String setActorOwner(const Common::String &name, const Common::String &newOwner) { return newOwner; }
	virtual void actorZClip(const Common::String &name, int zClip) {}
	virtual void actorSpeed(const Common::String &name, int speed) {}
	virtual void actorScale(const Common::String &name, int scale) {}
	virtual void actorTurn(const Common::String &name, int turn) {}
	virtual void turnToDeg(const Common::String &name, int deg) {}
	virtual void walkToStar(const Common::String &name, const Common::String &star) {}
	virtual void stopWalk(const Common::String &name) {}
	virtual bool isWalk(const Common::String &name) { return false; }
	virtual Common::String walkDest(const Common::String &name) { return "None"; }
	virtual int starXYZ(const Common::String &name, int selector) { return 0; }
};

class VMInteractionHost {
public:
	virtual ~VMInteractionHost() {}

	/** openshopfile('name.shp'): load a .SHP prop container and dispatch its
	 *  openshop()/openprop() messages (TI.EXE 0x2f18 FUN_00428450). */
	virtual void openShopFile(const Common::String &name) {}

	/** closeshopfile('name.shp'): remove the shop and its props (0x2f19). */
	virtual void closeShopFile(const Common::String &name) {}
	virtual void propInstance(const Common::String &source, const Common::String &newName) {}

	/**
	 * sendtoshop('file.shp', message(args)): dispatch @p message against the
	 * shop's script with the global library as fallback (TI.EXE 0x2f1b
	 * FUN_0042b2b0; chain [shop script, BOOTFILE res2]). The message arrives
	 * with its arguments already evaluated in the caller's scope.
	 */
	virtual void sendToShop(const Common::String &shop, const Common::String &message,
			const Common::Array<Value> &args) {}
	virtual Value sendToShopFx(const Common::String &shop, const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/**
	 * sendtoprop('propname', message(args)): dispatch against the prop's own
	 * script, then its shop's script, then the global library (TI.EXE 0x2f17
	 * FUN_0042ae80; 3-entry chain with "Prop Script: "/"Shop Script: "/
	 * "System: " labels).
	 */
	virtual void sendToProp(const Common::String &prop, const Common::String &message,
			const Common::Array<Value> &args) {}
	virtual Value sendToPropFx(const Common::String &prop, const Common::String &message,
			const Common::Array<Value> &args) { return Value(); }

	/** propvisible(name) getter (0x3e8f FUN_00429dc0). */
	virtual bool propVisible(const Common::String &name) { return false; }

	/** propvisible(name, flag) setter (0x3e8f FUN_00429d00). */
	virtual void propVisible(const Common::String &name, bool visible) {}

	/** propview(name) getter (0x3e99 FUN_004294a0). */
	virtual Common::String propView(const Common::String &name) { return Common::String(); }

	/** propview(name, shape) setter (0x3e99 FUN_004293a0): select the named shape. */
	virtual void propView(const Common::String &name, const Common::String &shape) {}

	/** propset(name, set) (0x3e9b FUN_00428c20): assign SET placement and
	 * switch back to world/SET-space mode. */
	virtual void propSet(const Common::String &name, const Common::String &setName) {}

	/** propxyz(name, x, y, z) (0x3e91 FUN_0042a140): world/SET-space placement,
	 * setting mode=1 so the prop is no longer a screen overlay. */
	virtual void propXYZ(const Common::String &name, int x, int y, int z) {}

	/** propxy(name, selector) (0x3e92 FUN_0042a450): selector 1 = x,
	 *  2 = y, 3 = packed point. */
	virtual int propXY(const Common::String &name, int selector) { return 0; }

	/** propxy(name, x, y) (0x3e92 FUN_0042a370): screen-space placement —
	 *  sets mode=0 and changes non-negative depths to -1 alongside the anchor point. */
	virtual void setPropXY(const Common::String &name, int x, int y) {}

	/** propscale(name, scale) (0x3e9c FUN_00429870): non-negative world scale. */
	virtual void propScale(const Common::String &name, int scale) {}

	/** propzclip(name, dist) (0x3eb4 FUN_00428ea0): world z clip distance. */
	virtual void propZClip(const Common::String &name, int dist) {}

	/** propdist(name, d) (0x3e8d FUN_004295c0): screen-space stacking depth
	 *  (only applied when d < 0 and the prop is in screen mode). */
	virtual void propDist(const Common::String &name, int dist) {}

	/** propdeg(name[, deg]) (0x3e90 FUN_00429730/FUN_00429520): get or set
	 *  the prop's 0..255 view angle. */
	virtual int getPropDeg(const Common::String &name) { return 0; }
	virtual int setPropDeg(const Common::String &name, int newDeg) { return newDeg; }

	/** propowner(name[, owner]) (0x3ea0 FUN_00428d40): set (2 args) or get
	 *  (1 arg) the prop's owner string; the player is "frank". */
	virtual Common::String getPropOwner(const Common::String &name) { return Common::String(); }
	virtual Common::String setPropOwner(const Common::String &name, const Common::String &newOwner) { return newOwner; }

	/** propvalue(name[, value]) (0x3eaa FUN_004290d0/FUN_00428e00): stored int. */
	virtual int getPropValue(const Common::String &name) { return 0; }
	virtual int setPropValue(const Common::String &name, int newValue) { return newValue; }

	/** countprops() (0x4e3f FUN_0042b4f0): total props across ALL open shops. */
	virtual int countProps() { return 0; }

	/** indextoprop(i) (0x4e40 FUN_0042b550): 1-based global index -> name. */
	virtual Common::String indexToProp(int index) { return Common::String(); }

	/** pointinbutton(flat, button, point) (0x4e4b FUN_0040a0d0): named STG
	 *  button rect containment, with point packed as (x << 16) | y. */
	virtual bool pointInButton(const Common::String &flat,
			const Common::String &button, int32 packedPoint) { return false; }

	/** pointinpainting(scene, view, painting, point) (0x4e31 FUN_004322e0):
	 *  named SET painting rect containment, with point packed as (x << 16) | y. */
	virtual bool pointInPainting(const Common::String &scene,
			const Common::String &view, const Common::String &painting,
			int32 packedPoint) { return false; }

	/**
	 * hittest(point) (0x4e66 FUN_00435e70). @p packedPoint packs the screen
	 * point as (x << 16) | (y & 0xffff) (the in-rect test FUN_0041ac60 checks
	 * the high word against left/right and the low word against top/bottom of
	 * {t,l,b,r} rects). Returns the name of the topmost hit and records its
	 * kind for result(): sprite items first (per-pixel through the cel mask,
	 * top-down — FUN_004430f0 walks the compositor item list backwards) ->
	 * "actor"/"prop"; then the open set's viewport -> "painting"/"scene";
	 * then the open stage -> "button"/"flat"; else "None" with empty name.
	 */
	virtual Common::String hitTest(int32 packedPoint) { return Common::String(); }

	/** result() (0x3e8a FUN_004366a0): kind recorded by the last hittest
	 *  (global DAT_00461298). */
	virtual Common::String hitTestResult() { return Common::String(); }

	/** mouse() (0x4e26 FUN_004368b0): current mouse point, packed as
	 *  (x << 16) | (y & 0xffff). */
	virtual int32 mousePoint() { return 0; }

	/** makepoint(x, y) (0x4e24 FUN_00436800): pack an x/y point value. */
	virtual int32 makePoint(int x, int y) { return (static_cast<int32>(static_cast<int16>(x)) << 16) | (static_cast<int32>(y) & 0xffff); }

	/** button() (0x4e25 FUN_00436880): current left mouse button state. */
	virtual bool buttonDown() { return false; }

	/** stilldown() (0x4e27 FUN_00436920): whether a mouse button is still down. */
	virtual bool stillDown() { return false; }

	/** tick() (0x4e28 FUN_004368f0): the native 60 Hz scaled timer. */
	virtual int tick() { return 0; }

	/**
	 * cursor(arg) (0x2f07 FUN_00446920), with the name already resolved to a
	 * TI.EXE PE resource name: int arg n -> "CURS<n>" (FUN_0041b700, format
	 * "%s%d"), "arrow" -> "CURS.ARROW" (FUN_004051b0), "watch" -> "CURS2002"
	 * (the wait cursor preloaded into DAT_00461280 by FUN_0043b610), any other
	 * name -> "CURS.<NAME>" (FUN_0041b580, format "%s.%s" + strupr).
	 */
	virtual void setCursorResource(const Common::String &resourceName) {}

	/** calcdeg(a, b) (0x4e67 FUN_00435c70): signed 8-bit angle in 256ths of a
	 *  full turn from packed point @p a to packed point @p b. */
	virtual int calcDeg(int32 a, int32 b) { return 0; }

	/** calcvectx/y(deg, dist) (0x4e5d/0x4e5e FUN_00437770/004377f0):
	 *  fixed-point trig vector components in native 256-unit angles. */
	virtual int calcVectX(int deg, int dist) { return 0; }
	virtual int calcVectY(int deg, int dist) { return 0; }

	/** calcdist(pointA, pointB) (0x4e71 FUN_00435d20): integer Euclidean distance. */
	virtual int calcDist(int32 a, int32 b) { return 0; }

	/** calcmod(a, b) (0x4e72 FUN_004358f0): signed integer remainder. */
	virtual int calcMod(int a, int b) { return b != 0 ? a % b : 0; }
};

class VMSaveHost {
public:
	virtual ~VMSaveHost() {}

	/** savegame(signature) (0x2f2d FUN_00426620): open the save UI and persist state. */
	virtual void saveGame(const Common::String &signature) {}

	/** opengame(signature) (0x2f2e FUN_004266e0/FUN_00426f00): open the load UI. */
	virtual void openGame(const Common::String &signature) {}

	/** questiondialog(text) (0x4e64 FUN_004363f0): native yes/no message box. */
	virtual bool questionDialog(const Common::String &message) { return false; }

	/** quit() (0x2f27 FUN_00446c80): request engine shutdown. */
	virtual void requestQuit() {}
};

class VMHost : public virtual VMMovieHost, public virtual VMNavigationHost,
		public virtual VMSystemHost, public virtual VMAudioHost,
		public virtual VMInputHost, public virtual VMActorHost,
		public virtual VMInteractionHost, public virtual VMSaveHost {
public:
	~VMHost() override {}
};

/**
 * Executes a parsed CyberFlix Script. This is the structural harness that
 * mirrors the TI.EXE interpreter: a program counter indexing the 8-byte
 * instruction array and an operand stack. Opcode semantics are added
 * incrementally; unimplemented opcodes are logged (in trace mode) and skipped
 * so the VM can be stepped over real scripts during reverse engineering.
 */
class ScriptVM {
public:
	ScriptVM();

	void setTrace(bool on) { _trace = on; }

	/**
	 * Attach the engine host that realises effectful builtins (playMovie, ...).
	 * Without a host, those builtins are logged no-ops. Not owned.
	 */
	void setHost(VMHost *host) {
		_host = host;
		_movieHost = host;
		_navigationHost = host;
		_systemHost = host;
		_audioHost = host;
		_inputHost = host;
		_actorHost = host;
		_interactionHost = host;
		_saveHost = host;
	}

	/** Run @p script from the top until the terminator or a step budget. */
	void run(const Script &script, uint32 maxSteps = 100000);

	/**
	 * Execute @p script as a statement program: a statement loop that handles
	 * the control-flow builtins (if/else/endif/return) using the verified block
	 * scanners, evaluating conditions through the expression evaluator. Other
	 * statements (method calls, assignments) are consumed as expressions for
	 * now; their side effects land as the subsystems are implemented.
	 *
	 * Returns the number of statements executed. Mirrors the TI.EXE main loop
	 * at vaddr 0x0040ba4f.
	 */
	uint32 runProgram(const Script &script, uint32 maxSteps = 100000);

	/**
	 * Register @p script as a function library on the dispatch scope chain
	 * (later additions are searched FIRST, mirroring the 2-entry chain built by
	 * TI.EXE FUN_0040ad80: [stage script, BOOTFILE res2 global library]).
	 * @p script is retained, not owned, and must outlive the VM's use.
	 */
	void addLibrary(const Script *script) { _libraries.push_back(script); }
	void clearLibraries() { _libraries.clear(); }

	/**
	 * Replace the whole dispatch scope chain, returning the previous one so a
	 * caller can restore it. Each TI.EXE dispatch passes a complete, freshly
	 * built chain to the call executor FUN_0040b690 — e.g. sendtoprop
	 * (FUN_0042ae80) builds exactly [prop script, shop script, BOOTFILE res2]
	 * and boot messages (FUN_004390a0) exactly [BOOTFILE res1, BOOTFILE res2]
	 * — so inner dispatches REPLACE the outer chain rather than stack on it.
	 */
	Common::Array<const Script *> swapLibraries(const Common::Array<const Script *> &libs) {
		Common::Array<const Script *> prev = _libraries;
		_libraries = libs;
		return prev;
	}

	/**
	 * Hot-path variant for fixed native dispatch chains. @p scope1 is searched
	 * first, then @p scope2, @p scope3, and finally @p tail. The VM stores the
	 * chain in reverse because callFunction() walks from newest to oldest; this
	 * helper centralizes that ordering and avoids caller-side temporary arrays.
	 */
	Common::Array<const Script *> swapLibrariesFixed(const Script *scope1,
			const Script *scope2, const Script *scope3, const Script *tail) {
		Common::Array<const Script *> prev;
		prev.swap(_libraries);
		_libraries.reserve(4);
		if (tail)
			_libraries.push_back(tail);
		if (scope3)
			_libraries.push_back(scope3);
		if (scope2)
			_libraries.push_back(scope2);
		if (scope1)
			_libraries.push_back(scope1);
		return prev;
	}

	void restoreLibraries(Common::Array<const Script *> &libs) {
		_libraries.swap(libs);
	}

	/** Remove the most recent registration of @p script from the scope chain
	 *  (used to pop temporary shop/prop dispatch scopes). */
	void removeLibrary(const Script *script) {
		for (int i = static_cast<int>(_libraries.size()) - 1; i >= 0; --i)
			if (_libraries[i] == script) {
				_libraries.remove_at(i);
				return;
			}
	}

	/**
	 * Set the dispatch context strings read by the atom opcodes 0xfba ("self":
	 * the name of the prop/shop/stage whose script is running, chain entry
	 * +0x1e) and 0xfbb (the target prop's name, chain entry +0x3e). TI.EXE
	 * builds these per scope-chain entry in FUN_0042ae80/FUN_0040ad80; prop
	 * scripts use 0xfba as the name argument to propvisible/propxy/propview.
	 */
	void setDispatchContext(const Common::String &self, const Common::String &targetProp) {
		_ctxSelf = self;
		_ctxProp = targetProp;
	}
	const Common::String &contextSelf() const { return _ctxSelf; }
	const Common::String &contextProp() const { return _ctxProp; }

	/**
	 * Dispatch the message `name(args)` against the library chain: find a
	 * matching definition (TI.EXE FUN_0040b7a0/FUN_0040b870), bind the formal
	 * params to @p args in a fresh local scope, and run the body with the
	 * statement executor (FUN_0040ba20). A kOpPass statement makes the
	 * definition "pretend unhandled" and the next library is tried.
	 *
	 * @param handled optionally receives whether any definition ran to
	 *        completion (false = no match anywhere on the chain).
	 * @return the kOpReturnValue result, or a default Value.
	 */
	Value callFunction(const Common::String &name, const Common::Array<Value> &args,
			bool *handled = nullptr);

	const Common::HashMap<Common::String, Value> &globalVars() const { return _vars; }
	Common::HashMap<Common::String, Value> &globalVars() { return _vars; }

private:
	void execute(const Script &script, uint32 index);
	void applyOperator(uint16 opcode);

	/// Result codes of the statement executor, mirroring TI.EXE FUN_0040ba20.
	enum RunResult {
		kRunDone,     ///< Body finished (kOpReturn / marker / end).
		kRunReturned, ///< kOpReturnValue produced a result.
		kRunPassed    ///< kOpPass: pretend no match, try next scope (code 4).
	};

	/**
	 * Run one definition body (or a whole flat program when @p stopAtMarker is
	 * false) starting at @p pc. The single statement loop behind runProgram and
	 * callFunction; mirrors TI.EXE FUN_0040ba20.
	 */
	RunResult runBody(const Script &script, uint32 pc, Value &result, uint32 maxSteps);
	static bool isTruthy(const Value &v) {
		return (v.type == Value::kBool || v.type == Value::kInt) && v.intValue != 0;
	}

	/**
	 * Read a variable by name from the current scope. Unbound names yield a
	 * symbol Value (harmless when used as data); bound names yield their stored
	 * value. Mirrors the TI.EXE scope lookup at 0x004138f0 (object entries of
	 * 0x20 bytes, name at +0x10), reduced to a flat name->value map until the
	 * object scope chain is modelled.
	 */
	Value getVar(const Common::String &name) const;
	Value getVar(const Common::String &name, const Common::String &lowerName) const;

	/** Store @p v under @p name in the current scope (TI.EXE store 0x00413610). */
	void setVar(const Common::String &name, const Value &v);


	/**
	 * Evaluate the expression starting at instruction @p pc, advancing @p pc
	 * past every instruction consumed. Mirrors the TI.EXE evaluator 0x00419cf0:
	 * an atom followed by zero or more (operator, atom) pairs, reduced by the
	 * recovered operator precedence.
	 */
	Value evaluateExpression(const Script &script, uint32 &pc);
	Value evaluateExpression(const Script &script, uint32 &pc, uint8 minBindingPower);

	/** Decode a single atom (literal/symbol/call) at @p pc, advancing @p pc. */
	Value decodeAtom(const Script &script, uint32 &pc);

	/**
	 * Handle a message-carrying builtin (sendtostage/sendtocast/sendtoshop/
	 * sendtoactor) whose kOpOpenParen is at @p pc: evaluate leading target
	 * args, capture the trailing message UNevaluated and route it (TI.EXE
	 * FUN_0040ad80 -> dispatcher FUN_0040b690). Advances @p pc past ')'.
	 */
	Value dispatchMessageBuiltin(const Script &script, uint32 &pc, uint16 opcode);

	/**
	 * Parse a call argument list whose opening kOpOpenParen is at @p pc,
	 * evaluating each comma-separated argument and advancing @p pc past the
	 * closing kOpCloseParen. Mirrors the argument scan at TI.EXE 0x0040b690.
	 */
	void parseCallArgs(const Script &script, uint32 &pc, Common::Array<Value> &outArgs);

	/**
	 * Dispatch a builtin method @p opcode with already-evaluated @p args. The
	 * named handlers are filled in per subsystem; unimplemented opcodes are
	 * logged (in trace mode) and return a placeholder. @p name is the source
	 * symbol when the call head was a symbol atom, else empty.
	 */
	Value callMethod(uint16 opcode, const Common::String &name, const Common::Array<Value> &args);
	bool callCoreMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callAudioMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callActorMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callPropMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callPuppetMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callStageSetMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callInputMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);
	bool callRuntimeMethod(uint16 opcode, const Common::Array<Value> &args, Value &result);

	/** Apply @p opcode to two operand values (no stack involved). */
	Value applyBinary(uint16 opcode, const Value &lhs, const Value &rhs);

	void push(const Value &v) { _stack.push_back(v); }
	Value pop();

	Common::Array<Value> _stack;
	Common::Array<uint32> _whileStack; ///< Saved condition-start PCs for active whiles.

	/**
	 * Active for-loop frames. Each binds a loop variable that is incremented
	 * and compared against an inclusive upper bound at kOpForNext, looping back
	 * to the body start or falling through (TI.EXE for-setup 0x0040bda9,
	 * frame stack 0x45ed48).
	 */
	struct ForFrame {
		Common::String var; ///< Loop-variable name.
		int32 end;          ///< Inclusive upper bound.
		uint32 bodyStart;   ///< Instruction index of the body's first statement.
	};
	Common::Array<ForFrame> _forStack;

	/// Global variable scope (the TI.EXE global object at [0x45f010]).
	Common::HashMap<Common::String, Value> _vars;

	/**
	 * Local-scope stack: one map per active function call, created by the
	 * dispatcher to bind formal parameters and kOpDeclLocal variables (TI.EXE
	 * builds a fresh scope object per dispatch in FUN_0040b870; lookup checks
	 * local first via 0x4138f0, then global). Empty at top level.
	 */
	Common::Array<Common::HashMap<Common::String, Value> > _locals;

	/// Function-library scope chain for callFunction (latest searched first).
	Common::Array<const Script *> _libraries;
	uint32 _callDepth; ///< Recursion guard for script-to-script calls.

	uint32 _pc;
	uint32 _executed; ///< Statements executed by the last runProgram (for the console).
	bool _trace;
	VMHost *_host; ///< Engine host for effectful builtins; null = no-op. Not owned.
	VMMovieHost *_movieHost;
	VMNavigationHost *_navigationHost;
	VMSystemHost *_systemHost;
	VMAudioHost *_audioHost;
	VMInputHost *_inputHost;
	VMActorHost *_actorHost;
	VMInteractionHost *_interactionHost;
	VMSaveHost *_saveHost;

	/// Dispatch context for the atoms 0xfba/0xfbb (see setDispatchContext).
	Common::String _ctxSelf;
	Common::String _ctxProp;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_VM_H

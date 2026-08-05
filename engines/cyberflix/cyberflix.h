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
#include "common/events.h"
#include "common/ptr.h"
#include "common/queue.h"
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
#include "cyberflix/vm.h"

namespace Cyberflix {

class Console;
class Script;
class Stage;
class Set;
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

class CyberflixEngine : public Engine {
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

	// --- Script-builtin bindings with engine-owned logic ------------------
	// (implemented in cyberflix.cpp / runtime/system.cpp / saveload.cpp).
	bool actionFrame(int n);
	int randomNumber(int n);
	int getFrameRate();
	int setFrameRate(int newRate);
	void sendToBoot(const Common::String &message, const Common::Array<Value> &args);
	Value sendToBootFx(const Common::String &message, const Common::Array<Value> &args);
	Value sendToSetFx(const Common::String &message, const Common::Array<Value> &args);
	void sendToSet(const Common::String &message, const Common::Array<Value> &args);
	void setClut(const Common::String &name);
	void blackScreen();
	void forceUpdate();
	bool hostQuitRequested() { return shouldQuit(); }
	void message(const Common::String &text);
	void delayTicks(int ticks);
	void noteDialog(const Common::String &text);
	void flushEvents();
	void drawString(const Common::String &text, int32 packedPoint, int color, int size);
	int stringWidth(const Common::String &text, int fontId, int size);
	void fadePalette(const Common::String &target, int steps, bool toBlack);
	/** mixclut(a, b, first, last, weight) (0x2f32 FUN_00446570): program the
	 *  palette with clut @p a blended toward clut @p b by weight/255 over
	 *  entries first..last; entries outside the range stay at @p a. A-14's
	 *  lights-out uses ("set", "black", 0, 127, 240), which darkens the room
	 *  colors but leaves the interface half of the palette lit. */
	void mixClut(const Common::String &nameA, const Common::String &nameB,
			int first, int last, int weight);
	void setVisualEffect(uint16 effect, int duration);
	void makeLoop(const Common::String &kind, const Common::String &target,
			const Common::String &message, int delay);
	void stopLoop(const Common::String &kind, const Common::String &target);
	void pauseLoop(const Common::String &kind, bool paused);
	void makeCricket(const Common::String &name);
	void stopCricket(const Common::String &name);
	void pauseCricket(const Common::String &kind, bool paused);
	bool keyAborts(const Common::String *resource, const Common::String *key,
			const bool *enabled);
	bool optionKey();
	bool shiftKey();
	Common::String getPathSlot(int slot);
	Common::String setPathSlot(int slot, const Common::String &newPath);
	Common::String getCurrentCD();
	Common::String setCurrentCD(const Common::String &requested);
	bool pointInButton(const Common::String &flat,
			const Common::String &button, int32 packedPoint);
	bool pointInPainting(const Common::String &scene, const Common::String &view,
			const Common::String &painting, int32 packedPoint);
	Common::String hitTest(int32 packedPoint);
	Common::String hitTestResult();
	int32 mousePoint();
	int32 makePoint(int x, int y);
	bool buttonDown();
	bool stillDown();
	int tick();
	int frameCounter();
	int calcDeg(int32 a, int32 b);
	int calcVectX(int deg, int dist);
	int calcVectY(int deg, int dist);
	int calcDist(int32 a, int32 b);
	int calcMod(int a, int b);
	void setCursorResource(const Common::String &resourceName);
	void saveGame(const Common::String &signature);
	void openGame(const Common::String &signature);
	bool questionDialog(const Common::String &message);
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
	/** currentflat() (0x4e46): current stage node name, or native "None". */
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

	// Puppet (talking-head) subsystem (vm_host_puppet.cpp).
	/** currentpuppet() (0x4e51), or "none" when no puppet file is open. */
	Common::String currentPuppet();
	void openPuppetFile(const Common::String &name);
	void closePuppetFile();
	void sendToPuppet(const Common::String &puppet, const Common::String &message,
			const Common::Array<Value> &args);
	Value sendToPuppetFx(const Common::String &puppet, const Common::String &message,
			const Common::Array<Value> &args);
	void puppetScript(const Common::String &name);
	void puppetClear();
	void puppetSpeak(const Common::String &name, int mode);
	void puppetBevel(const Common::String &name, int mode);
	void puppetGrab(bool enabled);
	int puppetEvent(int timeout);
	Common::String getPuppetBase();
	Common::String setPuppetBase(const Common::String &newBase);
	bool getPuppetVisible();
	bool setPuppetVisible(bool visible);
	int getPuppetParam(int selector);
	int setPuppetParam(int selector, int newValue);
	int countPuppets();
	Common::String indexToPuppet(int index);

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
	Common::String getActorStar(const Common::String &name);
	Common::String setActorStar(const Common::String &name, const Common::String &newStar);
	Common::String getActorPose(const Common::String &name);
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
	bool pollScriptEvent(Common::Event &event);
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
	int _cargoPaintingTimerStartFrame = 0;
	int _lastCargoPaintingTimerLogBucket = -1;
	bool _cargoPaintingTimerExpiredLogged = false;

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
	int _pendingLoadSlot = -1;
	Common::String _pendingLoadSignature;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_CYBERFLIX_H

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

#include "common/debug.h"

#include "cyberflix/vm.h"

namespace Cyberflix {

bool ScriptVM::callAudioMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenTrackFile: // opentrackfile('name.trk') -> FUN_00411be0/FUN_00411cc0
		_audioHost->openTrackFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseTrackFile: // closetrackfile('name.trk') -> FUN_00412070
		_audioHost->closeTrackFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPlayTheme: // playtheme('name.trk') -> FUN_00412250: start the track's
	             // theme playlist on the theme channel (replaces current)
		_audioHost->playTheme(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodSingleSound: // singlesound(name) -> FUN_004122d0/FUN_0042fa80
	case Script::kMethodMultipleSound: // multiplesound(name) -> FUN_00412310/FUN_0042fb20
	case Script::kMethodDualSound: // dualsound(name) -> FUN_00412350/FUN_0042fbc0
	case Script::kMethodBothSound: // bothsound(name) -> FUN_00412390/FUN_0042fc30
		_audioHost->playSound(args.empty() ? Common::String() : args[0].strValue,
				opcode - Script::kMethodSingleSound);
		return true;
	case Script::kMethodVoiceSound: // voicesound(name) -> FUN_004123d0/FUN_0042fc70
		_audioHost->playVoice(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodHaltSound: // haltsound(1|2|3) -> FUN_00412430/FUN_0042f690
		_audioHost->haltSound(args.empty() ? 3 : args[0].intValue);
		return true;
	case Script::kMethodHaltTheme: // halttheme() -> FUN_00412410: stop the theme channel
		_audioHost->haltTheme();
		return true;
	case Script::kMethodHaltVoice: // haltvoice() -> FUN_004124d0/FUN_0042f690
		_audioHost->haltVoice();
		return true;
	case Script::kMethodThemeVol: // themevol('name.trk', vol 0-255) -> FUN_004125c0
		_audioHost->themeVolume(args.size() > 0 ? args[0].strValue : Common::String(),
				args.size() > 1 ? args[1].intValue : 255);
		return true;
	case Script::kMethodWaveVolume: { // wavevolume([level 0..9]) -> FUN_00436670/FUN_00439df0
		int level = args.empty() ? 0 : args[0].intValue;
		const int *newLevel = args.empty() ? nullptr : &level;
		result = Value::makeInt(_audioHost->waveVolume(newLevel));
		return true;
	}
	case Script::kMethodSoundVol: { // soundvol(name[, volume 0..255]) -> FUN_00412ad0/FUN_004125c0
		if (args.empty()) {
			result = Value::makeInt(0);
			return true;
		}
		int volume = args.size() > 1 ? args[1].intValue : 0;
		const int *newVolume = args.size() > 1 ? &volume : nullptr;
		result = Value::makeInt(_audioHost->soundVolume(args[0].strValue, newVolume));
		return true;
	}
	case Script::kMethodCurrentTheme: // currenttheme(1|2) -> FUN_00412f20: 1 = playing cue name,
	             // 2 = its track file name; 'none' if silent
		result = Value::makeString(_audioHost->currentTheme(args.empty() ? 1 : args[0].intValue));
		return true;
	case Script::kMethodCurrentSound: // currentsound(1|2|3) -> FUN_00412e60: active SFX cue or 'None'
		result = Value::makeString(_audioHost->currentSound(args.empty() ? 1 : args[0].intValue));
		return true;
	case Script::kMethodCurrentVoice: // currentvoice() -> FUN_00412ff0: active voice cue or 'None'
		result = Value::makeString(_audioHost->currentVoice());
		return true;
	case Script::kMethodVoiceDone: // voicedone() -> true once the voice channel is idle
		result = Value::makeBool(_audioHost->voiceDone());
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callActorMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenCastFile: // opencastfile('name.cst') -> FUN_0041f1c0
		_actorHost->openCastFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseCastFile: // closecastfile('name.cst') -> FUN_004211b0
		_actorHost->closeCastFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodWalkToStar: // walktostar(actor, star) -> FUN_00424410
		if (args.size() >= 2)
			_actorHost->walkToStar(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodStopWalk: // stopwalk(actor|'all') -> FUN_00423b50
		_actorHost->stopWalk(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodIsWalk: // iswalk(actor) -> FUN_00423cf0
		result = Value::makeBool(!args.empty() && _actorHost->isWalk(args[0].strValue));
		return true;
	case Script::kMethodWalkDest: // walkdest(actor) -> FUN_00423df0; "None" if no queued walk
		result = Value::makeString(!args.empty() ? _actorHost->walkDest(args[0].strValue) : "None");
		return true;
	case Script::kMethodCountActors: // countactors() -> FUN_00420a70
		result = Value::makeInt(_actorHost->countActors());
		return true;
	case Script::kMethodIndexToActor: // indextoactor(i) -> native 1-based actor lookup
		result = Value::makeString(_actorHost->indexToActor(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodActorVisible: { // actorvisible(name[, flag]) -> FUN_00420f10/FUN_00420d30
		bool visible = args.size() > 1 && args[1].intValue != 0;
		const bool *newVisible = args.size() > 1 ? &visible : nullptr;
		result = !args.empty() ?
				Value::makeBool(_actorHost->actorVisible(args[0].strValue, newVisible)) :
				Value::makeBool(false);
		return true;
	}
	case Script::kMethodActorDeg: { // actordeg(name[, deg]) -> FUN_0041fef0 / getter
		if (args.size() >= 2) {
			int deg = args[1].intValue;
			result = Value::makeInt(_actorHost->actorDeg(args[0].strValue, &deg));
		} else if (args.size() == 1) {
			result = Value::makeInt(_actorHost->actorDeg(args[0].strValue, nullptr));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	}
	case Script::kMethodActorXYZ: // actorxyz(name, x, y, z) or actorxyz(name, selector)
		if (args.size() >= 4) {
			_actorHost->actorXYZ(args[0].strValue, args[1].intValue,
					args[2].intValue, args[3].intValue);
		} else if (args.size() == 2) {
			result = Value::makeInt(_actorHost->actorXYZ(args[0].strValue, args[1].intValue));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	case Script::kMethodActorStar: // actorstar(name[, scene]) -> FUN_0041fbb0
		if (args.size() >= 2)
			result = Value::makeString(_actorHost->actorStar(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_actorHost->actorStar(args[0].strValue, nullptr));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorPose: // actorpose(name[, pose]) -> FUN_0041fd70
		if (args.size() >= 2)
			result = Value::makeString(_actorHost->actorPose(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_actorHost->actorPose(args[0].strValue, nullptr));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorSet: // actorset(name[, set]) -> FUN_0041f970
		if (args.size() >= 2)
			result = Value::makeString(_actorHost->actorSet(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_actorHost->actorSet(args[0].strValue, nullptr));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorSpeed: // actorspeed(name, speed)
		if (args.size() >= 2)
			_actorHost->actorSpeed(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorScale: // actorscale(name, scale)
		if (args.size() >= 2)
			_actorHost->actorScale(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorTurn: // actorturn(name, turn)
		if (args.size() >= 2)
			_actorHost->actorTurn(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodTurnToDeg: // turntodeg(actor, deg): native queues a turn; apply current target angle.
		if (args.size() >= 2)
			_actorHost->turnToDeg(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorOwner: // actorowner(name[, owner]) -> FUN_00422210
		if (args.size() >= 2)
			result = Value::makeString(_actorHost->actorOwner(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_actorHost->actorOwner(args[0].strValue, nullptr));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorValue: { // actorvalue(name[, value]) -> FUN_004222d0
		if (args.size() >= 2) {
			int value = args[1].intValue;
			result = Value::makeInt(_actorHost->actorValue(args[0].strValue, &value));
		} else if (args.size() == 1) {
			result = Value::makeInt(_actorHost->actorValue(args[0].strValue, nullptr));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	}
	case Script::kMethodActorZClip: // actorzclip(name, zclip)
		if (args.size() >= 2)
			_actorHost->actorZClip(args[0].strValue, args[1].intValue);
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callPropMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenShopFile: // openshopfile('name.shp') -> FUN_00428450: parse the .SHP,
	             // then dispatch openshop() and per-prop openprop().
		_interactionHost->openShopFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseShopFile: // closeshopfile('name.shp') -> FUN_0042a7e0
		_interactionHost->closeShopFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPropInstance: // propinstance(source, newName) -> FUN_00428940
		if (args.size() >= 2)
			_interactionHost->propInstance(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodPropVisible: // propvisible(name, flag) -> FUN_00429d00
		if (args.size() >= 2)
			_interactionHost->propVisible(args[0].strValue, args[1].intValue != 0);
		if (args.size() == 1)
			result = Value::makeBool(_interactionHost->propVisible(args[0].strValue));
		return true;
	case Script::kMethodPropView: // propview(name, shape) -> FUN_004293a0
		if (args.size() >= 2)
			_interactionHost->propView(args[0].strValue, args[1].strValue);
		if (args.size() == 1)
			result = Value::makeString(_interactionHost->propView(args[0].strValue));
		return true;
	case Script::kMethodPropSet: // propset(name, set) -> FUN_00428c20
		if (args.size() >= 2)
			_interactionHost->propSet(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodPropXYZ: // propxyz(name, x, y, z) -> FUN_0042a140 (mode=1)
		if (args.size() >= 4)
			_interactionHost->propXYZ(args[0].strValue, args[1].intValue,
					args[2].intValue, args[3].intValue);
		return true;
	case Script::kMethodPropXY: // propxy(name, x, y) -> FUN_0042a370 (mode=0; depth=-1 if >=0)
		if (args.size() >= 3)
			_interactionHost->setPropXY(args[0].strValue, args[1].intValue, args[2].intValue);
		if (args.size() == 2)
			result = Value::makeInt(_interactionHost->propXY(args[0].strValue, args[1].intValue));
		return true;
	case Script::kMethodPropScale: // propscale(name, scale) -> FUN_00429870
		if (args.size() >= 2)
			_interactionHost->propScale(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodPropZClip: // propzclip(name, dist) -> FUN_00428ea0
		if (args.size() >= 2)
			_interactionHost->propZClip(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodPropDist: // propdist(name, d) -> FUN_004295c0
		if (args.size() >= 2)
			_interactionHost->propDist(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodPropDeg: // propdeg(name[, deg]) -> FUN_00429730/FUN_00429520
		if (args.size() >= 2) {
			int deg = args[1].intValue;
			result = Value::makeInt(_interactionHost->propDeg(args[0].strValue, &deg));
		} else if (args.size() == 1) {
			result = Value::makeInt(_interactionHost->propDeg(args[0].strValue, nullptr));
		}
		return true;
	case Script::kMethodPropOwner: // propowner(name[, owner]) -> FUN_00428d40: get or set
		if (args.size() >= 2)
			result = Value::makeString(_interactionHost->propOwner(args[0].strValue, &args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_interactionHost->propOwner(args[0].strValue, nullptr));
		return true;
	case Script::kMethodPropValue: // propvalue(name[, value]) -> FUN_004290d0/FUN_00428e00
		if (args.size() >= 2) {
			int value = args[1].intValue;
			result = Value::makeInt(_interactionHost->propValue(args[0].strValue, &value));
		} else if (args.size() == 1) {
			result = Value::makeInt(_interactionHost->propValue(args[0].strValue, nullptr));
		}
		return true;
	case Script::kMethodCountProps: // countprops() -> FUN_0042b4f0: global count, all shops
		result = Value::makeInt(_interactionHost->countProps());
		return true;
	case Script::kMethodIndexToProp: // indextoprop(i) -> FUN_0042b550: 1-based global index
		result = Value::makeString(_interactionHost->indexToProp(args.empty() ? 0 : args[0].intValue));
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callPuppetMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenPuppetFile: // openpuppetfile('name.pup') -> FUN_004473c0/FUN_00447470
		_navigationHost->openPuppetFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPuppetClear: // puppetclear(): native display-list clear, rendering pending
		_navigationHost->puppetClear();
		return true;
	case Script::kMethodClosePuppetFile: // closepuppetfile() -> FUN_00447880
		_navigationHost->closePuppetFile();
		return true;
	case Script::kMethodPuppetSpeak: // puppetspeak(name[, mode]) -> FUN_00447ce0/FUN_00448b60
		_navigationHost->puppetSpeak(args.empty() ? Common::String() : args[0].strValue,
				args.size() > 1 ? args[1].intValue : 0);
		return true;
	case Script::kMethodPuppetBevel: // puppetbevel(name[, mode]) -> FUN_00447b30
		_navigationHost->puppetBevel(args.empty() ? Common::String() : args[0].strValue,
				args.size() > 1 ? args[1].intValue : 0);
		return true;
	case Script::kMethodPuppetGrab: // puppetgrab(bool) -> FUN_00447e30 stores DAT_00461248.
		_navigationHost->puppetGrab(!args.empty() && isTruthy(args[0]));
		return true;
	case Script::kMethodPuppetScript: // puppetscript(name) -> FUN_004482c0
		_navigationHost->puppetScript(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCurrentPuppet: // currentpuppet() -> current puppet name or 'none'
		result = Value::makeString(_navigationHost->currentPuppet());
		return true;
	case Script::kMethodPuppetParam: { // puppetparam(selector[, value]) -> FUN_00448730/FUN_004485f0
		if (args.empty()) {
			result = Value::makeInt(0);
			return true;
		}
		if (args.size() >= 2) {
			int value = args[1].intValue;
			result = Value::makeInt(_navigationHost->puppetParam(args[0].intValue, &value));
		} else {
			result = Value::makeInt(_navigationHost->puppetParam(args[0].intValue, nullptr));
		}
		return true;
	}
	case Script::kMethodPuppetVisible: { // puppetvisible([flag]) -> FUN_00448550/FUN_004485b0
		bool visible = !args.empty() && args[0].intValue != 0;
		const bool *newVisible = args.empty() ? nullptr : &visible;
		result = Value::makeBool(_navigationHost->puppetVisible(newVisible));
		return true;
	}
	case Script::kMethodPuppetBase: { // puppetbase([name]) -> FUN_00447ee0
		const Common::String *base = args.empty() ? nullptr : &args[0].strValue;
		result = Value::makeString(_navigationHost->puppetBase(base));
		return true;
	}
	case Script::kMethodCountPuppets: // countpuppets() -> FUN_00448380: PUP resource-2 script count
		result = Value::makeInt(_navigationHost->countPuppets());
		return true;
	case Script::kMethodIndexToPuppet: // indextopuppet(i) -> FUN_004483f0, 1-based
		result = Value::makeString(_navigationHost->indexToPuppet(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodPuppetEvent: // puppetevent(timeout) -> FUN_00449e40 waits for a clicked bevel id
		result = Value::makeInt(_navigationHost->puppetEvent(args.empty() ? -1 : args[0].intValue));
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callStageSetMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenStageFile: // openstagefile('name.stg')
		_navigationHost->openStageFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseStageFile: // closestagefile() -> FUN_00409330
		_navigationHost->closeStageFile();
		return true;
	case Script::kMethodGotoFlat: // gotoflat(name|index) -> FUN_00409460
		if (!args.empty())
			_navigationHost->gotoFlat(args[0]);
		return true;
	case Script::kMethodOpenSetFile: // opensetfile('name.set'[, scene[, view]]) -> FUN_00430690
		_navigationHost->openSetFile(args.size() > 0 ? args[0].strValue : Common::String(),
				args.size() > 1 ? args[1].strValue : Common::String(),
				args.size() > 2 ? args[2].strValue : Common::String());
		return true;
	case Script::kMethodCloseSetFile: // closesetfile() -> TI.EXE set-archive close
		_navigationHost->closeSetFile();
		return true;
	// (sendtoscene 0x2f02 never reaches here: it always appears with a
	// parenthesised message and routes through dispatchMessageBuiltin.)
	case Script::kMethodCurrentSet: // currentset() -> open set name or 'none'
		result = Value::makeString(_navigationHost->currentSet());
		return true;
	case Script::kMethodCurrentStage: // currentstage() -> open stage name or native 'None'
		result = Value::makeString(_navigationHost->currentStage());
		return true;
	case Script::kMethodStageVisible: { // stagevisible([flag]) -> DAT_00461156
		bool visible = args.empty() ? false : (args[0].intValue != 0);
		const bool *newVisible = args.empty() ? nullptr : &visible;
		result = Value::makeBool(_navigationHost->stageVisible(newVisible));
		return true;
	}
	case Script::kMethodCurrentFlat: // currentflat() -> current stage node name or native 'None'
		result = Value::makeString(_navigationHost->currentFlat());
		return true;
	case Script::kMethodCurrentView: { // currentview([name]) -> current SET view name
		const Common::String *target = args.empty() ? nullptr : &args[0].strValue;
		result = Value::makeString(_navigationHost->currentView(target));
		return true;
	}
	case Script::kMethodCurrentDeg: // currentdeg() -> DAT_004611ac
		result = Value::makeInt(_navigationHost->currentDeg());
		return true;
	case Script::kMethodCurrentScene: { // currentscene([name|left|right|strait])
		const Common::String *target = args.empty() ? nullptr : &args[0].strValue;
		result = Value::makeString(_navigationHost->currentScene(target));
		return true;
	}
	case Script::kMethodSetVisible: { // setvisible([flag]) -> current SET visibility flag
		bool visible = args.empty() ? false : (args[0].intValue != 0);
		const bool *newVisible = args.empty() ? nullptr : &visible;
		result = Value::makeBool(_navigationHost->setVisible(newVisible));
		return true;
	}
	case Script::kMethodCountPaintings: // countpaintings(scene, view) -> FUN_00431fe0
		result = args.size() >= 2 ?
				Value::makeInt(_navigationHost->countPaintings(args[0].strValue, args[1].strValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodIndexToPainting: // indextopainting(scene, view, index) -> FUN_00432120, 1-based
		if (args.size() >= 3)
			result = Value::makeString(_navigationHost->indexToPainting(args[0].strValue,
					args[1].strValue, args[2].intValue));
		return true;
	case Script::kMethodRoadAhead: // roadahead(scene, view) -> FUN_00431bd0/FUN_004337b0
		result = args.size() >= 2 ?
				Value::makeBool(_navigationHost->roadAhead(args[0].strValue, args[1].strValue)) :
				Value::makeBool(false);
		return true;
	case Script::kMethodCameraXYZ: // cameraxyz(selector) -> FUN_00437870
		result = Value::makeInt(_navigationHost->cameraXYZ(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodPlayerXYZ: // playerxyz(selector) -> FUN_00437950
		result = Value::makeInt(_navigationHost->playerXYZ(args.empty() ? 0 : args[0].intValue));
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callInputMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodKeyAborts: { // keyaborts([resource, key, flag]) -> FUN_00435a00/FUN_00446e10
		bool enabled = args.size() > 2 && args[2].intValue != 0;
		result = Value::makeBool(_inputHost->keyAborts(
				args.size() > 0 ? &args[0].strValue : nullptr,
				args.size() > 1 ? &args[1].strValue : nullptr,
				args.size() > 2 ? &enabled : nullptr));
		return true;
	}
	case Script::kMethodPointX: // pointx(point) -> high word of packed (x << 16) | y
		result = Value::makeInt(args.empty() ? 0 : static_cast<int16>(args[0].intValue >> 16));
		return true;
	case Script::kMethodPointY: // pointy(point) -> low word of packed (x << 16) | y
		result = Value::makeInt(args.empty() ? 0 : static_cast<int16>(args[0].intValue & 0xffff));
		return true;
	case Script::kMethodMakePoint: // makepoint(x, y) -> packed (x << 16) | y
		result = Value::makeInt(_interactionHost->makePoint(args.size() > 0 ? args[0].intValue : 0,
				args.size() > 1 ? args[1].intValue : 0));
		return true;
	case Script::kMethodButton: // button() -> FUN_00436880: live left mouse button state
		result = Value::makeBool(_interactionHost->buttonDown());
		return true;
	case Script::kMethodStillDown: // stilldown() -> FUN_00436920: live mouse button state
		result = Value::makeBool(_interactionHost->stillDown());
		return true;
	case Script::kMethodTick: // tick() -> FUN_004368f0: native 60 Hz timer
		result = Value::makeInt(_interactionHost->tick());
		return true;
	case Script::kMethodQuestionDialog: // questiondialog(text) -> FUN_004363f0/FUN_00409030
		result = Value::makeBool(_saveHost->questionDialog(
				args.empty() ? Common::String() : args[0].strValue));
		return true;
	case Script::kMethodOptionKey: // optionkey() -> FUN_004376e0/GetAsyncKeyState(VK_SHIFT)
		result = Value::makeBool(_inputHost->optionKey());
		return true;
	case Script::kMethodPointInButton: // pointinbutton(flat, button, point) -> FUN_0040a0d0
		result = args.size() >= 3 ?
				Value::makeBool(_interactionHost->pointInButton(args[0].strValue,
						args[1].strValue, args[2].intValue)) :
				Value::makeBool(false);
		return true;
	case Script::kMethodPointInPainting: // pointinpainting(scene, view, painting, point) -> FUN_004322e0
		result = args.size() >= 4 ?
				Value::makeBool(_interactionHost->pointInPainting(args[0].strValue,
						args[1].strValue, args[2].strValue, args[3].intValue)) :
				Value::makeBool(false);
		return true;
	case Script::kMethodHitTest: // hittest(point) -> FUN_00435e70: name of the topmost item
	             // under the packed point; kind stored for result()
		if (!args.empty())
			result = Value::makeString(_interactionHost->hitTest(args[0].intValue));
		return true;
	case Script::kMethodCalcDeg: // calcdeg(pointA, pointB) -> FUN_00435c70
		result = args.size() >= 2 ?
				Value::makeInt(_interactionHost->calcDeg(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodCalcVectX: // calcvectx(deg, dist) -> FUN_00437770/FUN_004432e0
		result = args.size() >= 2 ?
				Value::makeInt(_interactionHost->calcVectX(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodCalcVectY: // calcvecty(deg, dist) -> FUN_004377f0/FUN_00443310
		result = args.size() >= 2 ?
				Value::makeInt(_interactionHost->calcVectY(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodStarXYZ: // starxyz(name, selector) -> FUN_00435a60/FUN_00432fc0
		result = args.size() >= 2 ?
				Value::makeInt(_actorHost->starXYZ(args[0].strValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodCalcDist: // calcdist(pointA, pointB) -> FUN_00435d20
		result = args.size() >= 2 ?
				Value::makeInt(_interactionHost->calcDist(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodCalcMod: // calcmod(a, b) -> FUN_004358f0
		result = args.size() >= 2 ?
				Value::makeInt(_interactionHost->calcMod(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodResult: // result() -> FUN_004366a0: last hittest kind (DAT_00461298)
		result = Value::makeString(_interactionHost->hitTestResult());
		return true;
	case Script::kMethodMouse: // mouse() -> FUN_004368b0: current mouse point, packed
		result = Value::makeInt(_interactionHost->mousePoint());
		return true;
	case Script::kMethodCursor: // cursor(id|name) -> FUN_00446920. Resource-name mapping
	             // verified against TI.EXE (see VMHost::setCursorResource):
	             // int -> CURS<n>; "arrow" -> CURS.ARROW; "watch" ->
	             // CURS2002; other names -> CURS.<NAME>.
		if (!args.empty()) {
			Common::String res;
			if (args[0].type == Value::kInt)
				res = Common::String::format("CURS%d", args[0].intValue);
			else if (args[0].strValue.equalsIgnoreCase("arrow"))
				res = "CURS.ARROW";
			else if (args[0].strValue.equalsIgnoreCase("watch"))
				res = "CURS2002";
			else
				res = Common::String("CURS.") + args[0].strValue;
			res.toUppercase();
			_interactionHost->setCursorResource(res);
		}
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callRuntimeMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodMessage: // message(text) -> FUN_00446240 (no-op debug stub)
		_systemHost->message(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodNoteDialog: // notedialog(text) -> FUN_004461e0 -> FUN_00408fc0 (modal OK box)
		_systemHost->noteDialog(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPlayMovie: // playmovie('name.mov')
		_movieHost->playMovie(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodClut: // clut(name): snap the hardware palette (FUN_00446500)
		_systemHost->setClut(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodBlackScreen: // blackscreen(): fill the window with black pixels (FUN_00446b80)
		_systemHost->blackScreen();
		return true;
	case Script::kMethodForceUpdate: // forceupdate() -> FUN_00446910 -> FUN_00423a60
		_systemHost->forceUpdate();
		return true;
	case Script::kMethodFlushEvents: // flushevents() -> FUN_00446cb0/FUN_00405110
		_systemHost->flushEvents();
		return true;
	case Script::kMethodDrawString: // drawstring(text, point, color, size) -> FUN_004460c0
		if (args.size() >= 2)
			_systemHost->drawString(args[0].strValue, args[1].intValue,
					args.size() > 2 ? args[2].intValue : 0,
					args.size() > 3 ? args[3].intValue : 12);
		return true;
	case Script::kMethodQuit: // quit() -> FUN_00446c80: restore cursor, set native quit flag
		_saveHost->requestQuit();
		return true;
	case Script::kMethodBlackToScreen: // blacktoscreen(target, steps): palette fade black -> target
		_systemHost->fadePalette(args.size() > 0 ? args[0].strValue : Common::String("current"),
				args.size() > 1 ? args[1].intValue : 1, false);
		return true;
	case Script::kMethodScreenToBlack: // screentoblack(target, steps): palette fade target -> black
		_systemHost->fadePalette(args.size() > 0 ? args[0].strValue : Common::String("current"),
				args.size() > 1 ? args[1].intValue : 1, true);
		return true;
	case Script::kMethodVisualEffect: // visualeffect(effect, dur): set default transition (FUN_00446400)
		// The effect names ('plain', ...) are bare method opcodes 0x5dc1..
		// 0x5dd5 used as atoms; decodeAtom yields Value() for them, so the
		// effect code is not currently propagated. Only 'plain' is used by
		// the boot scripts; forward the duration.
		_systemHost->setVisualEffect(0x5dce, args.size() > 1 ? args[1].intValue : 0);
		return true;
	case Script::kMethodMakeLoop: // makeloop(kind, target, message, delay) -> FUN_00423e60
		if (args.size() >= 4)
			_systemHost->makeLoop(args[0].strValue, args[1].strValue,
					args[2].strValue, args[3].intValue);
		return true;
	case Script::kMethodStopLoop: // stoploop(kind[, target]) -> FUN_00446d30/FUN_00423bf0
		if (!args.empty())
			_systemHost->stopLoop(args[0].strValue,
					args.size() > 1 ? args[1].strValue : Common::String());
		return true;
	case Script::kMethodPauseLoop: // pauseloop(kind, flag)
		if (args.size() >= 2)
			_systemHost->pauseLoop(args[0].strValue, args[1].intValue != 0);
		return true;
	case Script::kMethodMakeCricket: // makecricket(name, x, y, dist, angle, delay) -> FUN_00425640
		if (!args.empty())
			_systemHost->makeCricket(args[0].strValue);
		return true;
	case Script::kMethodStopCricket: // stopcricket(name) -> FUN_00446db0/FUN_00423c80
		if (!args.empty())
			_systemHost->stopCricket(args[0].strValue);
		return true;
	case Script::kMethodPauseCricket: // pausecricket(kind, flag)
		if (args.size() >= 2)
			_systemHost->pauseCricket(args[0].strValue, args[1].intValue != 0);
		return true;
	case Script::kMethodActionFrame: // actionframe(n) -> bool, FUN_004362c0 (n must be 1 or 2)
		result = Value::makeBool(_systemHost->actionFrame(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodFrameRate: { // framerate([n]) -> DAT_00461126
		int rate = args.empty() ? 0 : args[0].intValue;
		const int *newRate = args.empty() ? nullptr : &rate;
		result = Value::makeInt(_systemHost->frameRate(newRate));
		return true;
	}
	case Script::kMethodFrame: // frame() -> FUN_00435a30: DAT_00461122 compositor counter
		result = Value::makeInt(_systemHost->frameCounter());
		return true;
	case Script::kMethodPath: { // path(slot[, value]) -> FUN_004462a0/FUN_00438450
		int slot = args.empty() ? 0 : args[0].intValue;
		const Common::String *newPath = args.size() >= 2 ? &args[1].strValue : nullptr;
		result = Value::makeString(_inputHost->pathSlot(slot, newPath));
		return true;
	}
	case Script::kMethodCurrentCD: { // currentcd([name]) -> FUN_00439df0/FUN_0043a290
		const Common::String *requested = args.empty() ? nullptr : &args[0].strValue;
		result = Value::makeString(_inputHost->currentCD(requested));
		return true;
	}
	case Script::kMethodSaveGame: // savegame(signature) -> FUN_00426620/FUN_00426790
		_saveHost->saveGame(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodOpenGame: // opengame(signature) -> FUN_004266e0/FUN_00426f00
		_saveHost->openGame(args.empty() ? Common::String() : args[0].strValue);
		return true;
	default:
		return false;
	}
}

Value ScriptVM::callMethod(uint16 opcode, const Common::String &name, const Common::Array<Value> &args) {
	// Dispatch a builtin method by opcode. The interpreter routes 0x2Exx/0x2Fxx
	// through dispatch B and 0x3Exx/0x4Exx through dispatch A; ~340 handlers are
	// stubbed as logged no-ops until their subsystems (renderer/video/audio/
	// navigation) are implemented. Arguments are fully evaluated so trace output
	// reflects real call sites.
	if (_trace) {
		Common::String a;
		for (uint32 i = 0; i < args.size(); ++i) {
			if (i)
				a += ", ";
			a += args[i].toString();
		}
		const char *builtin = Script::methodName(opcode);
		const char *label = !name.empty() ? name.c_str()
				: (builtin ? builtin : "method");
		debug(0, "    call %s#%#06x(%s)", label, opcode, a.c_str());
	}

	Value result;
	if (callCoreMethod(opcode, args, result))
		return result;

	// Forward the effectful builtins we implement to the engine host. Opcodes
	// not handled here remain logged no-ops.
	// (The message-carrying send* builtins never reach this path; they are
	// routed through dispatchMessageBuiltin with the message unevaluated.)

	if (_host) {
		if (callAudioMethod(opcode, args, result))
			return result;
		if (callActorMethod(opcode, args, result))
			return result;
		if (callPropMethod(opcode, args, result))
			return result;
		if (callPuppetMethod(opcode, args, result))
			return result;
		if (callStageSetMethod(opcode, args, result))
			return result;
		if (callInputMethod(opcode, args, result))
			return result;
		if (callRuntimeMethod(opcode, args, result))
			return result;
	}
	return Value();
}

} // End of namespace Cyberflix

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

#include "cyberflix/cyberflix.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

bool ScriptVM::callAudioMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenTrackFile: // opentrackfile('name.trk') -> FUN_00411be0/FUN_00411cc0
		_host->openTrackFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseTrackFile: // closetrackfile('name.trk') -> FUN_00412070
		_host->closeTrackFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPlayTheme: // playtheme('name.trk') -> FUN_00412250: start the track's
	             // theme playlist on the theme channel (replaces current)
		_host->playTheme(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodSingleSound: // singlesound(name) -> FUN_004122d0/FUN_0042fa80
	case Script::kMethodMultipleSound: // multiplesound(name) -> FUN_00412310/FUN_0042fb20
	case Script::kMethodDualSound: // dualsound(name) -> FUN_00412350/FUN_0042fbc0
	case Script::kMethodBothSound: // bothsound(name) -> FUN_00412390/FUN_0042fc30
		_host->playSound(args.empty() ? Common::String() : args[0].strValue,
				opcode - Script::kMethodSingleSound);
		return true;
	case Script::kMethodVoiceSound: // voicesound(name) -> FUN_004123d0/FUN_0042fc70
		_host->playVoice(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodHaltSound: // haltsound(1|2|3) -> FUN_00412430/FUN_0042f690
		_host->haltSound(args.empty() ? 3 : args[0].intValue);
		return true;
	case Script::kMethodHaltTheme: // halttheme() -> FUN_00412410: stop the theme channel
		_host->haltTheme();
		return true;
	case Script::kMethodHaltVoice: // haltvoice() -> FUN_004124d0/FUN_0042f690
		_host->haltVoice();
		return true;
	case Script::kMethodThemeVol: // themevol('name.trk', vol 0-255) -> FUN_004125c0
		_host->themeVolume(args.size() > 0 ? args[0].strValue : Common::String(),
				args.size() > 1 ? args[1].intValue : 255);
		return true;
	case Script::kMethodWaveVolume: { // wavevolume([level 0..9]) -> FUN_00436670/FUN_00439df0
		result = args.empty() ?
				Value::makeInt(_host->getWaveVolume()) :
				Value::makeInt(_host->setWaveVolume(args[0].intValue));
		return true;
	}
	case Script::kMethodSoundVol: { // soundvol(name[, volume 0..255]) -> FUN_00412ad0/FUN_004125c0
		if (args.empty()) {
			result = Value::makeInt(0);
			return true;
		}
		result = args.size() > 1 ?
				Value::makeInt(_host->setSoundVolume(args[0].strValue, args[1].intValue)) :
				Value::makeInt(_host->getSoundVolume(args[0].strValue));
		return true;
	}
	case Script::kMethodCurrentTheme: // currenttheme(1|2) -> FUN_00412f20: 1 = playing cue name,
	             // 2 = its track file name; 'none' if silent
		result = Value::makeString(_host->currentTheme(args.empty() ? 1 : args[0].intValue));
		return true;
	case Script::kMethodCurrentSound: // currentsound(1|2|3) -> FUN_00412e60: active SFX cue or 'None'
		result = Value::makeString(_host->currentSound(args.empty() ? 1 : args[0].intValue));
		return true;
	case Script::kMethodCurrentVoice: // currentvoice() -> FUN_00412ff0: active voice cue or 'None'
		result = Value::makeString(_host->currentVoice());
		return true;
	case Script::kMethodVoiceDone: // voicedone() -> true once the voice channel is idle
		result = Value::makeBool(_host->voiceDone());
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callActorMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenCastFile: // opencastfile('name.cst') -> FUN_0041f1c0
		_host->openCastFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseCastFile: // closecastfile('name.cst') -> FUN_004211b0
		_host->closeCastFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodActorInstance: // actorinstance(source, newName) -> FUN_0041f6a0
		if (args.size() >= 2)
			_host->actorInstance(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodWalkToStar: // walktostar(actor, star) -> FUN_00424410
		if (args.size() >= 2)
			_host->walkToStar(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodWalkOnPath: // walkonpath(actor, pathStart, destStar) -> FUN_00424640
		if (args.size() >= 3)
			_host->walkOnPath(args[0].strValue, args[1].strValue, args[2].strValue);
		return true;
	case Script::kMethodWalkToXYZ: // walktoxyz(actor, x, y, z) -> FUN_00424540
		if (args.size() >= 4)
			_host->walkToXYZ(args[0].strValue, args[1].intValue,
					args[2].intValue, args[3].intValue);
		return true;
	case Script::kMethodStopWalk: // stopwalk(actor|'all') -> FUN_00423b50
		_host->stopWalk(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPauseWalk: // pausewalk(actor, flag) -> FUN_00446ea0/FUN_00425590
		if (args.size() >= 2)
			_host->pauseWalk(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorExists: // actorexists(name) -> FUN_0041fb20/FUN_004225b0
		result = Value::makeBool(!args.empty() && _host->actorExists(args[0].strValue));
		return true;
	case Script::kMethodIsWalk: // iswalk(actor) -> FUN_00423cf0
		result = Value::makeBool(!args.empty() && _host->isWalk(args[0].strValue));
		return true;
	case Script::kMethodWalkDest: // walkdest(actor) -> FUN_00423df0; "None" if no queued walk
		result = Value::makeString(!args.empty() ? _host->walkDest(args[0].strValue) : "None");
		return true;
	case Script::kMethodCountActors: // countactors() -> FUN_00420a70
		result = Value::makeInt(_host->countActors());
		return true;
	case Script::kMethodIndexToActor: // indextoactor(i) -> native 1-based actor lookup
		result = Value::makeString(_host->indexToActor(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodActorVisible: { // actorvisible(name[, flag]) -> FUN_00420f10/FUN_00420d30
		result = !args.empty() ?
				(args.size() > 1 ?
						Value::makeBool(_host->setActorVisible(args[0].strValue, args[1].intValue != 0)) :
						Value::makeBool(_host->getActorVisible(args[0].strValue))) :
				Value::makeBool(false);
		return true;
	}
	case Script::kMethodActorDeg: { // actordeg(name[, deg]) -> FUN_0041fef0 / getter
		if (args.size() >= 2) {
			result = Value::makeInt(_host->setActorDeg(args[0].strValue, args[1].intValue));
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->getActorDeg(args[0].strValue));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	}
	case Script::kMethodActorDist: { // actordist(name[, d]) -> FUN_00420040/FUN_0041ff90
		if (args.size() >= 2) {
			_host->setActorDist(args[0].strValue, args[1].intValue);
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->getActorDist(args[0].strValue));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	}
	case Script::kMethodActorXYZ: // actorxyz(name, x, y, z) or actorxyz(name, selector)
		if (args.size() >= 4) {
			_host->actorXYZ(args[0].strValue, args[1].intValue,
					args[2].intValue, args[3].intValue);
		} else if (args.size() == 2) {
			result = Value::makeInt(_host->actorXYZ(args[0].strValue, args[1].intValue));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	case Script::kMethodActorStar: // actorstar(name[, scene]) -> FUN_0041fbb0
		if (args.size() >= 2)
			result = Value::makeString(_host->setActorStar(args[0].strValue, args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->getActorStar(args[0].strValue));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorPose: // actorpose(name[, pose]) -> FUN_0041fd70
		if (args.size() >= 2)
			result = Value::makeString(_host->setActorPose(args[0].strValue, args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->getActorPose(args[0].strValue));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorSet: // actorset(name[, set]) -> FUN_0041f970
		if (args.size() >= 2)
			result = Value::makeString(_host->setActorSet(args[0].strValue, args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->getActorSet(args[0].strValue));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorSpeed: // actorspeed(name, speed)
		if (args.size() >= 2)
			_host->actorSpeed(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorScale: // actorscale(name, scale)
		if (args.size() >= 2)
			_host->actorScale(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorTurn: // actorturn(name, turn)
		if (args.size() >= 2)
			_host->actorTurn(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodTurnToDeg: // turntodeg(actor, deg): native queues a turn; apply current target angle.
		if (args.size() >= 2)
			_host->turnToDeg(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodActorOwner: // actorowner(name[, owner]) -> FUN_00422210
		if (args.size() >= 2)
			result = Value::makeString(_host->setActorOwner(args[0].strValue, args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->getActorOwner(args[0].strValue));
		else
			result = Value::makeString(Common::String());
		return true;
	case Script::kMethodActorValue: { // actorvalue(name[, value]) -> FUN_004222d0
		if (args.size() >= 2) {
			result = Value::makeInt(_host->setActorValue(args[0].strValue, args[1].intValue));
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->getActorValue(args[0].strValue));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	}
	case Script::kMethodActorZClip: // actorzclip(name, zclip)
		if (args.size() >= 2)
			_host->actorZClip(args[0].strValue, args[1].intValue);
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callPropMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenShopFile: // openshopfile('name.shp') -> FUN_00428450: parse the .SHP,
	             // then dispatch openshop() and per-prop openprop().
		_host->openShopFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseShopFile: // closeshopfile('name.shp') -> FUN_0042a7e0
		_host->closeShopFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPropInstance: // propinstance(source, newName) -> FUN_00428940
		if (args.size() >= 2)
			_host->propInstance(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodPropExists: // propexists(name) -> FUN_00428fc0/FUN_0042b700
		result = Value::makeBool(!args.empty() && _host->propExists(args[0].strValue));
		return true;
	case Script::kMethodPropVisible: // propvisible(name, flag) -> FUN_00429d00
		if (args.size() >= 2)
			_host->propVisible(args[0].strValue, args[1].intValue != 0);
		if (args.size() == 1)
			result = Value::makeBool(_host->propVisible(args[0].strValue));
		return true;
	case Script::kMethodPropView: // propview(name, shape) -> FUN_004293a0
		if (args.size() >= 2)
			_host->propView(args[0].strValue, args[1].strValue);
		if (args.size() == 1)
			result = Value::makeString(_host->propView(args[0].strValue));
		return true;
	case Script::kMethodPropSet: // propset(name, set) -> FUN_00428c20
		if (args.size() >= 2)
			_host->propSet(args[0].strValue, args[1].strValue);
		return true;
	case Script::kMethodPropXYZ: // propxyz(name, x, y, z) -> FUN_0042a140 (mode=1);
	             // propxyz(name, selector) -> FUN_0042a250: world-space getter
	             // (1=x, 2=y, 3=z, 4=packed x/y point), like actorxyz.
		if (args.size() >= 4)
			_host->propXYZ(args[0].strValue, args[1].intValue,
					args[2].intValue, args[3].intValue);
		else if (args.size() == 2)
			result = Value::makeInt(_host->propXYZ(args[0].strValue, args[1].intValue));
		return true;
	case Script::kMethodPropStar: // propstar(name[, star]) -> FUN_00429320/FUN_004291f0
		if (args.size() >= 2)
			result = Value::makeString(_host->setPropStar(args[0].strValue, args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->getPropStar(args[0].strValue));
		return true;
	case Script::kMethodPropXY: // propxy(name, x, y) -> FUN_0042a370 (mode=0; depth=-1 if >=0)
		if (args.size() >= 3)
			_host->setPropXY(args[0].strValue, args[1].intValue, args[2].intValue);
		if (args.size() == 2)
			result = Value::makeInt(_host->propXY(args[0].strValue, args[1].intValue));
		return true;
	case Script::kMethodPropScale: // propscale(name, scale) -> FUN_00429870
		if (args.size() >= 2)
			_host->propScale(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodPropZClip: // propzclip(name, dist) -> FUN_00428ea0
		if (args.size() >= 2)
			_host->propZClip(args[0].strValue, args[1].intValue);
		return true;
	case Script::kMethodPropDist: // propdist(name[, d]) -> FUN_00429670/FUN_004295c0
		if (args.size() >= 2) {
			_host->propDist(args[0].strValue, args[1].intValue);
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->getPropDist(args[0].strValue));
		} else {
			result = Value::makeInt(0);
		}
		return true;
	case Script::kMethodPropDeg: // propdeg(name[, deg]) -> FUN_00429730/FUN_00429520
		if (args.size() >= 2) {
			result = Value::makeInt(_host->setPropDeg(args[0].strValue, args[1].intValue));
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->getPropDeg(args[0].strValue));
		}
		return true;
	case Script::kMethodPropOwner: // propowner(name[, owner]) -> FUN_00428d40: get or set
		if (args.size() >= 2)
			result = Value::makeString(_host->setPropOwner(args[0].strValue, args[1].strValue));
		else if (args.size() == 1)
			result = Value::makeString(_host->getPropOwner(args[0].strValue));
		return true;
	case Script::kMethodPropValue: // propvalue(name[, value]) -> FUN_004290d0/FUN_00428e00
		if (args.size() >= 2) {
			result = Value::makeInt(_host->setPropValue(args[0].strValue, args[1].intValue));
		} else if (args.size() == 1) {
			result = Value::makeInt(_host->getPropValue(args[0].strValue));
		}
		return true;
	case Script::kMethodCountProps: // countprops() -> FUN_0042b4f0: global count, all shops
		result = Value::makeInt(_host->countProps());
		return true;
	case Script::kMethodIndexToProp: // indextoprop(i) -> FUN_0042b550: 1-based global index
		result = Value::makeString(_host->indexToProp(args.empty() ? 0 : args[0].intValue));
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callPuppetMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenPuppetFile: // openpuppetfile('name.pup') -> FUN_004473c0/FUN_00447470
		_host->openPuppetFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPuppetClear: // puppetclear(): native display-list clear, rendering pending
		_host->puppetClear();
		return true;
	case Script::kMethodClosePuppetFile: // closepuppetfile() -> FUN_00447880
		_host->closePuppetFile();
		return true;
	case Script::kMethodPuppetSpeak: // puppetspeak(name[, mode]) -> FUN_00447ce0/FUN_00448b60
		_host->puppetSpeak(args.empty() ? Common::String() : args[0].strValue,
				args.size() > 1 ? args[1].intValue : 0);
		return true;
	case Script::kMethodPuppetBevel: // puppetbevel(name[, mode]) -> FUN_00447b30
		_host->puppetBevel(args.empty() ? Common::String() : args[0].strValue,
				args.size() > 1 ? args[1].intValue : 0);
		return true;
	case Script::kMethodPuppetGrab: // puppetgrab(bool) -> FUN_00447e30 stores DAT_00461248.
		_host->puppetGrab(!args.empty() && isTruthy(args[0]));
		return true;
	case Script::kMethodPuppetScript: // puppetscript(name) -> FUN_004482c0
		_host->puppetScript(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCurrentPuppet: // currentpuppet() -> current puppet name or 'none'
		result = Value::makeString(_host->currentPuppet());
		return true;
	case Script::kMethodPuppetParam: { // puppetparam(selector[, value]) -> FUN_00448730/FUN_004485f0
		if (args.empty()) {
			result = Value::makeInt(0);
			return true;
		}
		if (args.size() >= 2) {
			result = Value::makeInt(_host->setPuppetParam(args[0].intValue, args[1].intValue));
		} else {
			result = Value::makeInt(_host->getPuppetParam(args[0].intValue));
		}
		return true;
	}
	case Script::kMethodPuppetVisible: { // puppetvisible([flag]) -> FUN_00448550/FUN_004485b0
		result = args.empty() ?
				Value::makeBool(_host->getPuppetVisible()) :
				Value::makeBool(_host->setPuppetVisible(args[0].intValue != 0));
		return true;
	}
	case Script::kMethodPuppetBase: { // puppetbase([name]) -> FUN_00447ee0
		result = args.empty() ?
				Value::makeString(_host->getPuppetBase()) :
				Value::makeString(_host->setPuppetBase(args[0].strValue));
		return true;
	}
	case Script::kMethodCountPuppets: // countpuppets() -> FUN_00448380: PUP resource-2 script count
		result = Value::makeInt(_host->countPuppets());
		return true;
	case Script::kMethodIndexToPuppet: // indextopuppet(i) -> FUN_004483f0, 1-based
		result = Value::makeString(_host->indexToPuppet(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodPuppetEvent: // puppetevent(timeout) -> FUN_00449e40 waits for a clicked bevel id
		result = Value::makeInt(_host->puppetEvent(args.empty() ? -1 : args[0].intValue));
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callStageSetMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodOpenStageFile: // openstagefile('name.stg')
		_host->openStageFile(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodCloseStageFile: // closestagefile() -> FUN_00409330
		_host->closeStageFile();
		return true;
	case Script::kMethodGotoFlat: // gotoflat(name|index) -> FUN_00409460
		if (!args.empty())
			_host->gotoFlat(args[0]);
		return true;
	case Script::kMethodOpenSetFile: // opensetfile('name.set'[, scene[, view]]) -> FUN_00430690
		_host->openSetFile(args.size() > 0 ? args[0].strValue : Common::String(),
				args.size() > 1 ? args[1].strValue : Common::String(),
				args.size() > 2 ? args[2].strValue : Common::String());
		return true;
	case Script::kMethodCloseSetFile: // closesetfile() -> TI.EXE set-archive close
		_host->closeSetFile();
		return true;
	// (sendtoscene 0x2f02 never reaches here: it always appears with a
	// parenthesised message and routes through dispatchMessageBuiltin.)
	case Script::kMethodCurrentSet: // currentset() -> open set name or 'none'
		result = Value::makeString(_host->currentSet());
		return true;
	case Script::kMethodCurrentStage: // currentstage() -> open stage name or native 'None'
		result = Value::makeString(_host->currentStage());
		return true;
	case Script::kMethodStageVisible: { // stagevisible([flag]) -> DAT_00461156
		result = args.empty() ?
				Value::makeBool(_host->getStageVisible()) :
				Value::makeBool(_host->setStageVisible(args[0].intValue != 0));
		return true;
	}
	case Script::kMethodCurrentFlat: // currentflat() -> current stage node name or native 'None'
		result = Value::makeString(_host->currentFlat());
		return true;
	case Script::kMethodCountFlats: // countflats() (0x4e43 FUN_00409980) -> stage node count
		result = Value::makeInt(_host->countFlats());
		return true;
	case Script::kMethodIndexToFlat: // indextoflat(i) (0x4e44 FUN_004099e0) -> 1-based flat name
		result = Value::makeString(_host->indexToFlat(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodFlatToIndex: // flattoindex(name) (0x4e45 FUN_00409d70) -> 1-based index or 0
		result = Value::makeInt(_host->flatToIndex(args.empty() ? Common::String() : args[0].strValue));
		return true;
	case Script::kMethodCurrentView: { // currentview([name]) -> current SET view name
		result = args.empty() ?
				Value::makeString(_host->getCurrentView()) :
				Value::makeString(_host->setCurrentView(args[0].strValue));
		return true;
	}
	case Script::kMethodCurrentDeg: // currentdeg() -> DAT_004611ac
		result = Value::makeInt(_host->currentDeg());
		return true;
	case Script::kMethodCurrentScene: { // currentscene([name|left|right|strait])
		result = args.empty() ?
				Value::makeString(_host->getCurrentScene()) :
				Value::makeString(_host->setCurrentScene(args[0].strValue));
		return true;
	}
	case Script::kMethodSetVisible: { // setvisible([flag]) -> current SET visibility flag
		result = args.empty() ?
				Value::makeBool(_host->getSetVisible()) :
				Value::makeBool(_host->setSetVisible(args[0].intValue != 0));
		return true;
	}
	case Script::kMethodCountPaintings: // countpaintings(scene, view) -> FUN_00431fe0
		result = args.size() >= 2 ?
				Value::makeInt(_host->countPaintings(args[0].strValue, args[1].strValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodIndexToPainting: // indextopainting(scene, view, index) -> FUN_00432120, 1-based
		if (args.size() >= 3)
			result = Value::makeString(_host->indexToPainting(args[0].strValue,
					args[1].strValue, args[2].intValue));
		return true;
	case Script::kMethodRoadAhead: // roadahead(scene, view) -> FUN_00431bd0/FUN_004337b0
		result = args.size() >= 2 ?
				Value::makeBool(_host->roadAhead(args[0].strValue, args[1].strValue)) :
				Value::makeBool(false);
		return true;
	case Script::kMethodCameraXYZ: // cameraxyz(selector) -> FUN_00437870
		result = Value::makeInt(_host->cameraXYZ(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodCameraHi: { // camerahi([z]) -> FUN_00436170 get /
	             // FUN_00446190 set of DAT_0046119a, the world-projection base height
		result = args.empty() ?
				Value::makeInt(_host->getCameraHi()) :
				Value::makeInt(_host->setCameraHi(args[0].intValue));
		return true;
	}
	case Script::kMethodPlayerXYZ: // playerxyz(selector) -> FUN_00437950
		result = Value::makeInt(_host->playerXYZ(args.empty() ? 0 : args[0].intValue));
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callInputMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodKeyAborts: { // keyaborts([resource, key, flag]) -> FUN_00435a00/FUN_00446e10
		bool enabled = args.size() > 2 && args[2].intValue != 0;
		result = Value::makeBool(_host->keyAborts(
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
		result = Value::makeInt(_host->makePoint(args.size() > 0 ? args[0].intValue : 0,
				args.size() > 1 ? args[1].intValue : 0));
		return true;
	case Script::kMethodButton: // button() -> FUN_00436880: live left mouse button state
		result = Value::makeBool(_host->buttonDown());
		return true;
	case Script::kMethodStillDown: // stilldown() -> FUN_00436920: live mouse button state
		result = Value::makeBool(_host->stillDown());
		return true;
	case Script::kMethodTick: // tick() -> FUN_004368f0: native 60 Hz timer
		result = Value::makeInt(_host->tick());
		return true;
	case Script::kMethodQuestionDialog: // questiondialog(text) -> FUN_004363f0/FUN_00409030
		result = Value::makeBool(_host->questionDialog(
				args.empty() ? Common::String() : args[0].strValue));
		return true;
	case Script::kMethodOptionKey: // optionkey() -> FUN_004376e0/GetAsyncKeyState(VK_SHIFT)
		result = Value::makeBool(_host->optionKey());
		return true;
	case Script::kMethodShiftKey: // shiftkey() -> FUN_00437710/GetAsyncKeyState(VK_SHIFT)
		result = Value::makeBool(_host->shiftKey());
		return true;
	case Script::kMethodPointInButton: // pointinbutton(flat, button, point) -> FUN_0040a0d0
		result = args.size() >= 3 ?
				Value::makeBool(_host->pointInButton(args[0].strValue,
						args[1].strValue, args[2].intValue)) :
				Value::makeBool(false);
		return true;
	case Script::kMethodPointInPainting: // pointinpainting(scene, view, painting, point) -> FUN_004322e0
		result = args.size() >= 4 ?
				Value::makeBool(_host->pointInPainting(args[0].strValue,
						args[1].strValue, args[2].strValue, args[3].intValue)) :
				Value::makeBool(false);
		return true;
	case Script::kMethodPointInProp: // pointinprop(prop, point) -> FUN_00434af1
		result = args.size() >= 2 ?
				Value::makeBool(_host->pointInProp(args[0].strValue,
						args[1].intValue)) :
				Value::makeBool(false);
		return true;
	case Script::kMethodHitTest: // hittest(point) -> FUN_00435e70: name of the topmost item
	             // under the packed point; kind stored for result()
		if (!args.empty())
			result = Value::makeString(_host->hitTest(args[0].intValue));
		return true;
	case Script::kMethodCalcDeg: // calcdeg(pointA, pointB) -> FUN_00435c70
		result = args.size() >= 2 ?
				Value::makeInt(_host->calcDeg(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodCalcVectX: // calcvectx(deg, dist) -> FUN_00437770/FUN_004432e0
		result = args.size() >= 2 ?
				Value::makeInt(_host->calcVectX(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodCalcVectY: // calcvecty(deg, dist) -> FUN_004377f0/FUN_00443310
		result = args.size() >= 2 ?
				Value::makeInt(_host->calcVectY(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodStarXYZ: // starxyz(name, selector) -> FUN_00435a60/FUN_00432fc0
		result = args.size() >= 2 ?
				Value::makeInt(_host->starXYZ(args[0].strValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodCalcDist: // calcdist(pointA, pointB) -> FUN_00435d20
		result = args.size() >= 2 ?
				Value::makeInt(_host->calcDist(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodCalcMod: // calcmod(a, b) -> FUN_004358f0
		result = args.size() >= 2 ?
				Value::makeInt(_host->calcMod(args[0].intValue, args[1].intValue)) :
				Value::makeInt(0);
		return true;
	case Script::kMethodResult: // result() -> FUN_004366a0: last hittest kind (DAT_00461298)
		result = Value::makeString(_host->hitTestResult());
		return true;
	case Script::kMethodMouse: // mouse() -> FUN_004368b0: current mouse point, packed
		result = Value::makeInt(_host->mousePoint());
		return true;
	case Script::kMethodCursor: // cursor(id|name) -> FUN_00446920. Resource-name mapping
	             // verified against TI.EXE (see CyberflixEngine::setCursorResource):
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
			_host->setCursorResource(res);
		}
		return true;
	default:
		return false;
	}
}

bool ScriptVM::callRuntimeMethod(uint16 opcode, const Common::Array<Value> &args, Value &result) {
	switch (opcode) {
	case Script::kMethodMessage: // message(text) -> FUN_00446240 (no-op debug stub)
		_host->message(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodDelay: // delay(ticks) -> FUN_00405160
		_host->delayTicks(args.empty() ? 0 : args[0].intValue);
		return true;
	case Script::kMethodNoteDialog: // notedialog(text) -> FUN_004461e0 -> FUN_00408fc0 (modal OK box)
		_host->noteDialog(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodPlayMovie: // playmovie('name.mov')
		_host->playMovie(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodClut: // clut(name): snap the hardware palette (FUN_00446500)
		_host->setClut(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodBlackScreen: // blackscreen(): fill the window with black pixels (FUN_00446b80)
		_host->blackScreen();
		return true;
	case Script::kMethodForceUpdate: // forceupdate() -> FUN_00446910 -> FUN_00423a60
		_host->forceUpdate();
		return true;
	case Script::kMethodFlushEvents: // flushevents() -> FUN_00446cb0/FUN_00405110
		_host->flushEvents();
		return true;
	case Script::kMethodDrawString: // drawstring(text, point, color, size) -> FUN_004460c0
		if (args.size() >= 2)
			_host->drawString(args[0].strValue, args[1].intValue,
					args.size() > 2 ? args[2].intValue : 0,
					args.size() > 3 ? args[3].intValue : 12);
		return true;
	case Script::kMethodQuit: // quit() -> FUN_00446c80: restore cursor, set native quit flag
		_host->requestQuit();
		return true;
	case Script::kMethodBlackToScreen: // blacktoscreen(target, steps): palette fade black -> target
		_host->fadePalette(args.size() > 0 ? args[0].strValue : Common::String("current"),
				args.size() > 1 ? args[1].intValue : 1, false);
		return true;
	case Script::kMethodScreenToBlack: // screentoblack(target, steps): palette fade target -> black
		_host->fadePalette(args.size() > 0 ? args[0].strValue : Common::String("current"),
				args.size() > 1 ? args[1].intValue : 1, true);
		return true;
	case Script::kMethodMixClut: // mixclut(clutA, clutB, first, last, weight) -> FUN_00446570
		if (args.size() >= 5)
			_host->mixClut(args[0].strValue, args[1].strValue, args[2].intValue,
					args[3].intValue, args[4].intValue);
		return true;
	case Script::kMethodVisualEffect: { // visualeffect(effect, dur): set default transition (FUN_00446400)
		// The effect names ('plain', 'wipeleft', ...) are bare method opcodes
		// 0x5dc1..0x5dd5 used as atoms, which decodeAtom pushes as integers.
		// Anything outside that range (or a missing argument) means the script
		// asked for no transition, which is what the boot scripts do.
		uint16 effect = Script::kEffectPlain;
		if (!args.empty() && args[0].intValue >= Script::kVisualEffectFirst &&
				args[0].intValue <= Script::kVisualEffectLast)
			effect = static_cast<uint16>(args[0].intValue);
		_host->setVisualEffect(effect, args.size() > 1 ? args[1].intValue : 0);
		return true;
	}
	case Script::kMethodMakeLoop: // makeloop(kind, target, message, delay) -> FUN_00423e60
		if (args.size() >= 4)
			_host->makeLoop(args[0].strValue, args[1].strValue,
					args[2].strValue, args[3].intValue);
		return true;
	case Script::kMethodStopLoop: // stoploop(kind[, target]) -> FUN_00446d30/FUN_00423bf0
		if (!args.empty())
			_host->stopLoop(args[0].strValue,
					args.size() > 1 ? args[1].strValue : Common::String());
		return true;
	case Script::kMethodPauseLoop: // pauseloop(kind, flag)
		if (args.size() >= 2)
			_host->pauseLoop(args[0].strValue, args[1].intValue != 0);
		return true;
	case Script::kMethodMakeCricket: // makecricket(name, x, y, dist, angle, delay) -> FUN_00425640
		if (!args.empty())
			_host->makeCricket(args[0].strValue);
		return true;
	case Script::kMethodStopCricket: // stopcricket(name) -> FUN_00446db0/FUN_00423c80
		if (!args.empty())
			_host->stopCricket(args[0].strValue);
		return true;
	case Script::kMethodPauseCricket: // pausecricket(kind, flag)
		if (args.size() >= 2)
			_host->pauseCricket(args[0].strValue, args[1].intValue != 0);
		return true;
	case Script::kMethodActionFrame: // actionframe(n) -> bool, FUN_004362c0 (n must be 1 or 2)
		result = Value::makeBool(_host->actionFrame(args.empty() ? 0 : args[0].intValue));
		return true;
	case Script::kMethodFrameRate: { // framerate([n]) -> DAT_00461126
		result = args.empty() ?
				Value::makeInt(_host->getFrameRate()) :
				Value::makeInt(_host->setFrameRate(args[0].intValue));
		return true;
	}
	case Script::kMethodFrame: // frame() -> FUN_00435a30: DAT_00461122 compositor counter
		result = Value::makeInt(_host->frameCounter());
		return true;
	case Script::kMethodPath: { // path(slot[, value]) -> FUN_004462a0/FUN_00438450
		int slot = args.empty() ? 0 : args[0].intValue;
		result = args.size() >= 2 ?
				Value::makeString(_host->setPathSlot(slot, args[1].strValue)) :
				Value::makeString(_host->getPathSlot(slot));
		return true;
	}
	case Script::kMethodCurrentCD: { // currentcd([name]) -> FUN_00439df0/FUN_0043a290
		result = args.empty() ?
				Value::makeString(_host->getCurrentCD()) :
				Value::makeString(_host->setCurrentCD(args[0].strValue));
		return true;
	}
	case Script::kMethodSaveGame: // savegame(signature) -> FUN_00426620/FUN_00426790
		_host->saveGame(args.empty() ? Common::String() : args[0].strValue);
		return true;
	case Script::kMethodOpenGame: // opengame(signature) -> FUN_004266e0/FUN_00426f00
		_host->openGame(args.empty() ? Common::String() : args[0].strValue);
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
	//
	// Builtins go inert once the host is quitting: the statement loop only
	// unwinds a running script at its periodic quit check, and the statements
	// executed before that boundary must not keep rendering, playing audio, or
	// mutating game state (e.g. a puppet dialogue advancing its options while
	// the application exits).
	if (_host && _host->hostQuitRequested())
		return Value();
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
	// ~128 of ~308 opcodes are still stubbed. Without this log they vanish
	// silently and return int 0 / falsy, which can make scripts take wrong
	// branches (e.g. gating on a getter that always returns 0). Report each
	// unimplemented opcode once so a trace yields a clean manifest instead of
	// hundreds of repeats per second.
	if (!_reportedOpcodes.contains(opcode)) {
		_reportedOpcodes.setVal(opcode, true);
		Common::String a;
		for (uint32 i = 0; i < args.size(); ++i) {
			if (i)
				a += ", ";
			a += args[i].toString();
		}
		const char *builtin = Script::methodName(opcode);
		debug(1, "Cyberflix: unimplemented builtin %s#%#06x(%s) -> 0",
				builtin ? builtin : "?", opcode, a.c_str());
	}
	return Value();
}

} // End of namespace Cyberflix

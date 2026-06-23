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

#ifndef CYBERFLIX_VM_HOST_H
#define CYBERFLIX_VM_HOST_H

#include "cyberflix/vm.h"

namespace Cyberflix {

class CyberflixEngine;

class CyberflixMovieVMHost : public virtual VMMovieHost {
public:
	void playMovie(const Common::String &name) override;

private:
	CyberflixEngine &engine();
};

class CyberflixStageSetVMHost : public virtual VMNavigationHost {
public:
	void openStageFile(const Common::String &name) override;
	void closeStageFile() override;
	void gotoFlat(const Value &flat) override;
	Common::String currentStage() override;
	bool getStageVisible() override;
	bool setStageVisible(bool visible) override;
	Common::String currentFlat() override;
	void sendToStage(const Common::String &message, const Common::Array<Value> &args) override;
	Value sendToStageFx(const Common::String &message, const Common::Array<Value> &args) override;
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
	Common::String getCurrentView() override;
	Common::String setCurrentView(const Common::String &target) override;
	int currentDeg() override;
	Common::String getCurrentScene() override;
	Common::String setCurrentScene(const Common::String &target) override;
	bool getSetVisible() override;
	bool setSetVisible(bool visible) override;
	void sendToScene(const Common::String &scene,
			const Common::String &message = Common::String(),
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
	int cameraXYZ(int selector) override;
	int playerXYZ(int selector) override;

private:
	CyberflixEngine &engine();
};

class CyberflixPuppetVMHost : public virtual VMNavigationHost {
public:
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
	Common::String getPuppetBase() override;
	Common::String setPuppetBase(const Common::String &newBase) override;
	bool getPuppetVisible() override;
	bool setPuppetVisible(bool visible) override;
	int getPuppetParam(int selector) override;
	int setPuppetParam(int selector, int newValue) override;
	int countPuppets() override;
	Common::String indexToPuppet(int index) override;

private:
	CyberflixEngine &engine();
};

class CyberflixAudioVMHost : public virtual VMAudioHost {
public:
	void openTrackFile(const Common::String &name) override;
	void closeTrackFile(const Common::String &name) override;
	void playTheme(const Common::String &name) override;
	void haltTheme() override;
	void playSound(const Common::String &name, int mode) override;
	void playVoice(const Common::String &name) override;
	void haltSound(int which) override;
	void haltVoice() override;
	void themeVolume(const Common::String &name, int volume) override;
	int getWaveVolume() override;
	int setWaveVolume(int newLevel) override;
	int getSoundVolume(const Common::String &name) override;
	int setSoundVolume(const Common::String &name, int newVolume) override;
	Common::String currentTheme(int which) override;
	Common::String currentSound(int which) override;
	Common::String currentVoice() override;
	bool voiceDone() override;

private:
	CyberflixEngine &engine();
};

class CyberflixActorVMHost : public virtual VMActorHost {
public:
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
	bool getActorVisible(const Common::String &name) override;
	bool setActorVisible(const Common::String &name, bool visible) override;
	Common::String getActorSet(const Common::String &name) override;
	Common::String setActorSet(const Common::String &name, const Common::String &newSet) override;
	Common::String getActorStar(const Common::String &name) override;
	Common::String setActorStar(const Common::String &name, const Common::String &newStar) override;
	Common::String getActorPose(const Common::String &name) override;
	Common::String setActorPose(const Common::String &name, const Common::String &newPose) override;
	void actorXYZ(const Common::String &name, int x, int y, int z) override;
	int actorXYZ(const Common::String &name, int selector) override;
	int getActorDeg(const Common::String &name) override;
	int setActorDeg(const Common::String &name, int newDeg) override;
	int getActorDist(const Common::String &name) override;
	void setActorDist(const Common::String &name, int newDist) override;
	int getActorValue(const Common::String &name) override;
	int setActorValue(const Common::String &name, int newValue) override;
	Common::String getActorOwner(const Common::String &name) override;
	Common::String setActorOwner(const Common::String &name, const Common::String &newOwner) override;
	void actorZClip(const Common::String &name, int zClip) override;
	void actorSpeed(const Common::String &name, int speed) override;
	void actorScale(const Common::String &name, int scale) override;
	void actorTurn(const Common::String &name, int turn) override;
	void turnToDeg(const Common::String &name, int deg) override;
	void walkToStar(const Common::String &name, const Common::String &star) override;
	void stopWalk(const Common::String &name) override;
	bool isWalk(const Common::String &name) override;
	Common::String walkDest(const Common::String &name) override;
	int starXYZ(const Common::String &name, int selector) override;

private:
	CyberflixEngine &engine();
};

class CyberflixPropVMHost : public virtual VMInteractionHost {
public:
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
	int getPropDeg(const Common::String &name) override;
	int setPropDeg(const Common::String &name, int newDeg) override;
	Common::String getPropOwner(const Common::String &name) override;
	Common::String setPropOwner(const Common::String &name, const Common::String &newOwner) override;
	int getPropValue(const Common::String &name) override;
	int setPropValue(const Common::String &name, int newValue) override;
	int countProps() override;
	Common::String indexToProp(int index) override;

private:
	CyberflixEngine &engine();
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_VM_HOST_H

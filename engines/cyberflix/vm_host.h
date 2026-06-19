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
	bool stageVisible(const bool *newVisible) override;
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
	Common::String currentView(const Common::String *target = nullptr) override;
	int currentDeg() override;
	Common::String currentScene(const Common::String *target) override;
	bool setVisible(const bool *newVisible) override;
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
	Common::String puppetBase(const Common::String *newBase) override;
	bool puppetVisible(const bool *newVisible) override;
	int puppetParam(int selector, const int *newValue) override;
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
	int waveVolume(const int *newLevel) override;
	int soundVolume(const Common::String &name, const int *newVolume) override;
	Common::String currentTheme(int which) override;
	Common::String currentSound(int which) override;
	Common::String currentVoice() override;

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
	int propDeg(const Common::String &name, const int *newDeg) override;
	Common::String propOwner(const Common::String &name, const Common::String *newOwner) override;
	int propValue(const Common::String &name, const int *newValue) override;
	int countProps() override;
	Common::String indexToProp(int index) override;

private:
	CyberflixEngine &engine();
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_VM_HOST_H

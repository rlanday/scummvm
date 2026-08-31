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

namespace CyberFlix {

void CyberFlixEngine::openSetFile(const Common::String &name,
		const Common::String &scene, const Common::String &view) {
	// FUN_004307f0 zeroes the camerahi base height for the incoming set;
	// BOOTFILE's global openset handler then re-derives it via adjustcamera().
	_cameraHiValue = 0;
	setRuntime().openSetFile(*this, name, scene, view);
}

void CyberFlixEngine::closeSetFile() {
	setRuntime().closeSetFile(*this);
}

Common::String CyberFlixEngine::currentSet() {
	return setRuntime().currentSet();
}

Common::String CyberFlixEngine::getCurrentView() {
	return setRuntime().getCurrentView();
}

Common::String CyberFlixEngine::setCurrentView(const Common::String &target) {
	return setRuntime().setCurrentView(*this, target);
}

int CyberFlixEngine::currentDeg() {
	return setRuntime().currentDeg();
}

Common::String CyberFlixEngine::getCurrentScene() {
	return setRuntime().getCurrentScene(*this);
}

Common::String CyberFlixEngine::setCurrentScene(const Common::String &target) {
	return setRuntime().setCurrentScene(*this, target);
}

bool CyberFlixEngine::getSetVisible() {
	return setRuntime().getSetVisible(*this);
}

bool CyberFlixEngine::setSetVisible(bool visible) {
	return setRuntime().setSetVisible(*this, visible);
}

void CyberFlixEngine::sendToScene(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	setRuntime().sendToScene(*this, scene, message, args);
}

Value CyberFlixEngine::sendToSceneFx(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	return setRuntime().sendToSceneFx(*this, scene, message, args);
}

void CyberFlixEngine::sendToPainting(const Common::String &scene,
		const Common::String &view, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	setRuntime().sendToPainting(*this, scene, view, painting, message, args);
}

Value CyberFlixEngine::sendToPaintingFx(const Common::String &scene,
		const Common::String &view, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	return setRuntime().sendToPaintingFx(*this, scene, view, painting, message, args);
}

int CyberFlixEngine::countPaintings(const Common::String &scene, const Common::String &view) {
	return setRuntime().countPaintings(scene, view);
}

Common::String CyberFlixEngine::indexToPainting(const Common::String &scene,
		const Common::String &view, int index) {
	return setRuntime().indexToPainting(scene, view, index);
}

bool CyberFlixEngine::roadAhead(const Common::String &scene, const Common::String &view) {
	return setRuntime().roadAhead(scene, view);
}

int CyberFlixEngine::cameraXYZ(int selector) {
	return setRuntime().cameraXYZ(selector);
}

int CyberFlixEngine::playerXYZ(int selector) {
	return setRuntime().playerXYZ(selector);
}

int CyberFlixEngine::setCameraHi(int z) {
	// FUN_00446190 stores a 16-bit value into DAT_0046119a.
	_cameraHiValue = static_cast<int16>(z);
	debug(1, "CyberFlix: camerahi(%d)", _cameraHiValue);
	if (setRuntime().set() && setRuntime().set()->isOpen() &&
			setRuntime().set()->baseZ() != _cameraHiValue) {
		setRuntime().set()->setBaseZ(_cameraHiValue);
		_propRuntime.setDirty(true);
	}
	return _cameraHiValue;
}


void CyberFlixEngine::openStageFile(const Common::String &name) {
	stageRuntime().openStageFile(*this, name);
}

void CyberFlixEngine::closeStageFile() {
	stageRuntime().closeStageFile(*this);
}

void CyberFlixEngine::gotoFlat(const Value &flat) {
	stageRuntime().gotoFlat(*this, flat);
}

Common::String CyberFlixEngine::currentStage() {
	return stageRuntime().currentStage();
}

bool CyberFlixEngine::getStageVisible() {
	return stageRuntime().getStageVisible();
}

bool CyberFlixEngine::setStageVisible(bool visible) {
	return stageRuntime().setStageVisible(visible);
}

Common::String CyberFlixEngine::currentFlat() {
	return stageRuntime().currentFlat();
}

int CyberFlixEngine::countFlats() {
	return stageRuntime().countFlats();
}

Common::String CyberFlixEngine::indexToFlat(int index) {
	return stageRuntime().indexToFlat(index);
}

int CyberFlixEngine::flatToIndex(const Common::String &name) {
	return stageRuntime().flatToIndex(name);
}

void CyberFlixEngine::sendToStage(const Common::String &message, const Common::Array<Value> &args) {
	stageRuntime().sendToStage(*this, message, args);
}

Value CyberFlixEngine::sendToStageFx(const Common::String &message, const Common::Array<Value> &args) {
	return stageRuntime().sendToStageFx(*this, message, args);
}

void CyberFlixEngine::sendToFlat(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	stageRuntime().sendToFlat(*this, flat, message, args);
}

Value CyberFlixEngine::sendToFlatFx(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	return stageRuntime().sendToFlatFx(*this, flat, message, args);
}

void CyberFlixEngine::sendToButton(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	stageRuntime().sendToButton(*this, flat, button, message, args);
}

Value CyberFlixEngine::sendToButtonFx(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	return stageRuntime().sendToButtonFx(*this, flat, button, message, args);
}

} // End of namespace CyberFlix

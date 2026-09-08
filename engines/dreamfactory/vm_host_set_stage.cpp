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

#include "dreamfactory/dreamfactory.h"

namespace DreamFactory {

void DreamFactoryEngine::openSetFile(const Common::String &name,
		const Common::String &scene, const Common::String &view) {
	// FUN_004307f0 zeroes the camerahi base height for the incoming set;
	// BOOTFILE's global openset handler then re-derives it via adjustcamera().
	_cameraHiValue = 0;
	setRuntime().openSetFile(*this, name, scene, view);
}

void DreamFactoryEngine::closeSetFile() {
	setRuntime().closeSetFile(*this);
}

Common::String DreamFactoryEngine::currentSet() {
	return setRuntime().currentSet();
}

Common::String DreamFactoryEngine::getCurrentView() {
	return setRuntime().getCurrentView();
}

Common::String DreamFactoryEngine::setCurrentView(const Common::String &target) {
	return setRuntime().setCurrentView(*this, target);
}

int DreamFactoryEngine::currentDeg() {
	return setRuntime().currentDeg();
}

Common::String DreamFactoryEngine::getCurrentScene() {
	return setRuntime().getCurrentScene(*this);
}

Common::String DreamFactoryEngine::setCurrentScene(const Common::String &target) {
	return setRuntime().setCurrentScene(*this, target);
}

bool DreamFactoryEngine::getSetVisible() {
	return setRuntime().getSetVisible(*this);
}

bool DreamFactoryEngine::setSetVisible(bool visible) {
	return setRuntime().setSetVisible(*this, visible);
}

void DreamFactoryEngine::sendToScene(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	setRuntime().sendToScene(*this, scene, message, args);
}

Value DreamFactoryEngine::sendToSceneFx(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	return setRuntime().sendToSceneFx(*this, scene, message, args);
}

void DreamFactoryEngine::sendToPainting(const Common::String &scene,
		const Common::String &view, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	setRuntime().sendToPainting(*this, scene, view, painting, message, args);
}

Value DreamFactoryEngine::sendToPaintingFx(const Common::String &scene,
		const Common::String &view, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	return setRuntime().sendToPaintingFx(*this, scene, view, painting, message, args);
}

int DreamFactoryEngine::countPaintings(const Common::String &scene, const Common::String &view) {
	return setRuntime().countPaintings(scene, view);
}

Common::String DreamFactoryEngine::indexToPainting(const Common::String &scene,
		const Common::String &view, int index) {
	return setRuntime().indexToPainting(scene, view, index);
}

bool DreamFactoryEngine::roadAhead(const Common::String &scene, const Common::String &view) {
	return setRuntime().roadAhead(scene, view);
}

int DreamFactoryEngine::cameraXYZ(int selector) {
	return setRuntime().cameraXYZ(selector);
}

int DreamFactoryEngine::playerXYZ(int selector) {
	return setRuntime().playerXYZ(selector);
}

int DreamFactoryEngine::setCameraHi(int z) {
	// FUN_00446190 stores a 16-bit value into DAT_0046119a.
	_cameraHiValue = static_cast<int16>(z);
	debug(1, "DreamFactory: camerahi(%d)", _cameraHiValue);
	if (setRuntime().set() && setRuntime().set()->isOpen() &&
			setRuntime().set()->baseZ() != _cameraHiValue) {
		setRuntime().set()->setBaseZ(_cameraHiValue);
		_propRuntime.setDirty(true);
	}
	return _cameraHiValue;
}


void DreamFactoryEngine::openStageFile(const Common::String &name) {
	stageRuntime().openStageFile(*this, name);
}

void DreamFactoryEngine::closeStageFile() {
	stageRuntime().closeStageFile(*this);
}

void DreamFactoryEngine::gotoFlat(const Value &flat) {
	stageRuntime().gotoFlat(*this, flat);
}

Common::String DreamFactoryEngine::currentStage() {
	return stageRuntime().currentStage();
}

bool DreamFactoryEngine::getStageVisible() {
	return stageRuntime().getStageVisible();
}

bool DreamFactoryEngine::setStageVisible(bool visible) {
	return stageRuntime().setStageVisible(visible);
}

Common::String DreamFactoryEngine::currentFlat() {
	return stageRuntime().currentFlat();
}

int DreamFactoryEngine::countFlats() {
	return stageRuntime().countFlats();
}

Common::String DreamFactoryEngine::indexToFlat(int index) {
	return stageRuntime().indexToFlat(index);
}

int DreamFactoryEngine::flatToIndex(const Common::String &name) {
	return stageRuntime().flatToIndex(name);
}

void DreamFactoryEngine::sendToStage(const Common::String &message, const Common::Array<Value> &args) {
	stageRuntime().sendToStage(*this, message, args);
}

Value DreamFactoryEngine::sendToStageFx(const Common::String &message, const Common::Array<Value> &args) {
	return stageRuntime().sendToStageFx(*this, message, args);
}

void DreamFactoryEngine::sendToFlat(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	stageRuntime().sendToFlat(*this, flat, message, args);
}

Value DreamFactoryEngine::sendToFlatFx(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	return stageRuntime().sendToFlatFx(*this, flat, message, args);
}

void DreamFactoryEngine::sendToButton(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	stageRuntime().sendToButton(*this, flat, button, message, args);
}

Value DreamFactoryEngine::sendToButtonFx(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	return stageRuntime().sendToButtonFx(*this, flat, button, message, args);
}

} // End of namespace DreamFactory

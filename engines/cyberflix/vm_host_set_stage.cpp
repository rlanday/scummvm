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

#include "cyberflix/cyberflix.h"

namespace Cyberflix {

void CyberflixStageSetVMHost::openSetFile(const Common::String &name,
		const Common::String &scene, const Common::String &view) {
	engine().setRuntime().openSetFile(engine(), name, scene, view);
}

void CyberflixStageSetVMHost::closeSetFile() {
	engine().setRuntime().closeSetFile(engine());
}

Common::String CyberflixStageSetVMHost::currentSet() {
	return engine().setRuntime().currentSet();
}

Common::String CyberflixStageSetVMHost::currentView(const Common::String *target) {
	return engine().setRuntime().currentView(engine(), target);
}

int CyberflixStageSetVMHost::currentDeg() {
	return engine().setRuntime().currentDeg();
}

Common::String CyberflixStageSetVMHost::currentScene(const Common::String *target) {
	return engine().setRuntime().currentScene(engine(), target);
}

bool CyberflixStageSetVMHost::setVisible(const bool *newVisible) {
	return engine().setRuntime().setVisible(engine(), newVisible);
}

void CyberflixStageSetVMHost::sendToScene(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	engine().setRuntime().sendToScene(engine(), scene, message, args);
}

Value CyberflixStageSetVMHost::sendToSceneFx(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	return engine().setRuntime().sendToSceneFx(engine(), scene, message, args);
}

void CyberflixStageSetVMHost::sendToPainting(const Common::String &scene,
		const Common::String &view, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	engine().setRuntime().sendToPainting(engine(), scene, view, painting, message, args);
}

Value CyberflixStageSetVMHost::sendToPaintingFx(const Common::String &scene,
		const Common::String &view, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	return engine().setRuntime().sendToPaintingFx(engine(), scene, view, painting, message, args);
}

int CyberflixStageSetVMHost::countPaintings(const Common::String &scene, const Common::String &view) {
	return engine().setRuntime().countPaintings(scene, view);
}

Common::String CyberflixStageSetVMHost::indexToPainting(const Common::String &scene,
		const Common::String &view, int index) {
	return engine().setRuntime().indexToPainting(scene, view, index);
}

bool CyberflixStageSetVMHost::roadAhead(const Common::String &scene, const Common::String &view) {
	return engine().setRuntime().roadAhead(scene, view);
}

int CyberflixStageSetVMHost::cameraXYZ(int selector) {
	return engine().setRuntime().cameraXYZ(selector);
}

int CyberflixStageSetVMHost::playerXYZ(int selector) {
	return engine().setRuntime().playerXYZ(selector);
}


void CyberflixStageSetVMHost::openStageFile(const Common::String &name) {
	engine().stageRuntime().openStageFile(engine(), name);
}

void CyberflixStageSetVMHost::closeStageFile() {
	engine().stageRuntime().closeStageFile(engine());
}

void CyberflixStageSetVMHost::gotoFlat(const Value &flat) {
	engine().stageRuntime().gotoFlat(engine(), flat);
}

Common::String CyberflixStageSetVMHost::currentStage() {
	return engine().stageRuntime().currentStage();
}

bool CyberflixStageSetVMHost::stageVisible(const bool *newVisible) {
	return engine().stageRuntime().stageVisible(newVisible);
}

Common::String CyberflixStageSetVMHost::currentFlat() {
	return engine().stageRuntime().currentFlat();
}

void CyberflixStageSetVMHost::sendToStage(const Common::String &message, const Common::Array<Value> &args) {
	engine().stageRuntime().sendToStage(engine(), message, args);
}

Value CyberflixStageSetVMHost::sendToStageFx(const Common::String &message, const Common::Array<Value> &args) {
	return engine().stageRuntime().sendToStageFx(engine(), message, args);
}

void CyberflixStageSetVMHost::sendToFlat(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	engine().stageRuntime().sendToFlat(engine(), flat, message, args);
}

Value CyberflixStageSetVMHost::sendToFlatFx(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	return engine().stageRuntime().sendToFlatFx(engine(), flat, message, args);
}

void CyberflixStageSetVMHost::sendToButton(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	engine().stageRuntime().sendToButton(engine(), flat, button, message, args);
}

Value CyberflixStageSetVMHost::sendToButtonFx(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	return engine().stageRuntime().sendToButtonFx(engine(), flat, button, message, args);
}

} // End of namespace Cyberflix

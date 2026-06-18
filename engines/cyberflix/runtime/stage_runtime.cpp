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
#include "common/system.h"

#include "graphics/cursorman.h"
#include "graphics/surface.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/runtime/graphics_helpers.h"
#include "cyberflix/stage.h"

namespace Cyberflix {

void StageRuntime::openStageFile(CyberflixEngine &engine, const Common::String &name) {
	if (name.empty())
		return;
	debug(1, "Cyberflix: openstagefile('%s') from stage '%s' flat '%s'",
			name.c_str(), currentStage().c_str(), currentFlat().c_str());
	Common::SharedPtr<Stage> newStage(new Stage());
	if (!newStage->open(name)) {
		debug(1, "Cyberflix: openstagefile('%s') failed", name.c_str());
		return;
	}
	clearShellFrame();
	stage() = newStage;
	visible() = true;
	debug(1, "Cyberflix: stage '%s' open (%u nodes)", name.c_str(), stage()->nodeCount());

	renderStageNode(engine, 0);
	Common::Array<Value> noArgs;
	sendToStage(engine, "openstage", noArgs);
	sendToFlat(engine, currentFlat(), "openflat", noArgs);
}

void StageRuntime::closeStageFile(CyberflixEngine &engine) {
	if (!stage() || !stage()->isOpen())
		return;
	debug(1, "Cyberflix: closestagefile() closing stage '%s' flat '%s'",
			currentStage().c_str(), currentFlat().c_str());
	Common::Array<Value> noArgs;
	sendToFlat(engine, currentFlat(), "closeflat", noArgs);
	sendToStage(engine, "closestage", noArgs);
	reset();
	engine.blackScreen();
}

void StageRuntime::gotoFlat(CyberflixEngine &engine, const Value &flat) {
	if (!stage() || !stage()->isOpen())
		return;
	debug(1, "Cyberflix: gotoflat(%s) in stage '%s' flat '%s'",
			flat.toString().c_str(), currentStage().c_str(), currentFlat().c_str());
	int targetNode = -1;
	if (flat.type == Value::kInt) {
		targetNode = flat.intValue - 1;
	} else {
		targetNode = stage()->findNode(flat.strValue);
	}
	if (targetNode < 0 || static_cast<uint32>(targetNode) >= stage()->nodeCount()) {
		warning("Cyberflix: gotoflat('%s') not found in stage '%s'",
				flat.toString().c_str(), stage()->name().c_str());
		return;
	}
	if (targetNode == node())
		return;
	debug(1, "Cyberflix: gotoflat(%s) resolved node %d", flat.toString().c_str(), targetNode);
	Common::String openedName = stage()->name();
	uint32 openedCount = stage()->nodeCount();
	Common::String oldFlat = currentFlat();
	Common::Array<Value> noArgs;
	sendToFlat(engine, oldFlat, "closeflat", noArgs);
	if (!stage() || !stage()->isOpen() || stage()->name() != openedName ||
			stage()->nodeCount() != openedCount)
		return;
	renderStageNode(engine, targetNode);
	sendToFlat(engine, currentFlat(), "openflat", noArgs);
}

Common::String StageRuntime::currentStage() const {
	if (stage() && stage()->isOpen())
		return stage()->name();
	return "None";
}

bool StageRuntime::stageVisible(const bool *newVisible) {
	if (!stage() || !stage()->isOpen())
		return false;
	if (newVisible)
		visible() = *newVisible;
	return visible();
}

Common::String StageRuntime::currentFlat() const {
	if (stage() && stage()->isOpen())
		return stage()->nodeName(static_cast<uint32>(node()));
	return "None";
}

StageRuntime::Snapshot StageRuntime::snapshot() const {
	Snapshot state;
	if (stage() && stage()->isOpen()) {
		state.stageName = stage()->name();
		state.node = node();
		if (node() >= 0 && static_cast<uint32>(node()) < stage()->nodeCount())
			state.flatName = stage()->nodeName(static_cast<uint32>(node()));
		state.visible = visible();
	}
	return state;
}

bool StageRuntime::restoreSnapshot(const Snapshot &snapshot) {
	reset();
	if (snapshot.stageName.empty())
		return true;

	Common::SharedPtr<Stage> newStage(new Stage());
	if (!newStage->open(snapshot.stageName)) {
		warning("Cyberflix: load could not reopen stage '%s'", snapshot.stageName.c_str());
		return false;
	}

	stage() = newStage;
	visible() = snapshot.visible;
	node() = snapshot.node;
	if (node() < 0 || static_cast<uint32>(node()) >= stage()->nodeCount()) {
		int foundNode = stage()->findNode(snapshot.flatName);
		node() = foundNode >= 0 ? foundNode : 0;
	}
	return true;
}

const FrameImage *StageRuntime::stageShellFrame() {
	if (!stage() || !stage()->isOpen())
		return nullptr;
	if (!shellFrameValid()) {
		if (!stage()->renderNode(0, shellFrameData()))
			return nullptr;
		shellFrameValid() = true;
	}
	return &shellFrameData();
}

void StageRuntime::sendToStage(CyberflixEngine &engine, const Common::String &message, const Common::Array<Value> &args) {
	if (!stage() || !stage()->isOpen()) {
		warning("Cyberflix: sendtostage('%s') with no stage open", message.c_str());
		return;
	}
	Common::SharedPtr<Stage> dispatchStage = stage();
	engine.dispatchWithScopes(dispatchStage->stageScript(), nullptr,
			dispatchStage->name(), Common::String(), message, args, "stage");
	engine.propRuntime().refreshPropsIfDirty(engine);
}

Value StageRuntime::sendToStageFx(CyberflixEngine &engine, const Common::String &message, const Common::Array<Value> &args) {
	if (!stage() || !stage()->isOpen()) {
		warning("Cyberflix: sendtostagefx('%s') with no stage open", message.c_str());
		return Value();
	}
	Common::SharedPtr<Stage> dispatchStage = stage();
	return engine.dispatchWithScopesValue(dispatchStage->stageScript(), nullptr,
			dispatchStage->name(), Common::String(), message, args, "stagefx");
}

void StageRuntime::sendToFlat(CyberflixEngine &engine, const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!stage() || !stage()->isOpen()) {
		warning("Cyberflix: sendtoflat('%s') with no stage open", flat.c_str());
		return;
	}
	Common::SharedPtr<Stage> dispatchStage = stage();
	int dispatchNode = flat.empty() ? node() : dispatchStage->findNode(flat);
	if (dispatchNode < 0 || static_cast<uint32>(dispatchNode) >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat named '%s'",
				dispatchStage->name().c_str(), flat.c_str());
		return;
	}
	Common::String flatName = dispatchStage->nodeName(static_cast<uint32>(dispatchNode));
	engine.dispatchWithScopes(dispatchStage->nodeScript(static_cast<uint32>(dispatchNode)),
			dispatchStage->stageScript(), flatName, flatName, message, args, "flat");
	engine.propRuntime().refreshPropsIfDirty(engine);
}

Value StageRuntime::sendToFlatFx(CyberflixEngine &engine, const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!stage() || !stage()->isOpen()) {
		warning("Cyberflix: sendtoflatfx('%s') with no stage open", flat.c_str());
		return Value();
	}
	Common::SharedPtr<Stage> dispatchStage = stage();
	int dispatchNode = flat.empty() ? node() : dispatchStage->findNode(flat);
	if (dispatchNode < 0 || static_cast<uint32>(dispatchNode) >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat named '%s'",
				dispatchStage->name().c_str(), flat.c_str());
		return Value();
	}
	Common::String flatName = dispatchStage->nodeName(static_cast<uint32>(dispatchNode));
	return engine.dispatchWithScopesValue(dispatchStage->nodeScript(static_cast<uint32>(dispatchNode)),
			dispatchStage->stageScript(), flatName, flatName, message, args, "flatfx");
}

void StageRuntime::sendToButton(CyberflixEngine &engine, const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!stage() || !stage()->isOpen()) {
		warning("Cyberflix: sendtobutton('%s') with no stage open", button.c_str());
		return;
	}
	Common::SharedPtr<Stage> dispatchStage = stage();
	int dispatchNode = flat.empty() ? node() : dispatchStage->findNode(flat);
	if (dispatchNode < 0 || static_cast<uint32>(dispatchNode) >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat named '%s'",
				dispatchStage->name().c_str(), flat.c_str());
		return;
	}
	if (!dispatchStage->hasButton(static_cast<uint32>(dispatchNode), button)) {
		warning("Cyberflix: stage '%s' flat '%s' has no button named '%s'",
				dispatchStage->name().c_str(), flat.c_str(), button.c_str());
		return;
	}
	Common::Array<const Script *> scopes;
	scopes.push_back(dispatchStage->buttonScript(static_cast<uint32>(dispatchNode), button));
	scopes.push_back(dispatchStage->nodeScript(static_cast<uint32>(dispatchNode)));
	scopes.push_back(dispatchStage->stageScript());
	debug(1, "Cyberflix: sendtobutton('%s', '%s') -> %s(%u args)",
			dispatchStage->nodeName(static_cast<uint32>(dispatchNode)).c_str(), button.c_str(),
			message.c_str(), args.size());
	engine.dispatchWithScopeChain(scopes, button, button, message, args, "button");
	engine.propRuntime().refreshPropsIfDirty(engine);
}

Value StageRuntime::sendToButtonFx(CyberflixEngine &engine, const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!stage() || !stage()->isOpen()) {
		warning("Cyberflix: sendtobuttonfx('%s') with no stage open", button.c_str());
		return Value();
	}
	Common::SharedPtr<Stage> dispatchStage = stage();
	int dispatchNode = flat.empty() ? node() : dispatchStage->findNode(flat);
	if (dispatchNode < 0 || static_cast<uint32>(dispatchNode) >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat named '%s'",
				dispatchStage->name().c_str(), flat.c_str());
		return Value();
	}
	if (!dispatchStage->hasButton(static_cast<uint32>(dispatchNode), button)) {
		warning("Cyberflix: stage '%s' flat '%s' has no button named '%s'",
				dispatchStage->name().c_str(), flat.c_str(), button.c_str());
		return Value();
	}
	Common::Array<const Script *> scopes;
	scopes.push_back(dispatchStage->buttonScript(static_cast<uint32>(dispatchNode), button));
	scopes.push_back(dispatchStage->nodeScript(static_cast<uint32>(dispatchNode)));
	scopes.push_back(dispatchStage->stageScript());
	return engine.dispatchWithScopeChainValue(scopes, button, button, message, args, "buttonfx");
}

void StageRuntime::renderStageNode(CyberflixEngine &engine, int targetNode, bool resetCursor) {
	if (!stage() || !stage()->isOpen()) {
		warning("Cyberflix: sendtostage(%d) with no stage open", targetNode);
		return;
	}
	node() = targetNode;

	FrameImage frame;
	if (!stage()->renderNode(static_cast<uint32>(targetNode), frame))
		return;

	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	if (stage()->loadStagePalette(rgb) && !engine.paletteIsBlack())
		engine.programPalette(rgb);

	engine.propRuntime().advancePropPoses();
	Graphics::Surface *screen = engine._system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	copyFrameToScreen(*screen, frame, 0, 0);

	Common::Array<const Shop::Prop *> draw;
	Common::Array<const Shop *> drawShop;
	engine.propRuntime().collectScreenProps(draw, drawShop);
	for (uint32 i = 0; i < draw.size(); ++i) {
		Common::SharedPtr<CelImage> cel;
		Common::Rect r;
		if (!drawShop[i]->renderProp(*draw[i], cel, r))
			continue;
		drawCel(screen, *cel, r, Common::Rect(kScreenWidth, kScreenHeight));
	}
	engine._system->unlockScreen();

	if (resetCursor && engine.setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
	engine.propRuntime().clearDirtyRects();
	engine.propRuntime().setDirty(false);
	engine._system->updateScreen();
	if (targetNode == 0) {
		shellFrameData() = frame;
		shellFrameValid() = true;
	}

	debug(1, "Cyberflix: rendered stage '%s' node %d (%ux%u)",
			stage()->name().c_str(), targetNode, frame.width, frame.height);
}

void StageRuntime::repaintDirtyStageRects(CyberflixEngine &engine) {
	if (!stage() || !stage()->isOpen() || engine.propRuntime().dirtyRects().empty())
		return;

	FrameImage frame;
	if (!stage()->renderNode(static_cast<uint32>(node()), frame)) {
		renderStageNode(engine, node(), false);
		return;
	}

	engine.propRuntime().advancePropPoses();
	Common::Array<const Shop::Prop *> draw;
	Common::Array<const Shop *> drawShop;
	engine.propRuntime().collectScreenProps(draw, drawShop);

	Graphics::Surface *screen = engine._system->lockScreen();
	for (uint32 r = 0; r < engine.propRuntime().dirtyRects().size(); ++r) {
		Common::Rect dirty = engine.propRuntime().dirtyRects()[r];
		dirty.clip(Common::Rect(kScreenWidth, kScreenHeight));
		if (dirty.isEmpty())
			continue;

		for (int y = dirty.top; y < dirty.bottom; ++y) {
			for (int x = dirty.left; x < dirty.right; ++x) {
				if (x < frame.width && y < frame.height)
					*(reinterpret_cast<byte *>(screen->getBasePtr(x, y))) = frame.pixels[static_cast<uint>(y) * frame.width + x];
				else
					*(reinterpret_cast<byte *>(screen->getBasePtr(x, y))) = 0;
			}
		}

		for (uint32 i = 0; i < draw.size(); ++i) {
			Common::SharedPtr<CelImage> cel;
			Common::Rect propRect;
			if (!drawShop[i]->renderProp(*draw[i], cel, propRect))
				continue;
			if (!dirty.intersects(propRect))
				continue;
			drawCel(screen, *cel, propRect, dirty);
		}
	}
	engine._system->unlockScreen();

	engine.propRuntime().clearDirtyRects();
	engine.propRuntime().setDirty(false);
	engine._system->updateScreen();
}

bool StageRuntime::pointInButton(const Common::String &flat,
		const Common::String &button, int32 packedPoint) const {
	if (!stage() || !stage()->isOpen())
		return false;
	int targetNode = flat.empty() ? node() : stage()->findNode(flat);
	if (targetNode < 0 || static_cast<uint32>(targetNode) >= stage()->nodeCount())
		return false;
	const int16 x = static_cast<int16>(packedPoint >> 16);
	const int16 y = static_cast<int16>(packedPoint & 0xffff);
	bool hit = stage()->pointInButton(static_cast<uint32>(targetNode), button, x, y);
	debug(1, "Cyberflix: pointinbutton('%s', '%s', %d,%d) -> %s",
			stage()->nodeName(static_cast<uint32>(targetNode)).c_str(), button.c_str(), x, y,
			hit ? "true" : "false");
	return hit;
}

} // End of namespace Cyberflix

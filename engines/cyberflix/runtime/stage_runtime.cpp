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

// Dispatch a flat script message without ScummVM's normal post-dispatch prop
// refresh. This is intentionally different from public sendToFlat(): a script
// sendtoflat("name", message(...)) is a complete script-visible dispatch in our
// runtime, so sendToFlat() refreshes dirty props after the handler just like the
// other sendto* entry points.
//
// gotoflat() is different in the original executable. Native FUN_00409460
// switches flats as one indivisible operation: FUN_00409660 dispatches the old
// flat's closeflat, FUN_0040b180 decodes the target node into the stage backing
// buffer and marks a full-screen dirty redraw, then FUN_004095a0 dispatches the
// new flat's openflat. The close/open helpers only construct the message string
// and route it through FUN_0040a860; they do not run forceupdate()/FUN_00423a60
// or present through FUN_00407000. The marked redraw is presented later by the
// normal compositor pass, which also rebuilds the prop display list.
//
// Using sendToFlat() for gotoflat's internal closeflat made ScummVM insert an
// extra repaint at exactly that forbidden point, and using renderStageNode()
// inside gotoflat presented the target background before the script's next
// forceupdate() animation frame. BOIL.SHP's coal-gate scripts hide/show the
// animated gate prop immediately before gotoflat(); either premature present can
// expose coal for one frame. This helper keeps gotoflat's internal close/open
// messages on the native no-refresh path.
bool StageRuntime::dispatchFlatMessage(CyberflixEngine &engine, const Common::SharedPtr<Stage> &dispatchStage,
		int dispatchNode, const Common::String &message, const Common::Array<Value> &args) {
	if (!dispatchStage || !dispatchStage->isOpen())
		return false;
	if (dispatchNode < 0 || static_cast<uint32>(dispatchNode) >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat index %d",
				dispatchStage->name().c_str(), dispatchNode);
		return false;
	}

	Common::String flatName = dispatchStage->nodeName(static_cast<uint32>(dispatchNode));
	engine.dispatchWithScopes(dispatchStage->nodeScript(static_cast<uint32>(dispatchNode)),
			dispatchStage->stageScript(), flatName, flatName, message, args, "flat");
	return true;
}

bool StageRuntime::queueStageNodeRedraw(CyberflixEngine &engine, int targetNode) {
	if (!stage() || !stage()->isOpen()) {
		warning("Cyberflix: gotoflat(%d) with no stage open", targetNode + 1);
		return false;
	}

	node() = targetNode;
	if (targetNode == 0) {
		if (stage()->renderNode(0, shellFrameData()))
			shellFrameValid() = true;
		else
			clearShellFrame();
	}

	engine.propRuntime().queueDirtyRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight));
	engine.propRuntime().setDirty(true);
	return true;
}

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
	Common::SharedPtr<Stage> dispatchStage = stage();
	int oldNode = node();
	Common::Array<Value> noArgs;
	dispatchFlatMessage(engine, dispatchStage, oldNode, "closeflat", noArgs);
	if (!stage() || !stage()->isOpen() || stage()->name() != openedName ||
			stage()->nodeCount() != openedCount)
		return;
	if (!queueStageNodeRedraw(engine, targetNode))
		return;
	dispatchFlatMessage(engine, dispatchStage, targetNode, "openflat", noArgs);
}

Common::String StageRuntime::currentStage() const {
	if (stage() && stage()->isOpen())
		return stage()->name();
	return "None";
}

bool StageRuntime::getStageVisible() const {
	if (!stage() || !stage()->isOpen())
		return false;
	return visible();
}

bool StageRuntime::setStageVisible(bool newVisible) {
	if (!stage() || !stage()->isOpen())
		return false;
	visible() = newVisible;
	return visible();
}

Common::String StageRuntime::currentFlat() const {
	if (stage() && stage()->isOpen())
		return stage()->nodeName(static_cast<uint32>(node()));
	return "None";
}

int StageRuntime::countFlats() const {
	// countflats() (0x4e43): number of nodes in the open stage. Flats are 1-based
	// in script (gotoflat/indextoflat), matching countprops/countpuppets.
	if (stage() && stage()->isOpen())
		return static_cast<int>(stage()->nodeCount());
	return 0;
}

Common::String StageRuntime::indexToFlat(int index) const {
	// indextoflat(i) (0x4e44): name of the 1-based flat i, or native "None".
	if (stage() && stage()->isOpen() && index >= 1 &&
			static_cast<uint32>(index) <= stage()->nodeCount())
		return stage()->nodeName(static_cast<uint32>(index - 1));
	return "None";
}

int StageRuntime::flatToIndex(const Common::String &name) const {
	// flattoindex(name) (0x4e45): 1-based index of the named flat, or 0 if none.
	if (stage() && stage()->isOpen()) {
		int node = stage()->findNode(name);
		if (node >= 0)
			return node + 1;
	}
	return 0;
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
	dispatchFlatMessage(engine, dispatchStage, dispatchNode, message, args);
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
	Common::String flatName = dispatchStage->nodeName(static_cast<uint32>(dispatchNode));
	Common::Array<Common::String> scopeSelf;
	scopeSelf.push_back(button);
	scopeSelf.push_back(flatName);
	scopeSelf.push_back(dispatchStage->name());
	Common::Array<Common::String> scopeProp;
	scopeProp.push_back(button);
	scopeProp.push_back(button);
	scopeProp.push_back(button);
	debug(1, "Cyberflix: sendtobutton('%s', '%s') -> %s(%u args)",
			flatName.c_str(), button.c_str(),
			message.c_str(), args.size());
	engine.dispatchWithScopeChainContextsValue(scopes, scopeSelf, scopeProp,
			button, button, message, args, "button");
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
	Common::String flatName = dispatchStage->nodeName(static_cast<uint32>(dispatchNode));
	Common::Array<Common::String> scopeSelf;
	scopeSelf.push_back(button);
	scopeSelf.push_back(flatName);
	scopeSelf.push_back(dispatchStage->name());
	Common::Array<Common::String> scopeProp;
	scopeProp.push_back(button);
	scopeProp.push_back(button);
	scopeProp.push_back(button);
	return engine.dispatchWithScopeChainContextsValue(scopes, scopeSelf, scopeProp,
			button, button, message, args, "buttonfx");
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

	Palette rgb = {};
	if (stage()->loadStagePalette(rgb) && !engine.paletteIsBlack())
		engine.programPalette(rgb);

	engine.propRuntime().advancePropPoses();
	engine.actorRuntime().advanceActorPoses();
	Graphics::Surface *screen = engine._system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	copyFrameToScreen(*screen, frame, 0, 0);

	Common::Array<const Shop::Prop *> draw;
	Common::Array<const Shop *> drawShop;
	engine.propRuntime().collectScreenProps(draw, drawShop);
	for (uint32 i = 0; i < draw.size(); ++i) {
		Shop::PropRenderResult rendered = drawShop[i]->renderProp(*draw[i]);
		if (!rendered.valid)
			continue;
		drawCel(screen, *rendered.cel, rendered.rect, Common::Rect(kScreenWidth, kScreenHeight));
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
	if (!stage() || !stage()->isOpen())
		return;

	engine.propRuntime().advancePropPosesAndMarkDirtyRects();
	if (engine.propRuntime().dirtyRects().empty())
		return;

	FrameImage frame;
	if (!stage()->renderNode(static_cast<uint32>(node()), frame)) {
		renderStageNode(engine, node(), false);
		return;
	}

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
			Shop::PropRenderResult rendered = drawShop[i]->renderProp(*draw[i]);
			if (!rendered.valid)
				continue;
			if (!dirty.intersects(rendered.rect))
				continue;
			drawCel(screen, *rendered.cel, rendered.rect, dirty);
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

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
#include "cyberflix/script.h"
#include "cyberflix/set.h"

namespace Cyberflix {

// Dispatch a message with a freshly built scope chain, mirroring the
// original's per-dispatch chains (FUN_0042ae80 builds [prop script, shop
// script, BOOTFILE res2]; FUN_0042b2b0 [shop script, BOOTFILE res2]). The
// chain REPLACES the active one for the duration of the call — TI.EXE passes
// each dispatch's complete chain to the call executor FUN_0040b690, and
// notably BOOTFILE res1 (the boot mousedown/idle handlers) is NOT part of a
// prop/shop dispatch, so a prop without its own handler leaves the message
// unhandled instead of recursing into the boot handler of the same name.
// The 0xfba/0xfbb context atoms are saved and restored around the call. The
// fixed one/two/three-scope helpers are used by hot scheduled-loop callbacks
// to avoid first building a temporary "scopes" array that is immediately
// reversed again.
void CyberflixEngine::dispatchWithScopes(const Script *scope1, const Script *scope2,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	dispatchWithScopesValue(scope1, scope2, self, targetProp, message, args, debugContext);
}

Value CyberflixEngine::dispatchWithScopesValue(const Script *scope1, const Script *scope2,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	return dispatchWithThreeScopesValue(scope1, scope2, nullptr, self, targetProp,
			message, args, debugContext);
}

Value CyberflixEngine::dispatchWithThreeScopesValue(const Script *scope1, const Script *scope2,
		const Script *scope3, const Common::String &self,
		const Common::String &targetProp, const Common::String &message,
		const Common::Array<Value> &args, const char *debugContext) {
	// Painting dispatch has exactly three native scopes: painting, scene, set.
	// Keep the same search order as dispatchWithScopeChainValue() but skip the
	// caller-side temporary scope array in this sampled hot path.
	Common::String prevSelf = _vm.contextSelf();
	Common::String prevProp = _vm.contextProp();
	ScriptVM::LibraryState prevChain = _vm.swapLibrariesFixed(
			scope1, scope2, scope3, _globalLib.get());
	_vm.setDispatchContext(self, targetProp);

	bool handled = false;
	Value result = _vm.callFunction(message, args, &handled);
	if (!handled)
		debug(1, "Cyberflix: %s message '%s' unhandled", debugContext, message.c_str());

	_vm.setDispatchContext(prevSelf, prevProp);
	_vm.restoreLibraries(prevChain);
	return result;
}

Value CyberflixEngine::dispatchWithScopeChainValue(const Common::Array<const Script *> &scopes,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	Common::Array<Common::String> scopeSelf;
	Common::Array<Common::String> scopeProp;
	return dispatchWithScopeChainContextsValue(scopes, scopeSelf, scopeProp,
			self, targetProp, message, args, debugContext);
}

Value CyberflixEngine::dispatchWithScopeChainContextsValue(const Common::Array<const Script *> &scopes,
		const Common::Array<Common::String> &scopeSelf,
		const Common::Array<Common::String> &scopeProp,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	Common::String prevSelf = _vm.contextSelf();
	Common::String prevProp = _vm.contextProp();
	Common::Array<const Script *> chain;
	Common::Array<Common::String> chainSelf;
	Common::Array<Common::String> chainProp;
	chain.reserve((_globalLib ? 1 : 0) + scopes.size());
	chainSelf.reserve((_globalLib ? 1 : 0) + scopes.size());
	chainProp.reserve((_globalLib ? 1 : 0) + scopes.size());
	if (_globalLib) {
		chain.push_back(_globalLib.get()); // "System: " tail, searched last
		chainSelf.push_back(self);
		chainProp.push_back(targetProp);
	}
	for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; --i) {
		const uint32 scopeIndex = static_cast<uint32>(i);
		if (scopes[scopeIndex]) {
			chain.push_back(scopes[static_cast<uint32>(i)]);
			chainSelf.push_back(scopeIndex < scopeSelf.size() ? scopeSelf[scopeIndex] : self);
			chainProp.push_back(scopeIndex < scopeProp.size() ? scopeProp[scopeIndex] : targetProp);
		}
	}
	ScriptVM::LibraryState prevChain = _vm.swapLibrariesWithContexts(chain, chainSelf, chainProp);
	_vm.setDispatchContext(self, targetProp);

	bool handled = false;
	Value result = _vm.callFunction(message, args, &handled);
	if (!handled)
		debug(1, "Cyberflix: %s message '%s' unhandled", debugContext, message.c_str());

	_vm.setDispatchContext(prevSelf, prevProp);
	_vm.restoreLibraries(prevChain);
	return result;
}

void CyberflixEngine::dispatchSetMessage(const Common::String &message, const Common::Array<Value> &args) {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen() || message.empty())
		return;
	Common::SharedPtr<Script> setScript = _setRuntime.set()->setScriptShared();
	dispatchWithScopes(setScript.get(), nullptr, _setRuntime.set()->setName(), Common::String(),
			message, args, "set");
}

Value CyberflixEngine::dispatchSetMessageValue(const Common::String &message, const Common::Array<Value> &args) {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen() || message.empty())
		return Value();
	Common::SharedPtr<Script> setScript = _setRuntime.set()->setScriptShared();
	return dispatchWithScopesValue(setScript.get(), nullptr, _setRuntime.set()->setName(),
			Common::String(), message, args, "setfx");
}

Value CyberflixEngine::sendToSetFx(const Common::String &message, const Common::Array<Value> &args) {
	return dispatchSetMessageValue(message, args);
}

void CyberflixEngine::sendToSet(const Common::String &message, const Common::Array<Value> &args) {
	dispatchSetMessage(message, args);
}

void CyberflixEngine::dispatchSceneMessage(uint32 scene, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen() || message.empty())
		return;
	Common::SharedPtr<Script> sceneScript = _setRuntime.set()->sceneScriptShared(scene);
	Common::SharedPtr<Script> setScript = _setRuntime.set()->setScriptShared();
	dispatchWithScopes(sceneScript.get(), setScript.get(), _setRuntime.set()->sceneName(scene),
			Common::String(), message, args, "scene");
}

bool CyberflixEngine::closeCurrentSceneForNavigation() {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen() || _setRuntime.scene() < 0)
		return false;

	// FUN_00430c70 calls FUN_00430f30 before FUN_00442140 starts movement, and
	// aborts if the set archive changed while the current scene handled close.
	Common::String openedName = _setRuntime.set()->setName();
	uint32 openedCount = _setRuntime.set()->sceneCount();
	Common::Array<Value> noArgs;
	dispatchSceneMessage(static_cast<uint32>(_setRuntime.scene()), "closescene", noArgs);
	return _setRuntime.set() && _setRuntime.set()->isOpen() && _setRuntime.set()->setName() == openedName &&
			_setRuntime.set()->sceneCount() == openedCount;
}

// actionframe(n): did the last movie display its n'th action-cue frame?
// Mirrors TI.EXE FUN_004362c0 reading the DAT_0046112a bitmask (n in 1..2).
} // End of namespace Cyberflix

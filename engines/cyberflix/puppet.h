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

#ifndef CYBERFLIX_PUPPET_H
#define CYBERFLIX_PUPPET_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/ptr.h"
#include "common/str.h"

#include "cyberflix/archive.h"
#include "cyberflix/image.h"
#include "cyberflix/script.h"

namespace Graphics {
struct Surface;
}

namespace Cyberflix {

struct CelImage;

/**
 * A CyberFlix puppet file (.PUP under PUPPETS1/PUPPETS2). Reversed from
 * TI.EXE FUN_00447470/FUN_00448380/FUN_004483f0/FUN_004489b0:
 * the master header is resource 0 with info tag 0x40000, the authored puppet
 * name is at master +0x85e, and resource 2 is the script-name table used by
 * countpuppets()/indextopuppet()/sendtopuppet().
 */
class Puppet {
public:
	struct ScriptEntry {
		uint32 resId = 0;
		Common::String name;
		Common::SharedPtr<Script> script;
	};

	struct ActionEntry {
		int16 baseState = 0;
		uint16 frameCount = 0;
		uint32 audioResourceId = 0;
		uint32 frameResourceId = 0;
		Common::String text;
		Common::String name;
		uint32 cacheIndex = 0;
	};

	enum {
		kMasterGlobalResourceOffset = 0x85a,
		kMasterNameOffset = 0x85e,
		kMasterBaseCountOffset = 0x86e,
		kMasterBaseTableOffset = 0x870,
		kMasterBaseStride = 0x138,
		kMasterActionBaseStateOffset = 0x00,
		kMasterActionFrameCountOffset = 0x06,
		kMasterActionAudioResourceOffset = 0x08,
		kMasterActionFrameResourceOffset = 0x0c,
		kMasterActionTextOffset = 0x18,
		kMasterActionNameOffset = 0x118,

		kScriptTableResourceId = 2,
		kScriptTableCountOffset = 0x12,
		kScriptTableEntriesOffset = 0x14,
		kScriptEntryStride = 0x28,
		kScriptEntryResourceOffset = 0x00,
		kScriptEntryNameOffset = 0x08,

		kBaseControllerResourceId = 3,
		kBaseControllerStateOffset = 0x14,
		kBaseControllerResourceOffset = 0x16,
		kBaseControllerStateCount = 4,

		kDisplayLayerCount = 11,
		kDisplayLayerOffset = 0x16,
		kDisplayLayerStride = 0x106,
		kDisplayLayerResourceListOffset = 0x06,
		kDisplayLayerCacheListOffset = 0x86,
		kDisplayLayerMaxResources = 0x20,

		kFrameRecordStride = 0x52,
		kFrameRecordLayersOffset = 0x10,
		kFrameLayerStride = 0x06
	};

	Puppet() {}

	/** Load and parse @p name (a puppet-directory basename, e.g. "smeth1.pup"). */
	bool open(const Common::String &name);

	bool isOpen() const { return _master >= 0; }
	const Common::String &sourceName() const { return _sourceName; }
	const Common::String &puppetName() const { return _puppetName; }
	uint32 baseCount() const { return _baseCount; }

	uint32 scriptCount() const { return _scripts.size(); }
	Common::String scriptName(uint32 index) const;
	Common::SharedPtr<Script> scriptByName(const Common::String &name) const;

	uint32 actionCount() const { return _actions.size(); }
	const ActionEntry *actionAt(uint32 index) const;
	const ActionEntry *actionByName(const Common::String &name) const;
	bool actionFramesVisuallyEqual(const ActionEntry &action, uint32 frameA,
			uint32 frameB, bool skipLayer0) const;
	bool renderActionFrame(const ActionEntry &action, uint32 frameIndex,
			Graphics::Surface &screen, bool skipLayer0) const;
	bool renderBevelBackdrop(Graphics::Surface &screen, int screenHeight, int screenWidth) const;
	bool decodeActionAudio(const ActionEntry &action, Common::Array<byte> &pcm) const;

	bool loadPuppetPalette(Palette &rgb) const;

private:
	struct RenderLayer {
		uint8 layer = 0;
		int16 nativeY = 0;
		int16 nativeX = 0;
		Common::SharedPtr<CelImage> cel;
	};

	struct RenderFrame {
		Common::Array<RenderLayer> layers;
	};

	const byte *engineBase(uint32 index) const;
	const byte *payload(uint32 index) const;
	int resourceIndexById(uint32 id) const;
	Common::String pascalString(const byte *p) const;
	Common::SharedPtr<Script> parseScriptResource(uint32 resId) const;
	Common::SharedPtr<CelImage> celResource(uint32 resId) const;
	const Common::Array<RenderFrame> *cachedActionFrames(const ActionEntry &action) const;
	bool renderCelImage(const CelImage &cel, int16 nativeY, int16 nativeX,
			Graphics::Surface &screen) const;
	uint32 baseDisplayListResource(int16 baseState) const;
	uint32 displayLayerResourceId(uint32 displayListResourceId,
			uint32 layer, int16 celIndex) const;
	bool renderCelResource(uint32 resId, int16 nativeY, int16 nativeX,
			Graphics::Surface &screen) const;

	Common::String _sourceName;
	Common::String _puppetName;
	Common::Array<byte> _fileData;
	Archive _archive;
	int _master = -1;
	uint32 _globalResourceId = 0;
	uint32 _baseCount = 0;
	uint32 _baseDisplayListResources[kBaseControllerStateCount] = {};
	Common::Array<ScriptEntry> _scripts;
	Common::Array<ActionEntry> _actions;
	// PUP script/action tables are immutable while open; these indexes avoid
	// repeated case-insensitive linear scans during puppet dispatch and playback.
	Common::HashMap<Common::String, uint32> _scriptIndexByName;
	Common::HashMap<Common::String, uint32> _actionIndexByName;
	mutable Common::HashMap<uint32, Common::SharedPtr<CelImage> > _celCache;
	mutable Common::HashMap<uint32, Common::Array<RenderFrame> > _actionFrameCache;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_PUPPET_H

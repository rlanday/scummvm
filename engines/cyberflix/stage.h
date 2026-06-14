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

#ifndef CYBERFLIX_STAGE_H
#define CYBERFLIX_STAGE_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

#include "cyberflix/archive.h"
#include "cyberflix/image.h"
#include "cyberflix/script.h"

namespace Cyberflix {

/**
 * A CyberFlix "stage" (a @c .STG deck under DATA/): the navigable world of a
 * deck/area, parsed from the same LPPALPPA container as everything else.
 * Reversed from the native stage interpreter the boot script hands off to after
 * @c sendtostage(0)
 * (openstagefile = TI.EXE FUN_004090b0 -> FUN_00409150 parse; node render =
 * FUN_0040b180). Full RE writeup in files/decomp/stage-notes.md.
 *
 * A stage's resource 0 is its master header (info tag @c 0x00040000), laid out
 * (offsets in the @c record+8 "engine base" frame, like movie master headers):
 *   +0x028 uint16 width   (== 512)
 *   +0x02a uint16 height  (== 384)
 *   +0x838 Pascal string  (the stage's own name)
 *   +0x848 uint32 nodeCount
 *   +0x84c NODE TABLE, @c nodeCount records of stride 0x2e (46) bytes:
 *            +0x00 uint32 flags       (bit 0 = KEYFRAME)
 *            +0x0a uint32 imageResId  (background frame resource, this file)
 *            +0x12 uint32 resHandle   (preload bookkeeping; unused here)
 *
 * A node's background is an inter-coded full-screen frame (info tag high half
 * 0x0200, the same codec as MOV video and SET backgrounds). Rendering a node
 * therefore means: scan back to the nearest keyframe, then replay every frame
 * forward up to the target through a FrameSequence (exactly FUN_0040b180).
 */
class Stage {
public:
	Stage() {}
	~Stage() {}

	/** Node-table record stride and field offsets (engine-base frame). */
	enum {
		kMasterWidthOffset = 0x028,
		kMasterHeightOffset = 0x02a,
		kNodeCountOffset = 0x848,
		kNodeTableOffset = 0x84c,
		kNodeRecordStride = 0x2e,
		kNodeFlagsOffset = 0x00,
		kNodeScriptResOffset = 0x06,
		kNodeImageResOffset = 0x0a,
		kNodeButtonTableOffset = 0x0e,
		kNodeNameOffset = 0x1e,
		kNodeFlagKeyframe = 0x1,

		kStageScriptIdOffset = 0x02c,
		kButtonCountOffset = 0x400,
		kButtonRecordsOffset = 0x404,
		kButtonRecordStride = 0x20,
		kButtonRectOffset = 0x04,
		kButtonScriptOffset = 0x0c,
		kButtonNameOffset = 0x10
	};

	/**
	 * Load and parse the stage file @p name (a DATA/ basename, e.g. "main.stg").
	 * Returns false if the file is missing, not a valid container, or lacks a
	 * well-formed master header. Replaces any previously loaded stage.
	 */
	bool open(const Common::String &name);

	bool isOpen() const { return _master >= 0; }
	const Common::String &name() const { return _name; }
	uint32 nodeCount() const { return _nodeCount; }
	uint16 width() const { return _width; }
	uint16 height() const { return _height; }

	/**
	 * Decode node @p node's background into @p out, compositing from the nearest
	 * preceding keyframe (the frames are inter-coded). Returns false on a bad
	 * node index or a malformed frame. Mirrors TI.EXE FUN_0040b180.
	 */
	bool renderNode(uint32 node, FrameImage &out);

	/**
	 * Node @p node's name: the Pascal string at +0x1e of its node record.
	 * This is what hittest (TI.EXE FUN_00435e70) returns as the "flat" name
	 * when the click lands on the stage background — it copies the current
	 * node's 0x2e-byte record and reads the string at +0x1e.
	 */
	Common::String nodeName(uint32 node) const;

	/** Index of the node named @p name, or -1 if missing. */
	int findNode(const Common::String &name) const;

	/**
	 * Back-to-front button hit-test for node @p node. Returns the button name
	 * under @p x,@p y, or empty on no hit. Mirrors TI.EXE FUN_0040af40.
	 */
	Common::String hitTestButton(uint32 node, int16 x, int16 y) const;

	/**
	 * Named button rectangle test used by pointinbutton(flat, button, point)
	 * (TI.EXE FUN_0040a0d0 -> FUN_0040b4b0 -> FUN_0041ac60).
	 */
	bool pointInButton(uint32 node, const Common::String &button, int16 x, int16 y) const;

	/** The stage-wide behavior script, or null if unavailable. */
	const Script *stageScript() const;

	/** Node/flat @p node's behavior script, or null if unavailable. */
	const Script *nodeScript(uint32 node) const;

	/** Button @p button's behavior script in @p node, or null if unavailable. */
	const Script *buttonScript(uint32 node, const Common::String &button) const;

	/** True if node @p node has a button record named @p button. */
	bool hasButton(uint32 node, const Common::String &button) const;

	/** Expand the stage's embedded palette into @p rgb (256*3, R,G,B). */
	bool loadStagePalette(byte *rgb) const;

private:
	/** Engine-base pointer (record+8) of resource @p index, or nullptr. */
	const byte *engineBase(uint32 index) const;
	/** Payload pointer (record+12) of resource @p index, or nullptr. */
	const byte *payload(uint32 index) const;
	/** Archive index of the resource whose id is @p id, or -1. */
	int resourceIndexById(uint32 id) const;
	/** Node record pointer for @p node, or nullptr if out of range. */
	const byte *nodeRecord(uint32 node) const;
	/** Button table payload pointer for @p node, or nullptr if unavailable. */
	const byte *buttonTable(uint32 node, uint32 &count, uint32 &length) const;
	/** Button record for @p button in @p node, or nullptr if missing. */
	const byte *buttonRecord(uint32 node, const Common::String &button) const;
	/** Read the Pascal string at @p p (bounded by the file buffer). */
	Common::String pascalString(const byte *p) const;
	/** Parsed script resource @p id, or null if missing/not a script. */
	const Script *scriptById(uint32 id) const;

	Common::String _name;
	// _fileData outlives _archive: the archive's stream and every frame pointer
	// reference into this buffer.
	Common::Array<byte> _fileData;
	Archive _archive;
	int _master = -1;     ///< Archive index of the master-header resource.
	uint32 _stageScriptId = 0;
	Common::Array<Common::SharedPtr<Script> > _scripts;
	uint32 _nodeCount = 0;
	uint16 _width = 0;
	uint16 _height = 0;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_STAGE_H

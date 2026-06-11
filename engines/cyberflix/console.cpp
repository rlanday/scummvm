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

#include "common/file.h"

#include "graphics/palette.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "common/system.h"
#include "common/memstream.h"

#include "cyberflix/console.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/archive.h"
#include "cyberflix/image.h"
#include "cyberflix/script.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

Console::Console(CyberflixEngine *engine) : GUI::Debugger(), _engine(engine) {
	registerCmd("dumpArchive", WRAP_METHOD(Console, cmdDumpArchive));
	registerCmd("disasm", WRAP_METHOD(Console, cmdDisasm));
	registerCmd("vmtrace", WRAP_METHOD(Console, cmdVmTrace));
	registerCmd("vmrun", WRAP_METHOD(Console, cmdVmRun));
	registerCmd("showshape", WRAP_METHOD(Console, cmdShowShape));
	registerCmd("showframe", WRAP_METHOD(Console, cmdShowFrame));
	registerCmd("showmovie", WRAP_METHOD(Console, cmdShowMovie));
}

bool Console::cmdDumpArchive(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Parses and lists the resources of an LPPALPPA container file.\n");
		debugPrintf("Usage: %s <filename> [count]   (e.g. BOOTFILE, CTL.STG, BEDSIT1.SET)\n", argv[0]);
		return true;
	}

	Common::File file;
	if (!file.open(argv[1])) {
		debugPrintf("Could not open '%s'\n", argv[1]);
		return true;
	}

	Archive archive;
	if (!archive.open(file.readStream(file.size()), argv[1])) {
		debugPrintf("'%s' is not a valid LPPALPPA container\n", argv[1]);
		return true;
	}

	debugPrintf("%s: %u resources, declared size %u bytes\n",
			archive.getName().c_str(), archive.getResourceCount(),
			archive.getDeclaredSize());

	uint32 count = archive.getResourceCount();
	uint32 limit = (argc >= 3) ? (uint32)atoi(argv[2]) : 16;
	debugPrintf("  idx        id      length     info       data@\n");
	for (uint32 i = 0; i < count && i < limit; ++i) {
		const Archive::Resource &res = archive.getResource(i);
		if (res.empty)
			debugPrintf("  [%4u]   <empty>\n", i);
		else
			debugPrintf("  [%4u] %6u  %#10x  %#010x  %#08x\n",
					i, res.id, res.length, res.info, res.dataOffset);
	}
	if (count > limit)
		debugPrintf("  ... %u more (pass a count to show more)\n", count - limit);
	return true;
}

bool Console::cmdDisasm(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Disassembles a script resource (info tag 0x0FA1).\n");
		debugPrintf("Usage: %s <filename> <resIndex> [count]\n", argv[0]);
		return true;
	}

	Common::File file;
	if (!file.open(argv[1])) {
		debugPrintf("Could not open '%s'\n", argv[1]);
		return true;
	}

	Archive archive;
	if (!archive.open(file.readStream(file.size()), argv[1])) {
		debugPrintf("'%s' is not a valid LPPALPPA container\n", argv[1]);
		return true;
	}

	uint32 idx = (uint32)atoi(argv[2]);
	if (idx >= archive.getResourceCount()) {
		debugPrintf("Resource index %u out of range (%u resources)\n",
				idx, archive.getResourceCount());
		return true;
	}

	const Archive::Resource &res = archive.getResource(idx);
	if (res.info != 0x0FA1)
		debugPrintf("Note: resource %u has info %#x, not a script (0x0FA1)\n", idx, res.info);

	Common::SeekableReadStream *stream = archive.createReadStreamForResource(idx);
	if (!stream) {
		debugPrintf("Resource %u is empty\n", idx);
		return true;
	}

	Script script;
	bool ok = script.parse(stream);
	delete stream;
	if (!ok) {
		debugPrintf("Failed to parse resource %u as a script\n", idx);
		return true;
	}

	debugPrintf("%u instructions, pool@%#x, %s\n", script.getInstructionCount(),
			script.getPoolOffset(), script.isTerminated() ? "terminated" : "UNTERMINATED");

	uint32 limit = (argc >= 4) ? (uint32)atoi(argv[3]) : 40;
	for (uint32 i = 0; i < script.getInstructionCount() && i < limit; ++i) {
		const Script::Instruction &inst = script.getInstruction(i);
		// Symbol/string atoms encode a self-relative pool offset in operandA.
		Common::String str;
		if (inst.opcode == Script::kOpPushSym || inst.opcode == Script::kOpPush3 ||
				inst.opcode == Script::kOpPush4)
			str = script.getSelfRelString(i);
		debugPrintf("  %4u: %-8s op=%#06x a=%#06x b=%#010x%s%s\n", i,
				Script::opcodeName(inst.opcode), inst.opcode, inst.operandA, inst.operandB,
				str.empty() ? "" : "  ; ", str.c_str());
	}
	return true;
}

bool Console::cmdVmTrace(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Executes a script resource on the VM harness with tracing.\n");
		debugPrintf("Usage: %s <filename> <resIndex> [maxSteps]\n", argv[0]);
		return true;
	}

	Common::File file;
	if (!file.open(argv[1])) {
		debugPrintf("Could not open '%s'\n", argv[1]);
		return true;
	}

	Archive archive;
	if (!archive.open(file.readStream(file.size()), argv[1])) {
		debugPrintf("'%s' is not a valid LPPALPPA container\n", argv[1]);
		return true;
	}

	uint32 idx = (uint32)atoi(argv[2]);
	if (idx >= archive.getResourceCount()) {
		debugPrintf("Resource index %u out of range (%u resources)\n",
				idx, archive.getResourceCount());
		return true;
	}

	Common::SeekableReadStream *stream = archive.createReadStreamForResource(idx);
	if (!stream) {
		debugPrintf("Resource %u is empty\n", idx);
		return true;
	}

	Script script;
	bool ok = script.parse(stream);
	delete stream;
	if (!ok) {
		debugPrintf("Failed to parse resource %u as a script\n", idx);
		return true;
	}

	uint32 maxSteps = (argc >= 4) ? (uint32)atoi(argv[3]) : 200;
	debugPrintf("Tracing %u instructions (max %u steps):\n",
			script.getInstructionCount(), maxSteps);

	ScriptVM vm;
	vm.setTrace(true);
	vm.run(script, maxSteps);
	debugPrintf("VM trace complete.\n");
	return true;
}

bool Console::cmdVmRun(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Runs a script resource through the statement interpreter.\n");
		debugPrintf("Usage: %s <filename> <resIndex> [maxSteps]\n", argv[0]);
		return true;
	}

	Common::File file;
	if (!file.open(argv[1])) {
		debugPrintf("Could not open '%s'\n", argv[1]);
		return true;
	}

	Archive archive;
	if (!archive.open(file.readStream(file.size()), argv[1])) {
		debugPrintf("'%s' is not a valid LPPALPPA container\n", argv[1]);
		return true;
	}

	uint32 idx = (uint32)atoi(argv[2]);
	if (idx >= archive.getResourceCount()) {
		debugPrintf("Resource index %u out of range (%u resources)\n",
				idx, archive.getResourceCount());
		return true;
	}

	Common::SeekableReadStream *stream = archive.createReadStreamForResource(idx);
	if (!stream) {
		debugPrintf("Resource %u is empty\n", idx);
		return true;
	}

	Script script;
	bool ok = script.parse(stream);
	delete stream;
	if (!ok) {
		debugPrintf("Failed to parse resource %u as a script\n", idx);
		return true;
	}

	uint32 maxSteps = (argc >= 4) ? (uint32)atoi(argv[3]) : 100000;
	ScriptVM vm;
	uint32 executed = vm.runProgram(script, maxSteps);
	debugPrintf("Executed %u statements over %u instructions.\n",
			executed, script.getInstructionCount());
	return true;
}

bool Console::cmdShowShape(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Decodes a cel resource and blits it to the screen.\n");
		debugPrintf("Usage: %s <filename> <resIndex> [paletteFile]\n", argv[0]);
		debugPrintf("  e.g. %s INVEN.SHP 7   or   %s INVEN.SHP 7 BRIDGE.SET\n", argv[0], argv[0]);
		return true;
	}

	Common::File file;
	if (!file.open(argv[1])) {
		debugPrintf("Could not open '%s'\n", argv[1]);
		return true;
	}

	// Read the whole container so it can both back the archive and be scanned
	// for the embedded palette.
	uint32 size = (uint32)file.size();
	Common::Array<byte> fileData(size);
	if (file.read(fileData.begin(), size) != size) {
		debugPrintf("Could not read '%s'\n", argv[1]);
		return true;
	}

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(fileData.begin(), size, DisposeAfterUse::NO), argv[1])) {
		debugPrintf("'%s' is not a valid LPPALPPA container\n", argv[1]);
		return true;
	}

	uint32 idx = (uint32)atoi(argv[2]);
	if (idx >= archive.getResourceCount()) {
		debugPrintf("Resource index %u out of range (%u resources)\n",
				idx, archive.getResourceCount());
		return true;
	}

	// For shapes the dimensions are packed into the resource info field.
	const Archive::Resource &res = archive.getResource(idx);
	uint16 width = (uint16)(res.info >> 16);
	uint16 height = (uint16)(res.info & 0xffff);

	Common::SeekableReadStream *stream = archive.createReadStreamForResource(idx);
	if (!stream) {
		debugPrintf("Resource %u is empty\n", idx);
		return true;
	}

	CelImage cel;
	bool ok = decodeCel(*stream, width, height, cel);
	delete stream;
	if (!ok) {
		debugPrintf("Resource %u (%ux%u) did not decode as a cel\n", idx, width, height);
		return true;
	}

	// Resolve a palette: from a separate container if supplied (inventory cels
	// are drawn against the active room palette), otherwise from this file.
	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	bool havePalette = false;
	if (argc >= 4) {
		Common::File palFile;
		if (palFile.open(argv[3])) {
			uint32 palSize = (uint32)palFile.size();
			Common::Array<byte> palData(palSize);
			if (palFile.read(palData.begin(), palSize) == palSize)
				havePalette = loadPalette(palData.begin(), palSize, rgb);
		}
		if (!havePalette)
			debugPrintf("No palette found in '%s'; falling back to '%s'\n", argv[3], argv[1]);
	}
	if (!havePalette)
		havePalette = loadPalette(fileData.begin(), size, rgb);

	debugPrintf("Resource %u: %ux%u, origin (%d,%d), palette %s\n", idx,
			cel.width, cel.height, cel.originX, cel.originY,
			havePalette ? "loaded" : "MISSING (grayscale)");
	if (!havePalette)
		for (int i = 0; i < 256; ++i)
			rgb[i * 3 + 0] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = (byte)i;

	// Blit centred on a black screen; transparent pixels show through as black.
	Graphics::Surface *screen = g_system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	int x0 = (kScreenWidth - cel.width) / 2;
	int y0 = (kScreenHeight - cel.height) / 2;
	for (int y = 0; y < cel.height; ++y) {
		for (int x = 0; x < cel.width; ++x) {
			if (!cel.isOpaque(x, y))
				continue;
			int sx = x0 + x, sy = y0 + y;
			if (sx >= 0 && sy >= 0 && sx < kScreenWidth && sy < kScreenHeight)
				*((byte *)screen->getBasePtr(sx, sy)) = cel.pixels[(uint)y * cel.width + x];
		}
	}
	g_system->unlockScreen();
	g_system->getPaletteManager()->setPalette(rgb, 0, 256);
	g_system->updateScreen();
	debugPrintf("Blitted. Close the console to view.\n");
	return true;
}

bool Console::cmdShowFrame(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Decodes a full-screen frame at a byte offset and blits it.\n");
		debugPrintf("Usage: %s <filename> <offset> [paletteFile]\n", argv[0]);
		debugPrintf("  offset accepts decimal or 0x-hex, and points at the frame's\n");
		debugPrintf("  16-bit height word (e.g. %s MOVIES/LOGO.MOV 0x42408)\n", argv[0]);
		return true;
	}

	Common::File file;
	if (!file.open(argv[1])) {
		debugPrintf("Could not open '%s'\n", argv[1]);
		return true;
	}

	// Read the whole file so it can both feed the decoder and be scanned for an
	// embedded palette.
	uint32 size = (uint32)file.size();
	Common::Array<byte> fileData(size);
	if (file.read(fileData.begin(), size) != size) {
		debugPrintf("Could not read '%s'\n", argv[1]);
		return true;
	}

	// Parse the offset as base-0 so both decimal and 0x-hex forms work.
	uint32 offset = (uint32)strtol(argv[2], nullptr, 0);
	if (offset >= size) {
		debugPrintf("Offset %u is past end of file (%u bytes)\n", offset, size);
		return true;
	}

	FrameImage frame;
	uint32 consumed = decodeFrame(fileData.begin() + offset, size - offset, frame);
	if (consumed == 0) {
		debugPrintf("No valid frame at offset 0x%x\n", offset);
		return true;
	}

	// Resolve a palette: from a separate container if supplied, otherwise scan
	// the frame's own file for an embedded CLUT.
	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	bool havePalette = false;
	if (argc >= 4) {
		Common::File palFile;
		if (palFile.open(argv[3])) {
			uint32 palSize = (uint32)palFile.size();
			Common::Array<byte> palData(palSize);
			if (palFile.read(palData.begin(), palSize) == palSize)
				havePalette = loadPalette(palData.begin(), palSize, rgb);
		}
		if (!havePalette)
			debugPrintf("No palette found in '%s'; falling back to '%s'\n", argv[3], argv[1]);
	}
	if (!havePalette)
		havePalette = loadPalette(fileData.begin(), size, rgb);

	debugPrintf("Frame at 0x%x: %ux%u, consumed %u bytes, palette %s\n", offset,
			frame.width, frame.height, consumed,
			havePalette ? "loaded" : "MISSING (grayscale)");
	if (!havePalette)
		for (int i = 0; i < 256; ++i)
			rgb[i * 3 + 0] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = (byte)i;

	// Blit centred on a black screen.
	Graphics::Surface *screen = g_system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	int x0 = (kScreenWidth - frame.width) / 2;
	int y0 = (kScreenHeight - frame.height) / 2;
	for (int y = 0; y < frame.height; ++y) {
		for (int x = 0; x < frame.width; ++x) {
			int sx = x0 + x, sy = y0 + y;
			if (sx >= 0 && sy >= 0 && sx < kScreenWidth && sy < kScreenHeight)
				*((byte *)screen->getBasePtr(sx, sy)) = frame.pixels[(uint)y * frame.width + x];
		}
	}
	g_system->unlockScreen();
	g_system->getPaletteManager()->setPalette(rgb, 0, 256);
	g_system->updateScreen();
	debugPrintf("Blitted. Close the console to view.\n");
	return true;
}

bool Console::cmdShowMovie(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Plays a MOV's frame sequence into a persistent framebuffer.\n");
		debugPrintf("Usage: %s <movfile> [frameIndex]\n", argv[0]);
		debugPrintf("  Decodes frames 0..frameIndex (default: the last frame) and\n");
		debugPrintf("  blits the composited result.  e.g. %s MOVIES/LOGO.MOV 150\n", argv[0]);
		return true;
	}

	Common::File file;
	if (!file.open(argv[1])) {
		debugPrintf("Could not open '%s'\n", argv[1]);
		return true;
	}

	uint32 size = (uint32)file.size();
	Common::Array<byte> fileData(size);
	if (file.read(fileData.begin(), size) != size) {
		debugPrintf("Could not read '%s'\n", argv[1]);
		return true;
	}

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(fileData.begin(), size, DisposeAfterUse::NO), argv[1])) {
		debugPrintf("'%s' is not a valid LPPALPPA container\n", argv[1]);
		return true;
	}

	// Video frames are the resources whose info tag is kFrameInfoTag; that tag
	// also doubles as the frame's {uint16 H, uint16 P} header, so the decoder
	// source is the 4 info bytes followed by the resource payload, i.e. the
	// bytes starting four bytes before the payload (dataOffset - 4).
	Common::Array<uint32> frameIndices;
	for (uint32 i = 0; i < archive.getResourceCount(); ++i) {
		const Archive::Resource &res = archive.getResource(i);
		if (!res.empty && res.info == kFrameInfoTag && res.dataOffset >= 4)
			frameIndices.push_back(i);
	}
	if (frameIndices.empty()) {
		debugPrintf("No video frames (info 0x%08x) found in '%s'\n", kFrameInfoTag, argv[1]);
		return true;
	}

	uint32 target = frameIndices.size() - 1;
	if (argc >= 3) {
		uint32 want = (uint32)atoi(argv[2]);
		if (want < frameIndices.size())
			target = want;
		else
			debugPrintf("Frame %u out of range; clamping to last (%u)\n", want, target);
	}

	// Apply each frame in order on top of the retained framebuffer.
	FrameSequence seq;
	for (uint32 f = 0; f <= target; ++f) {
		const Archive::Resource &res = archive.getResource(frameIndices[f]);
		if (seq.applyFrame(fileData.begin() + res.dataOffset - 4, res.length + 4) == 0) {
			debugPrintf("Frame %u failed to decode\n", f);
			return true;
		}
	}

	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	bool havePalette = loadPalette(fileData.begin(), size, rgb);

	debugPrintf("Movie '%s': %u frames, showing frame %u (%ux%u), palette %s\n",
			argv[1], frameIndices.size(), target, seq.width(), seq.height(),
			havePalette ? "loaded" : "MISSING (grayscale)");
	if (!havePalette)
		for (int i = 0; i < 256; ++i)
			rgb[i * 3 + 0] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = (byte)i;

	const byte *pixels = seq.pixels();
	Graphics::Surface *screen = g_system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	int x0 = (kScreenWidth - seq.width()) / 2;
	int y0 = (kScreenHeight - seq.height()) / 2;
	for (int y = 0; y < seq.height(); ++y) {
		for (int x = 0; x < seq.width(); ++x) {
			int sx = x0 + x, sy = y0 + y;
			if (sx >= 0 && sy >= 0 && sx < kScreenWidth && sy < kScreenHeight)
				*((byte *)screen->getBasePtr(sx, sy)) = pixels[(uint)y * seq.width() + x];
		}
	}
	g_system->unlockScreen();
	g_system->getPaletteManager()->setPalette(rgb, 0, 256);
	g_system->updateScreen();
	debugPrintf("Blitted. Close the console to view.\n");
	return true;
}

} // End of namespace Cyberflix

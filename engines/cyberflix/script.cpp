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

#include "common/stream.h"
#include "common/textconsole.h"

#include "cyberflix/script.h"

namespace Cyberflix {

Script::Script() : _valid(false), _terminated(false), _poolOffset(0), _defsScanned(false) {
}

bool Script::parse(Common::SeekableReadStream *stream) {
	_valid = false;
	_terminated = false;
	_poolOffset = 0;
	_code.clear();
	_payload.clear();

	if (!stream)
		return false;

	uint32 size = (uint32)stream->size();
	if (size < 8)
		return false;

	// Keep a private copy of the payload so pool strings can be resolved later.
	_payload.resize(size);
	stream->seek(0);
	if (stream->read(_payload.begin(), size) != size)
		return false;

	// Fixed 8-byte instructions: [u32 operandB][u16 opcode][u16 operandA] (LE),
	// terminated by opcode 0x0000. The pool follows immediately afterwards.
	//
	// The original evaluator is passed a pointer to the opcode field (record+4),
	// so a self-relative string reference is read as a 32-bit dword at opcode+2:
	// operandA supplies the low word and the following record's operandB supplies
	// the high word. Most scripts fit under 64 KiB and have a zero high word, but
	// BOOTFILE res2 needs this for definitions such as spotmovie.
	uint32 pos = 0;
	while (pos + 8 <= size) {
		Instruction inst;
		inst.operandB = READ_LE_UINT32(_payload.begin() + pos);
		inst.opcode = READ_LE_UINT16(_payload.begin() + pos + 4);
		inst.operandA = READ_LE_UINT16(_payload.begin() + pos + 6);
		pos += 8;

		if (inst.opcode == kOpEnd) {
			_terminated = true;
			break;
		}
		_code.push_back(inst);
	}

	_poolOffset = pos;
	_valid = true;
	return true;
}

Common::String Script::getPoolString(uint32 offset) const {
	if (offset >= _payload.size())
		return Common::String();

	uint32 len = _payload[offset];
	if (len == 0 || offset + 1 + len > _payload.size())
		return Common::String();

	Common::String result;
	for (uint32 i = 0; i < len; ++i) {
		byte c = _payload[offset + 1 + i];
		if (c < 32 || c > 126)
			return Common::String();
		result += (char)c;
	}
	return result;
}

uint32 Script::getSplitOperand(uint32 index) const {
	if (index >= _code.size())
		return 0;
	uint32 rel = _code[index].operandA;
	if (index + 1 < _code.size())
		rel |= (_code[index + 1].operandB & 0xffff) << 16;
	return rel;
}

Common::String Script::getSelfRelString(uint32 index) const {
	if (index >= _code.size())
		return Common::String();
	// The operand is relative to the instruction's opcode field, which sits 4
	// bytes into the 8-byte record (after the leading operandB dword).
	uint32 opcodeFieldOffset = index * 8 + 4;
	uint32 rel = getSplitOperand(index);
	return getPoolString(opcodeFieldOffset + rel);
}

void Script::neutralizeRange(uint32 first, uint32 last) {
	if (last >= _code.size())
		last = _code.size() - 1;
	for (uint32 i = first; i <= last; ++i) {
		_code[i].operandB = 0;
		_code[i].opcode = kOpPushInt;
		_code[i].operandA = 0;
	}
}

const char *Script::opcodeName(uint16 opcode) {
	switch (opcode) {
	case kOpEnd:     return "end";
	case kOpPush3:   return "push3";
	case kOpPush4:   return "push4";
	case kOpPushSym: return "pushSym";
	case kOpPushInt: return "pushInt";
	case kOpAdd:     return "add";
	case kOpSub:     return "sub";
	case kOpMul:     return "mul";
	case kOpDiv:     return "div";
	case kOpAnd:     return "and";
	case kOpOr:      return "or";
	case kOpConcat:  return "concat";
	case kOpEq:      return "eq";
	case kOpNe:      return "ne";
	case kOpGt:      return "gt";
	case kOpLt:      return "lt";
	case kOpGe:      return "ge";
	case kOpLe:      return "le";
	default:
		if (opcode >= kOpCmdBase && opcode < 0x1000)
			return "cmd";
		return "call";
	}
}


struct MethodName {
	uint16 opcode;
	const char *name;
};

// Generated from TI.EXE builtin name registration table (6-byte records
// [u32 name ptr][u16 opcode] at ~0x00459000) and dispatch tables. Do not edit
// by hand; this is the verbatim CyberFlix script builtin vocabulary.

static const MethodName kMethodNames[] = {
	{ 0x2ee1, "message" },
	{ 0x2ee2, "hidecursor" },
	{ 0x2ee3, "showcursor" },
	{ 0x2ee4, "delay" },
	{ 0x2ee5, "makeloop" },
	{ 0x2ee6, "walktostar" },
	{ 0x2ee7, "makecricket" },
	{ 0x2ee8, "exportclut" },
	{ 0x2ee9, "visualeffect" },
	{ 0x2eea, "stopwalk" },
	{ 0x2eeb, "stoploop" },
	{ 0x2eec, "stopcricket" },
	{ 0x2eed, "opencastfile" },
	{ 0x2eee, "closecastfile" },
	{ 0x2eef, "actorscript" },
	{ 0x2ef0, "sendtoactor" },
	{ 0x2ef1, "playmovie" },
	{ 0x2ef2, "openpuppetfile" },
	{ 0x2ef3, "opentrackfile" },
	{ 0x2ef4, "closetrackfile" },
	{ 0x2ef5, "playtheme" },
	{ 0x2ef6, "singlesound" },
	{ 0x2ef7, "multiplesound" },
	{ 0x2ef8, "dualsound" },
	{ 0x2ef9, "bothsound" },
	{ 0x2efa, "voicesound" },
	{ 0x2efb, "plugin" },
	{ 0x2efc, "haltsound" },
	{ 0x2efd, "halttheme" },
	{ 0x2efe, "haltvoice" },
	{ 0x2eff, "bootscript" },
	{ 0x2f00, "opensetfile" },
	{ 0x2f01, "closesetfile" },
	{ 0x2f02, "sendtoscene" },
	{ 0x2f03, "setscript" },
	{ 0x2f04, "scenescript" },
	{ 0x2f05, "paintingscript" },
	{ 0x2f06, "clut" },
	{ 0x2f07, "cursor" },
	{ 0x2f08, "debugger" },
	{ 0x2f09, "puppetclear" },
	{ 0x2f0a, "closepuppetfile" },
	{ 0x2f0b, "puppetspeak" },
	{ 0x2f0c, "puppetbevel" },
	{ 0x2f0d, "sendtopuppet" },
	{ 0x2f0e, "puppetscript" },
	{ 0x2f0f, "castscript" },
	{ 0x2f10, "sendtocast" },
	{ 0x2f11, "blacktoscreen" },
	{ 0x2f12, "screentoblack" },
	{ 0x2f13, "blackscreen" },
	{ 0x2f14, "forceupdate" },
	{ 0x2f15, "error" },
	{ 0x2f16, "propscript" },
	{ 0x2f17, "sendtoprop" },
	{ 0x2f18, "openshopfile" },
	{ 0x2f19, "closeshopfile" },
	{ 0x2f1a, "shopscript" },
	{ 0x2f1b, "sendtoshop" },
	{ 0x2f1c, "openstagefile" },
	{ 0x2f1d, "closestagefile" },
	{ 0x2f1e, "gotoflat" },
	{ 0x2f1f, "stagescript" },
	{ 0x2f20, "flatscript" },
	{ 0x2f21, "buttonscript" },
	{ 0x2f22, "sendtopainting" },
	{ 0x2f23, "sendtoset" },
	{ 0x2f24, "sendtobutton" },
	{ 0x2f25, "sendtoflat" },
	{ 0x2f26, "sendtostage" },
	{ 0x2f27, "quit" },
	{ 0x2f28, "turntodeg" },
	{ 0x2f29, "flushevents" },
	{ 0x2f2a, "puppetgrab" },
	{ 0x2f2b, "actorinstance" },
	{ 0x2f2c, "propinstance" },
	{ 0x2f2d, "savegame" },
	{ 0x2f2e, "opengame" },
	{ 0x2f2f, "notedialog" },
	{ 0x2f30, "drawstring" },
	{ 0x2f31, "sendtoboot" },
	{ 0x2f32, "mixclut" },
	{ 0x2f33, "puppetscramble" },
	{ 0x2f34, "puppetsubtitle" },
	{ 0x2f35, "actorwarm" },
	{ 0x2f36, "propwarm" },
	{ 0x2f37, "shopwarm" },
	{ 0x2f38, "castwarm" },
	{ 0x2f39, "postscript" },
	{ 0x2f3a, "sendtopost" },
	{ 0x2f3b, "serverscript" },
	{ 0x2f3c, "sendtoserver" },
	{ 0x2f3d, "actordelete" },
	{ 0x2f3e, "propdelete" },
	{ 0x2f3f, "netconnect" },
	{ 0x2f40, "netdisconnect" },
	{ 0x2f41, "netbroadcast" },
	{ 0x2f42, "netjoingroup" },
	{ 0x2f43, "netleavegroup" },
	{ 0x2f44, "walkonpath" },
	{ 0x2f45, "walktoxyz" },
	{ 0x2f46, "walkonroad" },
	{ 0x2f47, "walkonframes" },
	{ 0x2f48, "actorhide" },
	{ 0x2f49, "prophide" },
	{ 0x2f4a, "reboot" },
	{ 0x2f4b, "actorlock" },
	{ 0x2f4c, "proplock" },
	{ 0x2f4d, "shoplock" },
	{ 0x2f4e, "castlock" },
	{ 0x3e81, "actorvisible" },
	{ 0x3e82, "actordeg" },
	{ 0x3e83, "actorxyz" },
	{ 0x3e84, "actorxy" },
	{ 0x3e85, "actoris3d" },
	{ 0x3e86, "actorstar" },
	{ 0x3e87, "setvisible" },
	{ 0x3e88, "stagevisible" },
	{ 0x3e89, "path" },
	{ 0x3e8a, "result" },
	{ 0x3e8b, "currentview" },
	{ 0x3e8c, "actordist" },
	{ 0x3e8d, "propdist" },
	{ 0x3e8e, "actorpose" },
	{ 0x3e8f, "propvisible" },
	{ 0x3e90, "propdeg" },
	{ 0x3e91, "propxyz" },
	{ 0x3e92, "propxy" },
	{ 0x3e93, "propis3d" },
	{ 0x3e94, "propstar" },
	{ 0x3e95, "actorset" },
	{ 0x3e96, "framerate" },
	{ 0x3e97, "actorspeed" },
	{ 0x3e98, "actorscale" },
	{ 0x3e99, "propview" },
	{ 0x3e9a, "propspeed" },
	{ 0x3e9b, "propset" },
	{ 0x3e9c, "propscale" },
	{ 0x3e9d, "currentscene" },
	{ 0x3e9e, "variable" },
	{ 0x3e9f, "currentdeg" },
	{ 0x3ea0, "propowner" },
	{ 0x3ea1, "wavevolume" },
	{ 0x3ea2, "currentcd" },
	{ 0x3ea3, "camerahi" },
	{ 0x3ea4, "actorturn" },
	{ 0x3ea5, "menuvisible" },
	{ 0x3ea6, "soundpan" },
	{ 0x3ea7, "soundloop" },
	{ 0x3ea8, "soundvol" },
	{ 0x3ea9, "themevol" },
	{ 0x3eaa, "propvalue" },
	{ 0x3eab, "actorowner" },
	{ 0x3eac, "actorvalue" },
	{ 0x3ead, "keyaborts" },
	{ 0x3eae, "pauseloop" },
	{ 0x3eaf, "pausecricket" },
	{ 0x3eb0, "pausewalk" },
	{ 0x3eb1, "puppetparam" },
	{ 0x3eb2, "puppetvisible" },
	{ 0x3eb3, "actorzclip" },
	{ 0x3eb4, "propzclip" },
	{ 0x3eb5, "puppetbase" },
	{ 0x3eb6, "themepan" },
	{ 0x3eb7, "setparam" },
	{ 0x3eb8, "stageparam" },
	{ 0x4e21, "random" },
	{ 0x4e22, "pointx" },
	{ 0x4e23, "pointy" },
	{ 0x4e24, "makepoint" },
	{ 0x4e25, "button" },
	{ 0x4e26, "mouse" },
	{ 0x4e27, "stilldown" },
	{ 0x4e28, "tick" },
	{ 0x4e29, "iswalk" },
	{ 0x4e2a, "isloop" },
	{ 0x4e2b, "iscricket" },
	{ 0x4e2c, "countactors" },
	{ 0x4e2d, "indextoactor" },
	{ 0x4e2e, "countsounds" },
	{ 0x4e2f, "indextosound" },
	{ 0x4e30, "sounddone" },
	{ 0x4e31, "pointinpainting" },
	{ 0x4e32, "countpaintings" },
	{ 0x4e33, "countscenes" },
	{ 0x4e34, "indextoscene" },
	{ 0x4e35, "sendtopostfx" },
	{ 0x4e36, "indextopainting" },
	{ 0x4e37, "actorexists" },
	{ 0x4e38, "propexists" },
	{ 0x4e39, "stringtonum" },
	{ 0x4e3a, "numtostring" },
	{ 0x4e3b, "freemem" },
	{ 0x4e3c, "puppetevent" },
	{ 0x4e3d, "countcasts" },
	{ 0x4e3e, "indextocast" },
	{ 0x4e3f, "countprops" },
	{ 0x4e40, "indextoprop" },
	{ 0x4e41, "countshops" },
	{ 0x4e42, "indextoshop" },
	{ 0x4e43, "countflats" },
	{ 0x4e44, "indextoflat" },
	{ 0x4e45, "flattoindex" },
	{ 0x4e46, "currentflat" },
	{ 0x4e47, "pointinactor" },
	{ 0x4e48, "pointinprop" },
	{ 0x4e49, "pointinset" },
	{ 0x4e4a, "pointinstage" },
	{ 0x4e4b, "pointinbutton" },
	{ 0x4e4c, "countbuttons" },
	{ 0x4e4d, "indextobutton" },
	{ 0x4e4e, "countpuppets" },
	{ 0x4e4f, "indextopuppet" },
	{ 0x4e50, "currentstage" },
	{ 0x4e51, "currentpuppet" },
	{ 0x4e52, "type" },
	{ 0x4e53, "countglobals" },
	{ 0x4e54, "indextoglobal" },
	{ 0x4e55, "currentset" },
	{ 0x4e56, "findword" },
	{ 0x4e57, "substring" },
	{ 0x4e58, "stringlength" },
	{ 0x4e59, "putword" },
	{ 0x4e5a, "optionkey" },
	{ 0x4e5b, "shiftkey" },
	{ 0x4e5c, "commandkey" },
	{ 0x4e5d, "calcvectx" },
	{ 0x4e5e, "calcvecty" },
	{ 0x4e5f, "cameraxyz" },
	{ 0x4e60, "playerxyz" },
	{ 0x4e61, "machinetype" },
	{ 0x4e62, "machinespeed" },
	{ 0x4e63, "fileexists" },
	{ 0x4e64, "questiondialog" },
	{ 0x4e65, "textdialog" },
	{ 0x4e66, "hittest" },
	{ 0x4e67, "calcdeg" },
	{ 0x4e68, "calcturn" },
	{ 0x4e69, "starxyz" },
	{ 0x4e6a, "frame" },
	{ 0x4e6b, "counttracks" },
	{ 0x4e6c, "indextotrack" },
	{ 0x4e6d, "currentsound" },
	{ 0x4e6e, "currentvoice" },
	{ 0x4e6f, "currenttheme" },
	{ 0x4e70, "soundrate" },
	{ 0x4e71, "calcdist" },
	{ 0x4e72, "calcmod" },
	{ 0x4e73, "actionframe" },
	{ 0x4e74, "sendtoactorfx" },
	{ 0x4e75, "sendtoscenefx" },
	{ 0x4e76, "sendtopuppetfx" },
	{ 0x4e77, "sendtocastfx" },
	{ 0x4e78, "sendtopropfx" },
	{ 0x4e79, "sendtoshopfx" },
	{ 0x4e7a, "sendtopaintingfx" },
	{ 0x4e7b, "sendtosetfx" },
	{ 0x4e7c, "sendtobuttonfx" },
	{ 0x4e7d, "sendtoflatfx" },
	{ 0x4e7e, "sendtostagefx" },
	{ 0x4e7f, "sendtobootfx" },
	{ 0x4e80, "scenexyz" },
	{ 0x4e81, "voicedone" },
	{ 0x4e82, "pluginfx" },
	{ 0x4e83, "walkdest" },
	{ 0x4e84, "sendtoserverfx" },
	{ 0x4e85, "indextocricket" },
	{ 0x4e86, "indextoloop" },
	{ 0x4e87, "indextowalk" },
	{ 0x4e88, "countcrickets" },
	{ 0x4e89, "countloops" },
	{ 0x4e8a, "countwalks" },
	{ 0x4e8b, "countbevels" },
	{ 0x4e8c, "sqrt" },
	{ 0x4e8d, "pluginexists" },
	{ 0x4e8e, "networkon" },
	{ 0x4e8f, "netcountmembers" },
	{ 0x4e90, "netindextomember" },
	{ 0x4e91, "netourid" },
	{ 0x4e92, "netmessagesender" },
	{ 0x4e93, "netmessagegroup" },
	{ 0x4e94, "roadahead" },
	{ 0x4e95, "stringwidth" },
	{ 0x4e96, "countviews" },
	{ 0x4e97, "indextoview" },
	{ 0x4e98, "sysmem" },
	{ 0x4e99, "heapsize" },
	{ 0x5dc1, "barndoorclose" },
	{ 0x5dc2, "barndooropen" },
	{ 0x5dc3, "irisclose" },
	{ 0x5dc4, "irisopen" },
	{ 0x5dc5, "scrolldown" },
	{ 0x5dc6, "scrollup" },
	{ 0x5dc7, "scrollright" },
	{ 0x5dc8, "scrolleft" },
	{ 0x5dc9, "venetian" },
	{ 0x5dca, "wipedown" },
	{ 0x5dcb, "wipeup" },
	{ 0x5dcc, "wiperight" },
	{ 0x5dcd, "wipeleft" },
	{ 0x5dce, "plain" },
	{ 0x5dcf, "turnright" },
	{ 0x5dd0, "turnleft" },
	{ 0x5dd1, "turnup" },
	{ 0x5dd2, "turndown" },
	{ 0x5dd3, "nodraw" },
	{ 0x5dd4, "turnhalfleft" },
	{ 0x5dd5, "turnhalfright" },
};

const char *Script::methodName(uint16 opcode) {
int lo = 0, hi = (int)(sizeof(kMethodNames) / sizeof(kMethodNames[0])) - 1;
while (lo <= hi) {
int mid = (lo + hi) / 2;
uint16 m = kMethodNames[mid].opcode;
if (m == opcode)
return kMethodNames[mid].name;
if (m < opcode)
lo = mid + 1;
else
hi = mid - 1;
}
return nullptr;
}

uint8 Script::operatorPrecedence(uint16 opcode) {
	switch (opcode) {
	case kOpMul: case kOpDiv:                       return 0;
	case kOpAdd: case kOpSub:                        return 1;
	case kOpConcat:                                  return 2;
	case kOpGt: case kOpLt: case kOpGe: case kOpLe:  return 3;
	case kOpEq: case kOpNe:                          return 4;
	case kOpAnd:                                     return 5;
	case kOpOr:                                      return 6;
	default:                                         return 0xFF;
	}
}

int Script::findEndIfFrom(uint32 index) const {
	int depth = 0;
	for (uint32 i = index; i < _code.size(); ++i) {
		uint16 op = _code[i].opcode;
		if (op == kOpEnd || op == kOpReturn)
			return -1;
		if (op == kOpIf) {
			++depth;
		} else if (op == kOpEndIf) {
			if (depth == 0)
				return (int)i;
			--depth;
		}
	}
	return -1;
}

int Script::findMatchingElse(uint32 index) const {
	int depth = 0;
	for (uint32 i = index + 1; i < _code.size(); ++i) {
		uint16 op = _code[i].opcode;
		if (op == kOpEnd || op == kOpReturn)
			return -1;
		if (op == kOpIf) {
			++depth;
		} else if (op == kOpEndIf) {
			if (depth == 0)
				return -1;
			--depth;
		} else if (op == kOpElse && depth == 0) {
			return (int)(i + 1);
		}
	}
	return -1;
}

int Script::findEndWhileFrom(uint32 index) const {
	int depth = 0;
	for (uint32 i = index; i < _code.size(); ++i) {
		uint16 op = _code[i].opcode;
		if (op == kOpEnd || op == kOpReturn)
			return -1;
		if (op == kOpWhile) {
			++depth;
		} else if (op == kOpEndWhile) {
			if (depth == 0)
				return (int)i;
			--depth;
		}
	}
	return -1;
}

int Script::findEndSwitchFrom(uint32 index) const {
	// TI.EXE FUN_0040c610: scan to the 0xfaa closing the current switch,
	// nesting on 0xfa9; 0xfa4/stream-end aborts (err 0x1f/0x1b there).
	int depth = 0;
	for (uint32 i = index; i < _code.size(); ++i) {
		uint16 op = _code[i].opcode;
		if (op == kOpEnd || op == kOpReturn)
			return -1;
		if (op == kOpSwitch) {
			++depth;
		} else if (op == kOpEndSwitch) {
			if (depth == 0)
				return (int)i;
			--depth;
		}
	}
	return -1;
}

int Script::findForNextFrom(uint32 index) const {
	int depth = 0;
	for (uint32 i = index; i < _code.size(); ++i) {
		uint16 op = _code[i].opcode;
		if (op == kOpEnd || op == kOpReturn)
			return -1;
		if (op == kOpFor) {
			++depth;
		} else if (op == kOpForNext) {
			if (depth == 0)
				return (int)i;
			--depth;
		}
	}
	return -1;
}

int Script::findCloseParen(uint32 openIndex) const {
	// openIndex must reference a kOpOpenParen; walk forward matching nested
	// parens. Unlike the block scanners this does not stop on kOpEnd/kOpReturn
	// because an argument list never spans a statement boundary (TI.EXE
	// 0x0040b690 simply balances kOpOpenParen/kOpCloseParen).
	int depth = 0;
	for (uint32 i = openIndex; i < _code.size(); ++i) {
		uint16 op = _code[i].opcode;
		if (op == kOpOpenParen) {
			++depth;
		} else if (op == kOpCloseParen) {
			--depth;
			if (depth == 0)
				return (int)i;
		}
	}
	return -1;
}

const Common::Array<Script::Definition> &Script::definitions() const {
	// Lazily index the named definitions. The runtime re-scans the resource on
	// every dispatch (TI.EXE 0x0040b7a0 walks kOpScriptMarker separators and
	// 0x0040b870 parses each header); scripts are immutable here, so cache.
	if (_defsScanned)
		return _defs;
	_defsScanned = true;

	const uint32 n = _code.size();
	uint32 i = 0;
	while (i < n) {
		// A definition header is `pushSym 'name' ( params... )`; anything else
		// after a marker (or at the top) is not a definition (e.g. data pads).
		if (_code[i].opcode == kOpScriptMarker) {
			++i;
			continue;
		}
		uint32 headerAt = i;
		// Advance i to the next marker regardless of parse success.
		uint32 next = headerAt;
		while (next < n && _code[next].opcode != kOpScriptMarker)
			++next;

		if (_code[headerAt].opcode == kOpPushSym &&
				headerAt + 1 < n && _code[headerAt + 1].opcode == kOpOpenParen) {
			Definition def;
			def.name = getSelfRelString(headerAt);
			def.name.toLowercase();
			int close = findCloseParen(headerAt + 1);
			if (!def.name.empty() && close >= 0 && (uint32)close < next) {
				// Formal parameters: symbols separated by kOpArgSep.
				for (uint32 p = headerAt + 2; p < (uint32)close; ++p) {
					if (_code[p].opcode == kOpPushSym) {
						Common::String pn = getSelfRelString(p);
						pn.toLowercase();
						def.params.push_back(pn);
					}
				}
				// The body starts after the header's trailing pushInt padding
				// (the matcher 0x0040b870 skips pushInt records before the
				// first statement).
				uint32 body = (uint32)close + 1;
				while (body < next && _code[body].opcode == kOpPushInt)
					++body;
				def.bodyStart = body;
				_defs.push_back(def);
			}
		}
		i = next;
	}
	return _defs;
}

const Script::Definition *Script::findDefinition(const Common::String &name) const {
	// Case-insensitive match, mirroring the Pascal-string compare the matcher
	// uses (TI.EXE 0x0041ae80).
	const Common::Array<Definition> &defs = definitions();
	for (uint32 i = 0; i < defs.size(); ++i) {
		if (defs[i].name.equalsIgnoreCase(name))
			return &defs[i];
	}
	return nullptr;
}

} // End of namespace Cyberflix

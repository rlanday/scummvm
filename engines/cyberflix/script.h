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

#ifndef CYBERFLIX_SCRIPT_H
#define CYBERFLIX_SCRIPT_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace Common {
class SeekableReadStream;
}

namespace Cyberflix {

/**
 * Parser for the CyberFlix script resources (directory entries whose @c info
 * tag is 0x0FA1), found in BOOTFILE and in the .STG / .SET containers.
 *
 * A script resource is a flat array of fixed 8-byte instructions followed by a
 * Pascal-string pool. Each instruction is little-endian:
 *
 *   +0x00  uint32  operandB  (frequently 0)
 *   +0x04  uint16  opcode
 *   +0x06  uint16  operandA
 *
 * The instruction stream is terminated by an opcode of 0x0000; the pool of
 * length-prefixed (Pascal) symbol and string-constant names begins immediately
 * after. This layout has been validated across all 141 script resources on the
 * two retail CDs (every one terminates cleanly at the pool boundary).
 *
 * Opcode families observed:
 *   0x0003..0x0006  stack pushes (0x0006 = push integer in operandA)
 *   0x0Fxx          built-in engine commands
 *   0x1Fxx..0x5Dxx  handler / method dispatch (long tail of distinct values)
 *
 * The precise semantics of each opcode are still being mapped; this class
 * provides faithful structural decoding and a disassembly aid to drive that
 * work, and will back the interpreter once the opcode set is pinned down.
 */
class Script {
public:
	enum {
		kOpEnd     = 0x0000, ///< Terminates the instruction stream.
		kOpPush3   = 0x0003, ///< String literal (self-relative pool offset).
		kOpPush4   = 0x0004, ///< 32-bit int literal: operandA | (next operandB low16 << 16).
		kOpPushSym = 0x0005, ///< Push symbol/variable reference.
		kOpPushInt = 0x0006, ///< Push integer constant (operandA); statement separator.
		kOpCmdBase = 0x0FA0, ///< Start of the built-in command range.

		// Control-flow builtins dispatched by the main loop's small switch
		// (TI.EXE 0x0040ba4f, map 0x40c458; statement executor 0x0040ba20).
		// See files/opcode-map.md section 3 and decomp/stage-notes.md.
		kOpScriptMarker = 0x0FA1, ///< Separates named definitions in a resource.
		kOpDeclGlobal   = 0x0FA2, ///< Declare var list in the global scope (0x45f010).
		kOpDeclLocal    = 0x0FA3, ///< Declare var list in the current local scope.
		kOpReturn  = 0x0FA4, ///< End of body, no result; requires balanced blocks.
		kOpExit    = 0x0FA5, ///< Early return, no result (unwinds loop depth).
		kOpIf      = 0x0FA6, ///< Evaluate condition; enter or skip THEN block.
		kOpEndIf   = 0x0FA7, ///< Close an if/then(/else) block.
		kOpElse    = 0x0FA8, ///< Else marker; skipped after the THEN ran.
		kOpSwitch  = 0x0FA9, ///< Switch head: eval selector, jump to matching case body.
		kOpEndSwitch = 0x0FAA, ///< Switch end marker (executed: just continue).
		kOpCase    = 0x0FAB, ///< Case label + value expr; executed = break to past kOpEndSwitch.
		kOpFor     = 0x0FAC, ///< For-loop setup (loop var + start bound).
		kOpForTo   = 0x0FAD, ///< For-loop bound separator ("to").
		kOpForNext = 0x0FAF, ///< For-loop iterate/close.
		kOpWhile   = 0x0FB0, ///< While: test condition, enter or skip body.
		kOpEndWhile = 0x0FB1, ///< While close: re-test and loop or exit.
		kOpOpenParen  = 0x0FB2, ///< Open a call argument list: name '(' args ')'.
		kOpCloseParen = 0x0FB3, ///< Close a call argument list.
		kOpArgSep     = 0x0FB4, ///< Argument separator (',') inside a call.
		kOpTrue    = 0x0FB5, ///< Boolean TRUE literal atom (0x41a550 case 0xfb5).
		kOpFalse   = 0x0FB6, ///< Boolean FALSE literal atom.
		kOpNot     = 0x0FB7, ///< Prefix boolean NOT (applies to following atom).
		kOpReturnValue = 0x0FB8, ///< return <expr>: yields the message's result.
		kOpPass    = 0x0FB9, ///< Pretend unhandled: dispatcher tries next scope.

		// Infix operator opcodes, applied by the TI.EXE evaluator
		// (applier 0x00419f30, jump table 0x41a484). See files/opcode-map.md.
		kOpAdd     = 0x1F41, ///< int: lhs + rhs.
		kOpSub     = 0x1F42, ///< int: lhs - rhs.
		kOpMul     = 0x1F43, ///< int: lhs * rhs.
		kOpDiv     = 0x1F44, ///< int: lhs / rhs (error on divide by zero).
		kOpAnd     = 0x1F45, ///< bool: lhs & rhs.
		kOpOr      = 0x1F46, ///< bool: lhs | rhs.
		kOpConcat  = 0x1F47, ///< string: concat(lhs, rhs).
		kOpEq      = 0x1F48, ///< any: lhs == rhs -> bool.
		kOpNe      = 0x1F49, ///< any: lhs != rhs -> bool.
		kOpGt      = 0x1F4A, ///< int: lhs > rhs -> bool.
		kOpLt      = 0x1F4B, ///< int: lhs < rhs -> bool.
		kOpGe      = 0x1F4C, ///< int: lhs >= rhs -> bool.
		kOpLe      = 0x1F4D, ///< int: lhs <= rhs -> bool.

		kOpOperatorFirst = 0x1F41,
		kOpOperatorLast  = 0x1F4D
	};

	enum MethodOpcode {
		kMethodMessage          = 0x2EE1,
		kMethodMakeLoop         = 0x2EE5,
		kMethodMakeCricket      = 0x2EE7,
		kMethodVisualEffect     = 0x2EE9,
		kMethodStopLoop         = 0x2EEB,
		kMethodStopCricket      = 0x2EEC,
		kMethodOpenCastFile     = 0x2EED,
		kMethodCloseCastFile    = 0x2EEE,
		kMethodSendToActor      = 0x2EF0,
		kMethodPlayMovie        = 0x2EF1,
		kMethodOpenPuppetFile   = 0x2EF2,
		kMethodOpenTrackFile    = 0x2EF3,
		kMethodCloseTrackFile   = 0x2EF4,
		kMethodPlayTheme        = 0x2EF5,
		kMethodSingleSound      = 0x2EF6,
		kMethodMultipleSound    = 0x2EF7,
		kMethodDualSound        = 0x2EF8,
		kMethodBothSound        = 0x2EF9,
		kMethodVoiceSound       = 0x2EFA,
		kMethodHaltSound        = 0x2EFC,
		kMethodHaltTheme        = 0x2EFD,
		kMethodHaltVoice        = 0x2EFE,
		kMethodOpenSetFile      = 0x2F00,
		kMethodCloseSetFile     = 0x2F01,
		kMethodSendToScene      = 0x2F02,
		kMethodClut             = 0x2F06,
		kMethodCursor           = 0x2F07,
		kMethodPuppetClear      = 0x2F09,
		kMethodClosePuppetFile  = 0x2F0A,
		kMethodPuppetSpeak      = 0x2F0B,
		kMethodPuppetBevel      = 0x2F0C,
		kMethodSendToPuppet     = 0x2F0D,
		kMethodPuppetScript     = 0x2F0E,
		kMethodSendToCast       = 0x2F10,
		kMethodBlackToScreen    = 0x2F11,
		kMethodScreenToBlack    = 0x2F12,
		kMethodBlackScreen      = 0x2F13,
		kMethodForceUpdate      = 0x2F14,
		kMethodSendToProp       = 0x2F17,
		kMethodOpenShopFile     = 0x2F18,
		kMethodCloseShopFile    = 0x2F19,
		kMethodSendToShop       = 0x2F1B,
		kMethodOpenStageFile    = 0x2F1C,
		kMethodCloseStageFile   = 0x2F1D,
		kMethodGotoFlat         = 0x2F1E,
		kMethodSendToPainting   = 0x2F22,
		kMethodSendToButton     = 0x2F24,
		kMethodSendToFlat       = 0x2F25,
		kMethodSendToStage      = 0x2F26,
		kMethodQuit             = 0x2F27,
		kMethodFlushEvents      = 0x2F29,
		kMethodPuppetGrab       = 0x2F2A,
		kMethodSaveGame         = 0x2F2D,
		kMethodOpenGame         = 0x2F2E,
		kMethodDrawString       = 0x2F30,
		kMethodSendToBoot       = 0x2F31,
		kMethodActorVisible     = 0x3E81,
		kMethodActorDeg         = 0x3E82,
		kMethodActorXYZ         = 0x3E83,
		kMethodActorStar        = 0x3E86,
		kMethodSetVisible       = 0x3E87,
		kMethodStageVisible     = 0x3E88,
		kMethodPath             = 0x3E89,
		kMethodResult           = 0x3E8A,
		kMethodCurrentView      = 0x3E8B,
		kMethodPropDist         = 0x3E8D,
		kMethodActorPose        = 0x3E8E,
		kMethodPropVisible      = 0x3E8F,
		kMethodPropDeg          = 0x3E90,
		kMethodPropXYZ          = 0x3E91,
		kMethodPropXY           = 0x3E92,
		kMethodActorSet         = 0x3E95,
		kMethodFrameRate        = 0x3E96,
		kMethodActorSpeed       = 0x3E97,
		kMethodActorScale       = 0x3E98,
		kMethodPropView         = 0x3E99,
		kMethodPropSet          = 0x3E9B,
		kMethodPropScale        = 0x3E9C,
		kMethodCurrentScene     = 0x3E9D,
		kMethodPropOwner        = 0x3EA0,
		kMethodWaveVolume       = 0x3EA1,
		kMethodCurrentCD        = 0x3EA2,
		kMethodActorTurn        = 0x3EA4,
		kMethodSoundVol         = 0x3EA8,
		kMethodThemeVol         = 0x3EA9,
		kMethodPropValue        = 0x3EAA,
		kMethodActorOwner       = 0x3EAB,
		kMethodActorValue       = 0x3EAC,
		kMethodKeyAborts        = 0x3EAD,
		kMethodPauseLoop        = 0x3EAE,
		kMethodPauseCricket     = 0x3EAF,
		kMethodPuppetParam      = 0x3EB1,
		kMethodPuppetVisible    = 0x3EB2,
		kMethodActorZClip       = 0x3EB3,
		kMethodPropZClip        = 0x3EB4,
		kMethodPuppetBase       = 0x3EB5,
		kMethodRandom           = 0x4E21,
		kMethodPointX           = 0x4E22,
		kMethodPointY           = 0x4E23,
		kMethodMakePoint        = 0x4E24,
		kMethodButton           = 0x4E25,
		kMethodMouse            = 0x4E26,
		kMethodStillDown        = 0x4E27,
		kMethodTick             = 0x4E28,
		kMethodCountActors      = 0x4E2C,
		kMethodIndexToActor     = 0x4E2D,
		kMethodPointInPainting  = 0x4E31,
		kMethodCountPaintings   = 0x4E32,
		kMethodIndexToPainting  = 0x4E36,
		kMethodStringToNum      = 0x4E39,
		kMethodNumToString      = 0x4E3A,
		kMethodPuppetEvent      = 0x4E3C,
		kMethodCountProps       = 0x4E3F,
		kMethodIndexToProp      = 0x4E40,
		kMethodCurrentFlat      = 0x4E46,
		kMethodPointInButton    = 0x4E4B,
		kMethodCountPuppets     = 0x4E4E,
		kMethodIndexToPuppet    = 0x4E4F,
		kMethodCurrentStage     = 0x4E50,
		kMethodCurrentPuppet    = 0x4E51,
		kMethodCurrentSet       = 0x4E55,
		kMethodFindWord         = 0x4E56,
		kMethodStringLength     = 0x4E58,
		kMethodOptionKey        = 0x4E5A,
		kMethodQuestionDialog   = 0x4E64,
		kMethodHitTest          = 0x4E66,
		kMethodCalcDeg          = 0x4E67,
		kMethodCurrentSound     = 0x4E6D,
		kMethodCurrentVoice     = 0x4E6E,
		kMethodCurrentTheme     = 0x4E6F,
		kMethodActionFrame      = 0x4E73,
		kMethodSendToActorFx    = 0x4E74,
		kMethodSendToSceneFx    = 0x4E75,
		kMethodSendToPuppetFx   = 0x4E76,
		kMethodSendToCastFx     = 0x4E77,
		kMethodSendToPropFx     = 0x4E78,
		kMethodSendToShopFx     = 0x4E79,
		kMethodSendToPaintingFx = 0x4E7A,
		kMethodSendToSetFx      = 0x4E7B,
		kMethodSendToButtonFx   = 0x4E7C,
		kMethodSendToFlatFx     = 0x4E7D,
		kMethodSendToStageFx    = 0x4E7E,
		kMethodSendToBootFx     = 0x4E7F,
		kMethodRoadAhead        = 0x4E94
	};

	/** Resource @c info tag identifying a script resource (BOOTFILE, .SET, ...). */
	static const uint32 kScriptInfoTag = 0x0FA1;

	/** True if @p opcode is an infix binary operator (0x1F41..0x1F4D). */
	static bool isOperator(uint16 opcode) {
		return opcode >= kOpOperatorFirst && opcode <= kOpOperatorLast;
	}

	/**
	 * Precedence class of an infix operator (0 binds tightest, 6 loosest).
	 *
	 * Recovered from the TI.EXE classifier at vaddr 0x0041a4c0 (byte map
	 * 0x41a534, value table 0x41a514). Returns 0xFF for non-operators.
	 */
	static uint8 operatorPrecedence(uint16 opcode);

	/**
	 * Index of the kOpEndIf that closes the if-block opened at @p index (which
	 * must be a kOpIf), accounting for nested ifs, or -1 if not found.
	 */
	int findMatchingEndIf(uint32 index) const { return findEndIfFrom(index + 1); }

	/**
	 * Index of the next kOpEndIf at the current nesting level, scanning forward
	 * from @p index which is already inside an if/then or else body. Mirrors the
	 * scanner at TI.EXE 0x0040c550; aborts (-1) on end/return.
	 */
	int findEndIfFrom(uint32 index) const;

	/**
	 * If the if-block opened at @p index (a kOpIf) has an else clause, return the
	 * index of the first instruction after its kOpElse; if the matching kOpEndIf
	 * is reached first (no else), return -1. Mirrors TI.EXE 0x0040c6d0.
	 */
	int findMatchingElse(uint32 index) const;

	/**
	 * Index of the kOpEndWhile at the current nesting level, scanning forward
	 * from @p index inside a while body (counting nested kOpWhile). Mirrors the
	 * scanner at TI.EXE 0x0040c5b0; aborts (-1) on end/return.
	 */
	int findEndWhileFrom(uint32 index) const;

	/**
	 * Scans forward from @p index for the kOpEndSwitch closing the current
	 * switch, tracking nested kOpSwitch blocks (TI.EXE FUN_0040c610).
	 * @return instruction index of the kOpEndSwitch, or -1.
	 */
	int findEndSwitchFrom(uint32 index) const;

	/**
	 * Index of the kOpForNext at the current nesting level, scanning forward from
	 * @p index inside a for body (counting nested kOpFor), or -1.
	 */
	int findForNextFrom(uint32 index) const;

	/**
	 * Given @p openIndex pointing at a kOpOpenParen, return the index of the
	 * matching kOpCloseParen, counting nested parentheses. Mirrors the balanced
	 * argument-list span scanner at TI.EXE 0x0040b690 (called from the atom
	 * decoder 0x0041a626 to size a call). Returns -1 if unbalanced.
	 */
	int findCloseParen(uint32 openIndex) const;

	/**
	 * A named function/handler definition inside a script resource. A resource
	 * holds multiple definitions separated by kOpScriptMarker instructions;
	 * each definition is shaped
	 *
	 *   pushSym 'name' ( param1 , param2 ... ) pad... body... return
	 *
	 * The runtime resolves a call `name(args)` by scanning these definitions
	 * (per-scope runner TI.EXE 0x0040b7a0, matcher 0x0040b870) and matching the
	 * name case-insensitively (0x0041ae80). See decomp/stage-notes.md.
	 */
	struct Definition {
		Common::String name;                ///< Definition name (lowercased).
		Common::Array<Common::String> params; ///< Formal parameter names, in order.
		uint32 bodyStart;                   ///< Instruction index of the first body statement.
	};

	/**
	 * The definitions in this resource, in declaration order. Built lazily on
	 * first use by scanning for kOpScriptMarker separators (mirrors the per-call
	 * scan at TI.EXE 0x0040b7a0; cached because scripts are immutable).
	 */
	const Common::Array<Definition> &definitions() const;

	/** Find a definition by case-insensitive name, or nullptr. */
	const Definition *findDefinition(const Common::String &name) const;

	struct Instruction {
		uint32 operandB;
		uint16 opcode;
		uint16 operandA;
	};

	Script();

	/** Parse a script resource payload. Does not take ownership of @p stream. */
	bool parse(Common::SeekableReadStream *stream);

	bool isValid() const { return _valid; }
	uint32 getInstructionCount() const { return _code.size(); }
	const Instruction &getInstruction(uint32 i) const { return _code[i]; }

	/**
	 * Overwrite instructions [@p first, @p last] (inclusive) with harmless
	 * integer-push padding (kOpPushInt 0), which the statement loop treats as a
	 * no-op separator. This is a structural editing primitive used by the engine
	 * to special-case individual scripts (e.g. excising the boot script's CD
	 * presence check so the game can run from an installed directory) without
	 * altering any shared opcode/method semantics.
	 */
	void neutralizeRange(uint32 first, uint32 last);

	/** True if the instruction stream ended with an explicit 0x0000. */
	bool isTerminated() const { return _terminated; }

	/** Offset (within the payload) where the string pool begins. */
	uint32 getPoolOffset() const { return _poolOffset; }

	/** Read the Pascal string at @p offset within the payload, or "". */
	Common::String getPoolString(uint32 offset) const;

	/**
	 * Return the 32-bit operand dword read by TI.EXE from opcode+2.
	 *
	 * In the on-disk 8-byte record [u32 operandB][u16 opcode][u16 operandA],
	 * the dword starts at operandA and continues into the low half of the next
	 * record's operandB. This split operand is used both for self-relative
	 * string references and 32-bit integer literals.
	 */
	uint32 getSplitOperand(uint32 index) const;

	/**
	 * Resolve the self-relative symbol name referenced by instruction @p index.
	 *
	 * pushSym (0x0005) and related operand atoms encode the symbol name as a
	 * Pascal string located at (opcodeFieldOffset + getSplitOperand(index)),
	 * i.e. the operand is a byte offset relative to the instruction's own opcode
	 * field. This was recovered from the TI.EXE evaluator (vaddr 0x00419cc0:
	 * "mov ecx,[eax+2]; add ecx,eax") and verified against BOOTFILE res2
	 * definitions whose pool lies beyond 64 KiB (spotmovie, premovie, ...).
	 */
	Common::String getSelfRelString(uint32 index) const;
	const Common::String &getSelfRelStringRef(uint32 index) const;

	/**
	 * Lowercase form of getSelfRelString(@p index), cached per instruction.
	 * ScummVM uses this for case-insensitive VM keys (dispatch names,
	 * variables, formal params) so hot script paths do not repeatedly scan and
	 * fold the same immutable pool strings.
	 */
	Common::String getSelfRelStringLowercase(uint32 index) const;
	const Common::String &getSelfRelStringLowercaseRef(uint32 index) const;

	/** Human-readable mnemonic for @p opcode. */
	static const char *opcodeName(uint16 opcode);

	/**
	 * Name of the CyberFlix builtin method @p opcode, or nullptr if unknown.
	 *
	 * Recovered from the TI.EXE name registration table (6-byte records
	 * [u32 name ptr][u16 opcode] at ~0x00459000). These are the hardcoded
	 * builtin method IDs dispatched by the VM (e.g. clut, openset, frame).
	 */
	static const char *methodName(uint16 opcode);

private:
	bool _valid;
	bool _terminated;
	uint32 _poolOffset;
	Common::Array<Instruction> _code;
	Common::Array<byte> _payload;

	// ScummVM-only optimization: native TI.EXE resolves self-relative pool
	// strings while evaluating each atom, but parsed script resources are
	// immutable here, so repeated idle/mouse dispatches can reuse the decoded
	// strings without changing VM semantics.
	mutable Common::Array<Common::String> _selfRelStringCache;
	mutable Common::Array<byte> _selfRelStringCached;
	mutable Common::Array<Common::String> _selfRelLowerStringCache;
	mutable Common::Array<byte> _selfRelLowerStringCached;
	mutable bool _defsScanned; ///< Lazy definitions() cache flag.
	mutable Common::Array<Definition> _defs;
	// Definition names are immutable after parsing, so dispatch can use this
	// index instead of linearly scanning every handler on each nested sendto*.
	mutable Common::HashMap<Common::String, uint32> _defIndexByName;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_SCRIPT_H

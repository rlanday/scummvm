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

#ifndef CYBERFLIX_SOUND_H
#define CYBERFLIX_SOUND_H

#include "common/scummsys.h"
#include "common/array.h"

namespace Cyberflix {

/**
 * MOV audio. The soundtrack is NOT the per-frame @c info==0x6 chunks (those
 * carry something else); it lives in the resources tagged @c kAudioResourceInfoTag.
 * Each such resource is a "cbx" ADPCM stream reversed from TI.EXE: the servicer
 * thread (@c 0x0042ea90 -> @c FUN_0042ebb0) decodes it block-by-block into the
 * DirectSound ring with @c FUN_00430480 (22050 Hz) or @c FUN_00430590 (11025 Hz,
 * sample-doubled). See decodeCbxAudio() and files/audio-re-notes.md.
 *
 * The runtime queues several such resources back-to-back (the servicer walks a
 * node chain); concatenating a movie's 22050 Hz resources in directory order
 * reproduces its continuous soundtrack. The 11025 Hz resources are short
 * effects triggered individually by the dialogue/SFX players, not part of the
 * linear track, so the movie player skips them. The DirectSound output format
 * (WAVEFORMATEX built at @c 0x0042e870) is @c kAudioSampleRate Hz, 8-bit mono.
 */
static const uint32 kAudioResourceInfoTag = 0x00010000;
static const int kAudioSampleRate = 22050;

/** Sample-rate field value selecting the native (non-upsampled) 22050 Hz path. */
static const uint32 kAudioRate22050 = 0x5622;

/**
 * Master header resource tag (@c info==0x40000). A linear movie's first
 * resource. Among other things it references two cue tables that drive audio:
 * a MUSIC table (continuous score, played from t=0) and an SFX/event table
 * (named one-shots triggered by individual video frames). The engine reads the
 * header through a data pointer at @c record+8 (== payload-4), so a field at
 * engine byte offset @c X is at @c payload[X-4]. See
 * files/decomp/movie-playback.md for the full reverse-engineering record.
 */
static const uint32 kMasterHeaderInfoTag = 0x00040000;

struct CbxAudioInfo {
	uint32 blockSamples = 0;
	uint32 blockCount = 0;
};

/**
 * Return the decoded block geometry for a cbx audio resource without decoding
 * it. This mirrors the native servicer's block-at-a-time traversal of the
 * offset table.
 */
CbxAudioInfo getCbxAudioInfo(const byte *payload, uint32 payloadLen);

/**
 * Decode one cbx block into @p out. @p out must hold at least
 * CbxAudioInfo::blockSamples bytes for this resource.
 */
uint32 decodeCbxAudioBlock(const byte *payload, uint32 payloadLen, uint32 blockIndex,
		byte *out, uint32 outSize);

/**
 * Decode a CyberFlix "cbx" audio resource (MOV @c info==kAudioResourceInfoTag)
 * into 8-bit unsigned mono PCM at @c kAudioSampleRate, appending the samples to
 * @p out. Reversed byte-for-byte from TI.EXE @c FUN_00430480 (22050 Hz) and
 * @c FUN_00430590 (11025 Hz, each sample emitted twice to upsample). See
 * files/audio-re-notes.md for the full derivation.
 *
 * The resource payload (the @c Archive::Resource @c dataOffset pointer) holds a
 * header followed by an offset table and the encoded blocks. Relative to the
 * decoder base @c B (== @c payload-4): rate @c B+0x1c, per-block output size
 * @c B+0x20 (== 1024 bytes), block count @c B+0x28, and a @c uint32 offset
 * table at @c B+0x2c giving each block's start (also relative to @c B). Each
 * block decodes to a fixed output size via a control-byte scheme: literal
 * (@c 0x00-0x7f), 4-bit DPCM delta-run (@c 0x80-0xbf, signed nibble residuals),
 * and predictor repeat-run (@c 0xc0-0xff).
 *
 * @param payload     Resource payload start (must have >= 4 valid bytes before it).
 * @param payloadLen  Bytes available at @p payload.
 * @param out         Receives the decoded PCM (appended).
 * @return number of sample bytes appended.
 */
uint32 decodeCbxAudio(const byte *payload, uint32 payloadLen, Common::Array<byte> &out);

} // End of namespace Cyberflix

#endif

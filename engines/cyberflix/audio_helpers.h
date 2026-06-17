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

#ifndef CYBERFLIX_AUDIO_HELPERS_H
#define CYBERFLIX_AUDIO_HELPERS_H

#include "common/array.h"
#include "common/scummsys.h"

#include "audio/audiostream.h"
#include "audio/decoders/raw.h"

#include "cyberflix/cbx_audio.h"

namespace Cyberflix {

inline Audio::SeekableAudioStream *makeOwnedRawPcmStream(const Common::Array<byte> &pcm) {
	if (pcm.empty())
		return nullptr;

	byte *buf = (byte *)malloc(pcm.size());
	if (!buf)
		return nullptr;
	memcpy(buf, pcm.begin(), pcm.size());

	Audio::SeekableAudioStream *stream = Audio::makeRawStream(buf, pcm.size(),
			kAudioSampleRate, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
	if (!stream)
		free(buf);
	return stream;
}

} // End of namespace Cyberflix

#endif

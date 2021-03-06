/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2015 Synergy Si Inc.
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "synergy/ClipboardChunk.h"

#include "synergy/ProtocolUtil.h"
#include "synergy/protocol_types.h"
#include "io/IStream.h"
#include "base/Log.h"

ClipboardChunk::ClipboardChunk(size_t size) :
	Chunk(size)
{
		m_dataSize = size - CLIPBOARD_CHUNK_META_SIZE;
}

ClipboardChunk*
ClipboardChunk::start(
					ClipboardID id,
					UInt32 sequence,
					const String& size)
{
	size_t sizeLength = size.size();
	ClipboardChunk* start = new ClipboardChunk(sizeLength + CLIPBOARD_CHUNK_META_SIZE);
	char* chunk = start->m_chunk;

	chunk[0] = id;
	UInt32* seq = reinterpret_cast<UInt32*>(&chunk[1]);
	*seq = sequence;
	chunk[5] = kDataStart;
	memcpy(&chunk[6], size.c_str(), sizeLength);
	chunk[sizeLength + CLIPBOARD_CHUNK_META_SIZE - 1] = '\0';

	return start;
}

ClipboardChunk*
ClipboardChunk::data(
					ClipboardID id,
					UInt32 sequence,
					const String& data)
{
	size_t dataSize = data.size();
	ClipboardChunk* chunk = new ClipboardChunk(dataSize + CLIPBOARD_CHUNK_META_SIZE);
	char* chunkData = chunk->m_chunk;

	chunkData[0] = id;
	UInt32* seq = reinterpret_cast<UInt32*>(&chunkData[1]);
	*seq = sequence;
	chunkData[5] = kDataChunk;
	memcpy(&chunkData[6], data.c_str(), dataSize);
	chunkData[dataSize + CLIPBOARD_CHUNK_META_SIZE - 1] = '\0';

	return chunk;
}

ClipboardChunk*
ClipboardChunk::end(ClipboardID id, UInt32 sequence)
{
	ClipboardChunk* end = new ClipboardChunk(CLIPBOARD_CHUNK_META_SIZE);
	char* chunk = end->m_chunk;
	
	chunk[0] = id;
	UInt32* seq = reinterpret_cast<UInt32*>(&chunk[1]);
	*seq = sequence;
	chunk[5] = kDataEnd;
	chunk[CLIPBOARD_CHUNK_META_SIZE - 1] = '\0';

	return end;
}

int
ClipboardChunk::assemble(synergy::IStream* stream,
					String& dataCached,
					ClipboardID& id,
					UInt32& sequence)
{
	static size_t expectedSize;
	UInt8 mark;
	String data;

	if (!ProtocolUtil::readf(stream, kMsgDClipboard + 4, &id, &sequence, &mark, &data)) {
		return kError;
	}
	
	if (mark == kDataStart) {
		expectedSize = synergy::string::stringToSizeType(data);
		LOG((CLOG_DEBUG "start receiving clipboard data"));
		dataCached.clear();
		return kNotFinish;
	}
	else if (mark == kDataChunk) {
		dataCached.append(data);
		return kNotFinish;
	}
	else if (mark == kDataEnd) {
		// validate
		if (id >= kClipboardEnd) {
			return kError;
		}
		else if (expectedSize != dataCached.size()) {
			LOG((CLOG_ERR "corrupted clipboard data, expected size=%d actual size=%d", expectedSize, dataCached.size()));
			return kError;
		}
		return kFinish;
	}

	return kError;
}

void
ClipboardChunk::send(synergy::IStream* stream, void* data)
{
	ClipboardChunk* clipboardData = reinterpret_cast<ClipboardChunk*>(data);

	LOG((CLOG_DEBUG1 "sending clipboard chunk"));

	char* chunk = clipboardData->m_chunk;
	ClipboardID id = chunk[0];
	UInt32* seq = reinterpret_cast<UInt32*>(&chunk[1]);
	UInt32 sequence = *seq;
	UInt8 mark = chunk[5];
	String dataChunk(&chunk[6], clipboardData->m_dataSize);

	switch (mark) {
	case kDataStart:
		LOG((CLOG_DEBUG2 "sending clipboard chunk start: size=%s", dataChunk.c_str()));
		break;

	case kDataChunk:
		LOG((CLOG_DEBUG2 "sending clipboard chunk data: size=%i", dataChunk.size()));
		break;

	case kDataEnd:
		LOG((CLOG_DEBUG2 "sending clipboard finished"));
		break;
	}

	ProtocolUtil::writef(stream, kMsgDClipboard, id, sequence, mark, &dataChunk);
}

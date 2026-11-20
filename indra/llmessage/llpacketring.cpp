/**
 * @file llpacketring.cpp
 * @brief implementation of LLPacketRing class for a packet.
 *
 * $LicenseInfo:firstyear=2001&license=viewergpl$
 *
 * Copyright (c) 2001-2026, Linden Research, Inc.
 *
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 *
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution.
 *
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 *
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llpacketring.h"

#include "llpacketbuffer.h"

constexpr U32 MAX_BUFFER_RING_SIZE = 8192;
// DANGER: do not adjust DEFAULT_BUFFER_RING_SIZE unless you know what you are
// doing.  Its value affects the "buffer load rate" which is used to supply
// backpressure to an overloaded nework queue.
constexpr U32 DEFAULT_BUFFER_RING_SIZE = 512;
constexpr U32 BUFFER_RING_EXPANSION = 512;

LLPacketRing::LLPacketRing()
:	mHeadIndex(0),
	mNumBufferedPackets(0),
	mNumBufferedBytes(0)
{
	LLHost invalid_host;
	mRing.reserve(DEFAULT_BUFFER_RING_SIZE);
	for (U32 i = 0; i < DEFAULT_BUFFER_RING_SIZE; ++i)
	{
		mRing.emplace_back(new LLPacketBuffer(invalid_host, NULL, 0));
	}
}

LLPacketRing::~LLPacketRing()
{
	for (size_t i = 0, count = mRing.size(); i < count; ++i)
	{
		delete mRing[i];
	}
}

bool LLPacketRing::hasRoom() const
{
	return mNumBufferedPackets < mRing.size() || mRing.size() < MAX_BUFFER_RING_SIZE;
}

bool LLPacketRing::pushPacket(const LLPacketBuffer& packet)
{
	if (!hasRoom())
	{
		llwarns_once << "Inbound packet ring full: leaving the newest packet unqueued"
					 << llendl;
		return false;
	}
	U32 ring_size = mRing.size();
	if (mNumBufferedPackets >= ring_size)
	{
		ring_size = expandRing();
	}
	LLPacketBuffer* slotp = mRing[mHeadIndex];
	*slotp = packet;
	mHeadIndex = (mHeadIndex + 1) % ring_size;
	++mNumBufferedPackets;
	mNumBufferedBytes += packet.getSize();
	return true;
}

bool LLPacketRing::popPacket(LLPacketBuffer& packet)
{
	if (!mNumBufferedPackets)
	{
		return false;
	}

	U32 ring_size = mRing.size();
	U32 tail_idx = (mHeadIndex + ring_size - mNumBufferedPackets) % ring_size;
	LLPacketBuffer* slotp = mRing[tail_idx];
	U32 packet_size = slotp->getSize();
	packet = *slotp;
	if (mNumBufferedPackets)
	{
		--mNumBufferedPackets;
	}
	else	// This should not happen. HB
	{
		llwarns << "Buffered packets accounting error. Kept to 0." << llendl;
	}
	if (mNumBufferedBytes >= packet_size)
	{
		mNumBufferedBytes -= packet_size;
	}
	else	// This should not happen. HB
	{
		llwarns << "Buffered bytes accounting error. Reset to 0." << llendl;
		mNumBufferedBytes = 0;
	}
	return true;
}

U32 LLPacketRing::expandRing()
{
	U32 old_size = mRing.size();
	U32 new_size = llmin(old_size + BUFFER_RING_EXPANSION,
						 MAX_BUFFER_RING_SIZE);
	if (new_size == old_size)
	{
		llwarns_once << "Ring buffer size already maxed out." << llendl;
		return old_size;
	}

	// Make a larger ring and copy packet pointers
	packet_vec_t new_ring;
	new_ring.reserve(new_size);
	for (U32 i = 0; i < old_size; ++i)
	{
		new_ring.emplace_back(mRing[(mHeadIndex + i) % old_size]);
	}
	// Allocate new packets for the remainder of new_ring
	LLHost invalid_host;
	for (U32 i = old_size; i < new_size; ++i)
	{
		new_ring.emplace_back(new LLPacketBuffer(invalid_host, NULL, 0));
	}
	// Swap the rings and reset mHeadIndex
	mRing.swap(new_ring);
	mHeadIndex = mNumBufferedPackets;

	llinfos << "Ring buffer expanded to hold up to " << new_size << " packets."
			<< llendl
	return new_size;
}

F32 LLPacketRing::getBufferLoadRate() const
{
	constexpr F32 SCALER = 1.f / (F32)DEFAULT_BUFFER_RING_SIZE;
	return (F32)mNumBufferedPackets * SCALER;
}

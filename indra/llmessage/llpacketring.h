/**
 * @file llpacketring.h
 * @brief definition of LLPacketRing class
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

#pragma once

#include <vector>

#include "llerror.h"

class LLMessageSystem;
class LLPacketBuffer;

class LLPacketRing
{
protected:
	LOG_CLASS(LLPacketRing);

public:
	LLPacketRing();
    ~LLPacketRing();

	// Copies 'packet' onto the tail of the ring, growing the ring or
	// overwriting the oldest entry when the ring is at capacity.
	void pushPacket(const LLPacketBuffer& packet);

	// Copies the head (oldest) packet into 'packet'. Returns true if a packet
	// was available, false if the ring was empty.
	bool popPacket(LLPacketBuffer& packet);

	LL_INLINE U32 getNumBufferedPackets() const
	{
		return mNumBufferedPackets;
	}

	LL_INLINE U32 getNumBufferedBytes() const
	{
		return mNumBufferedBytes;
	}

	// Ratio of buffered packets to DEFAULT_BUFFER_RING_SIZE (0 = empty,
	// 1 = nominal full).
	F32 getBufferLoadRate() const;

private:
	// Returns the new ring size.
	U32 expandRing();

protected:
	typedef std::vector<LLPacketBuffer*> packet_vec_t;
	packet_vec_t	mRing;

	U32				mHeadIndex;
	U32				mNumBufferedPackets;
	U32				mNumBufferedBytes;
};

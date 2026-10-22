/**
 * @file llpartdata.cpp
 * @brief Particle system data packing
 *
 * $LicenseInfo:firstyear=2003&license=viewergpl$
 *
 * Copyright (c) 2003-2009, Linden Research, Inc.
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

#include "llpartdata.h"

#include "lldatapacker.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llmessage.h"
#include "llcolor4u.h"

constexpr S32 PS_PART_DATA_GLOW_SIZE = 2;
constexpr S32 PS_PART_DATA_BLEND_SIZE = 2;
constexpr S32 PS_LEGACY_PART_DATA_BLOCK_SIZE = 4 + 2 + 4 + 4 + 2 + 2;	// 18
constexpr S32 PS_SYS_DATA_BLOCK_SIZE = 68;
constexpr S32 PS_MAX_DATA_BLOCK_SIZE = PS_SYS_DATA_BLOCK_SIZE +
									   PS_LEGACY_PART_DATA_BLOCK_SIZE +
									   PS_PART_DATA_BLEND_SIZE +
									   PS_PART_DATA_GLOW_SIZE +
									   8;	// Two S32 size fields

constexpr S32 PS_LEGACY_DATA_BLOCK_SIZE = PS_SYS_DATA_BLOCK_SIZE +
										  PS_LEGACY_PART_DATA_BLOCK_SIZE;

// Helper function.
// Skips size bytes of unrecognized data. The size comes from the sender and
// may be huge, so stop at the first failed read instead of looping size times.
static bool skip_unknown_data(LLDataPacker& dp, S32 size)
{
	U8 discard;
	for (S32 i = 0; i < size; ++i)
	{
		if (!dp.unpackU8(discard, "whippang"))
		{
			return false;
		}
	}
	return true;
}

S32 LLPartData::getSize() const
{
	S32 size = PS_LEGACY_PART_DATA_BLOCK_SIZE;
	if (hasGlow())
	{
		size += PS_PART_DATA_GLOW_SIZE;
	}
	if (hasBlendFunc())
	{
		size += PS_PART_DATA_BLEND_SIZE;
	}
	return size;
}

bool LLPartData::unpackLegacy(LLDataPacker& dp)
{
	static LLColor4U start_color, end_color;

	// Stop at the first failed read, rather than logging a warning for every
	// field
	if (!(dp.unpackU32(mFlags, "pdflags") &&
		  dp.unpackFixed(mMaxAge, "pdmaxage", false, 8, 8) &&
		  dp.unpackColor4U(start_color, "pdstartcolor") &&
		  dp.unpackColor4U(end_color, "pdendcolor") &&
		  dp.unpackFixed(mStartScale.mV[0], "pdstartscalex", false, 3, 5) &&
		  dp.unpackFixed(mStartScale.mV[1], "pdstartscaley", false, 3, 5) &&
		  dp.unpackFixed(mEndScale.mV[0], "pdendscalex", false, 3, 5) &&
		  dp.unpackFixed(mEndScale.mV[1], "pdendscaley", false, 3, 5)))
	{
		return false;
	}

	mStartColor.set(start_color);
	mEndColor.set(end_color);
	mStartGlow = mEndGlow = 0.f;
	mBlendFuncSource = LL_PART_BF_SOURCE_ALPHA;
	mBlendFuncDest = LL_PART_BF_ONE_MINUS_SOURCE_ALPHA;
	return true;
}

bool LLPartData::unpack(LLDataPacker& dp)
{
	S32 size = 0;
	if (!dp.unpackS32(size, "partsize") || !unpackLegacy(dp))
	{
		return false;
	}
	size -= PS_LEGACY_PART_DATA_BLOCK_SIZE;

	if (mFlags & LL_PART_DATA_GLOW)
	{
		if (size < PS_PART_DATA_GLOW_SIZE)
		{
			return false;
		}

		U8 start_glow, end_glow;
		if (!dp.unpackU8(start_glow, "pdstartglow") ||
			!dp.unpackU8(end_glow, "pdendglow"))
		{
			return false;
		}
		constexpr F32 scaler = 1.f / 255.f;
		mStartGlow = start_glow * scaler;
		mEndGlow = end_glow * scaler;

		size -= PS_PART_DATA_GLOW_SIZE;
	}
	else
	{
		mStartGlow = 0.f;
		mEndGlow = 0.f;
	}

	if (mFlags & LL_PART_DATA_BLEND)
	{
		if (size < PS_PART_DATA_BLEND_SIZE)
		{
			return false;
		}

		if (!dp.unpackU8(mBlendFuncSource, "pdblendsource") ||
			!dp.unpackU8(mBlendFuncDest, "pdblenddest"))
		{
			return false;
		}
		size -= PS_PART_DATA_BLEND_SIZE;
	}
	else
	{
		mBlendFuncSource = LL_PART_BF_SOURCE_ALPHA;
		mBlendFuncDest = LL_PART_BF_ONE_MINUS_SOURCE_ALPHA;
	}

	if (size > 0)
	{
		// Leftover bytes, unrecognized parameters
		skip_unknown_data(dp, size);

		// This particle system wo uld not display properly, better to not show
		// anything
		return false;
	}

	return true;
}

LLPartSysData::LLPartSysData()
{
	mCRC = 0;
	mFlags = 0;

	mPartData.mFlags = 0;
	mPartData.mStartColor = LLColor4(1.f, 1.f, 1.f, 1.f);
	mPartData.mEndColor = LLColor4(1.f, 1.f, 1.f, 1.f);
	mPartData.mStartScale = LLVector2(1.f, 1.f);
	mPartData.mEndScale = LLVector2(1.f, 1.f);
	mPartData.mMaxAge = 10.0;
	mPartData.mBlendFuncSource = LLPartData::LL_PART_BF_SOURCE_ALPHA;
	mPartData.mBlendFuncDest = LLPartData::LL_PART_BF_ONE_MINUS_SOURCE_ALPHA;
	mPartData.mStartGlow = 0.f;
	mPartData.mEndGlow = 0.f;

	mMaxAge = 0.0;
	mStartAge = 0.0;
	mPattern = LL_PART_SRC_PATTERN_DROP;	// Pattern for particle velocity
	mInnerAngle = 0.0;						// Inner angle of PATTERN_ANGLE_*
	mOuterAngle = 0.0;						// Outer angle of PATTERN_ANGLE_*
	mBurstRate = 0.1f;						// How often to do a burst of particles
	mBurstPartCount = 1;					// How many particles in a burst
	mBurstSpeedMin = 1.f;					// Minimum particle velocity
	mBurstSpeedMax = 1.f;					// Maximum particle velocity
	mBurstRadius = 0.f;

	mNumParticles = 0;
}

bool LLPartSysData::unpackSystem(LLDataPacker& dp)
{
	// Stop at the first failed read, rather than logging a warning for every
	// field
	if (!(dp.unpackU32(mCRC, "pscrc") &&
		  dp.unpackU32(mFlags, "psflags") &&
		  dp.unpackU8(mPattern, "pspattern") &&
		  dp.unpackFixed(mMaxAge, "psmaxage", false, 8, 8) &&
		  dp.unpackFixed(mStartAge, "psstartage", false, 8, 8) &&
		  dp.unpackFixed(mInnerAngle, "psinnerangle", false, 3, 5) &&
		  dp.unpackFixed(mOuterAngle, "psouterangle", false, 3, 5) &&
		  dp.unpackFixed(mBurstRate, "psburstrate", false, 8, 8) &&
		  dp.unpackFixed(mBurstRadius, "psburstradius", false, 8, 8) &&
		  dp.unpackFixed(mBurstSpeedMin, "psburstspeedmin", false, 8, 8) &&
		  dp.unpackFixed(mBurstSpeedMax, "psburstspeedmax", false, 8, 8) &&
		  dp.unpackU8(mBurstPartCount, "psburstpartcount") &&
		  dp.unpackFixed(mAngularVelocity.mV[0], "psangvelx", true, 8, 7) &&
		  dp.unpackFixed(mAngularVelocity.mV[1], "psangvely", true, 8, 7) &&
		  dp.unpackFixed(mAngularVelocity.mV[2], "psangvelz", true, 8, 7) &&
		  dp.unpackFixed(mPartAccel.mV[0], "psaccelx", true, 8, 7) &&
		  dp.unpackFixed(mPartAccel.mV[1], "psaccely", true, 8, 7) &&
		  dp.unpackFixed(mPartAccel.mV[2], "psaccelz", true, 8, 7) &&
		  dp.unpackUUID(mPartImageID, "psuuid") &&
		  dp.unpackUUID(mTargetUUID, "pstargetuuid")))
	{
		return false;
	}
	mBurstRate = llmax(0.01f, mBurstRate);
	return true;
}

bool LLPartSysData::unpackLegacy(LLDataPacker& dp)
{
	return unpackSystem(dp) && mPartData.unpackLegacy(dp);
}

bool LLPartSysData::unpack(LLDataPacker& dp)
{
	// syssize is currently unused. Adding now when modifying the version to
	// make extensible in the future
	S32 size;
	if (!dp.unpackS32(size, "syssize"))
	{
		return false;
	}

	if (size != PS_SYS_DATA_BLOCK_SIZE)
	{
		// Unexpected size, this viewer does not know how to parse this
		// particle system. Skip to LLPartData block start, then skip
		// the entire LLPartData block.
		if (skip_unknown_data(dp, size) && dp.unpackS32(size, "partsize"))
		{
			skip_unknown_data(dp, size);
		}
		return false;
	}

	return unpackSystem(dp) && mPartData.unpack(dp);
}

std::ostream& operator<<(std::ostream& s, const LLPartSysData& data)
{
	s << "Flags: " << std::hex << data.mFlags << std::dec;
	s << " Pattern: " << std::hex << (U32)data.mPattern << std::dec << "\n";
	s << "Source age: [" << data.mStartAge << ", " << data.mMaxAge << "]\n";
	s << "Particle Age: " << data.mPartData.mMaxAge << "\n";
	s << "Angle: [" << data.mInnerAngle << ", " << data.mOuterAngle << "]\n";
	s << "Burst rate: " << data.mBurstRate << "\n";
	s << "Burst radius: " << data.mBurstRadius << "\n";
	s << "Burst speed: [" << data.mBurstSpeedMin << ", "
	  << data.mBurstSpeedMax << "]\n";
	s << "Burst part count: " << std::hex << (U32)data.mBurstPartCount
	  << std::dec << "\n";
	s << "Angular velocity: " << data.mAngularVelocity << "\n";
	s << "Accel: " << data.mPartAccel;
	return s;
}

bool LLPartSysData::isNullPS(S32 block_num)
{
	LLMessageSystem* msg = gMessageSystemp;

	// Check size of block
	S32 size;
	size = msg->getSize(_PREHASH_ObjectData, block_num, _PREHASH_PSBlock);
	if (size == 0)
	{
		return true;	// Valid, null particle system
	}
	if (size < 0)
	{
		llwarns << "Error decoding ObjectData/PSBlock" << llendl;
		return true;
	}
	if (size > PS_MAX_DATA_BLOCK_SIZE)
	{
		llwarns_once << "PSBlock is wrong size for particle system data: "
					 << " unknown/unsupported particle system." << llendl;
		return true;	// Invalid particle system. Treat as null.
	}

	U8 ps_data_block[PS_MAX_DATA_BLOCK_SIZE];
	msg->getBinaryData(_PREHASH_ObjectData, _PREHASH_PSBlock, ps_data_block,
					   size, block_num, PS_MAX_DATA_BLOCK_SIZE);

	LLDataPackerBinaryBuffer dp(ps_data_block, size);
	if (size > PS_LEGACY_DATA_BLOCK_SIZE)
	{
		// non legacy systems pack a size before the CRC
		S32 tmp = 0;
		dp.unpackS32(tmp, "syssize");

		if (tmp > PS_SYS_DATA_BLOCK_SIZE)
		{
			// Unknown system data block size, do not know how to parse it,
			// treat as null.
			llwarns_once << "PSBlock is wrong size for particle system data: "
						 << " unknown/unsupported particle system." << llendl;
			return true;
		}
	}

	U32 crc;
	dp.unpackU32(crc, "crc");
	return crc == 0;
}

bool LLPartSysData::unpackBlock(S32 block_num)
{
	LLMessageSystem* msg = gMessageSystemp;
	U8 ps_data_block[PS_MAX_DATA_BLOCK_SIZE];

	// Check size of block
	S32 size = msg->getSize(_PREHASH_ObjectData, block_num, _PREHASH_PSBlock);
	if (size <= 0)
	{
		llwarns << "Error decoding ObjectData/PSBlock" << llendl;
		return false;
	}
	if (size > PS_MAX_DATA_BLOCK_SIZE)
	{
		llwarns_once << "PSBlock is wrong size for particle system data: "
					 << " unknown/unsupported particle system." << llendl;
		return false;
	}

	// Get from message
	msg->getBinaryData(_PREHASH_ObjectData, _PREHASH_PSBlock, ps_data_block,
					   size, block_num, PS_MAX_DATA_BLOCK_SIZE);

	LLDataPackerBinaryBuffer dp(ps_data_block, size);
	if (size == PS_LEGACY_DATA_BLOCK_SIZE)
	{
		return unpackLegacy(dp);
	}
	return unpack(dp);
}

void LLPartSysData::clampSourceParticleRate()
{
	if (mBurstRate > 0.f)	// Paranoia
	{
		F32 particle_rate = (F32)mBurstPartCount / mBurstRate;
		if (particle_rate > 256.f)
		{
			mBurstPartCount = llfloor((F32)mBurstPartCount * 256.f /
									  particle_rate);
		}
	}
}

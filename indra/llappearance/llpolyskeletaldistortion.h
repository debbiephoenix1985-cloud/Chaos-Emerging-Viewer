/**
 * @file llpolyskeletaldistortion.h
 * @brief Implementation of LLPolyMesh class
 *
 * $LicenseInfo:firstyear=2001&license=viewergpl$
 *
 * Copyright (c) 2001-2009, Linden Research, Inc.
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

#include "linden_common.h"

#include "lljoint.h"
#include "llpreprocessor.h"
#include "llquaternion.h"
#include "llstl.h"
#include "llviewervisualparam.h"
#include "llvector2.h"
#include "llvector3.h"

class LLAvatarAppearance;

//-----------------------------------------------------------------------------
// LLPolySkeletalDeformationInfo
// Shared information for LLPolySkeletalDeformations
//-----------------------------------------------------------------------------
struct LLPolySkeletalBoneInfo
{
	LLPolySkeletalBoneInfo(const std::string& name, const LLVector3& scale,
						   const LLVector3& pos, bool haspos)
	:	mJointKey(LLJoint::getKey(name)),
		mScaleDeformation(scale),
		mPositionDeformation(pos),
		mHasPositionDeformation(haspos)
	{
	}

	LLVector3	mScaleDeformation;
	LLVector3	mPositionDeformation;
	U32			mJointKey;
	bool		mHasPositionDeformation;
};

class LLPolySkeletalDistortionInfo : public LLViewerVisualParamInfo
{
	friend class LLPolySkeletalDistortion;

protected:
	LOG_CLASS(LLPolySkeletalDistortionInfo);

public:
	LLPolySkeletalDistortionInfo() = default;

	LL_INLINE LLPolySkeletalDistortionInfo* asPolySkeletalDistortionInfo() override
	{
		return this;
	}

	bool parseXml(LLXmlTreeNode* nodep) override;

protected:
	typedef std::vector<LLPolySkeletalBoneInfo> bone_info_list_t;
	bone_info_list_t mBoneInfoList;
};

//-----------------------------------------------------------------------------
// LLPolySkeletalDeformation
// A set of joint scale data for deforming the avatar mesh
//-----------------------------------------------------------------------------
class LLPolySkeletalDistortion : public LLViewerVisualParam
{
protected:
	LOG_CLASS(LLPolySkeletalDistortion);

	LLPolySkeletalDistortion(const LLPolySkeletalDistortion& other);

public:
	LLPolySkeletalDistortion(LLAvatarAppearance* avatarp);

	LL_INLINE LLPolySkeletalDistortion* asPolySkeletalDistortion() override
	{
		return this;
	}

	// Special: these functions are overridden by child classes
	LL_INLINE LLPolySkeletalDistortionInfo* getInfo() const
	{
		return (LLPolySkeletalDistortionInfo*)mInfo;
	}

	// This sets mInfo and calls initialization functions
	bool setInfo(LLPolySkeletalDistortionInfo* info);

	LLViewerVisualParam* cloneParam(LLWearable* wearable) const override;

	// LLVisualParam Virtual function
	void apply(ESex sex) override;

protected:
	// Backlink only; do not make this an LLPointer:
	LLAvatarAppearance*	mAvatar;

	typedef std::map<LLJoint*, LLVector3> joint_vec_map_t;
	joint_vec_map_t		mJointScales;
	joint_vec_map_t		mJointOffsets;
};

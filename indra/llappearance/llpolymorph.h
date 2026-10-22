/**
 * @file llpolymorph.h
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

#include <string>
#include <vector>

#include "llfile.h"
#include "llviewervisualparam.h"

class LLAvatarJointCollisionVolume;
class LLPolyMeshSharedData;
class LLVector2;
class LLWearable;

class LLPolyMorphData
{
protected:
	LOG_CLASS(LLPolyMorphData);

public:
	LLPolyMorphData(const char* morph_name);
	~LLPolyMorphData();
	LLPolyMorphData(const LLPolyMorphData& rhs);

	LL_INLINE bool isSuccesfullyAllocated()		{ return mSuccessfullyAllocated; }

	bool loadBinary(LLFILE* fp, LLPolyMeshSharedData* mesh);

	LL_INLINE const std::string& getName()		{ return mName; }

private:
	void freeData();

public:
	// Morphology
	LLVector4a*				mCoords;
	LLVector4a*				mNormals;
	LLVector4a*				mBinormals;
	LLVector2*				mTexCoords;
	U32*					mVertexIndices;
	U32						mNumIndices;
	U32						mCurrentIndex;

	LLPolyMeshSharedData*	mMesh;

	std::string				mName;

private:
	bool					mSuccessfullyAllocated;
};

class LLPolyVertexMask
{
public:
	LLPolyVertexMask(LLPolyMorphData* morph_data);
	LLPolyVertexMask(const LLPolyVertexMask& other);
	~LLPolyVertexMask();

	void generateMask(U8* mask_data, S32 width, S32 height, S32 num_components,
					  bool invert, LLVector4a* clothing_weights);
	F32* getMorphMaskWeights();

protected:
	F32*				mWeights;
	LLPolyMorphData*	mMorphData;
	bool				mWeightsGenerated;
};

struct LLPolyVolumeMorphInfo
{
	LLPolyVolumeMorphInfo(const std::string& name, const LLVector3& scale,
						  const LLVector3& pos)
	:	mName(name),
		mScale(scale),
		mPos(pos)
	{
	}

	std::string			mName;
	LLVector3			mScale;
	LLVector3			mPos;
};

struct LLPolyVolumeMorph
{
	LLPolyVolumeMorph(LLAvatarJointCollisionVolume* volume,
					  const LLVector3& scale, const LLVector3& pos)
	:	mVolume(volume),
		mScale(scale),
		mPos(pos)
	{
	}

	LLAvatarJointCollisionVolume*	mVolume;
	LLVector3						mScale;
	LLVector3						mPos;
};

// Shared information for LLPolyMorphTargets
class LLPolyMorphTargetInfo : public LLViewerVisualParamInfo
{
	friend class LLPolyMorphTarget;

protected:
	LOG_CLASS(LLPolyMorphTargetInfo);

public:
	LLPolyMorphTargetInfo();

	bool parseXml(LLXmlTreeNode* node) override;

protected:
	std::string			mMorphName;
	bool				mIsClothingMorph;
	typedef std::vector<LLPolyVolumeMorphInfo> volume_info_list_t;
	volume_info_list_t	mVolumeInfoList;
};

// A set of vertex data associated with morph target. These morph targets must
// be topologically consistent with a given Polymesh (share face sets)
class LLPolyMorphTarget : public LLViewerVisualParam
{
protected:
	LOG_CLASS(LLPolyMorphTarget);

	LLPolyMorphTarget(const LLPolyMorphTarget& other);

public:
	LLPolyMorphTarget(LLPolyMesh* poly_mesh);
	~LLPolyMorphTarget();

	LL_INLINE LLPolyMorphTarget* asPolyMorphTarget() override
	{
		return this;
	}

	// Special: These functions are overridden by child classes
	LL_INLINE LLPolyMorphTargetInfo* getInfo() const
	{
		return (LLPolyMorphTargetInfo*)mInfo;
	}

	// Sets mInfo and calls initialization functions
	bool setInfo(LLPolyMorphTargetInfo* info);

	LLViewerVisualParam* cloneParam(LLWearable* wearable) const override;

	// LLVisualParam Virtual function
	void apply(ESex sex) override;

	void applyMask(U8* mask_data, S32 width, S32 height, S32 num_components,
				   bool invert);

	LL_INLINE void addPendingMorphMask()		{ ++mNumMorphMasksPending; }

	// Also used by LLVOAvatar::resetSkeleton()
	void applyVolumeChanges(F32 delta_weight);

protected:
	LLPolyMorphData*				mMorphData;
	LLPolyMesh*						mMesh;
	LLPolyVertexMask*				mVertMask;
	ESex							mLastSex;

	// Number of morph masks that haven't been generated, must be 0 before
	// this morph is applied
	S32								mNumMorphMasksPending;

	typedef std::vector<LLPolyVolumeMorph> volume_list_t;
	volume_list_t 					mVolumeMorphs;

};

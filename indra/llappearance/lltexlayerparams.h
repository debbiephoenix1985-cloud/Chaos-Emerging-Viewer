/**
 * @file lltexlayerparams.h
 * @brief Texture layer parameters, used by lltexlayer.
 *
 * $LicenseInfo:firstyear=2002&license=viewergpl$
 *
 * Copyright (c) 2010, Linden Research, Inc.
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

#include "llpointer.h"
#include "llviewervisualparam.h"
#include "llcolor4.h"

class LLAvatarAppearance;
class LLGLTexture;
class LLImageRaw;
class LLImageTGA;
class LLTexLayer;
class LLTexLayerInterface;
class LLWearable;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLTexLayerParam
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLTexLayerParam : public LLViewerVisualParam
{
protected:
	LOG_CLASS(LLTexLayerParam);

	LLTexLayerParam(const LLTexLayerParam& other);

public:
	LLTexLayerParam(LLTexLayerInterface* layer);
	LLTexLayerParam(LLAvatarAppearance* appearance);
	bool setInfo(LLViewerVisualParamInfo* info, bool add_to_app);
	LLViewerVisualParam* cloneParam(LLWearable* wearable) const override = 0;

protected:
	LLTexLayerInterface*	mTexLayer;
	LLAvatarAppearance*		mAvatarAppearance;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLTexLayerParamAlpha
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLTexLayerParamAlpha final : public LLTexLayerParam
{
protected:
	LOG_CLASS(LLTexLayerParamAlpha);

	LLTexLayerParamAlpha(const LLTexLayerParamAlpha& other);

public:
	LLTexLayerParamAlpha(LLTexLayerInterface* layer);
	LLTexLayerParamAlpha(LLAvatarAppearance* appearance);
	~LLTexLayerParamAlpha() override;

	LLViewerVisualParam* cloneParam(LLWearable* wearp = NULL) const override;

	// LLVisualParam Virtual functions

	void apply(ESex avatar_sex) override					{}
	void setWeight(F32 weight, bool upload_bake) override;
	void setAnimationTarget(F32 target_value, bool upload_bake) override;
	void animate(F32 delta, bool upload_bake) override;

	// New functions
	bool render(S32 x, S32 y, S32 width, S32 height);
	bool getSkip() const;
	void deleteCaches();
	bool getMultiplyBlend() const;

private:
	LLPointer<LLGLTexture>	mCachedProcessedTexture;
	LLPointer<LLImageTGA>	mStaticImageTGA;
	LLPointer<LLImageRaw>	mStaticImageRaw;
	bool					mNeedsCreateTexture;
	bool					mStaticImageInvalid;
	F32						mCachedEffectiveWeight;

public:
	// Global list of instances for gathering statistics
	static void dumpCacheByteCount();
	static void getCacheByteCount(S32* gl_bytes);

	typedef std::list< LLTexLayerParamAlpha* > param_alpha_ptr_list_t;
	static param_alpha_ptr_list_t sInstances;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLTexLayerParamAlphaInfo
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLTexLayerParamAlphaInfo final : public LLViewerVisualParamInfo
{
	friend class LLTexLayerParamAlpha;

public:
	LLTexLayerParamAlphaInfo();

	bool parseXml(LLXmlTreeNode* node) override;

private:
	F32			mDomain;
	bool		mMultiplyBlend;
	bool		mSkipIfZeroWeight;
	std::string	mStaticImageFileName;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLTexLayerParamColor
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLTexLayerParamColor : public LLTexLayerParam
{
protected:
	LOG_CLASS(LLTexLayerParamColor);

	LLTexLayerParamColor(const LLTexLayerParamColor& other);

public:
	enum EColorOperation
	{
		OP_ADD = 0,
		OP_MULTIPLY = 1,
		OP_BLEND = 2,
		OP_COUNT = 3 // Number of operations
	};

	LLTexLayerParamColor(LLTexLayerInterface* layer);
	LLTexLayerParamColor(LLAvatarAppearance* appearance);

	LLViewerVisualParam* cloneParam(LLWearable* wearp = NULL) const override;

	// LLVisualParam Virtual functions

	LL_INLINE void apply(ESex avatar_sex) override			{}
	void setWeight(F32 weight, bool upload_bake) override;
	void setAnimationTarget(F32 target_value, bool upload_bake) override;
	void animate(F32 delta, bool upload_bake) override;

	// New functions
	LLColor4			getNetColor() const;

protected:
	LL_INLINE virtual void onGlobalColorChanged(bool upload_bake)	{}
};

class LLTexLayerParamColorInfo final : public LLViewerVisualParamInfo
{
	friend class LLTexLayerParamColor;

protected:
	LOG_CLASS(LLTexLayerParamColorInfo);

public:
	LLTexLayerParamColorInfo();

	bool parseXml(LLXmlTreeNode* node);

	LL_INLINE LLTexLayerParamColor::EColorOperation getOperation() const
	{
		return mOperation;
	}

private:
	enum { MAX_COLOR_VALUES = 20 };
	LLTexLayerParamColor::EColorOperation	mOperation;
	LLColor4								mColors[MAX_COLOR_VALUES];
	S32										mNumColors;
};

typedef std::vector<LLTexLayerParamColor*> param_color_list_t;
typedef std::vector<LLTexLayerParamAlpha*> param_alpha_list_t;
typedef std::vector<LLTexLayerParamColorInfo*> param_color_info_list_t;
typedef std::vector<LLTexLayerParamAlphaInfo*> param_alpha_info_list_t;

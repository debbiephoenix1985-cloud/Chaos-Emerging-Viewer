/**
 * @file llfontbitmapcache.h
 * @brief Storage for previously rendered glyphs.
 *
 * $LicenseInfo:firstyear=2008&license=viewergpl$
 *
 * Copyright (c) 2008-2009, Linden Research, Inc.
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

#include "llimagegl.h"

enum EFontGlyphType : U32
{
	Grayscale = 0,
	Color,
	Count,
	Unspecified,
};

// Maintain a collection of bitmaps containing rendered glyphs.
// Generalizes the single-bitmap logic from LLFontFreetype and LLFontGL.
class LLFontBitmapCache : public LLRefCount
{
public:
	LLFontBitmapCache();

	// This must be called once, before caching any glyphs.
 	void init(S32 max_char_width, S32 max_char_height);

	void reset();

	void nextOpenPos(S32 width, S32& pos_x, S32& pos_y, U32 bitmap_type,
					 U32& bitmap_num);

	void destroyGL();

 	LLImageRaw* getImageRaw(U32 bitmap_type, U32 bitmap_num) const;
 	LLImageGL* getImageGL(U32 bitmap_type, U32 bitmap_num) const;  

	LL_INLINE S32 getMaxCharWidth() const			{ return mMaxCharWidth; }
	LL_INLINE S32 getBitmapWidth() const			{ return mBitmapWidth; }
	LL_INLINE S32 getBitmapHeight() const			{ return mBitmapHeight; }

	LL_INLINE U32 getNumBitmaps(U32 bitmap_type) const
	{
		return bitmap_type < EFontGlyphType::Count ?
					(U32)mImageRawVec[bitmap_type].size() : 0;
	}

	LL_INLINE U32 getCacheGeneration() const		{ return mGeneration; }

private:
	LL_INLINE static U32 getNumComponents(U32 bitmap_type)
	{
		return bitmap_type == EFontGlyphType::Color ? 4 : 2;
	}

private:
	std::vector<LLPointer<LLImageRaw> >	mImageRawVec[EFontGlyphType::Count];
	std::vector<LLPointer<LLImageGL> >	mImageGLVec[EFontGlyphType::Count];

	S32									mCurrentOffsetX[EFontGlyphType::Count];
	S32									mCurrentOffsetY[EFontGlyphType::Count];

	S32									mBitmapWidth;
	S32									mBitmapHeight;

	S32									mMaxCharWidth;
	S32									mMaxCharHeight;

	U32									mGeneration;
};

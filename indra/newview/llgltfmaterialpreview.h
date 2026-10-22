/**
 * @file llgltfmaterialpreview.h
 * @brief The LLAppViewer class definitions
 *
 * $LicenseInfo:firstyear=2023&license=viewergpl$
 *
 * Copyright (c) 2023, Linden Research, Inc.
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

#include "lldynamictexture.h"
#include "llfetchedgltfmaterial.h"

class LLGLTFPreviewTexture final : public LLViewerDynamicTexture
{
protected:
	// Use getPreview()
	LLGLTFPreviewTexture(LLFetchedGLTFMaterial* matp, S32 width = 512);

public:
	// Returns NULL if the material is not loaded yet. Note: the texture should
	// be cached if the same material is being previewed.
	static LLPointer<LLViewerTexture> getPreview(LLFetchedGLTFMaterial* matp);

	bool needsRender() override;
	void preRender(bool clear_depth = true) override;
	bool render() override;
	void postRender(bool success) override;

	class MaterialLoadLevels
	{
	public:
		MaterialLoadLevels();

		bool isFullyLoaded() const;

		LL_INLINE S32& operator[](size_t i)				{ return mLevels[i]; }
		LL_INLINE const S32& operator[](size_t i) const	{ return mLevels[i]; }

		// Less is better. Returns false if lhs is not strictly less or equal
		// for all levels.
		bool operator<(const MaterialLoadLevels& other) const;

		// Less is better. Returns false if lhs is not strictly greater or
		// equal for all levels
		bool operator>(const MaterialLoadLevels& other) const;

	public:
		S32 mLevels[LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT];
	};

private:
	LLPointer<LLFetchedGLTFMaterial>	mGLTFMaterial;
	MaterialLoadLevels					mBestLoad;
	bool								mShouldRender;
};

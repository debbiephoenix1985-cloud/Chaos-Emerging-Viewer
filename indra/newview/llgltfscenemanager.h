/**
 * @file llgltfscenemanager.h
 * @brief LLGLTFSceneManager class declaration.
 *
 * $LicenseInfo:firstyear=2024&license=viewergpl$
 *
 * Copyright (c) 2024, Linden Research, Inc.
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

#include "llassettype.h"
#include "llextendedstatus.h"
#include "hbfastset.h"
#include "hbfastmap.h"
#include "llgltfasset.h"

class LLDrawable;
class LLVector2;
class LLVector4a;
class LLViewerObject;
class LLViewerFetchedTexture;
class LLVOVolume;

// Purely static class (instead of a LLSimpleton in LL's code). HB
class LLGLTFSceneManager
{
protected:
	LOG_CLASS(LLGLTFSceneManager);

public:
	LLGLTFSceneManager() = delete;
	~LLGLTFSceneManager() = delete;

	// Called at viewer initialization from LLAppViewer::initWindow().
	// Initializes the newview function hooks needed by the llgltf library. HB
	static void init();

	// Called on viewer shutdown, before clearing objects list, from
	// LLAppViewer::cleanup(). HB
	static void cleanup();

	// The two following methods return an empty string on success, or the
	// reason for a failure. HB
	static std::string load(const std::string& filename,
							const LLUUID& handle_obj_id);
	static std::string save(const std::string& filename,
							const LLUUID& handle_obj_id);

	static std::string upload(const LLUUID& handle_obj_id);

	LL_INLINE static bool isUploading()	{ return sUploadingAsset.notNull(); }

	static bool lineSegmentIntersect(LLVOVolume* volp, LLGLTF::Asset* assetp,
									 const LLVector4a& start,
									 const LLVector4a& end, S32 face,
									 bool pick_transparent, bool pick_rigged,
									 S32* node_hitp, S32* prim_hitp,
									 LLVector4a* interp,LLVector2* tcoordp,
									 LLVector4a* normp, LLVector4a* tgtp);

	static LLDrawable* lineSegmentIntersect(const LLVector4a& start,
											const LLVector4a& end,
											bool pick_transparent,
											bool pick_rigged, S32* node_hitp,
											S32* prim_hitp, LLVector4a* interp,
											LLVector2* tcoordp,
											LLVector4a* normp,
											LLVector4a* tgtp);

	static void update();

	static void render(bool opaque, bool rigged = false, bool unlit = false);

	LL_INLINE static void renderOpaque(bool rigged = false)
	{
		render(true, rigged);
	}

	LL_INLINE static void renderAlpha(bool rigged = false)
	{
		render(false, rigged);
	}

	static void renderDebug();

	static void addGLTFObject(LLViewerObject* objp, const LLUUID& gltf_id);

private:
	static void onGLTFLoadComplete(const LLUUID& id,
								   LLAssetType::EType asset_type,
								   void* user_data, S32 status,
								   LLExtStat ext_status);
	static void onGLTFBinLoadComplete(const LLUUID& id,
									  LLAssetType::EType asset_type,
									  void* user_data, S32 status,
									  LLExtStat ext_status);

	enum TextureType : U8
	{
		BASE_COLOR = 0,
		NORMAL,
		METALLIC_ROUGHNESS,
		OCCLUSION,
		EMISSIVE,
		TEXTURE_TYPE_COUNT
	};

	static void renderVariant(U8 variant);
	static void renderAsset(LLGLTF::Asset& asset, U8 variant);
	static void bindTexture(LLGLTF::Asset& asset, U8 type,
							LLGLTF::TextureInfo& info,
							LLViewerFetchedTexture* fallback_texp);
	static void bind(LLGLTF::Asset& asset, LLGLTF::Material& mat);

private:
	typedef fast_hset<LLPointer<LLViewerObject> > objects_set_t;
	static objects_set_t			 sObjects;

	typedef fast_hmap<LLPointer<LLViewerObject>, LLUUID> objects_map_t;
	static objects_map_t			 sPendingObjects;

	static S32						 sLastTexture[TEXTURE_TYPE_COUNT];

	// glTF asset upload
	static LLPointer<LLViewerObject> sUploadingObject;
	static LLPointer<LLGLTF::Asset>	 sUploadingAsset;
	static S32						 sPendingImageUploads;
	static S32						 sPendingBinaryUploads;
	static bool						 sPendingGLTFUpload;
};

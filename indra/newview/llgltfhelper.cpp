/**
 * @file llgltfhelper.cpp
 * @brief LLGLTFHelper class implementation
 *
 * $LicenseInfo:firstyear=2022&license=viewergpl$
 *
 * Copyright (c) 2022-2026, Linden Research, Inc.
 * Copyright (c) 2026, Henri Beauchamp.
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

#include "llviewerprecompiledheaders.h"

#include "json.hpp"

#include "llgltfhelper.h"

#include "lldir.h"
#include "llimage.h"
#include "lluri.h"

#include "llviewertexturelist.h"

using lljson = nlohmann::json;

// External helper function implemented in llgltfscenemanager.cpp to create a
// raw image from a memory image data block. HB
extern LLPointer<LLImageRaw> raw_image_from_memory(const U8* datap, U32 size,
												   const std::string& mimetype);

static void strip_alpha_channel(LLPointer<LLImageRaw>& imagep)
{
	if (imagep->getComponents() == 4)
	{
		LLImageRaw* rawp = new LLImageRaw(imagep->getWidth(),
										  imagep->getHeight(), 3);
		rawp->copyUnscaled4onto3(imagep);
		imagep = rawp;
	}
}

// Copy red channel from src_imgp to dst_imgp. Preconditions: dst_imgp must have 3
// components, src_imgp and dst_image must have the same dimensions.
static void copy_red_channel(LLPointer<LLImageRaw>& src_imgp,
							 LLPointer<LLImageRaw>& dst_imgp)
{
	llassert(src_imgp->getWidth() == dst_imgp->getWidth() &&
			 src_imgp->getHeight() == dst_imgp->getHeight() &&
			 dst_imgp->getComponents() == 3);

	U32 pixel_count = dst_imgp->getWidth() * dst_imgp->getHeight();
	U8* src = src_imgp->getData();
	U8* dst = dst_imgp->getData();
	S8 src_components = src_imgp->getComponents();
	for (U32 i = 0; i < pixel_count; ++i)
	{
		dst[i * 3] = src[i * src_components];
	}
}

void LLGLTFHelper::initFetchedTextures(const LLGLTF::Material& material,
									   LLPointer<LLImageRaw>& basecol_imgp,
									   LLPointer<LLImageRaw>& norm_imgpp,
									   LLPointer<LLImageRaw>& mr_imgp,
									   LLPointer<LLImageRaw>& emissive_imgp,
									   LLPointer<LLImageRaw>& occl_imgp,
									   LLPointer<LLViewerFetchedTexture>& basecolp,
									   LLPointer<LLViewerFetchedTexture>& normalp,
									   LLPointer<LLViewerFetchedTexture>& mrp,
									   LLPointer<LLViewerFetchedTexture>& emissivep)
{
	if (basecol_imgp)
	{
		basecolp = LLViewerTextureManager::getFetchedTexture(basecol_imgp);
	}

	if (norm_imgpp)
	{
		strip_alpha_channel(norm_imgpp);
		normalp = LLViewerTextureManager::getFetchedTexture(norm_imgpp);
	}

	if (mr_imgp)
	{
		strip_alpha_channel(mr_imgp);
		if (occl_imgp)
		{
			S32 occlusion_idx = material.mOcclusionTexture.mIndex;
			S32 mr_idx =
				material.mPbrMetallicRoughness.mMetallicRoughnessTexture.mIndex;
			// When occlusion is a distinct texture from pbrMetallicRoughness,
			// pack into MR red channel.
			if (occlusion_idx != mr_idx)
			{
				// Scale occlusion image to match resolution of mr image
				occl_imgp->scale(mr_imgp->getWidth(), mr_imgp->getHeight());
				copy_red_channel(occl_imgp, mr_imgp);
			}
		}
		else if (material.mOcclusionTexture.mIndex == -1)
		{
			// No occlusion, make sure red channel of ORM is all 255
			occl_imgp = new LLImageRaw(mr_imgp->getWidth(),
									   mr_imgp->getHeight(), 3);
			occl_imgp->clear(255, 255, 255);
			copy_red_channel(occl_imgp, mr_imgp);
		}
	}
	else if (occl_imgp)
	{
		// No mr but occlusion exists, make a white mr_imgp and copy occlusion
		// red channel over.
		mr_imgp = new LLImageRaw(occl_imgp->getWidth(), occl_imgp->getHeight(),
								 3);
		mr_imgp->clear(255, 255, 255);
		copy_red_channel(occl_imgp, mr_imgp);
	}

	if (mr_imgp)
	{
		mrp = LLViewerTextureManager::getFetchedTexture(mr_imgp);
	}

	if (emissive_imgp)
	{
		strip_alpha_channel(emissive_imgp);
		emissivep = LLViewerTextureManager::getFetchedTexture(emissive_imgp);
	}
}

LLColor4 LLGLTFHelper::getColor(const std::vector<double>& in)
{
	LLColor4 out;
	for (S32 i = 0, count = llmin((S32)in.size(), 4); i < count; ++i)
	{
		out.mV[i] = in[i];
	}
	return out;
}

LLPointer<LLImageRaw> LLGLTFHelper::getTexture(const std::string& folder,
											   const LLGLTF::Asset& asset,
											   S32 tex_idx, std::string& name,
											   bool flip)
{
	if (tex_idx < 0 || (size_t)tex_idx >= asset.mTextures.size())
	{
		llwarns << "Invalid texture index: " << tex_idx << llendl;
		return NULL;
	}

	S32 src_idx = asset.mTextures[tex_idx].mSource;
	if (src_idx < 0 || (size_t)src_idx >= asset.mImages.size())
	{
		llwarns << "Invalid texture source index: " << src_idx << llendl;
		return NULL;
	}

	const auto& image = asset.mImages[src_idx];
	name = image.mName;

	LLPointer<LLImageRaw> rawp;

	if (image.mBufferView != LLGLTF::INVALID_INDEX &&
		(size_t)image.mBufferView < asset.mBufferViews.size())
	{
		const U8* datap = NULL;
		U32 data_size = 0;
		const auto& bv = asset.mBufferViews[image.mBufferView];
		if (bv.mBuffer != LLGLTF::INVALID_INDEX &&
			(size_t)bv.mBuffer < asset.mBuffers.size())
		{
			const auto& buf = asset.mBuffers[bv.mBuffer];
			if ((size_t)(bv.mByteOffset + bv.mByteLength) <= buf.mData.size())
			{
				datap = (const U8*)buf.mData.data() + bv.mByteOffset;
				data_size = (U32)bv.mByteLength;
				LL_DEBUGS("GLTF") << "Image data recovered from buffer view "
								  << bv.mBuffer << " - Data size: "
								  << data_size << " bytes." << LL_ENDL;
			}
		}
		if (!datap || !data_size)
		{
			llwarns << "Failed to buffer view " << image.mBufferView << llendl;
			return rawp;
		}
		rawp = raw_image_from_memory(datap, data_size, image.mMimeType);
		if (rawp.isNull())
		{
			llwarns << "Could not convert data to raw image." << llendl;
		}
		else
		{
			if (flip)
			{
				rawp->verticalFlip();
			}
			rawp->optimizeAwayAlpha();
		}
	}
	else if (!image.mUri.empty())
	{
		LL_DEBUGS("GLTF") << "Recovering Image data from file URI: "
						  << image.mUri << LL_ENDL;
		// External file URI - load from disk relative to asset
		std::string folder = gDirUtil.getDirName(asset.mFilename);
		std::string filepath = folder + LL_DIR_DELIM_STR + image.mUri;
		if (!LLFile::exists(filepath))
		{
			// URI might be escaped, unescape
			filepath = folder + LL_DIR_DELIM_STR + LLURI::unescape(image.mUri);
		}
		if (!LLFile::exists(filepath))
		{
			llwarns << "Could not find a file for image URI: " << image.mUri
					<< llendl;
			return rawp;
		}
		rawp = new LLImageRaw;
		if (!LLViewerTextureList::createRawImageFromFile(filepath, rawp))
		{
			llwarns << "Failed to load file '" << filepath
					<< "' for image URI: " << image.mUri << llendl;
			rawp = NULL;
			return rawp;
		}
		LL_DEBUGS("GLTF") << "Image loaded from file " << filepath << LL_ENDL;
		if (flip)
		{
			rawp->verticalFlip();
		}
		rawp->optimizeAwayAlpha();
	}

	return rawp;
}

LLPointer<LLImageRaw> LLGLTFHelper::getTexture(const std::string& folder,
									 const LLGLTF::Asset& asset, S32 tex_idx,
									 bool flip)
{
	std::string name;
	return getTexture(folder, asset, tex_idx, name, flip);
}

bool LLGLTFHelper::loadModel(const std::string& filename, LLGLTF::Asset& asset_out)
{
	// Note: my implementation of LLGLTF::Asset::load() takes care about all
	// checks (with proper warnings issued) and does not throw. HB
	if (!asset_out.load(filename, false))
	{
		return false;
	}

	if (asset_out.mMaterials.empty())
	{
		// Materials are missing
		llwarns << "Cannot load. File has no materials " << filename << llendl;
		return false;
	}

	return true;
}

bool LLGLTFHelper::getMaterialFromModel(const std::string& filename,
										const LLGLTF::Asset& asset,
										S32 mat_idx,
										LLFetchedGLTFMaterial* matp,
										std::string& mat_name, bool flip)
{
	if (!matp)
	{
		llwarns << "NULL material passed for " << filename << llendl;
		llassert(false);
		return false;
	}

	if ((S32)asset.mMaterials.size() <= mat_idx)
	{
		// Materials are missing
		llwarns << "Cannot load. No material at index " << mat_idx
				<< " in file " << filename << llendl;
		return false;
	}

	const auto& mat_in = asset.mMaterials[mat_idx];
	mat_name = mat_in.mName;

	// Serialize material to JSON, then load via fromJSON to populate
	// LLGLTFMaterial fields
	try
	{
		lljson doc = lljson::object();

		lljson asset_obj = lljson::object();
		asset_obj["version"] = "2.0";
		doc["asset"] = asset_obj;

		// Serialize the material
		lljson mat_json = lljson::object();
		mat_in.serialize(mat_json);

		// Serialize referenced textures and images
		lljson textures_arr = lljson::array();
		lljson images_arr = lljson::array();

		auto serialize_tex_idx =
			[&](S32 tex_idx)
			{
				if (tex_idx < 0 || (size_t)tex_idx >= asset.mTextures.size())
				{
					return;
				}
				const auto& tex = asset.mTextures[tex_idx];
				// Ensure we have entries up to the needed index
				while ((S32)textures_arr.size() <= tex_idx)
				{
					textures_arr.push_back(lljson::object());
				}
				lljson tex_obj = lljson::object();
				tex_obj["source"] = tex.mSource;
				textures_arr[(size_t)tex_idx] = tex_obj;

				if (tex.mSource >= 0 &&
					(size_t)tex.mSource < asset.mImages.size())
				{
					while ((S32)images_arr.size() <= tex.mSource)
					{
						images_arr.push_back(lljson::object());
					}
					// For material system, URIs are UUIDs, but not from file;
					// we use a placeholder and rely on texture loading below.
					lljson img_obj = lljson::object();
					img_obj["uri"] = LLUUID::null.asString();
					images_arr[(size_t)tex.mSource] = img_obj;
				}
			};

		serialize_tex_idx(mat_in.mPbrMetallicRoughness.mBaseColorTexture.mIndex);
		serialize_tex_idx(mat_in.mNormalTexture.mIndex);
		serialize_tex_idx(mat_in.mPbrMetallicRoughness.mMetallicRoughnessTexture.mIndex);
		serialize_tex_idx(mat_in.mEmissiveTexture.mIndex);
		serialize_tex_idx(mat_in.mOcclusionTexture.mIndex);

		doc["materials"] = lljson::array({ mat_json });
		if (!textures_arr.empty())
		{
			doc["textures"] = textures_arr;
		}
		if (!images_arr.empty())
		{
			doc["images"] = images_arr;
		}

		std::string json_str = doc.dump();
		std::string warn_msg, error_msg;
		matp->fromJSON(json_str, warn_msg, error_msg);
	}
	catch (const lljson::exception& e)
	{
		llwarns << "Encountered nlohmann::json error: " << e.what() << llendl;
		return false;
	}

	std::string folder = gDirUtil.getDirName(filename);

	// Get base color texture
	LLPointer<LLImageRaw> base_imgp =
		getTexture(folder, asset,
				   mat_in.mPbrMetallicRoughness.mBaseColorTexture.mIndex,
				   flip);
	// Get normal map
	LLPointer<LLImageRaw> norm_imgp = getTexture(folder, asset,
												 mat_in.mNormalTexture.mIndex,
												 flip);
	// Get metallic-roughness texture
	LLPointer<LLImageRaw> mr_imgp =
		getTexture(folder, asset,
				   mat_in.mPbrMetallicRoughness.mMetallicRoughnessTexture.mIndex,
				   flip);
	// Get emissive texture
	LLPointer<LLImageRaw> em_imgp = getTexture(folder, asset,
											   mat_in.mEmissiveTexture.mIndex,
											   flip);
	// Get occlusion map if needed
	LLPointer<LLImageRaw> occl_imgp;
	if (mat_in.mOcclusionTexture.mIndex !=
			mat_in.mPbrMetallicRoughness.mMetallicRoughnessTexture.mIndex)
	{
		occl_imgp = getTexture(folder, asset, mat_in.mOcclusionTexture.mIndex,
							   flip);
	}

	LLPointer<LLViewerFetchedTexture> basecolp;
	LLPointer<LLViewerFetchedTexture> normalp;
	LLPointer<LLViewerFetchedTexture> mrp;
	LLPointer<LLViewerFetchedTexture> emissivep;
	initFetchedTextures(mat_in, base_imgp, norm_imgp, mr_imgp, em_imgp,
						occl_imgp, basecolp, normalp, mrp, emissivep);

	constexpr F32 VIRTUAL_SIZE = 64.f * 64.f;

	if (basecolp)
	{
		basecolp->addTextureStats(VIRTUAL_SIZE);
		matp->mTextureId[BASECOLIDX] = basecolp->getID();
	}
	else
	{
		matp->mTextureId[BASECOLIDX] = LLUUID::null;
	}
	matp->mBaseColorTexture = basecolp;

	if (normalp)
	{
		normalp->addTextureStats(VIRTUAL_SIZE);
		matp->mTextureId[NORMALIDX] = normalp->getID();
	}
	else
	{
		matp->mTextureId[NORMALIDX] = LLUUID::null;
	}
	matp->mNormalTexture = normalp;

	if (mrp)
	{
		mrp->addTextureStats(VIRTUAL_SIZE);
		matp->mTextureId[MROUGHIDX] = mrp->getID();
	}
	else
	{
		matp->mTextureId[MROUGHIDX] = LLUUID::null;
	}
	matp->mMetallicRoughnessTexture = mrp;

	if (emissivep)
	{
		emissivep->addTextureStats(VIRTUAL_SIZE);
		matp->mTextureId[EMISSIVEIDX] = emissivep->getID();
	}
	else
	{
		matp->mTextureId[EMISSIVEIDX] = LLUUID::null;
	}
	matp->mEmissiveTexture = emissivep;

	matp->bumpVersion();

	return true;
}

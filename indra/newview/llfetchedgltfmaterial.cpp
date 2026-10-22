/**
 * @file llfetchedgltfmaterial.cpp
 *
 * $LicenseInfo:firstyear=2022&license=viewergpl$
 *
 * Copyright (c) 2022, Linden Research, Inc.
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

#include "llfetchedgltfmaterial.h"

#include "llshadermgr.h"

#include "llgltfmaterialpreview.h"
#include "lllocalbitmaps.h"
#include "llpipeline.h"
#include "llviewertexturelist.h"

LLFetchedGLTFMaterial& LLFetchedGLTFMaterial::operator=(const LLFetchedGLTFMaterial& rhs)
{
	LLGLTFMaterial::operator=(rhs);
	mBaseColorTexture = rhs.mBaseColorTexture;
	mNormalTexture = rhs.mNormalTexture;
	mMetallicRoughnessTexture = rhs.mMetallicRoughnessTexture;
	mEmissiveTexture = rhs.mEmissiveTexture;
	mPreview = rhs.mEmissiveTexture;
	return *this;
}

void LLFetchedGLTFMaterial::clearFetchedTextures()
{
	mBaseColorTexture = NULL;
	mNormalTexture = NULL;
	mMetallicRoughnessTexture = NULL;
	mEmissiveTexture = NULL;
	mPreview = NULL;
}

void LLFetchedGLTFMaterial::bind(LLViewerTexture* media_texp, F32 vsize)
{
	if (!gUsePBRShaders)
	{
		return;
	}

	LLGLSLShader* shaderp = LLGLSLShader::sCurBoundShaderPtr;
	if (!shaderp)	// Paranoia
	{
		llwarns << "No bound shader !" << llendl;
		return;
	}

	// Override emissive and base color textures with media texture if present
	LLViewerTexture* basecolorp;
	LLViewerTexture* emissivep;
	if (media_texp)
	{
		basecolorp = emissivep = media_texp;
	}
	else
	{
		basecolorp = mBaseColorTexture;
		emissivep = mEmissiveTexture;
	}

	// glTF 2.0 Specification 3.9.4. Alpha Coverage: mAlphaCutoff is only valid
	// for LLGLTFMaterial::ALPHA_MODE_MASK.
	bool is_alpha_mask = mAlphaMode == ALPHA_MODE_MASK;
	if (is_alpha_mask || !LLPipeline::sShadowRender)
	{
		F32 min_alpha = is_alpha_mask ? mAlphaCutoff : -1.f;
		shaderp->uniform1f(LLShaderMgr::MINIMUM_ALPHA, min_alpha);
	}

	if (basecolorp)
	{
		shaderp->bindTexture(LLShaderMgr::DIFFUSE_MAP, basecolorp);
		basecolorp->addTextureStats(vsize);
	}
	else
	{
		shaderp->bindTexture(LLShaderMgr::DIFFUSE_MAP,
							 LLViewerFetchedTexture::sWhiteImagep);
	}

	static F32 p_basecol[8], p_normal[8], p_roughness[8], p_emissive[8];

	mTextureTransform[BASECOLIDX].getPacked(p_basecol);
	shaderp->uniform4fv(LLShaderMgr::TEXTURE_BASE_COLOR_TRANSFORM, 2,
						p_basecol);

	if (LLPipeline::sShadowRender)
	{
		return;	// Nothing else to do.
	}

	if (mNormalTexture.notNull() && mNormalTexture->getDiscardLevel() <= 4)
	{
		shaderp->bindTexture(LLShaderMgr::BUMP_MAP, mNormalTexture);
		mNormalTexture->addTextureStats(vsize);
	}
	else
	{
		shaderp->bindTexture(LLShaderMgr::BUMP_MAP,
							 LLViewerFetchedTexture::sFlatNormalImagep);
	}
	if (mMetallicRoughnessTexture.notNull())
	{
		// PBR linear packed Occlusion, Roughness, Metal.
		shaderp->bindTexture(LLShaderMgr::SPECULAR_MAP,
							 mMetallicRoughnessTexture);
		mMetallicRoughnessTexture->addTextureStats(vsize);
	}
	else
	{
		shaderp->bindTexture(LLShaderMgr::SPECULAR_MAP,
							 LLViewerFetchedTexture::sWhiteImagep);
	}
	if (emissivep)
	{
		// PBR sRGB Emissive
		shaderp->bindTexture(LLShaderMgr::EMISSIVE_MAP, emissivep);
		emissivep->addTextureStats(vsize);
	}
	else
	{
		shaderp->bindTexture(LLShaderMgr::EMISSIVE_MAP,
							 LLViewerFetchedTexture::sWhiteImagep);
	}

	// Note: base color factor is baked into vertex stream.
	shaderp->uniform1f(LLShaderMgr::ROUGHNESS_FACTOR, mRoughnessFactor);
	shaderp->uniform1f(LLShaderMgr::METALLIC_FACTOR, mMetallicFactor);
	shaderp->uniform3fv(LLShaderMgr::EMISSIVE_COLOR, 1, mEmissiveColor.mV);

	mTextureTransform[NORMALIDX].getPacked(p_normal);
	shaderp->uniform4fv(LLShaderMgr::TEXTURE_NORMAL_TRANSFORM, 2, p_normal);

	mTextureTransform[MROUGHIDX].getPacked(p_roughness);
	shaderp->uniform4fv(LLShaderMgr::TEXTURE_ROUGHNESS_TRANSFORM, 2,
						p_roughness);

	mTextureTransform[EMISSIVEIDX].getPacked(p_emissive);
	shaderp->uniform4fv(LLShaderMgr::TEXTURE_EMISSIVE_TRANSFORM, 2,
						p_emissive);
}

void LLFetchedGLTFMaterial::onMaterialComplete(std::function<void()> cb)
{
	if (cb)
	{
		if (!mFetching)
		{
			cb();
			return;
		}
		mCompleteCallbacks.emplace_back(cb);
	}
}

void LLFetchedGLTFMaterial::materialComplete(bool success)
{
	mFetching = false;
	mFetchSuccess = success;
	for (U32 i = 0, count = mCompleteCallbacks.size(); i < count; ++i)
	{
		mCompleteCallbacks[i]();
	}
	mCompleteCallbacks.clear();
}

static LLViewerFetchedTexture* fetch_texture(const LLUUID& id)
{
	if (id.isNull())
	{
		return NULL;
	}
	LLViewerFetchedTexture* texp =
		LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, true,
												  LLGLTexture::BOOST_NONE,
												  LLViewerTexture::LOD_TEXTURE);
	if (texp)	// Paranoia
	{
		texp->addTextureStats(64.f * 64.f);
	}
	return texp;
}

//virtual
bool LLFetchedGLTFMaterial::replaceLocalTexture(const LLUUID& tracking_id,
												const LLUUID& old_id,
												const LLUUID& new_id)
{
	bool seen = false;

	if (mTextureId[BASECOLIDX] == old_id)
	{
		mTextureId[BASECOLIDX] = new_id;
		mBaseColorTexture = fetch_texture(new_id);
		seen = true;
	}
	if (mTextureId[NORMALIDX] == old_id)
	{
		mTextureId[NORMALIDX] = new_id;
		mNormalTexture = fetch_texture(new_id);
		seen = true;
	}
	if (mTextureId[MROUGHIDX] == old_id)
	{
		mTextureId[MROUGHIDX] = new_id;
		mMetallicRoughnessTexture = fetch_texture(new_id);
		seen = true;
	}
	if (mTextureId[EMISSIVEIDX] == old_id)
	{
		mTextureId[EMISSIVEIDX] = new_id;
		mEmissiveTexture = fetch_texture(new_id);
		seen = true;
	}

	for (U32 i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
	{
		if (mTextureId[i] == new_id)
		{
			seen = true;
		}
	}
	if (seen)
	{
		mTrackingIdToLocalTexture[tracking_id] = new_id;
	}
	else
	{
		mTrackingIdToLocalTexture.erase(tracking_id);
	}
	updateLocalTexDataDigest();

	return seen;
}

//virtual
void LLFetchedGLTFMaterial::addTextureEntry(LLTextureEntry* tep)
{
	mTextureEntries.insert(tep);
}

//virtual
void LLFetchedGLTFMaterial::removeTextureEntry(LLTextureEntry* tep)
{
	mTextureEntries.erase(tep);
}

//virtual
void LLFetchedGLTFMaterial::updateTextureTracking()
{
	if (!mTrackingIdToLocalTexture.empty())
	{
		for (local_tex_map_t::const_iterator
				it = mTrackingIdToLocalTexture.begin(),
				end = mTrackingIdToLocalTexture.end();
			 it != end; ++it)
		{
			LLLocalBitmap::associateGLTFMaterial(it->first, this);
		}
	}
}

LLViewerTexture* LLFetchedGLTFMaterial::getPreview()
{
	if (mPreview.isNull())
	{
		mPreview = LLGLTFPreviewTexture::getPreview(this);
	}
	return mPreview.get();
}

// ----------------------------------------------------------------------------
// These are functions used in the llgltf library to deal with fetched
// materials textures without knowing anything about them excepted that they
// derive from the parent LLGLTexture class... HB
// Merely implemented here because they reuse fetch_texture() which we need in
// this module anyway, and also because this module uses LLLocalBitmap already.
// Passed via LLGLTF::Asset::setHooks() by LLGLTFSceneManager::init(). HB

LLGLTexture* gl_texture_from_fetched(const LLUUID& id)
{
	return fetch_texture(id);
}

LLGLTexture* gl_texture_from_local_file(const std::string& filename)
{
	if (!LLFile::exists(filename))
	{
		return NULL;
	}
	LLLocalBitmap* bitmapp = LLLocalBitmap::addUnit(filename);
	if (!bitmapp)
	{
		return NULL;
	}
	LLViewerFetchedTexture* texp = fetch_texture(bitmapp->getWorldID());
	if (texp)
	{
		// Boost image so that it does not discard and force to save raw image
		// in case we save out or upload.
		texp->setBoostLevel(LLGLTexture::BOOST_PREVIEW);
		texp->dontDiscard();
#if !LL_IMPLICIT_SETNODELETE
		texp->setNoDelete();
#endif
		texp->forceToSaveRawImage(0, F32_MAX);
	}
	return texp;
}

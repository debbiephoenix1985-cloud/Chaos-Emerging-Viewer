/**
 * @file llgltfmaterial.cpp
 * @brief LLGLTFMaterial implementation
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

#include "linden_common.h"

#include <sstream>
#include <utility>

#include "json.hpp"

#include "llgltfmaterial.h"

#include "hbxxh.h"

using lljson = nlohmann::json;

// Let's make things look as simple as get<std::string> while avoiding a
// costly string copy. HB
#define get_string template get_ref<const lljson::string_t&>

static const char* const GLTF_FILE_EXT_TF = "KHR_texture_transform";
static const char* const GLTF_FILE_EXT_TF_SCALE = "scale";
static const char* const GLTF_FILE_EXT_TF_OFFSET = "offset";
static const char* const GLTF_FILE_EXT_TF_ROTATION = "rotation";

// Special UUID that indicates a null UUID in override data
static const LLUUID GLTF_OVERRIDE_NULL_UUID =
	LLUUID("ffffffff-ffff-ffff-ffff-ffffffffffff");

const char* const LLGLTFMaterial::ASSET_TYPE = "GLTF 2.0";
const char* const LLGLTFMaterial::ASSET_VERSION = "1.1";
// Make a static default material for accessors
const LLGLTFMaterial LLGLTFMaterial::sDefault;

LLGLTFMaterial::LLGLTFMaterial()
:	mVersion(1),
	mHashVersion(0)
{
	// IMPORTANT: since we use the hash of the member variables memory block of
	// this class to detect changes, we must ensure that all its padding bytes
	// have been zeroed out. But of course, we must leave the LLRefCount member
	// variable untouched (and skip it when hashing), and we cannot either
	// touch the local texture overrides map (else we destroy pointers, and
	// sundry private data, which would lead to a crash when using that map).
	// The variable members have therefore been arranged so that anything,
	// starting at mLocalTexDataDigest and up to the end of the members, can be
	// safely zeroed. HB
	const size_t offset = intptr_t(&mLocalTexDataDigest) - intptr_t(this);
	memset((void*)((const char*)this + offset), 0, sizeof(*this) - offset);

	// Now that we zeroed out our member variables, we can set the ones that
	// should not be zero to their default value. HB
	mBaseColor.set(1.f, 1.f, 1.f, 1.f);
	mMetallicFactor = mRoughnessFactor = 1.f;
	mAlphaCutoff = 0.5f;
	for (U32 i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
	{
		mTextureTransform[i].mScale.set(1.f, 1.f);
#if 0
		mTextureTransform[i].mOffset.clear();
		mTextureTransform[i].mRotation = 0.f;
#endif	
	}
#if 0
	mLocalTexDataDigest = 0;
	mAlphaMode = ALPHA_MODE_OPAQUE;	// This is 0
	mOverrideDoubleSided = mOverrideAlphaMode = false;
#endif
}

// We have to do a manual operator= because of LLRefCount
LLGLTFMaterial& LLGLTFMaterial::operator=(const LLGLTFMaterial& rhs)
{
	mTextureId = rhs.mTextureId;
	mTextureTransform = rhs.mTextureTransform;
	mBaseColor = rhs.mBaseColor;
	mEmissiveColor = rhs.mEmissiveColor;
	mMetallicFactor = rhs.mMetallicFactor;
	mRoughnessFactor = rhs.mRoughnessFactor;
	mAlphaCutoff = rhs.mAlphaCutoff;
	mDoubleSided = rhs.mDoubleSided;
	mAlphaMode = rhs.mAlphaMode;
	mOverrideDoubleSided = rhs.mOverrideDoubleSided;
	mOverrideAlphaMode = rhs.mOverrideAlphaMode;
	if (rhs.mTrackingIdToLocalTexture.empty())
	{
		mTrackingIdToLocalTexture.clear();
		mLocalTexDataDigest = 0;
		++mVersion;
	}
	else
	{
		mTrackingIdToLocalTexture = rhs.mTrackingIdToLocalTexture;
		updateLocalTexDataDigest();
		updateTextureTracking();
	}
	return *this;
}

void LLGLTFMaterial::updateLocalTexDataDigest()
{
	++mVersion;
	mLocalTexDataDigest = 0;
	if (!mTrackingIdToLocalTexture.empty())
	{
		for (local_tex_map_t::const_iterator
				it = mTrackingIdToLocalTexture.begin(),
				end = mTrackingIdToLocalTexture.end();
			 it != end; ++it)
		{
			mLocalTexDataDigest ^= it->first.getDigest64() ^
								   it->second.getDigest64();
		}
	}
}

const LLUUID& LLGLTFMaterial::getHash() const
{
	if (mHashVersion != mVersion)
	{
		mHashVersion = mVersion;
		// Hash the bytes of this object starting with the mLocalTexDataDigest
		// member variable. Note: this only works properly to compare two
		// LLGLTFMaterial instances because the padding bytes between their
		// member variables have been dutifully zeroed in the constructor. HB
		const size_t offset = intptr_t(&mLocalTexDataDigest) - intptr_t(this);
		mHash = HBXXH128::digest((const void*)((const char*)this + offset),
								 sizeof(*this) - offset);
	}
	return mHash;
}

bool LLGLTFMaterial::fromJSON(const std::string& json_str,
							  std::string& warn_msg, std::string& error_msg)
{
	try
	{
		lljson json = lljson::parse(json_str);
		return setFromDocument(json, 0);
	}
	catch (const lljson::exception& e)
	{
		error_msg = e.what();
		return false;
	}
}

std::string LLGLTFMaterial::asJSON(bool prettyprint) const
{
	std::string json_str;
	try
	{
		json_str = writeDocument().dump(prettyprint ? 4 : -1);
	}
	catch (const lljson::exception& e)
	{
		llwarns << "Encountered nlohmann::json error: " << e.what() << llendl;
	}
	return json_str;
}

//static
const std::string& LLGLTFMaterial::getTextureURI(const lljson& doc,
												 S32 tex_idx)
{
	// For disguised 'goto error;'. HB
	while (tex_idx >= 0)
	{
		auto it = doc.find("textures");
		if (it == doc.end() || !it->is_array()) break;

		const lljson& tex_array = *it;
		if ((size_t)tex_idx >= tex_array.size()) break;

		const lljson& tex = tex_array[tex_idx];
		if (!tex.is_object()) break;

		it = tex.find("source");
		if (it == tex.end() || !it->is_number_integer()) break;

		S32 src_idx = it->get<int>();
		if (src_idx < 0) break;

		it = doc.find("images");
		if (it == doc.end() || !it->is_array()) break;
	
		const lljson& img_array = *it;
		if ((size_t)tex_idx >= img_array.size()) break;

		const lljson& img = img_array[src_idx];
		if (!img.is_object()) break;

		it = img.find("uri");
		if (it == img.end() || !it->is_string()) break;

		return it->get_string();
	}
	return LLStringUtil::null;
}

//static
void LLGLTFMaterial::readTextureInfo(const lljson& doc, const lljson& tex_info,
									 LLUUID& tex_id, TextureTransform& tf)
{
	auto it = tex_info.find("index");
	if (it == tex_info.end() || !it->is_number_integer()) return;

	S32 tex_idx = it->get<int>();
	tex_id.set(getTextureURI(doc, tex_idx));

	it = tex_info.find("extensions");
	if (it == tex_info.end() || !it->is_object()) return;

	it = it->find(GLTF_FILE_EXT_TF);
	if (it == it->end() || !it->is_object()) return;

	tf.mOffset = vec2FromJson(*it, GLTF_FILE_EXT_TF_OFFSET,
							  getDefaultTextureOffset());
	tf.mScale = vec2FromJson(*it, GLTF_FILE_EXT_TF_SCALE,
							 getDefaultTextureScale());
	tf.mRotation = floatFromJson(*it, GLTF_FILE_EXT_TF_ROTATION,
								 getDefaultTextureRotation());
}

void LLGLTFMaterial::readTextureInfo(const lljson& doc, const lljson& tex_info,
									 U32 id)
{
    readTextureInfo(doc, tex_info, mTextureId[id], mTextureTransform[id]);
}

//static
LLVector2 LLGLTFMaterial::vec2FromJson(const lljson& obj, const char* key,
									   const LLVector2& default_value)
{
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_array()) return default_value;

	const auto& array = *it;
	if ((U32)array.size() < LENGTHOFVECTOR2) return default_value;

	LLVector2 result;
	for (U32 i = 0; i < LENGTHOFVECTOR2; ++i)
	{
		if (!array[i].is_number()) return default_value;
		result.mV[i] = (F32)array[i].get<double>();
	}
	return result;
}

//static
F32 LLGLTFMaterial::floatFromJson(const lljson& obj, const char* key,
								  F32 default_value)
{
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_number()) return default_value;
	return (F32)it->get<double>();
}

bool LLGLTFMaterial::setFromDocument(const lljson& doc, S32 mat_idx)
{
	if (mat_idx < 0) return false;

	auto it = doc.find("materials");
	if (it == doc.end() || !it->is_array()) return false;

	const lljson& materials = *it;
	if ((size_t)mat_idx >= materials.size()) return false;

	const auto& mat = materials[mat_idx];
	if (!mat.is_object()) return false;

	++mVersion;

	const auto& mat_end = mat.end();

	// PBR metallic roughness
	it = mat.find("pbrMetallicRoughness");
	if (it != mat_end && it->is_object())
	{
		const auto& pbr = *it;
		const auto& pbr_end = pbr.end();

		it = pbr.find("baseColorTexture");
		if (it != pbr_end && it->is_object())
		{
			readTextureInfo(doc, *it, BASECOLIDX);
		}

		it = pbr.find("metallicRoughnessTexture");
		if (it != pbr_end && it->is_object())
		{
			readTextureInfo(doc, *it, MROUGHIDX);
		}

		it = pbr.find("baseColorFactor");
		if (it != pbr_end && it->is_array())
		{
			const auto& array = *it;
			mBaseColor.set((F32)array[0].get<double>(),
						   (F32)array[1].get<double>(),
						   (F32)array[2].get<double>(),
						   (F32)array[3].get<double>());
		}

		it = pbr.find("metallicFactor");
		if (it != pbr_end && it->is_number())
		{
			mMetallicFactor = llclamp((F32)it->get<double>(), 0.f, 1.f);
		}

		it = pbr.find("roughnessFactor");
		if (it != pbr_end && it->is_number())
		{
			mRoughnessFactor = llclamp((F32)it->get<double>(), 0.f, 1.f);
		}
	}

	// Normal texture
	it = mat.find("normalTexture");
	if (it != mat_end && it->is_object())
	{
		readTextureInfo(doc, *it, GLTF_TEXTURE_INFO_NORMAL);
	}

	// Emissive texture
	it = mat.find("emissiveTexture");
	if (it != mat_end && it->is_object())
	{
		readTextureInfo(doc, *it, GLTF_TEXTURE_INFO_EMISSIVE);
	}

	// Alpha mode
	it = mat.find("alphaMode");
	if (it != mat_end && it->is_string())
	{
		setAlphaMode(it->get_string());
	}

	// Alpha cutoff
	it = mat.find("alphaCutoff");
	if (it != mat_end && it->is_number())
	{
		mAlphaCutoff = llclamp((F32)it->get<double>(), 0.f, 1.f);
	}

	// Emissive factor
	it = mat.find("emissiveFactor");
	if (it != mat_end && it->is_array())
	{
		const auto& arr = *it;
		if (arr.size() >= 3)
		{
			mEmissiveColor.set(
				(F32)arr[0].get<double>(),
				(F32)arr[1].get<double>(),
				(F32)arr[2].get<double>());
		}
	}

	// Double sided
	it = mat.find("doubleSided");
	if (it != mat_end && it->is_boolean())
	{
		mDoubleSided = it->get<bool>();
	}

	// Extras
	it = mat.find("extras");
	if (it != mat_end && it->is_object())
	{
		const auto& extras = *it;
		const auto& extras_end = extras.end();

		it = extras.find("override_alpha_mode");
		if (it != extras_end && it->is_boolean())
		{
			mOverrideAlphaMode = it->get<bool>();
		}

		it = extras.find("override_double_sided");
		if (it != extras_end && it->is_boolean())
		{
			mOverrideDoubleSided = it->get<bool>();
		}
	}

	return true;
}

//static
void LLGLTFMaterial::writeTextureEntry(lljson& images, lljson& textures,
									   lljson& dst, const char* key,
									   const LLUUID& tex_id,
									   const TextureTransform& tf,
									   bool force_write)
{
	bool is_blank_transform = tf == sDefault.mTextureTransform[0];
	if (!force_write && tex_id.isNull() && is_blank_transform)
	{
		return;
	}

	// Add image entry
	S32 image_idx = (S32)images.size();

	lljson image_obj;
	image_obj["uri"] = tex_id.asString();

	images.push_back(image_obj);

	// Add texture entry
	S32 texture_idx = (S32)textures.size();

	lljson tex_obj;
	tex_obj["source"] = image_idx;

	textures.push_back(tex_obj);

	// Build texture info
	lljson tex_info;
	tex_info["index"] = texture_idx;

	if (!is_blank_transform)
	{
		lljson tf_obj;

		tf_obj[GLTF_FILE_EXT_TF_OFFSET] = { tf.mOffset.mV[VX],
											tf.mOffset.mV[VY] };

		tf_obj[GLTF_FILE_EXT_TF_SCALE] = { tf.mScale.mV[VX],
										   tf.mScale.mV[VY] };

		tf_obj[GLTF_FILE_EXT_TF_ROTATION] = tf.mRotation;

		lljson extensions;
		extensions[GLTF_FILE_EXT_TF] = tf_obj;
		tex_info["extensions"] = extensions;
	}

	dst[key] = tex_info;
}

lljson LLGLTFMaterial::writeDocument() const
{
	lljson root = lljson::object();

	// Asset info
	lljson asset = lljson::object();
	asset["version"] = "2.0";
	root["asset"] = asset;

	lljson images = lljson::array();
	lljson textures = lljson::array();

	// Build material
	lljson mat_obj = nlohmann::json::object();

	// PBR metallic roughness

	lljson pbr = nlohmann::json::object();

	bool has_pbr = false;
	// Base color texture
	writeTextureEntry(images, textures, pbr, "baseColorTexture",
					  mTextureId[BASECOLIDX], mTextureTransform[BASECOLIDX]);
	if (pbr.contains("baseColorTexture"))
	{
		has_pbr = true;
	}

	// Metallic roughness texture
	writeTextureEntry(images, textures, pbr, "metallicRoughnessTexture",
					  mTextureId[MROUGHIDX], mTextureTransform[MROUGHIDX]);
	if (pbr.contains("metallicRoughnessTexture"))
	{
		has_pbr = true;
	}

	// Base color factor
	lljson bcf = { (F64)mBaseColor.mV[VRED], (F64)mBaseColor.mV[VGREEN],
				   (F64)mBaseColor.mV[VBLUE], (F64)mBaseColor.mV[VALPHA] };
	if (mBaseColor != getDefaultBaseColor())
	{
		pbr["baseColorFactor"] = bcf;
		has_pbr = true;
	}

	// Metallic factor
	if (mMetallicFactor != getDefaultMetallicFactor())
	{
		pbr["metallicFactor"] = (F64)mMetallicFactor;
		has_pbr = true;
	}

	// Roughness factor
	if (mRoughnessFactor != getDefaultRoughnessFactor())
	{
		pbr["roughnessFactor"] = (F64)mRoughnessFactor;
		has_pbr = true;
	}

	if (has_pbr)
	{
		// Always write base color factor if we have PBR at all
		if (!pbr.contains("baseColorFactor"))
		{
			pbr["baseColorFactor"] = bcf;
		}

		if (!pbr.contains("metallicFactor"))
		{
			pbr["metallicFactor"] = (F64)mMetallicFactor;
		}

		if (!pbr.contains("roughnessFactor"))
		{
			pbr["roughnessFactor"] = (F64)mRoughnessFactor;
		}

		mat_obj["pbrMetallicRoughness"] = pbr;
	}

	// Normal texture
	lljson normal_dst = lljson::object();
	writeTextureEntry(images, textures, normal_dst, "normalTexture_tmp",
					  mTextureId[NORMALIDX], mTextureTransform[NORMALIDX]);

	if (normal_dst.contains("normalTexture_tmp"))
	{
		mat_obj["normalTexture"] = normal_dst["normalTexture_tmp"];
	}

	// Emissive texture
	lljson emissive_dst = lljson::object();
	writeTextureEntry(images, textures, emissive_dst, "emissiveTexture_tmp",
		mTextureId[EMISSIVEIDX],
		mTextureTransform[EMISSIVEIDX]);

	if (emissive_dst.contains("emissiveTexture_tmp"))
	{
		mat_obj["emissiveTexture"] = emissive_dst["emissiveTexture_tmp"];
	}

	// Occlusion texture (GLTF compliance for ORM)
	lljson occlusion_dst = lljson::object();
	writeTextureEntry(images, textures, occlusion_dst, "occlusionTexture_tmp",
					  mTextureId[GLTF_TEXTURE_INFO_OCCLUSION],
					  mTextureTransform[GLTF_TEXTURE_INFO_OCCLUSION]);
	if (occlusion_dst.contains("occlusionTexture_tmp"))
	{
		mat_obj["occlusionTexture"] = occlusion_dst["occlusionTexture_tmp"];
	}

	mat_obj["alphaMode"] = getAlphaMode();
	mat_obj["alphaCutoff"] = (F64)mAlphaCutoff;
	mat_obj["doubleSided"] = mDoubleSided;

	if (mEmissiveColor != getDefaultEmissiveColor())
	{
		mat_obj["emissiveFactor"] = { (F64)mEmissiveColor.mV[0],
									  (F64)mEmissiveColor.mV[1],
									  (F64)mEmissiveColor.mV[2] };
	}

	// Extras
	lljson extras = lljson::object();
	bool write_extras = false;

	if (mOverrideAlphaMode && mAlphaMode == getDefaultAlphaMode())
	{
		extras["override_alpha_mode"] = true;
		write_extras = true;
	}

	if (mOverrideDoubleSided && mDoubleSided == getDefaultDoubleSided())
	{
		extras["override_double_sided"] = true;
		write_extras = true;
	}

	if (write_extras)
	{
		mat_obj["extras"] = extras;
	}

	lljson materials = lljson::array();
	materials.push_back(mat_obj);
	root["materials"] = materials;

	if (!images.empty())
	{
		root["images"] = images;
	}
	if (!textures.empty())
	{
		root["textures"] = textures;
	}

	return root;
}

bool LLGLTFMaterial::setBaseMaterial()
{
	const LLGLTFMaterial old_override = *this;
	*this = sDefault;
	setBaseMaterial(old_override);
	return *this != old_override;
}

bool LLGLTFMaterial::isClearedForBaseMaterial() const
{
	LLGLTFMaterial cleared_override = sDefault;
	cleared_override.setBaseMaterial(*this);
	return *this == cleared_override;
}

//static
void LLGLTFMaterial::hackOverrideUUID(LLUUID& id)
{
	if (id.isNull())
	{
		id = GLTF_OVERRIDE_NULL_UUID;
	}
}

void LLGLTFMaterial::setBaseColorFactor(const LLColor4& baseColor,
										bool for_override)
{
	++mVersion;
	mBaseColor.set(baseColor);
	mBaseColor.clamp();
	if (for_override)
	{
		// *HACK: nudge off of default value
		if (mBaseColor == getDefaultBaseColor())
		{
			mBaseColor.mV[3] -= FLT_EPSILON;
		}
	}
}

void LLGLTFMaterial::setAlphaCutoff(F32 cutoff, bool for_override)
{
	++mVersion;
	mAlphaCutoff = llclamp(cutoff, 0.f, 1.f);
	if (for_override)
	{
		// *HACK: nudge off of default value
		if (mAlphaCutoff == getDefaultAlphaCutoff())
		{
			mAlphaCutoff -= FLT_EPSILON;
		}
	}
}

void LLGLTFMaterial::setEmissiveColorFactor(const LLColor3& emissiveColor,
											bool for_override)
{
	++mVersion;
	mEmissiveColor = emissiveColor;
	mEmissiveColor.clamp();
	if (for_override)
	{
		// *HACK: nudge off of default value
		if (mEmissiveColor == getDefaultEmissiveColor())
		{
			mEmissiveColor.mV[0] += FLT_EPSILON;
		}
	}
}

void LLGLTFMaterial::setMetallicFactor(F32 metallic, bool for_override)
{
	++mVersion;
	mMetallicFactor = llclamp(metallic, 0.f,
							  for_override ? 1.f - FLT_EPSILON : 1.f);
}

void LLGLTFMaterial::setRoughnessFactor(F32 roughness, bool for_override)
{
	++mVersion;
	mRoughnessFactor = llclamp(roughness, 0.f,
							   for_override ? 1.f - FLT_EPSILON : 1.f);
}

void LLGLTFMaterial::setAlphaMode(const std::string& mode, bool for_override)
{
	S32 m = ALPHA_MODE_OPAQUE; //getDefaultAlphaMode();
	if (mode == "MASK")
	{
		m = ALPHA_MODE_MASK;
	}
	else if (mode == "BLEND")
	{
		m = ALPHA_MODE_BLEND;
	}
	setAlphaMode(m, for_override);
}

const char* LLGLTFMaterial::getAlphaMode() const
{
	switch (mAlphaMode)
	{
		default:
		case ALPHA_MODE_OPAQUE:
			return "OPAQUE";

		case ALPHA_MODE_BLEND:
			return "BLEND";

		case ALPHA_MODE_MASK:
			return "MASK";
	}
}

void LLGLTFMaterial::setAlphaMode(U32 mode, bool for_override)
{
	++mVersion;
	mAlphaMode = llclamp(mode, ALPHA_MODE_OPAQUE, ALPHA_MODE_MASK);
	mOverrideAlphaMode = for_override && mAlphaMode == getDefaultAlphaMode();
}

void LLGLTFMaterial::setDoubleSided(bool double_sided, bool for_override)
{
	++mVersion;
	// Sure, no clamping will ever be needed for a bool, but include the
	// setter for consistency with the clamping API
	mDoubleSided = double_sided;
	mOverrideDoubleSided = for_override &&
						   mDoubleSided == getDefaultDoubleSided();
}

LLSD LLGLTFMaterial::getOverrideLLSD(const LLGLTFMaterial& omat) const
{
	LLSD data;

	// Make every effort to shave off bytes here
	for (U32 i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
	{
		const LLUUID& texture_id = mTextureId[i];
		const LLUUID& override_texture_id = omat.mTextureId[i];
		if (override_texture_id.notNull() && texture_id != override_texture_id)
		{
			data["tex"][i] = LLSD::UUID(override_texture_id);
		}
	}
	if (omat.mBaseColor != sDefault.mBaseColor)
	{
		data["bc"] = omat.mBaseColor.getValue();
	}
	if (omat.mEmissiveColor != sDefault.mEmissiveColor)
	{
		data["ec"] = omat.mEmissiveColor.getValue();
	}
	if (omat.mMetallicFactor != sDefault.mMetallicFactor)
	{
		data["mf"] = omat.mMetallicFactor;
	}
	if (omat.mRoughnessFactor != sDefault.mRoughnessFactor)
	{
		data["rf"] = omat.mRoughnessFactor;
	}
	if (omat.mAlphaMode != sDefault.mAlphaMode || mOverrideAlphaMode)
	{
		data["am"] = omat.mAlphaMode;
	}
	if (omat.mAlphaCutoff != sDefault.mAlphaCutoff)
	{
		data["ac"] = omat.mAlphaCutoff;
	}
	if (omat.mDoubleSided != sDefault.mDoubleSided || omat.mOverrideDoubleSided)
	{
		data["ds"] = omat.mDoubleSided;
	}
	const LLVector2& def_tex_offset = getDefaultTextureOffset();
	const LLVector2& def_tex_scale = getDefaultTextureScale();
	const F32 def_tex_rot = getDefaultTextureRotation();
	for (U32 i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
	{
		if (omat.mTextureTransform[i].mOffset != def_tex_offset)
		{
			data["ti"][i]["o"] = omat.mTextureTransform[i].mOffset.getValue();
		}
		if (omat.mTextureTransform[i].mScale != def_tex_scale)
		{
			data["ti"][i]["s"] = omat.mTextureTransform[i].mScale.getValue();
		}
		if (omat.mTextureTransform[i].mRotation != def_tex_rot)
		{
			data["ti"][i]["r"] = omat.mTextureTransform[i].mRotation;
		}
	}

	return data;
}

void LLGLTFMaterial::applyOverrideLLSD(const LLSD& data)
{
	if (!data.isDefined())
	{
		return;	// Nothing to do. HB
	}

	++mVersion;

	const LLSD& tex = data["tex"];
	if (tex.isArray())
	{
		for (U32 i = 0, count = tex.size(); i < count; ++i)
		{
			mTextureId[i] = tex[i].asUUID();
		}
	}
	const LLSD& bc = data["bc"];
	if (bc.isDefined())
	{
		mBaseColor.setValue(bc);
		if (mBaseColor == getDefaultBaseColor())
		{
			// *HACK: nudge by epsilon if we receive a default value (indicates
			// override to default).
			mBaseColor.mV[3] -= FLT_EPSILON;
		}
	}
	const LLSD& ec = data["ec"];
	if (ec.isDefined())
	{
		mEmissiveColor.setValue(ec);
		if (mEmissiveColor == getDefaultEmissiveColor())
		{
			// *HACK: nudge by epsilon if we receive a default value (indicates
			// override to default).
			mEmissiveColor.mV[0] -= FLT_EPSILON;
		}
	}
	const LLSD& mf = data["mf"];
	if (mf.isReal())
	{
		mMetallicFactor = llclamp(F32(mf.asReal()), 0.f, 1.f);
		if (mMetallicFactor == getDefaultMetallicFactor())
		{
			// *HACK: nudge by epsilon if we receive a default value (indicates
			// override to default).
			mMetallicFactor -= FLT_EPSILON;
		}
	}
	const LLSD& rf = data["rf"];
	if (rf.isReal())
	{
		mRoughnessFactor = llclamp(F32(rf.asReal()), 0.f, 1.f);
		if (mRoughnessFactor == getDefaultRoughnessFactor())
		{
			// *HACK: nudge by epsilon if we receive a default value (indicates
			// override to default).
			mRoughnessFactor -= FLT_EPSILON;
		}
	}
	const LLSD& am = data["am"];
	if (am.isInteger())
	{
		mAlphaMode = llclamp(am.asInteger(), ALPHA_MODE_OPAQUE,
							 ALPHA_MODE_MASK);
		mOverrideAlphaMode = true;
	}
	const LLSD& ac = data["ac"];
	if (ac.isReal())
	{
		mAlphaCutoff = ac.asReal();
		if (mAlphaCutoff == getDefaultAlphaCutoff())
		{
			// *HACK: nudge by epsilon if we receive a default value (indicates
			// override to default).
			mAlphaCutoff -= FLT_EPSILON;
		}
	}
	const LLSD& ds = data["ds"];
	if (ds.isBoolean())
	{
		mDoubleSided = ds.asBoolean();
		mOverrideDoubleSided = true;
	}
	const LLSD& ti = data["ti"];
	if (ti.isArray())
	{
		for (U32 i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
		{
			const LLSD& o = ti[i]["o"];
			if (o.isDefined())
			{
				mTextureTransform[i].mOffset.setValue(o);
			}
			const LLSD& s = ti[i]["s"];
			if (s.isDefined())
			{
				mTextureTransform[i].mScale.setValue(s);
			}
			const LLSD& r = ti[i]["r"];
			if (r.isReal())
			{
				mTextureTransform[i].mRotation = r.asReal();
			}
		}
	}
}

static void apply_override_id(LLUUID& dst_id, const LLUUID& over_id)
{
	if (over_id == GLTF_OVERRIDE_NULL_UUID)
	{
		dst_id.setNull();
	}
	else if (over_id.notNull())
	{
		dst_id = over_id;
	}
}

void LLGLTFMaterial::applyOverride(const LLGLTFMaterial& override_mat)
{
	for (U32 i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
	{
		LLUUID& texture_id = mTextureId[i];
		const LLUUID& override_texture_id = override_mat.mTextureId[i];
		apply_override_id(texture_id, override_texture_id);
	}

	if (override_mat.mBaseColor != getDefaultBaseColor())
	{
		mBaseColor = override_mat.mBaseColor;
	}

	if (override_mat.mEmissiveColor != getDefaultEmissiveColor())
	{
		mEmissiveColor = override_mat.mEmissiveColor;
	}

	if (override_mat.mMetallicFactor != getDefaultMetallicFactor())
	{
		mMetallicFactor = override_mat.mMetallicFactor;
	}

	if (override_mat.mRoughnessFactor != getDefaultRoughnessFactor())
	{
		mRoughnessFactor = override_mat.mRoughnessFactor;
	}

	if (override_mat.mAlphaMode != getDefaultAlphaMode() ||
		override_mat.mOverrideAlphaMode)
	{
		mAlphaMode = override_mat.mAlphaMode;
	}
	if (override_mat.mAlphaCutoff != getDefaultAlphaCutoff())
	{
		mAlphaCutoff = override_mat.mAlphaCutoff;
	}

	if (override_mat.mDoubleSided != getDefaultDoubleSided() ||
		override_mat.mOverrideDoubleSided)
	{
		mDoubleSided = override_mat.mDoubleSided;
	}

	for (U32 i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
	{
		if (override_mat.mTextureTransform[i].mOffset !=
				getDefaultTextureOffset())
		{
			mTextureTransform[i].mOffset =
				override_mat.mTextureTransform[i].mOffset;
		}

		if (override_mat.mTextureTransform[i].mScale !=
				getDefaultTextureScale())
		{
			mTextureTransform[i].mScale =
				override_mat.mTextureTransform[i].mScale;
		}

		if (override_mat.mTextureTransform[i].mRotation !=
				getDefaultTextureRotation())
		{
			mTextureTransform[i].mRotation =
				override_mat.mTextureTransform[i].mRotation;
		}
	}

	if (!override_mat.mTrackingIdToLocalTexture.empty())
	{
		auto it = override_mat.mTrackingIdToLocalTexture.begin();
		mTrackingIdToLocalTexture.insert(it, it);
		updateLocalTexDataDigest();
		updateTextureTracking();
	}
	else
	{
		++mVersion;
	}
}

void LLGLTFMaterial::addLocalTextureTracking(const LLUUID& tracking_id,
											 const LLUUID& tex_id)
{
	mTrackingIdToLocalTexture[tracking_id] = tex_id;
	updateLocalTexDataDigest();
}

void LLGLTFMaterial::removeLocalTextureTracking(const LLUUID& tracking_id)
{
	mTrackingIdToLocalTexture.erase(tracking_id);
	updateLocalTexDataDigest();
}

//virtual
bool LLGLTFMaterial::replaceLocalTexture(const LLUUID& tracking_id,
										 const LLUUID& old_id,
										 const LLUUID& new_id)
{
	bool seen = false;

	for (U32 i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
	{
		if (mTextureId[i] == old_id)
		{
			mTextureId[i] = new_id;
			seen = true;
		}
		else if (mTextureId[i] == new_id)
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

//static
void LLGLTFMaterial::textureTransformToPBR(F32 scale_s, F32 scale_t,
										   F32 offset_s, F32 offset_t,
										   F32 rot, LLVector2& pbr_scale,
										   LLVector2& pbr_offset,
										   F32& pbr_rot)
{
	pbr_scale.set(scale_s, scale_t);
	// Legacy is rotation counter-clockwise, PBR is clockwise
	pbr_rot = -rot;

	// glTF scale does not match legacy scaling when rotation is applied.
	// Legacy applies scale then rotation, which allows for planar aligment
	// without deformations, but glTF rotates first, so when scale gets aplied
	// image gets deformed by rotation. It appears to be imposible to properly
	// match legacy scale, so this is an approximation that at least matches at
	// 0, 90, 180, 270 degrees rotations and is close enough at angles like 45.
	F32 c = cosf(rot);
	F32 s = sinf(rot);
	pbr_scale.mV[VX] = scale_s * c * c + scale_t * s * s;

	// Adjust for scale, with offset from center (0.5, 0.5)
	const F32 pos_s = 0.5f * (1.f - pbr_scale.mV[VX]) - 0.5f;
	const F32 pos_t = 0.5f * (1.f - pbr_scale.mV[VY]) - 0.5f;

	c = cosf(pbr_rot);
	s = sinf(pbr_rot);
	// Rotate around center (clockwise, as per glTF spec)
	const F32 rot_s = pos_s * c + pos_t * s;
	const F32 rot_t = -pos_s * s + pos_t * c;

	// Move back to top-left and apply offset
	 pbr_offset.set(rot_s + 0.5f + offset_s, rot_t + 0.5f - offset_t);
}

//static
void LLGLTFMaterial::PBRTransformToTexture(const TextureTransform& tt,
										   F32& scale_s, F32& scale_t,
										   F32& offset_s, F32& offset_t,
										   F32& rot)
{
	rot = -tt.mRotation;

	// Reverse the scale transformation
	F32 c = cosf(rot);
	F32 s = sinf(rot);
	F32 c2 = c * c;
	F32 s2 = s * s;
	F32 denom = c * c - s * s;
	if (fabsf(denom) < 0.0001f) // Near 45 degrees
	{
		// At 45°: both scales contribute equally: use the average and assume
		// symmetric scaling.
		scale_s = scale_t = (tt.mScale.mV[VX] + tt.mScale.mV[VY]) * 0.5f;
	}
	else
	{
		scale_s = (tt.mScale.mV[VX] * c2 - tt.mScale.mV[VY] * s2) / denom;
		scale_t = (tt.mScale.mV[VY] * c2 - tt.mScale.mV[VX] * s2) / denom;
	}

	// Adjust for scale, with offset from center (0.5, 0.5)
	const F32 pos_s = 0.5f * (1.f - tt.mScale.mV[VX]) - 0.5f;
	const F32 pos_t = 0.5f * (1.f - tt.mScale.mV[VY]) - 0.5f;

	// Rotate around center (clockwise, as per glTF spec)
	c = cosf(tt.mRotation);
	s = sinf(tt.mRotation);
	const F32 rot_s = pos_s * c + pos_t * s;
	const F32 rot_t = -pos_s * s + pos_t * c;

	// Recover legacy offset
	offset_s = tt.mOffset.mV[VX] - rot_s - 0.5f;
	offset_t = -tt.mOffset.mV[VY] + rot_t + 0.5f;
}

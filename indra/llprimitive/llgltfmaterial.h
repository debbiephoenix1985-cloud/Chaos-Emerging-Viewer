/**
 * @file llgltfmaterial.h
 * @brief LLGLTFMaterial definition
 *
 * $LicenseInfo:firstyear=2022&license=viewergpl$
 *
 * Copyright (c) 2022-2026, Linden Research, Inc.
 * Copyright (c) 2023-2026, Henri Beauchamp (hashing, nlohmann::json port).
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

#include <array>
#include <string>

// Do not include the full header (json.hpp) here: we just need the forward
// declarations. HB
#include "json_fwd.hpp"

#include "llcolor3.h"
#include "llcolor4.h"
#include "hbfastmap.h"
#include "llpointer.h"
#include "llrefcount.h"
#include "llsd.h"
#include "llvector2.h"

class LLFetchedGLTFMaterial;
class LLTextureEntry;

// Note: LLThreadSafeRefCount because used by threads. HB
class LLGLTFMaterial : public LLThreadSafeRefCount
{
public:
	enum AlphaMode : U32
	{
		ALPHA_MODE_OPAQUE = 0,
		ALPHA_MODE_BLEND,
		ALPHA_MODE_MASK
	};

	LLGLTFMaterial();

	LL_INLINE LLGLTFMaterial(const LLGLTFMaterial& rhs)
	{
		*this = rhs;
	}

	LLGLTFMaterial& operator=(const LLGLTFMaterial& rhs);

	LL_INLINE bool operator==(const LLGLTFMaterial& rhs) const
	{
		return mTextureId == rhs.mTextureId &&
			   mTextureTransform == rhs.mTextureTransform &&
			   mBaseColor == rhs.mBaseColor &&
			   mEmissiveColor == rhs.mEmissiveColor &&
			   mMetallicFactor == rhs.mMetallicFactor &&
			   mRoughnessFactor == rhs.mRoughnessFactor &&
			   mAlphaCutoff == rhs.mAlphaCutoff &&
			   mAlphaMode == rhs.mAlphaMode &&
			   mOverrideAlphaMode == rhs.mOverrideAlphaMode &&
			   mOverrideDoubleSided == rhs.mOverrideDoubleSided &&
			   mDoubleSided == rhs.mDoubleSided;
	}

	LL_INLINE bool operator!=(const LLGLTFMaterial& rhs) const
	{
		return mTextureId != rhs.mTextureId ||
			   mTextureTransform != rhs.mTextureTransform ||
			   mBaseColor != rhs.mBaseColor ||
			   mEmissiveColor != rhs.mEmissiveColor ||
			   mMetallicFactor != rhs.mMetallicFactor ||
			   mRoughnessFactor != rhs.mRoughnessFactor ||
			   mAlphaCutoff != rhs.mAlphaCutoff ||
			   mAlphaMode != rhs.mAlphaMode ||
			   mOverrideAlphaMode != rhs.mOverrideAlphaMode ||
			   mOverrideDoubleSided != rhs.mOverrideDoubleSided ||
			   mDoubleSided != rhs.mDoubleSided;
	}

	virtual LLFetchedGLTFMaterial* asFetched()					{ return NULL; }

	class TextureTransform
	{
	public:
		LL_INLINE TextureTransform()
		:	mScale(1.f, 1.f),
			mRotation(0.f)
		{
		}

		using Pack = F32[8];
		LL_INLINE void getPacked(Pack& packed) const
		{
			packed[0] = mScale.mV[VX];
			packed[1] = mScale.mV[VY];
			packed[2] = mRotation;
			packed[4] = mOffset.mV[VX];
			packed[5] = mOffset.mV[VY];
			// Not used but nonetheless zeroed for proper hashing. HB
			packed[3] = packed[6] = packed[7] = 0.f;
		}

		using PackTight = F32[5];
		LL_INLINE void getPackedTight(PackTight& packed) const
		{
			packed[0] = mScale.mV[VX];
			packed[1] = mScale.mV[VY];
			packed[2] = mRotation;
			packed[3] = mOffset.mV[VX];
			packed[4] = mOffset.mV[VY];
		}

		LL_INLINE bool operator==(const TextureTransform& rhs) const
		{
			return mOffset == rhs.mOffset && mScale == rhs.mScale &&
				   mRotation == rhs.mRotation;
		}

		LL_INLINE bool operator!=(const TextureTransform& rhs) const
		{
			return mOffset != rhs.mOffset || mScale != rhs.mScale ||
				   mRotation != rhs.mRotation;
		}

	public:
		LLVector2	mScale;
		LLVector2	mOffset;
		F32			mRotation;
	};

	enum TextureInfo : U32
	{
		GLTF_TEXTURE_INFO_BASE_COLOR,
		GLTF_TEXTURE_INFO_NORMAL,
		GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS,
		// *NOTE: GLTF_TEXTURE_INFO_OCCLUSION is currently ignored, in favor of
		// the values specified with GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS.
		// Currently, only ORM materials are supported (materials which define
		// occlusion, roughness, and metallic in the same texture).
		// -Cosmic,2023-01-26
		GLTF_TEXTURE_INFO_OCCLUSION = GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS,
		GLTF_TEXTURE_INFO_EMISSIVE,
		GLTF_TEXTURE_INFO_COUNT
	};

	// Get a UUID based on a hash of this LLGLTFMaterial
	const LLUUID& getHash() const;

	// Setters for various members (will clamp to acceptable ranges)
	// for_override - set to true if this value is being set as part of an
	// override (important for handling override to default value)

	LL_INLINE void setTextureId(TextureInfo tex_info, const LLUUID& id,
								// Use 'const' to try and hint the compiler to
								// optimize out for_override test during
								// inlining when possible. HB
								const bool for_override = false)
	{
		++mVersion;
		mTextureId[tex_info] = id;
		if (for_override)
		{
			hackOverrideUUID(mTextureId[tex_info]);
		}
	}

	LL_INLINE void setBaseColorId(const LLUUID& id,
								  const bool for_override = false)
	{
		setTextureId(GLTF_TEXTURE_INFO_BASE_COLOR, id, for_override);
	}

	LL_INLINE void setNormalId(const LLUUID& id,
							   const bool for_override = false)
	{
		setTextureId(GLTF_TEXTURE_INFO_NORMAL, id, for_override);
	}

	LL_INLINE void setMetallicRoughnessId(const LLUUID& id,
										  const bool overrd = false)
	{
		setTextureId(GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS, id, overrd);
	}

	LL_INLINE void setEmissiveId(const LLUUID& id,
								 const bool for_override = false)
	{
		setTextureId(GLTF_TEXTURE_INFO_EMISSIVE, id, for_override);
	}

	// Inlined (non-overridden) texture UUIDs getters, for ease of use. HB

	LL_INLINE const LLUUID& getBaseColorId() const
	{
		return mTextureId[GLTF_TEXTURE_INFO_BASE_COLOR];
	}

	LL_INLINE const LLUUID& getNormalId() const
	{
		return mTextureId[GLTF_TEXTURE_INFO_NORMAL];
	}

	LL_INLINE const LLUUID& getMetallicRoughnessId() const
	{
		return mTextureId[GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS];
	}

	LL_INLINE const LLUUID& getEmissiveId() const
	{
		return mTextureId[GLTF_TEXTURE_INFO_EMISSIVE];
	}

	void setBaseColorFactor(const LLColor4& base_color,
							bool for_override = false);
	void setAlphaCutoff(F32 cutoff, bool for_override = false);
	void setEmissiveColorFactor(const LLColor3& emissive_color,
								bool for_override = false);
	void setMetallicFactor(F32 metallic, bool for_override = false);
	void setRoughnessFactor(F32 roughness, bool for_override = false);
	void setAlphaMode(U32 mode, bool for_override = false);

	LL_INLINE bool isTransparent() const
	{
		return mAlphaMode != ALPHA_MODE_OPAQUE && mBaseColor.mV[3] < 0.001f;		
	}

	LL_INLINE bool isOpaque() const
	{
		return mAlphaMode == ALPHA_MODE_OPAQUE;		
	}

	void setDoubleSided(bool double_sided, bool for_override = false);

	// NOTE: texture offsets only exist in overrides, so "for_override" is not
	// needed

	LL_INLINE void setTextureOffset(TextureInfo tex_info,
									const LLVector2& offset)
	{
		mTextureTransform[tex_info].mOffset = offset;
	}

	LL_INLINE const LLVector2& getBaseColorOffset() const
	{
		return mTextureTransform[GLTF_TEXTURE_INFO_BASE_COLOR].mOffset;
	}

	LL_INLINE void setTextureScale(TextureInfo tex_info,
								   const LLVector2& scale)
	{
		mTextureTransform[tex_info].mScale = scale;
	}

	LL_INLINE const LLVector2& getBaseColorScale() const
	{
		return mTextureTransform[GLTF_TEXTURE_INFO_BASE_COLOR].mScale;
	}

	LL_INLINE void setTextureRotation(TextureInfo tex_info, F32 rotation)
	{
		mTextureTransform[tex_info].mRotation = rotation;
	}

	LL_INLINE F32 getBaseColorRotation() const
	{
		return mTextureTransform[GLTF_TEXTURE_INFO_BASE_COLOR].mRotation;
	}

	// Default value accessors these MUST match the glTF specification)

	LL_INLINE static F32 getDefaultAlphaCutoff()
	{
		return sDefault.mAlphaCutoff;
	}

	LL_INLINE static U32 getDefaultAlphaMode()	
	{
		return sDefault.mAlphaMode;
	}

	LL_INLINE static F32 getDefaultMetallicFactor()
	{
		return sDefault.mMetallicFactor;
	}

	LL_INLINE static F32 getDefaultRoughnessFactor()
	{
		return sDefault.mRoughnessFactor;
	}

	LL_INLINE static const LLColor4& getDefaultBaseColor()
	{
		return sDefault.mBaseColor;
	}

	LL_INLINE static const LLColor3& getDefaultEmissiveColor()
	{
		return sDefault.mEmissiveColor;
	}

	LL_INLINE static bool getDefaultDoubleSided()
	{
		return sDefault.mDoubleSided;
	}

	LL_INLINE static const LLVector2& getDefaultTextureOffset()
	{
		return sDefault.mTextureTransform[0].mOffset;
	}

	LL_INLINE static const LLVector2& getDefaultTextureScale()
	{
		return sDefault.mTextureTransform[0].mScale;
	}

	LL_INLINE static F32 getDefaultTextureRotation()
	{
		return sDefault.mTextureTransform[0].mRotation;
	}

	// Sets mAlphaMode from string. Anything other than "MASK" or "BLEND" sets
	// mAlphaMode to ALPHA_MODE_OPAQUE.
	void setAlphaMode(const std::string& mode, bool for_override = false);

	const char* getAlphaMode() const;

	// Sets the contents of this LLGLTFMaterial from the given json; returns
	// true if successful, false if unsuccessful and the contents of this
	// LLGLTFMaterial is then left unchanged.
	// json_str - the json text to load from
	// warn_msg - warning message from nlohmann::json, if any
	// error_msg - error_msg from nlohmann::json, if any
	bool fromJSON(const std::string& json_str, std::string& warn_msg,
				  std::string& error_msg);

	// Gets the contents of this LLGLTFMaterial as a json string
	std::string asJSON(bool prettyprint = false) const;

	void applyOverride(const LLGLTFMaterial& override_mat);

	// Applies the given LLSD override data
	void applyOverrideLLSD(const LLSD& data);

	// Gets the given override on this LLGLTFMaterial as LLSD.
	LLSD getOverrideLLSD(const LLGLTFMaterial& omat) const;

	// For base materials only (i.e. assets). Clears transforms to default
	// since they are not supported in assets yet.
	LL_INLINE void sanitizeAssetMaterial()
	{
		mTextureTransform = sDefault.mTextureTransform;
	}

	// For material overrides only. Clears most properties to
	// default/fallthrough, but preserves the transforms.
	bool setBaseMaterial();
	// True if setBaseMaterial() was just called
	bool isClearedForBaseMaterial() const;

	LL_INLINE void bumpVersion()
	{
		++mVersion;
	}

	LL_INLINE U32 getVersion() const
	{
		return mVersion;
	}

	LL_INLINE static bool isAcceptedVersion(const std::string& version)
	{
		return version == "1.1" || version == "1.0";
	}

	// For local materials, we have to keep track of where they are assigned to
	// for full updates.
	LL_INLINE virtual void addTextureEntry(LLTextureEntry* tep)		{}
	LL_INLINE virtual void removeTextureEntry(LLTextureEntry* tep)	{}

	// For local materials, so that editor will know to track changes.

	void addLocalTextureTracking(const LLUUID& tracking_id,
								 const LLUUID& tex_id);
	void removeLocalTextureTracking(const LLUUID& tracking_id);

	LL_INLINE bool hasLocalTextures() const
	{
		return !mTrackingIdToLocalTexture.empty();
	}

	virtual bool replaceLocalTexture(const LLUUID& tracking_id,
									 const LLUUID& old_id,
									 const LLUUID& new_id);
	LL_INLINE virtual void updateTextureTracking()					{}

	static void textureTransformToPBR(F32 scale_s, F32 scale_t,
									  F32 offset_s, F32 offset_t,
									  F32 rot, LLVector2& pbr_scale,
									  LLVector2& pbr_offset, F32& pbr_rot);
	static void PBRTransformToTexture(const TextureTransform& tt,
									  F32& scale_s, F32& scale_t,
									  F32& offset_s, F32& offset_t,
									  F32& rot);

protected:
	// Used to update the digest of the mTrackingIdToLocalTexture map each time
	// it is changed; this way, that digest can be used by the fast getHash()
	// method instead of having to hash all individual keys and values. HB
	void updateLocalTexDataDigest();

private:
	static void hackOverrideUUID(LLUUID& id);

	using lljson = nlohmann::json;

	// Parses from glTF JSON document
	bool setFromDocument(const lljson& doc, S32 mat_idx);
	// Serializes to glTF JSON document
	lljson writeDocument() const;
	// Resolves texture index -> textures[].source -> images[].uri
	static const std::string& getTextureURI(const lljson& doc, S32 tex_idx);
	// Those read texture info from JSON
	void readTextureInfo(const lljson& doc, const lljson& tex_info, U32 id);
	static void readTextureInfo(const lljson& doc, const lljson& tex_info,
								LLUUID& tex_id, TextureTransform& tf);
	// Writes texture entry to JSON arrays
	static void writeTextureEntry(lljson& images, lljson& textures,
								  lljson& dst, const char* key,
								  const LLUUID& tex_id,
								  const TextureTransform& tf,
								  bool force_write = false);
	// JSON helpers
	static LLVector2 vec2FromJson(const lljson& obj, const char* key,
								  const LLVector2& default_value);
	static F32 floatFromJson(const lljson& obj, const char* key,
							 F32 default_value);

	LL_INLINE void setBaseMaterial(const LLGLTFMaterial& old_override_mat)
	{
		mTextureTransform = old_override_mat.mTextureTransform;
		++mVersion;
	}

private:
	// Not to be hashed, so placed before mLocalTexDataDigest. HB
	U32							mVersion;
	mutable U32					mHashVersion;
	mutable LLUUID				mHash;

public:
	// This is local to the viewer and part of local material support.
	// IMPORTANT: do not move this member down (and do not move
	// mLocalTexDataDigest either): the getHash() method does rely on the
	// current ordering. HB
	typedef fast_hmap<LLUUID, LLUUID> local_tex_map_t;
	local_tex_map_t				mTrackingIdToLocalTexture;

	// Used to store a digest of mTrackingIdToLocalTexture when the latter is
	// not empty, or zero otherwise. HB
	U64							mLocalTexDataDigest;

	typedef std::array<LLUUID, GLTF_TEXTURE_INFO_COUNT> uuid_array_t;
	uuid_array_t				mTextureId;

	typedef std::array<TextureTransform, GLTF_TEXTURE_INFO_COUNT> tf_array_t;
	tf_array_t					mTextureTransform;

	// These values should be in linear color space.
	LLColor4					mBaseColor;
	LLColor3					mEmissiveColor;
	F32							mMetallicFactor;
	F32							mRoughnessFactor;
	F32							mAlphaCutoff;
	U32							mAlphaMode;

	bool						mDoubleSided;
	// Override specific flags for state that cannot use off-by-epsilon or UUID
	// hack
	bool						mOverrideDoubleSided;
	bool						mOverrideAlphaMode;

	static const char* const	ASSET_VERSION;
	static const char* const	ASSET_TYPE;
	// Default material for reference
	static const LLGLTFMaterial	sDefault;
};

typedef std::vector<LLPointer<LLGLTFMaterial> > gltf_mat_vec_t;

// Never-ending C++ names bother me to no end !  So, let's provide and use some
// shortcuts for the sake of sanity !  HB
constexpr U32 BASECOLIDX = LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR;
constexpr U32 NORMALIDX = LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL;
constexpr U32 MROUGHIDX = LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS;
constexpr U32 EMISSIVEIDX = LLGLTFMaterial::GLTF_TEXTURE_INFO_EMISSIVE;

/**
 * @file llprimitive.cpp
 * @brief LLPrimitive base class
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

#include "linden_common.h"

#include "llprimitive.h"

#include "llcolor4u.h"
#include "lldatapacker.h"
#include "llmaterialid.h"
#include "llmaterialtable.h"
#include "llmessage.h"
#include "llprimtexturelist.h"
#include "llsdutil_math.h"
#include "llstring.h"
#include "llvolume.h"
#include "llvolumemgr.h"

// gcc 13 sees array bound issues where there are none... HB
#if defined(GCC_VERSION) && GCC_VERSION >= 130000
# pragma GCC diagnostic ignored "-Warray-bounds"
# pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

// Exported (not so) "constants" (with default value, for SL)
F32 OBJECT_MIN_HOLE_SIZE = 0.05f;
F32 OBJECT_HOLLOW_MAX = 0.95f;

// Old inverted texture: "7595d345-a24c-e7ef-f0bd-78793792133e";
const char* SCULPT_DEFAULT_TEXTURE = "be293869-d0d9-0a69-5989-ad27f1946fd4";

//static
void LLPrimitive::setLimits(bool for_secondlife)
{
	if (for_secondlife)
	{
		OBJECT_HOLLOW_MAX = 0.95f;
		OBJECT_MIN_HOLE_SIZE = 0.05f;
	}
	else
	{
		OBJECT_HOLLOW_MAX = 0.99f;
		OBJECT_MIN_HOLE_SIZE = 0.01f;
	}
}

LLPrimitive::LLPrimitive()
:	mMiscFlags(0),
	mNumBumpmapTEs(0),
	mPrimitiveCode(0),
	mMaterial(LL_MCODE_STONE)
{
	mChanged = UNCHANGED;
	mScale.set(1.f, 1.f, 1.f);
	mRotation.loadIdentity();
}

LLPrimitive::~LLPrimitive()
{
	// Cleanup handled by volume manager
	if (mVolumep && gVolumeMgrp)
	{
		gVolumeMgrp->unrefVolume(mVolumep);
	}
	mVolumep = NULL;
}

void LLPrimitive::setPCode(LLPCode p_code)
{
	mPrimitiveCode = p_code;
	setAvatar(p_code == LL_PCODE_LEGACY_AVATAR);
}

LLTextureEntry* LLPrimitive::getTE(U8 index) const
{
	return index != 255 ? mTextureList.getTexture(index) : NULL;
}

//virtual
void LLPrimitive::setNumTEs(U8 num_tes)
{
	mTextureList.setSize(num_tes);
}

//virtual
void  LLPrimitive::setAllTETextures(const LLUUID& tex_id)
{
	mTextureList.setAllIDs(tex_id);
}

//virtual
void LLPrimitive::setTE(U8 index, const LLTextureEntry& te)
{
	if (index != 255 &&
		mTextureList.copyTexture(index, &te) != TEM_CHANGE_NONE &&
		te.getBumpmap() > 0)
	{
		++mNumBumpmapTEs;
	}
}

//virtual
S32 LLPrimitive::setTETexture(U8 index, const LLUUID& id)
{
	return index != 255 ? mTextureList.setID(index, id) : TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEColor(U8 index, const LLColor4& color)
{
	return index != 255 ? mTextureList.setColor(index, color)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEColor(U8 index, const LLColor3& color)
{
	return index != 255 ? mTextureList.setColor(index, color)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEAlpha(U8 index, F32 alpha)
{
	return index != 255 ? mTextureList.setAlpha(index, alpha)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEScale(U8 index, F32 s, F32 t)
{
	return index != 255 ? mTextureList.setScale(index, s, t)
						: TEM_CHANGE_NONE;
}

// Slow: done this way because texture entries have some voodoo related to
// texture coords
//virtual
S32 LLPrimitive::setTEScaleS(U8 index, F32 s)
{
	return index != 255 ? mTextureList.setScaleS(index, s)
						: TEM_CHANGE_NONE;
}

// Slow: done this way because texture entries have some voodoo related to
// texture coords
//virtual
S32 LLPrimitive::setTEScaleT(U8 index, F32 t)
{
	return index != 255 ? mTextureList.setScaleT(index, t)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEOffset(U8 index, F32 s, F32 t)
{
	return index != 255 ? mTextureList.setOffset(index, s, t)
						: TEM_CHANGE_NONE;
}

// Slow: done this way because texture entries have some voodoo related to
// texture coords
//virtual
S32 LLPrimitive::setTEOffsetS(U8 index, F32 s)
{
	return index != 255 ? mTextureList.setOffsetS(index, s)
						: TEM_CHANGE_NONE;
}

// Slow: done this way because texture entries have some voodoo related to
// texture coords
//virtual
S32 LLPrimitive::setTEOffsetT(U8 index, F32 t)
{
	return index != 255 ? mTextureList.setOffsetT(index, t)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTERotation(U8 index, F32 r)
{
	return index != 255 ? mTextureList.setRotation(index, r)
						: TEM_CHANGE_NONE;
}

LLMaterialPtr LLPrimitive::getTEMaterialParams(U8 index)
{
	return mTextureList.getMaterialParams(index);
}

//virtual
S32 LLPrimitive::setTEBumpShinyFullbright(U8 index, U8 bump)
{
	if (index != 255)
	{
		updateNumBumpmap(index, bump);
		return mTextureList.setBumpShinyFullbright(index, bump);
	}
	return TEM_CHANGE_NONE;
}

S32 LLPrimitive::setTEMediaTexGen(U8 index, U8 media)
{
	return index != 255 ? mTextureList.setMediaTexGen(index, media)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEBumpmap(U8 index, U8 bump)
{
	if (index != 255)
	{
		updateNumBumpmap(index, bump);
		return mTextureList.setBumpMap(index, bump);
	}
	return TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEBumpShiny(U8 index, U8 bump_shiny)
{
	if (index != 255)
	{
		updateNumBumpmap(index, bump_shiny);
		return mTextureList.setBumpShiny(index, bump_shiny);
	}
	return TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTETexGen(U8 index, U8 texgen)
{
	return index != 255 ? mTextureList.setTexGen(index, texgen)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEShiny(U8 index, U8 shiny)
{
	return index != 255 ? mTextureList.setShiny(index, shiny)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEFullbright(U8 index, U8 fullbright)
{
	return index != 255 ? mTextureList.setFullbright(index, fullbright)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEMediaFlags(U8 index, U8 media_flags)
{
	return index != 255 ? mTextureList.setMediaFlags(index, media_flags)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEGlow(U8 index, F32 glow)
{
	return index != 255 ? mTextureList.setGlow(index, glow)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEMaterialID(U8 index, const LLMaterialID& matidp)
{
	return index != 255 ? mTextureList.setMaterialID(index, matidp)
						: TEM_CHANGE_NONE;
}

//virtual
S32 LLPrimitive::setTEMaterialParams(U8 index, const LLMaterialPtr paramsp)
{
	return index != 255 ? mTextureList.setMaterialParams(index, paramsp)
						: TEM_CHANGE_NONE;
}

//virtual
void LLPrimitive::setAllTESelected(bool sel)
{
	for (S32 i = 0, count = getNumTEs(); i < count; ++i)
	{
		setTESelected(i, sel);
	}
}

void LLPrimitive::setTESelected(U8 te, bool sel)
{
	LLTextureEntry* tep = getTE(te);
	bool was_selected = tep && tep->setSelected(sel);
	if (was_selected && !sel && tep->hasPendingMaterialUpdate())
	{
		LLMaterialID material_id = tep->getMaterialID();
		setTEMaterialID(te, material_id);
	}
}

// Do not crash or llerrs here !  This function is used for debug strings.
//static
std::string LLPrimitive::pCodeToString(LLPCode pcode)
{
	std::string pcode_string;

	U8 base_code = pcode & LL_PCODE_BASE_MASK;
	if (!pcode)
	{
		pcode_string = "null";
	}
	else if (base_code == LL_PCODE_LEGACY)
	{
		// It is a legacy object
		switch (pcode)
		{
		case LL_PCODE_LEGACY_GRASS:
			pcode_string = "grass";
			break;

		case LL_PCODE_LEGACY_PART_SYS:
			pcode_string = "particle system";
			break;

		case LL_PCODE_LEGACY_AVATAR:
			pcode_string = "avatar";
			break;

		case LL_PCODE_LEGACY_TREE:
			pcode_string = "tree";
			break;

		default:
			pcode_string = llformat("unknown legacy pcode %i", (U32)pcode);
		}
	}
	else
	{
		std::string shape;
		std::string mask;
		if (base_code == LL_PCODE_CUBE)
		{
			shape = "cube";
		}
		else if (base_code == LL_PCODE_CYLINDER)
		{
			shape = "cylinder";
		}
		else if (base_code == LL_PCODE_CONE)
		{
			shape = "cone";
		}
		else if (base_code == LL_PCODE_PRISM)
		{
			shape = "prism";
		}
		else if (base_code == LL_PCODE_PYRAMID)
		{
			shape = "pyramid";
		}
		else if (base_code == LL_PCODE_SPHERE)
		{
			shape = "sphere";
		}
		else if (base_code == LL_PCODE_TETRAHEDRON)
		{
			shape = "tetrahedron";
		}
		else if (base_code == LL_PCODE_VOLUME)
		{
			shape = "volume";
		}
		else if (base_code == LL_PCODE_APP)
		{
			shape = "app";
		}
		else
		{
			llwarns << "Unknown base mask for pcode: " << base_code << llendl;
		}

		U8 mask_code = pcode & (~LL_PCODE_BASE_MASK);
		if (base_code == LL_PCODE_APP)
		{
			mask = llformat("%x", mask_code);
		}
		else if (mask_code & LL_PCODE_HEMI_MASK)
		{
			mask = "hemi";
		}
		else
		{
			mask = llformat("%x", mask_code);
		}

		if (mask[0])
		{
			pcode_string = llformat("%s-%s", shape.c_str(), mask.c_str());
		}
		else
		{
			pcode_string = llformat("%s", shape.c_str());
		}
	}

	return pcode_string;
}

void LLPrimitive::copyTEs(const LLPrimitive* primitivep)
{
	U8 num_tes = llmin(primitivep->getExpectedNumTEs(), getExpectedNumTEs());
	if (mTextureList.size() < num_tes)
	{
		mTextureList.setSize(num_tes);
	}
	for (U8 i = 0; i < num_tes; ++i)
	{
		mTextureList.copyTexture(i, primitivep->getTE(i));
	}
}

S32 face_idx_from_id(LLFaceID face_ID,
					   const std::vector<LLProfile::Face>& faceArray)
{
	for (U32 i = 0, count = faceArray.size(); i < count; ++i)
	{
		if (faceArray[i].mFaceID == face_ID)
		{
			return i;
		}
	}
	return -1;
}

//virtual
bool LLPrimitive::setVolume(const LLVolumeParams& volume_params,
							S32 detail, bool unique_volume)
{
	if (detail < 0 || detail >= LLVolumeLODGroup::NUM_LODS)
	{
		llwarns << "Attempt to set volume with out of range LOD: " << detail
				<< llendl;
		return false;
	}

	if (!gVolumeMgrp)
	{
		llwarns << "Attempt to set a volume while the volume manager is not initialized !"
				<< llendl;
		return false;
	}

	LLVolume* volumep;
	if (unique_volume)
	{
		F32 volume_detail = LLVolumeLODGroup::getVolumeScaleFromDetail(detail);
		if (mVolumep.notNull() && volume_params == mVolumep->getParams() &&
			volume_detail == mVolumep->getDetail())
		{
			return false;
		}
		volumep = new LLVolume(volume_params, volume_detail, false, true);
	}
	else
	{
		if (mVolumep.notNull())
		{
			F32 volume_detail =
				LLVolumeLODGroup::getVolumeScaleFromDetail(detail);
			if (volume_params == mVolumep->getParams() &&
				volume_detail == mVolumep->getDetail())
			{
				return false;
			}
		}

		volumep = gVolumeMgrp->refVolume(volume_params, detail);
		if (volumep == mVolumep.get())
		{
			// LLVolumeMgr::refVolume() creates a reference, but we do not need
			// a second one.
			gVolumeMgrp->unrefVolume(volumep);
			return true;
		}
	}

	setChanged(GEOMETRY);

	if (!mVolumep)
	{
		mVolumep = volumep;
		setNumTEs(mVolumep->getNumFaces());
		return true;
	}

	// Build the new object
	gVolumeMgrp->unrefVolume(mVolumep);
	mVolumep = volumep;

	if (getNumTEs() < mVolumep->getNumFaces())
	{
		setNumTEs(mVolumep->getNumFaces());
	}
	// else: decreases in the LLVolume's notion of "num_faces" happens for LOD
	// reasons which do not reflect actual change of faces of the object, so we
	// ignore them.

	return true;
}

//virtual
bool LLPrimitive::setMaterial(U8 material)
{
	if (material != mMaterial)
	{
		mMaterial = material;
		return true;
	}
	return false;
}

static U8* pack_te_variable_bitfield(U8* bufferp, U64 bits)
{
	// The bits are chunked into 7 per packed byte. The 8th bit of each byte is
	// used as a continuation flag: all bytes will have the 8th bit set (1)
	// except for the last one which will be unset (0).
	if (bits >= ((U64)0x1 << 7))
	{
		if (bits >= ((U64)0x1 << 14))
		{
			if (bits >= ((U64)0x1 << 21))
			{
				if (bits >= ((U64)0x1 << 28))
				{
					if (bits >= ((U64)0x1 << 35))
					{
						if (bits >= ((U64)0x1 << 42))
						{
							if (bits >= ((U64)0x1 << 49))
							{
								*bufferp++ = (U8)(((bits >> 49) & 0x7F) | 0x80);
							}
							*bufferp++ = (U8)(((bits >> 42) & 0x7F) | 0x80);
						}
						*bufferp++ = (U8)(((bits >> 35) & 0x7F) | 0x80);
					}
					*bufferp++ = (U8)(((bits >> 28) & 0x7F) | 0x80);
				}
				*bufferp++ = (U8)(((bits >> 21) & 0x7F) | 0x80);
			}
			*bufferp++ = (U8)(((bits >> 14) & 0x7F) | 0x80);
		}
		*bufferp++ = (U8)(((bits >> 7) & 0x7F) | 0x80);
	}
	// Do not set the continuation bit in the final byte
	*bufferp++ = (U8)(bits & 0x7F);
	return bufferp;
}

static S32 pack_te_field(U8* curp, U8* datap, U8 size, U8 last_face_idx,
						 EMsgVariableType type)
{
	U8* startp = curp;

	// Pack the value at last_face_idx as the "default value" which will be
	// assigned to all faces that are not flagged as exceptions
	htonmemcpy(curp, datap + (last_face_idx * size), type, size);
	curp += size;

	// Decrementing toward face_idx=0 find all unique non-default values and
	// bundle them together (variable_bitfield + value)
	U64 packed_mask = (U64)1 << last_face_idx;
	S32 max_index = static_cast<S32>(last_face_idx);
	for (S32 face_idx = max_index - 1; face_idx >= 0; --face_idx)
	{
		bool already_sent = packed_mask & ((U64)1 << face_idx);
		if (!already_sent)
		{
			// exception_faces is a bitmask of indices whose TE field is set to
			// 'value'
			U64 exception_faces = 0;
			for (S32 i = face_idx; i >= 0; --i)
			{
				if (!memcmp(datap + (size * face_idx), datap + (size * i), size))
				{
					exception_faces |= ((U64)1 << i);
				}
			}

			// Pack the bitfield
			curp = pack_te_variable_bitfield(curp, exception_faces);

			// Pack the 'value'
			htonmemcpy(curp, datap + (face_idx * size), type, size);
			curp += size;

			packed_mask |= exception_faces;
		}
	}
	return (S32)(curp - startp);
}

// Pack information about all texture entries into container:
// { TextureEntry Variable 2 }
// Includes information about image ID, color, scale S,T, offset S,T and
// rotation
void LLPrimitive::packTEMessage(LLMessageSystem* mesgsys) const
{
	U8 bufferp[MAX_TE_BUFFER];
	U8* curp = packTEMessageInternal(bufferp);
   	mesgsys->addBinaryDataFast(_PREHASH_TextureEntry, bufferp,
							   (S32)(curp - bufferp));
}

void LLPrimitive::packTEMessage(LLDataPacker& dp) const
{
	U8 bufferp[MAX_TE_BUFFER];
	U8* curp = packTEMessageInternal(bufferp);
	dp.packBinaryData(bufferp, (S32)(curp - bufferp), _PREHASH_TextureEntry);
}

U8* LLPrimitive::packTEMessageInternal(U8* curp) const
{
	U8 num_faces = getNumTEs();
	if (!num_faces)
	{
		return curp;
	}

	U8 image_ids[MAX_TES * UUID_BYTES];
	U8 colors[MAX_TES * 4];
	F32 scale_s[MAX_TES];
	F32 scale_t[MAX_TES];
	S16 offset_s[MAX_TES];
	S16 offset_t[MAX_TES];
	S16 image_rot[MAX_TES];
	U8 bump[MAX_TES];
	U8 media_flags[MAX_TES];
    U8 glow[MAX_TES];
	U8 material_data[MAX_TES * UUID_BYTES];

	U8 last_face_idx = llmin(num_faces, MAX_TES) - 1;
	LLColor4U coloru;
	for (U8 face_idx = 0; face_idx <= last_face_idx; ++face_idx)
	{
		// Directly sending image_ids is not safe !
		memcpy((void*)&image_ids[face_idx * UUID_BYTES],
			   getTE(face_idx)->getID().mData, UUID_BYTES);

		// Cast LLColor4 to LLColor4U
		coloru.set(getTE(face_idx)->getColor());

		// Note: this is an optimization to send common colors (white) as all
		// zeros. However, the subtraction and addition must be done in
		// unsigned byte space, not in float space, otherwise off-by-one errors
		// occur. JC
		colors[4 * face_idx]     = 255 - coloru.mV[0];
		colors[4 * face_idx + 1] = 255 - coloru.mV[1];
		colors[4 * face_idx + 2] = 255 - coloru.mV[2];
		colors[4 * face_idx + 3] = 255 - coloru.mV[3];

		const LLTextureEntry* te = getTE(face_idx);
		scale_s[face_idx] = (F32)te->getScaleS();
		scale_t[face_idx] = (F32)te->getScaleT();
		offset_s[face_idx] =
			(S16)ll_round(llclamp(te->getOffsetS(), -1.f, 1.f) * (F32)0x7FFF);
		offset_t[face_idx] =
			(S16)ll_round(llclamp(te->getOffsetT(), -1.f, 1.f) * (F32)0x7FFF);
		image_rot[face_idx] =
			(S16)ll_round((fmodf(te->getRotation(), F_TWO_PI) / F_TWO_PI) *
						  TEXTURE_ROTATION_PACK_FACTOR);
		bump[face_idx] = te->getBumpShinyFullbright();
		media_flags[face_idx] = te->getMediaTexGen();
		glow[face_idx] =
			(U8)ll_round(llclamp(te->getGlow(), 0.f, 1.f) * (F32)0xFF);
		// Directly sending material_ids is not safe !
		memcpy((void*)&material_data[face_idx * UUID_BYTES],
			   getTE(face_idx)->getMaterialID().get(), UUID_BYTES);
	}

	curp += pack_te_field(curp, (U8*)image_ids, sizeof(LLUUID), last_face_idx,
						  MVT_LLUUID);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)colors, 4, last_face_idx, MVT_U8);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)scale_s, 4, last_face_idx, MVT_F32);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)scale_t, 4, last_face_idx, MVT_F32);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)offset_s, 2, last_face_idx, MVT_S16Array);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)offset_t, 2, last_face_idx, MVT_S16Array);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)image_rot, 2, last_face_idx, MVT_S16Array);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)bump, 1, last_face_idx, MVT_U8);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)media_flags, 1, last_face_idx, MVT_U8);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)glow, 1, last_face_idx, MVT_U8);
	*curp++ = 0;
	curp += pack_te_field(curp, (U8*)material_data, UUID_BYTES, last_face_idx,
						  MVT_LLUUID);
   	return curp;
}


namespace LLTEField
{
template<typename T>
bool unpack(T dest[], U8& min_faces, U8*& srcp, U8* endp,
			EMsgVariableType type)
{
	const size_t size(sizeof(T));

	if (srcp + size + 1 > endp)
	{
		// We add 1 above to take into account the byte that we know must
		// follow the value.
		llwarns << "Buffer exhausted !  Requires " << size
				<< " + 1 bytes for default, " << endp - srcp
				<< " bytes remaning." << llendl;
		srcp = endp;
		return false;
	}

	// Extract the first value and fill the array with this "default value".
	// Note: this corresponds to the property of the last face (client packs
	// it first). This also initializes the destination array which is why we
	// do not need to explicitly initialize the member arrays in TEData.
	htonmemcpy(dest, srcp, type, size);
	srcp += size;
	for (U32 idx = 1; idx < MAX_TES; ++idx)
	{
		dest[idx] = dest[0];
	}

	while (srcp < endp)
	{
		// Unpack the variable length bitfield: the flags are stored in the
		// lower 7 bits of each byte while the 8th bit signals continuation;
		// the final byte of the flags will have a zero 8th bit. Meanwhile each
		// bit in the bitfield represents whether the subsequent value will be
		// placed at the corresponding index of the 'dest' array.
		U64 index_flags = 0;
		U8 sbit = 0;
		do
		{
			if (srcp >= endp)
			{
				llwarns << "Buffer exhausted! Reading index flags." << llendl;
				srcp = endp;
				return false;
			}

			// Get ready to load the next 7 bits
			index_flags <<= 7;

			// Copy 7 bits
			sbit = *srcp++;
			U8 flags = sbit & 0x7F;
			index_flags |= flags;
		}
		while (sbit & 0x80);

		if (!index_flags)
		{
			// We have hit the terminating 0 byte.
			break;
		}

		if (srcp + size + 1 > endp)
		{
			// We add 1 above to take into account the byte that we know must
			// follow the value.
			llwarns << "Buffer exhausted !  Requires " << size
					<< " + 1 bytes for default, " << endp - srcp
					<< " bytes remaning." << llendl;
			srcp = endp;
			return false;
		}

		// After unpacking the bitfield get the value.
		T value;
		htonmemcpy(&value, srcp, type, size);
		srcp += size;

		// Store the value in the array at the flagged indices
		U8 i = 0;
		while (index_flags > 0 && i < MAX_TES)
		{
			if (index_flags & 0x01)
			{
				dest[i] = value;
			}
			index_flags >>= 1;
			i++;
		}

		// The packer thought there are at least i+1 faces since otherwise it
		// would not have packed any info past the "default value".
		if (i >= min_faces)
		{
			// *HACK: we track the max number of possible faces based on what
			// was packed. We may decide to update our own notion of actual
			// faces later if this is for a mesh object.
			min_faces = i + 1;
		}
	}
	return true;
}

}	// namespace LLTEField

S32 LLPrimitive::unpackTEMessageInternal(LLTEContents& tec)
{
	// The last field is not zero-terminated. Rather than a special case for
	// unpack functions, just add the missing null byte.
	tec.packed_buffer[tec.size++] = 0x00;

	tec.face_count = llmin((U32)getNumTEs(), MAX_TES);

	U8* curp = tec.packed_buffer;
	U8* endp = tec.packed_buffer + tec.size;
	U8 dest_count = tec.face_count;
	bool success =
		LLTEField::unpack<LLUUID>(tec.image_data, dest_count, curp, endp,
								  MVT_LLUUID) &&
		LLTEField::unpack<LLColor4U>(tec.colors, dest_count, curp, endp,
									 MVT_U8) &&
		LLTEField::unpack<F32>(tec.scale_s, dest_count, curp, endp, MVT_F32) &&
		LLTEField::unpack<F32>(tec.scale_t, dest_count, curp, endp, MVT_F32) &&
		LLTEField::unpack<S16>(tec.offset_s, dest_count, curp, endp,
							   MVT_S16) &&
		LLTEField::unpack<S16>(tec.offset_t, dest_count, curp, endp,
							   MVT_S16) &&
		LLTEField::unpack<S16>(tec.image_rot, dest_count, curp, endp,
							   MVT_S16) &&
		LLTEField::unpack<U8>(tec.bump, dest_count, curp, endp, MVT_U8) &&
		LLTEField::unpack<U8>(tec.media_flags, dest_count, curp, endp,
							  MVT_U8) &&
		LLTEField::unpack<U8>(tec.glow, dest_count, curp, endp, MVT_U8);
	if (!success)
	{
		llwarns << "Failure parsing texture entry message due to malformed TE field."
				<< llendl;
		return 0;
	}

	// Buffer for material ID processing. Static to avoid memory reallocations.
	static LLUUID material_data[MAX_TES];
	if (curp >= endp ||
		!LLTEField::unpack<LLUUID>(material_data, dest_count, curp, endp,
								   MVT_LLUUID))
	{
		memset((void*)material_data, 0, sizeof(material_data));
	}

	// *BUG: server and client may disagree as to the face_count on a mesh
	// primitive. Workaround: when the server data suggests the mesh object
	// has more faces than we know about we will accept the server's value.
	if (mVolumep && mVolumep->getParams().isMeshSculpt())
	{
		U8 num_tes = getNumTEs();
		if (num_tes < dest_count && dest_count <= LL_SCULPT_MESH_MAX_FACES)
		{
			tec.face_count = dest_count;
			setNumTEs(dest_count);
		}
	}

	for (U8 i = 0, count = tec.face_count; i <  count; ++i)
	{
		tec.material_ids[i].set(&material_data[i]);
	}

	return 1;
}

S32 LLPrimitive::parseTEMessage(LLMessageSystem* mesgsys,
								char const* block_name, S32 block_num,
								LLTEContents& tec)
{
	if (block_num < 0)
	{
		tec.size = mesgsys->getSizeFast(block_name, _PREHASH_TextureEntry);
	}
	else
	{
		tec.size = mesgsys->getSizeFast(block_name, block_num,
										_PREHASH_TextureEntry);
	}

	if (tec.size == 0)
	{
		tec.face_count = 0;
		return 0;
	}

	if (tec.size >= MAX_TE_BUFFER)
	{
		llwarns << "Excessive buffer size detected in texture entry; truncating."
				<< llendl;
		tec.size = MAX_TE_BUFFER - 1;
	}

	// If block_num < 0 ask for block 0
	S32 block = 0;
	if (block_num > 0)
	{
		block = block_num;
	}
	mesgsys->getBinaryDataFast(block_name, _PREHASH_TextureEntry,
							   tec.packed_buffer, 0, block, MAX_TE_BUFFER - 1);

	return unpackTEMessageInternal(tec);
}

S32 LLPrimitive::applyParsedTEMessage(LLTEContents& tec)
{
	S32 retval = 0;

	LLColor4 color;
	for (U32 i = 0; i < tec.face_count; ++i)
	{
		LLUUID& req_id = ((LLUUID*)tec.image_data)[i];
		retval |= setTETexture(i, req_id);
		retval |= setTEScale(i, tec.scale_s[i], tec.scale_t[i]);
		retval |= setTEOffset(i, (F32)tec.offset_s[i] / (F32)0x7FFF,
							  (F32)tec.offset_t[i] / (F32)0x7FFF);
		retval |= setTERotation(i, ((F32)tec.image_rot[i] /
									TEXTURE_ROTATION_PACK_FACTOR) * F_TWO_PI);
		retval |= setTEBumpShinyFullbright(i, tec.bump[i]);
		retval |= setTEMediaTexGen(i, tec.media_flags[i]);
		retval |= setTEGlow(i, (F32)tec.glow[i] / (F32)0xFF);
		retval |= setTEMaterialID(i, tec.material_ids[i]);

		// Note: this is an optimization to send common colors (white) as all
		// zeros. However, the subtraction and addition must be done in
		// unsigned byte space, not in float space, otherwise off-by-one errors
		// occur. JC
		const LLColor4U& coloru = tec.colors[i];
		color.mV[VRED] = F32(255 - coloru.mV[VRED]) / 255.f;
		color.mV[VGREEN] = F32(255 - coloru.mV[VGREEN]) / 255.f;
		color.mV[VBLUE] = F32(255 - coloru.mV[VBLUE]) / 255.f;
		color.mV[VALPHA] = F32(255 - coloru.mV[VALPHA]) / 255.f;

		retval |= setTEColor(i, color);
	}

	return retval;
}

S32 LLPrimitive::unpackTEMessage(LLMessageSystem* mesgsys,
								 char const* block_name, S32 block_num)
{
	// Avoid construction of 90 UUIDs + 45 LLColor4U + 90 F32 + 135 S16 +
	// 135 U8 + a 4096 bytes buffer per call...
	static LLTEContents tec;
	memset((void*)&tec, 0, sizeof(tec));

	S32 retval = parseTEMessage(mesgsys, block_name, block_num, tec);
	if (!retval)
	{
		return retval;
	}
	return applyParsedTEMessage(tec);
}

S32 LLPrimitive::unpackTEMessage(LLDataPacker& dp)
{
	// Avoid construction of 90 UUIDs + 45 LLColor4U + 90 F32 + 135 S16 +
	// 135 U8 + a 4096 bytes buffer per call...
	static LLTEContents tec;
	memset((void*)&tec, 0, sizeof(tec));

	S32 size;
	if (!dp.unpackBinaryData(tec.packed_buffer, MAX_TE_BUFFER, size,
							 _PREHASH_TextureEntry))
	{
		llwarns << "Bad texture entry block !  Aborted !" << llendl;
		return TEM_INVALID;
	}
	if (size <= 0)
	{
		return 0;
	}
	if (size >= MAX_TE_BUFFER)
	{
		llwarns << "Excessive buffer size detected in texture entry; truncating."
				<< llendl;
		size = MAX_TE_BUFFER - 1;
	}
	tec.size = size;

	if (!unpackTEMessageInternal(tec))
	{
		return 0;
	}

	return applyParsedTEMessage(tec);
}

U8 LLPrimitive::getExpectedNumTEs() const
{
	U8 expected_face_count = (U8)mTextureList.size();
	if (mVolumep)
	{
		// For egacy prims the face count is implicit to the volume params and
		// may be larger than mTextureList.size() during initialization.
		expected_face_count = llmax(expected_face_count,
									mVolumep->getNumFaces());
	}
	return expected_face_count;
}

void LLPrimitive::copyTextureList(const LLPrimTextureList& other_list)
{
	mTextureList.copy(other_list);
}

void LLPrimitive::takeTextureList(LLPrimTextureList& other_list)
{
	mTextureList.take(other_list);
}

void LLPrimitive::updateNumBumpmap(U8 index, U8 bump)
{
	LLTextureEntry* te = getTE(index);
	if (!te)
	{
		return;
	}

	U8 old_bump = te->getBumpmap();
	if (old_bump > 0)
	{
		--mNumBumpmapTEs;
	}
	if ((bump & TEM_BUMP_MASK) > 0)
	{
		++mNumBumpmapTEs;
	}
}

//============================================================================

// Moved from llselectmgr.cpp
// Limitation: only works for boxes.
// Face numbering for flex boxes as of 1.14.2

//static
bool LLPrimitive::getTESTAxes(U8 face, U32* s_axis, U32* t_axis)
{
	if (face == 0)
	{
		*s_axis = VX; *t_axis = VY;
		return true;
	}
	else if (face == 1)
	{
		*s_axis = VX; *t_axis = VZ;
		return true;
	}
	else if (face == 2)
	{
		*s_axis = VY; *t_axis = VZ;
		return true;
	}
	else if (face == 3)
	{
		*s_axis = VX; *t_axis = VZ;
		return true;
	}
	else if (face == 4)
	{
		*s_axis = VY; *t_axis = VZ;
		return true;
	}
	else if (face == 5)
	{
		*s_axis = VX; *t_axis = VY;
		return true;
	}
	else if (face == 6)
	{
		*s_axis = VX; *t_axis = VY;
		return true;
	}
	else
	{
		// Unknown face
		return false;
	}
}

//============================================================================

//static
bool LLNetworkData::isValid(U16 param_type, U32 size)
{
	switch (param_type)
	{
		case PARAMS_FLEXIBLE:
			return size == 16;

		case PARAMS_LIGHT:
			return size == 16;

		case PARAMS_SCULPT:
			return size == 17;

		case PARAMS_LIGHT_IMAGE:
			return size == 28;

		case PARAMS_EXTENDED_MESH:
			return size == 4;

		case PARAMS_RENDER_MATERIAL:
			return size > 1;

		case PARAMS_REFLECTION_PROBE:
			return size == 9;
	}

	return false;
}

//============================================================================

LLLightParams::LLLightParams()
{
	mColor.setToWhite();
	mRadius = 10.f;
	mCutoff = 0.f;
	mFalloff = 0.75f;

	mType = PARAMS_LIGHT;
}

bool LLLightParams::pack(LLDataPacker& dp) const
{
	LLColor4U color4u(mColor);
	dp.packColor4U(color4u, "color");
	dp.packF32(mRadius, "radius");
	dp.packF32(mCutoff, "cutoff");
	dp.packF32(mFalloff, "falloff");
	return true;
}

bool LLLightParams::unpack(LLDataPacker& dp)
{
	LLColor4U color;
	F32 radius, cutoff, falloff;
	if (dp.unpackColor4U(color, "color") && dp.unpackF32(radius, "radius") &&
		dp.unpackF32(cutoff, "cutoff") && dp.unpackF32(falloff, "falloff"))
	{
		setLinearColor(LLColor4(color));
		setRadius(radius);
		setCutoff(cutoff);
		setFalloff(falloff);
		return true;
	}
	return false;
}

bool LLLightParams::operator==(const LLNetworkData& data) const
{
	if (data.mType != PARAMS_LIGHT)
	{
		return false;
	}
	const LLLightParams* param = (const LLLightParams*)&data;
	if (param->mColor != mColor || param->mRadius != mRadius ||
		param->mCutoff != mCutoff || param->mFalloff != mFalloff)
	{
		return false;
	}
	return true;
}

void LLLightParams::copy(const LLNetworkData& data)
{
	const LLLightParams* param = (LLLightParams*)&data;
	mType = param->mType;
	mColor = param->mColor;
	mRadius = param->mRadius;
	mCutoff = param->mCutoff;
	mFalloff = param->mFalloff;
}

LLSD LLLightParams::asLLSD() const
{
	LLSD sd;

	sd["color"] = ll_sd_from_color4(getLinearColor());
	sd["radius"] = getRadius();
	sd["falloff"] = getFalloff();
	sd["cutoff"] = getCutoff();

	return sd;
}

bool LLLightParams::fromLLSD(LLSD& sd)
{
	const char* w;

	w = "color";
	if (sd.has(w))
	{
		setLinearColor(ll_color4_from_sd(sd[w]));
	}
	else
	{
		return false;
	}

	w = "radius";
	if (sd.has(w))
	{
		setRadius((F32)sd[w].asReal());
	}
	else
	{
		return false;
	}

	w = "falloff";
	if (sd.has(w))
	{
		setFalloff((F32)sd[w].asReal());
	}
	else
	{
		return false;
	}

	w = "cutoff";
	if (sd.has(w))
	{
		setCutoff((F32)sd[w].asReal());
	}
	else
	{
		return false;
	}

	return true;
}

//============================================================================

LLFlexibleObjectData::LLFlexibleObjectData()
{
	mSimulateLOD				= FLEXIBLE_OBJECT_DEFAULT_NUM_SECTIONS;
	mGravity					= FLEXIBLE_OBJECT_DEFAULT_GRAVITY;
	mAirFriction				= FLEXIBLE_OBJECT_DEFAULT_AIR_FRICTION;
	mWindSensitivity			= FLEXIBLE_OBJECT_DEFAULT_WIND_SENSITIVITY;
	mTension					= FLEXIBLE_OBJECT_DEFAULT_TENSION;
	mUserForce					= LLVector3(0.f, 0.f, 0.f);
	mType						= PARAMS_FLEXIBLE;
#if 0
	mUsingCollisionSphere		= FLEXIBLE_OBJECT_DEFAULT_USING_COLLISION_SPHERE;
	mRenderingCollisionSphere	= FLEXIBLE_OBJECT_DEFAULT_RENDERING_COLLISION_SPHERE;
#endif
}

bool LLFlexibleObjectData::pack(LLDataPacker& dp) const
{
	// Custom, uber-svelte pack "softness" in upper bits of tension & drag
	U8 bit1 = (mSimulateLOD & 2) << 6;
	U8 bit2 = (mSimulateLOD & 1) << 7;
	dp.packU8((U8)(mTension * 10.01f) + bit1, "tension");
	dp.packU8((U8)(mAirFriction * 10.01f) + bit2, "drag");
	dp.packU8((U8)((mGravity + 10.f) * 10.01f), "gravity");
	dp.packU8((U8)(mWindSensitivity * 10.01f), "wind");
	dp.packVector3(mUserForce, "userforce");
	return true;
}

bool LLFlexibleObjectData::unpack(LLDataPacker& dp)
{
	LLVector3 user_force;
	U8 tension, friction, gravity, wind;
	if (dp.unpackU8(tension, "tension") && dp.unpackU8(friction, "drag") &&
		dp.unpackU8(gravity, "gravity") && dp.unpackU8(wind, "wind") &&
		(!dp.hasNext() || dp.unpackVector3(user_force, "userforce")))
	{
		U8 bit1 = (tension >> 6) & 2;
		mTension = (F32)(tension & 0x7f) / 10.f;

		U8 bit2 = (friction >> 7) & 1;
		mAirFriction = (F32)(friction & 0x7f) / 10.f;

		mSimulateLOD = bit1 | bit2;

		mGravity = (F32)gravity / 10.f - 10.f;

		mWindSensitivity = (F32)wind / 10.f;

		mUserForce = user_force;
		return true;
	}
	return false;
}

bool LLFlexibleObjectData::operator==(const LLNetworkData& data) const
{
	if (data.mType != PARAMS_FLEXIBLE)
	{
		return false;
	}
	LLFlexibleObjectData *flex_data = (LLFlexibleObjectData*)&data;
	return (mSimulateLOD == flex_data->mSimulateLOD &&
			mGravity == flex_data->mGravity &&
			mAirFriction == flex_data->mAirFriction &&
			mWindSensitivity == flex_data->mWindSensitivity &&
			mTension == flex_data->mTension &&
			mUserForce == flex_data->mUserForce);
#if 0
			mUsingCollisionSphere == flex_data->mUsingCollisionSphere &&
			mRenderingCollisionSphere == flex_data->mRenderingCollisionSphere;
#endif
}

void LLFlexibleObjectData::copy(const LLNetworkData& data)
{
	const LLFlexibleObjectData *flex_data = (LLFlexibleObjectData*)&data;
	mSimulateLOD = flex_data->mSimulateLOD;
	mGravity = flex_data->mGravity;
	mAirFriction = flex_data->mAirFriction;
	mWindSensitivity = flex_data->mWindSensitivity;
	mTension = flex_data->mTension;
	mUserForce = flex_data->mUserForce;
#if 0
	mUsingCollisionSphere = flex_data->mUsingCollisionSphere;
	mRenderingCollisionSphere = flex_data->mRenderingCollisionSphere;
#endif
}

LLSD LLFlexibleObjectData::asLLSD() const
{
	LLSD sd;

	sd["air_friction"] = getAirFriction();
	sd["gravity"] = getGravity();
	sd["simulate_lod"] = getSimulateLOD();
	sd["tension"] = getTension();
	sd["user_force"] = getUserForce().getValue();
	sd["wind_sensitivity"] = getWindSensitivity();

	return sd;
}

bool LLFlexibleObjectData::fromLLSD(LLSD& sd)
{
	const char* w;

	w = "air_friction";
	if (sd.has(w))
	{
		setAirFriction((F32)sd[w].asReal());
	}
	else
	{
		return false;
	}

	w = "gravity";
	if (sd.has(w))
	{
		setGravity((F32)sd[w].asReal());
	}
	else
	{
		return false;
	}

	w = "simulate_lod";
	if (sd.has(w))
	{
		setSimulateLOD(sd[w].asInteger());
	}
	else
	{
		return false;
	}

	w = "tension";
	if (sd.has(w))
	{
		setTension((F32)sd[w].asReal());
	}
	else
	{
		return false;
	}

	w = "user_force";
	if (sd.has(w))
	{
		LLVector3 user_force = ll_vector3_from_sd(sd[w], 0);
		setUserForce(user_force);
	}
	else
	{
		return false;
	}

	w = "wind_sensitivity";
	if (sd.has(w))
	{
		setWindSensitivity((F32)sd[w].asReal());
	}
	else
	{
		return false;
	}

	return true;
}

//============================================================================

LLSculptParams::LLSculptParams()
{
	mType = PARAMS_SCULPT;
	mSculptTexture.set(SCULPT_DEFAULT_TEXTURE);
	mSculptType = LL_SCULPT_TYPE_SPHERE;
}

bool LLSculptParams::pack(LLDataPacker& dp) const
{
	dp.packUUID(mSculptTexture, "texture");
	dp.packU8(mSculptType, "type");
	return true;
}

bool LLSculptParams::unpack(LLDataPacker& dp)
{
	LLUUID id;
	U8 type;
	if (dp.unpackUUID(id, "texture") && dp.unpackU8(type, "type"))
	{
		setSculptTexture(id, type);
		return true;
	}
	return false;
}

bool LLSculptParams::operator==(const LLNetworkData& data) const
{
	if (data.mType != PARAMS_SCULPT)
	{
		return false;
	}

	const LLSculptParams* param = (const LLSculptParams*)&data;
	return param->mSculptTexture == mSculptTexture &&
		   param->mSculptType == mSculptType;
}

void LLSculptParams::copy(const LLNetworkData& data)
{
	const LLSculptParams* param = (LLSculptParams*)&data;
	setSculptTexture(param->mSculptTexture, param->mSculptType);
}

LLSD LLSculptParams::asLLSD() const
{
	LLSD sd;
	sd["texture"] = mSculptTexture;
	sd["type"] = mSculptType;
	return sd;
}

bool LLSculptParams::fromLLSD(LLSD& sd)
{
	if (sd.has("type") && sd.has("texture"))
	{
		setSculptTexture(sd["texture"].asUUID(), sd["type"].asInteger());
		return true;
	}
	return false;
}

void LLSculptParams::setSculptTexture(const LLUUID& texture_id, U8 sculpt_type)
{
	U8 type = sculpt_type & LL_SCULPT_TYPE_MASK;
	U8 flags = sculpt_type & LL_SCULPT_FLAG_MASK;
	if (sculpt_type != (type | flags) || type > LL_SCULPT_TYPE_MAX)
	{
		mSculptTexture.set(SCULPT_DEFAULT_TEXTURE);
		mSculptType = LL_SCULPT_TYPE_SPHERE;
	}
	else
	{
		mSculptTexture = texture_id;
		mSculptType = sculpt_type;
	}
}

//============================================================================

LLLightImageParams::LLLightImageParams()
{
	mType = PARAMS_LIGHT_IMAGE;
	mParams.set(F_PI * 0.5f, 0.f, 0.f);
}

bool LLLightImageParams::pack(LLDataPacker& dp) const
{
	dp.packUUID(mLightTexture, "texture");
	dp.packVector3(mParams, "params");
	return true;
}

bool LLLightImageParams::unpack(LLDataPacker& dp)
{
	LLUUID light_texture;
	LLVector3 params;
	if (dp.unpackUUID(light_texture, "texture") &&
		dp.unpackVector3(params, "params"))
	{
		mLightTexture = light_texture;
		mParams = params;
		return true;
	}
	return false;
}

bool LLLightImageParams::operator==(const LLNetworkData& data) const
{
	if (data.mType != PARAMS_LIGHT_IMAGE)
	{
		return false;
	}

	const LLLightImageParams* param = (const LLLightImageParams*)&data;
	return param->mLightTexture == mLightTexture && param->mParams == mParams;
}

void LLLightImageParams::copy(const LLNetworkData& data)
{
	const LLLightImageParams* param = (LLLightImageParams*)&data;
	mLightTexture = param->mLightTexture;
	mParams = param->mParams;
}

LLSD LLLightImageParams::asLLSD() const
{
	LLSD sd;

	sd["texture"] = mLightTexture;
	sd["params"] = mParams.getValue();

	return sd;
}

bool LLLightImageParams::fromLLSD(LLSD& sd)
{
	if (sd.has("texture"))
	{
		setLightTexture(sd["texture"]);
		setParams(LLVector3(sd["params"]));
		return true;
	}

	return false;
}

//============================================================================

LLExtendedMeshParams::LLExtendedMeshParams()
{
	mType = PARAMS_EXTENDED_MESH;
	mFlags = 0;
}

bool LLExtendedMeshParams::pack(LLDataPacker& dp) const
{
	dp.packU32(mFlags, "flags");
	return true;
}

bool LLExtendedMeshParams::unpack(LLDataPacker& dp)
{
	// Note: unpackU32() leaves mFlags unchanged when failing. HB
	return dp.unpackU32(mFlags, "flags");
}

bool LLExtendedMeshParams::operator==(const LLNetworkData& data) const
{
	if (data.mType != PARAMS_EXTENDED_MESH)
	{
		return false;
	}

	const LLExtendedMeshParams* param = (const LLExtendedMeshParams*)&data;
	if (param->mFlags != mFlags)
	{
		return false;
	}

	return true;
}

void LLExtendedMeshParams::copy(const LLNetworkData& data)
{
	const LLExtendedMeshParams* param = (LLExtendedMeshParams*)&data;
	mFlags = param->mFlags;
}

LLSD LLExtendedMeshParams::asLLSD() const
{
	LLSD sd;
	sd["flags"] = LLSD::Integer(mFlags);
	return sd;
}

bool LLExtendedMeshParams::fromLLSD(LLSD& sd)
{
	if (sd.has("flags"))
	{
		setFlags(sd["flags"].asInteger());
		return true;
	}
	return false;
}

//============================================================================

// Relfection probes constants:
const F32 REFLECTION_PROBE_MIN_AMBIANCE = 0.f;
const F32 REFLECTION_PROBE_MAX_AMBIANCE = 100.f;
const F32 REFLECTION_PROBE_DEFAULT_AMBIANCE = 0.f;
// Note: clip distances are clamped in LLCamera::setNear. The max clip distance
// is currently limited by the skybox.
const F32 REFLECTION_PROBE_MIN_CLIP_DISTANCE = 0.f;
const F32 REFLECTION_PROBE_MAX_CLIP_DISTANCE = 1024.f;
const F32 REFLECTION_PROBE_DEFAULT_CLIP_DISTANCE = 0.f;

LLReflectionProbeParams::LLReflectionProbeParams()
:	mAmbiance(REFLECTION_PROBE_DEFAULT_AMBIANCE),
	mClipDistance(REFLECTION_PROBE_DEFAULT_CLIP_DISTANCE),
	mFlags(0)
{
	mType = PARAMS_REFLECTION_PROBE;
}

bool LLReflectionProbeParams::pack(LLDataPacker& dp) const
{
	dp.packF32(mAmbiance, "ambiance");
	dp.packF32(mClipDistance, "clip_distance");
	dp.packU8(mFlags, "flags");
	return true;
}

bool LLReflectionProbeParams::unpack(LLDataPacker& dp)
{
	F32 ambiance, clip_distance;
	if (dp.unpackF32(ambiance, "ambiance") &&
		dp.unpackF32(clip_distance, "clip_distance") &&
		// Note: unpackU8() leaves mFlags unchanged on failure. HB
		dp.unpackU8(mFlags, "flags"))
	{
		setAmbiance(ambiance);
		setClipDistance(clip_distance);
		return true;
	}
	return false;
}

bool LLReflectionProbeParams::operator==(const LLNetworkData& data) const
{
	if (data.mType != PARAMS_REFLECTION_PROBE)
	{
		return false;
	}

	const LLReflectionProbeParams* paramp =
		(const LLReflectionProbeParams*)&data;
	if (paramp->mAmbiance != mAmbiance)
	{
		return false;
	}
	if (paramp->mClipDistance != mClipDistance)
	{
		return false;
	}
	if (paramp->mFlags != mFlags)
	{
		return false;
	}

	return true;
}

void LLReflectionProbeParams::copy(const LLNetworkData& data)
{
	const LLReflectionProbeParams* paramp = (LLReflectionProbeParams*)&data;
	mType = paramp->mType;
	mAmbiance = paramp->mAmbiance;
	mClipDistance = paramp->mClipDistance;
	mFlags = paramp->mFlags;
}

LLSD LLReflectionProbeParams::asLLSD() const
{
	LLSD sd;
	sd["ambiance"] = getAmbiance();
	sd["clip_distance"] = getClipDistance();
	sd["flags"] = LLSD::Integer(mFlags);
	return sd;
}

bool LLReflectionProbeParams::fromLLSD(LLSD& sd)
{
	if (sd.has("ambiance") && sd.has("clip_distance") && sd.has("flags"))
	{
		setAmbiance((F32)sd["ambiance"].asReal());
		setClipDistance((F32)sd["clip_distance"].asReal());
		mFlags = (U8)sd["flags"].asInteger();
		return true;
	}
	return false;
}

//============================================================================

LLRenderMaterialParams::LLRenderMaterialParams()
{
	mType = PARAMS_RENDER_MATERIAL;
}

bool LLRenderMaterialParams::pack(LLDataPacker& dp) const
{
	// Limited to 255 bytes, no more than 14 material Ids
	U8 count = (U8)llmin(mEntries.size(), 14);
	dp.packU8(count, "count");

	for (U8 i = 0; i < count; ++i)
	{
		const Entry& entry = mEntries[i];
		dp.packU8(entry.te_idx, "te_idx");
		dp.packUUID(entry.id, "id");
	}

	return true;
}

bool LLRenderMaterialParams::unpack(LLDataPacker& dp)
{
	U8 count;
	if (!dp.unpackU8(count, "count"))
	{
		return false;
	}

	std::vector<Entry> entries(count);
	for (U8 i = 0; i < count; ++i)
	{
		Entry& entry = entries[i];
		if (!(dp.unpackU8(entry.te_idx, "te_idx") &&
			  dp.unpackUUID(entry.id, "te_id")))
		{
			return false;
		}
	}
	mEntries.swap(entries);

	return true;
}

bool LLRenderMaterialParams::operator==(const LLNetworkData& data) const
{
	if (data.mType != PARAMS_RENDER_MATERIAL)
	{
		return false;
	}

	const LLRenderMaterialParams* paramp =
		(const LLRenderMaterialParams*)&data;
	size_t count = mEntries.size();
	if (paramp->mEntries.size() != count)
	{
		return false;
	}

	for (size_t i = 0; i < count; ++i)
	{
		const Entry& entry = mEntries[i];
		if (paramp->getMaterial(entry.te_idx) != entry.id)
		{
			return false;
		}
	}

	return true;
}

void LLRenderMaterialParams::copy(const LLNetworkData& data)
{
	mEntries = ((const LLRenderMaterialParams*)&data)->mEntries;
}

#if 0	// Not used
LLSD LLRenderMaterialParams::asLLSD() const
{
	LLSD sd;
	for (U32 i = 0, count = mEntries.size(); i < count; ++i)
	{
		const Entry& entry = mEntries[i];
		sd[i]["te_idx"] = entry.te_idx;
		sd[i]["id"] = entry.id;
	}
	return sd;
}

bool LLRenderMaterialParams::fromLLSD(LLSD& sd)
{
    if (!sd.isArray())
    {
		return false;
	}
	U32 count = sd.size();
	mEntries.clear();
	mEntries.reserve(count);
	for (U32 i = 0; i < count; ++i)
	{
		if (!sd[i].has("te_idx") || !sd[i].has("id"))
		{
			return false;
		}
		mEntries.emplace_back(sd[i]["te_idx"].asInteger(),
							  sd[i]["id"].asUUID());
	}
	return true;
}
#endif

void LLRenderMaterialParams::setMaterial(U8 te, const LLUUID& id)
{
	bool erase_entry = id.isNull();
	for (U32 i = 0, count = mEntries.size(); i < count; ++i)
	{
		Entry& entry = mEntries[i];
		if (entry.te_idx == te)
		{
			if (erase_entry)
			{
				mEntries.erase(mEntries.begin() + i);
			}
			else
			{
				entry.id = id;
			}
			return;
		}
	}
	// This is a new te entry.
	mEntries.emplace_back(te, id);
}

const LLUUID& LLRenderMaterialParams::getMaterial(U8 te) const
{
	for (U32 i = 0, count = mEntries.size(); i < count; ++i)
	{
		const Entry& entry = mEntries[i];
		if (entry.te_idx == te)
		{
			return entry.id;
		}
	}
	return LLUUID::null;
}

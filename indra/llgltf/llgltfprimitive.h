/**
 * @file llgltfprimitive.h
 * @brief LL GLTF Implementation
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

#include "hbfastmap.h"
#include "llgltfaccessor.h"
#include "llmemory.h"			// For LL_ALIGNED16_NEW_DELETE
#include "llvertexbuffer.h"
#include "llvolumeoctree.h"

namespace LLGLTF
{
	class Asset;

	// Note 16-byte aligned because we use vectors of LLVector4a. HB
	class alignas(16) Primitive
	{
	protected:
		LOG_CLASS(LLGLTF::Primitive);

	public:
		LL_ALIGNED16_NEW_DELETE

		enum class Mode : U8
		{
			POINTS,
			LINES,
			LINE_LOOP,
			LINE_STRIP,
			TRIANGLES,
			TRIANGLE_STRIP,
			TRIANGLE_FAN
		};

		Primitive();
		~Primitive();

		// Creates an octree based on vertex buffer; must be called before
		// buffer is unmapped and after buffer is populated with valid data.
		void createOctree();

		// Gets the LLVolumeTriangle that intersects with the given line
		// segment at the point closest to start. Moves end to the point of
		// intersection. Returns NULL if no intersection.
		// Line segment must be in the same coordinate frame as this Primitive.
		const LLVolumeTriangle* lineSegmentIntersect(const LLVector4a& start,
													 const LLVector4a& end,
													 LLVector4a* interp = NULL,
													 LLVector2* tcoordp = NULL,
													 LLVector4a* normp = NULL,
													 LLVector4a* tgtp = NULL);

		void serialize(lljson& dst) const;
		const Primitive& operator=(const lljson& src);

		bool prep(Asset& asset);

		// Uploads geometry to given vertex buffer.
		void upload(LLVertexBuffer* bufferp);

		LL_INLINE U32 getVertexCount() const	{ return mPositions.size(); }
		LL_INLINE U32 getIndexCount() const		{ return mIndexArray.size(); }

	public:
		// Aligned members first...
		// CPU copy of mesh data
		alignas(16) std::vector<LLVector4a>	mNormals;
		alignas(16) std::vector<LLVector4a>	mTangents;
		alignas(16) std::vector<LLVector4a>	mPositions;
		alignas(16) std::vector<LLVector4a>	mWeights;
		// ... then the rest.
		std::vector<U64>					mJoints;
		std::vector<LLVector2>				mTexCoords0;
		std::vector<LLVector2>				mTexCoords1;
		std::vector<LLColor4U>				mColors;
		std::vector<U32>					mIndexArray;

		// Raycast acceleration structure
		std::vector<LLVolumeTriangle>		mOctreeTriangles;
		LLPointer<LLVolumeOctree>			mOctree;

		fast_hmap<std::string, S32>			mAttributes;

		// GPU copy of mesh data
		LLPointer<LLVertexBuffer>			mVertexBuffer;
		U32									mVertexOffset;
		U32									mIndexOffset;
		// Vertex attribute mask
		U32									mAttributeMask;

		S32									mIndices;
		S32									mMaterial;
		U32									mGLMode;
		Mode								mMode;
		// Shader variant according to LLGLSLShader::GLTFVariant flags
		U8									mShaderVariant;
	};
}

/**
 * @file llgltfjointdata.h
 * @brief Class for holding individual joint data and skeleton
 *
 * $LicenseInfo:firstyear=2025&license=viewergpl$
 *
 * Copyright (c) 2025, Linden Research, Inc.
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

#include <string>
#include <vector>

#include "llgltfglm.h"
#include "llvector3.h"

class LLAvatarBoneInfo;

namespace LLGLTF
{
	class JointData
	{
	public:
		JointData() = default;

		enum SupportCategory
		{
			SUPPORT_BASE,
			SUPPORT_EXTENDED
		};

		LL_INLINE void setSupport(const std::string& support)
		{
			mSupport = support == "extended" ? SUPPORT_EXTENDED : SUPPORT_BASE;
		}

		static void getJointMatricesAndHierarhy(std::vector<JointData>& data);

	private:
		static void getJointMatricesAndHierarhy(LLAvatarBoneInfo* infop,
												JointData& data,
												const mat4& parent_mat);

	public:
		typedef std::vector<JointData> bones_t;
		bones_t			mChildren;
		std::string		mName;
		std::string		mGroup;
		mat4			mJointMatrix;
		mat4			mRestMatrix;
		vec3			mScale;
		LLVector3		mRotation;
		SupportCategory	mSupport;
		// true for a joint, false for a collision volume
		bool			mIsJoint;
	};
}

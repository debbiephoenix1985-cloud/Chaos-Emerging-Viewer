/**
 * @File llavatarboneinfo.cpp
 * @brief Trans/Scale/Rot etc. info about each avatar bone. Used both in
 * llavatarappearance.cpp and in llgltfjointdata.cpp.
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

#include <algorithm>

#include "llavatarboneinfo.h"

#include "llstl.h"

LLAvatarBoneInfo::~LLAvatarBoneInfo()
{
	std::for_each(mChildren.begin(), mChildren.end(), DeletePointer());
	mChildren.clear();
}

bool LLAvatarBoneInfo::parseXml(LLXmlTreeNode* nodep)
{
	if (nodep->hasName("bone"))
	{
		mIsJoint = true;
		static LLStdStringHandle name_string =
			LLXmlTree::addAttributeString("name");
		if (!nodep->getFastAttributeString(name_string, mName))
		{
			llwarns << "Bone without name" << llendl;
			return false;
		}

		static LLStdStringHandle aliases_string =
			LLXmlTree::addAttributeString("aliases");
		// Note: aliases are not required.
		nodep->getFastAttributeString(aliases_string, mAliases);
	}
	else if (nodep->hasName("collision_volume"))
	{
		mIsJoint = false;
		static LLStdStringHandle name_string =
			LLXmlTree::addAttributeString("name");
		if (!nodep->getFastAttributeString(name_string, mName))
		{
			mName = "Collision Volume";
		}
	}
	else
	{
		llwarns << "Invalid node " << nodep->getName() << llendl;
		return false;
	}

	static LLStdStringHandle pos_string = LLXmlTree::addAttributeString("pos");
	if (!nodep->getFastAttributeVector3(pos_string, mPos))
	{
		llwarns << "Bone '" << mName << "' without position" << llendl;
		return false;
	}

	static LLStdStringHandle rot_string = LLXmlTree::addAttributeString("rot");
	if (!nodep->getFastAttributeVector3(rot_string, mRot))
	{
		llwarns << "Bone '" << mName << "' without rotation" << llendl;
		return false;
	}

	static LLStdStringHandle scale_string =
		LLXmlTree::addAttributeString("scale");
	if (!nodep->getFastAttributeVector3(scale_string, mScale))
	{
		llwarns << "Bone '" << mName << "' without scale" << llendl;
		return false;
	}

	static LLStdStringHandle end_string = LLXmlTree::addAttributeString("end");
	if (!nodep->getFastAttributeVector3(end_string, mEnd))
	{
		llwarns << "Bone '" << mName << "' without end" << llendl;
		mEnd = LLVector3(0.0f, 0.0f, 0.0f);
	}

	static LLStdStringHandle support_string =
		LLXmlTree::addAttributeString("support");
	if (!nodep->getFastAttributeString(support_string, mSupport))
	{
		llwarns << "Bone '" << mName << "' without support" << llendl;
		mSupport = "base";
	}

	// Skeleton has 133 bones, but shader only allows 110
	// (LL_MAX_JOINTS_PER_MESH_OBJECT). Groups can be used by importer to cut
	// out unused groups of joints.
	static LLStdStringHandle group_string =
		LLXmlTree::addAttributeString("group");
	if (!nodep->getFastAttributeString(group_string, mGroup))
	{
		llwarns << "Bone '" << mName << "' without group" << llendl;
		mGroup = "global";
	}

	if (mIsJoint)
	{
		static LLStdStringHandle pivot_string =
			LLXmlTree::addAttributeString("pivot");
		if (!nodep->getFastAttributeVector3(pivot_string, mPivot))
		{
			llwarns << "Bone '" << mName << "' without pivot" << llendl;
			return false;
		}
	}

	// Parse children
	for (LLXmlTreeNode* childp = nodep->getFirstChild(); childp;
		 childp = nodep->getNextChild())
	{
		LLAvatarBoneInfo* child_infop = new LLAvatarBoneInfo;
		if (!child_infop->parseXml(childp))
		{
			delete child_infop;
			return false;
		}
		mChildren.push_back(child_infop);
	}
	return true;
}

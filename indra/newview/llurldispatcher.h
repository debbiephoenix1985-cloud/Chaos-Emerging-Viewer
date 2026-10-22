/**
 * @file llurldispatcher.h
 * @brief Central registry for all SL URL handlers
 *
 * $LicenseInfo:firstyear=2007&license=viewergpl$
 *
 * Copyright (c) 2007-2009, Linden Research, Inc.
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

class LLMediaCtrl;

class LLURLDispatcher
{
public:
	// Called at startup time and on clicks in internal web browsers, teleport,
	// open map, or run requested command. Where:
	//	 -'url' is in the form:
	//		secondlife://RegionName/123/45/67/
	//		secondlife:///app/agent/3d6181b0-6a4b-97ef-18d8-722652995cf1/show
	//	- 'nav_type' can be either of "clicked", "external" or "navigated" or an
	//     empty string.
	//	- 'webp' is apointer to the LLMediaCtrl sending URL or NULL when none.
	//	- 'trusted_browser' is true if coming inside the app AND from a brower
	//	  instance which navigates to trusted (Linden Lab) pages.
	// Returns true if someone handled the URL.
	static bool dispatch(const std::string& url, const std::string& nav_type,
						 LLMediaCtrl* webp, bool trusted_browser);

	static bool dispatchRightClick(const std::string& url);
	static bool dispatchFromTextEditor(const std::string& url);
};

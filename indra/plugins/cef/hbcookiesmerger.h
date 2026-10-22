/**
 * @file hbcookiesmerger.h
 * @brief A CEF cookies database merger.
 *
 * $LicenseInfo:firstyear=2024&license=viewergpl$
 *
 * Copyright (C) 2024, Henri Beauchamp.
 *
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab. Terms of
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

#include "llfile.h"
#include "llstring.h"

struct sqlite3;

class HBCookiesMerger
{
public:
	HBCookiesMerger(const std::string& source_db, const std::string& dest_db,
					const std::string& debug_log = LLStringUtil::null);
	~HBCookiesMerger();

	bool merge();

	// Allows to retreive the last error message (e.g for when debug_log was
	// not used).
	LL_INLINE const std::string& getErrorMessage()	{ return mErrMsg; }

private:
	void close();
	bool hasError(sqlite3* db, int result);
	strings_set_t getTables();
	bool mergeTable(const std::string& table_name);

private:
	sqlite3*	mSrcDb;
	sqlite3*	mDstDb;
	std::string mErrMsg;
	std::string mSrcFileName;
	std::string mDstFileName;
	std::string mLogFileName;
	llofstream*	mLogStream;
};

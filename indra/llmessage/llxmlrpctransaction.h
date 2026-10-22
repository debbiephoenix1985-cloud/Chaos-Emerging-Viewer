/** 
 * @file llxmlrpctransaction.h
 * @brief LLXMLRPCTransaction and related class header file
 *
 * $LicenseInfo:firstyear=2006&license=viewergpl$
 * 
 * Copyright (c) 2006-2009, Linden Research, Inc.
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

// An asynchronous request and responses via XML-RPC
class LLXMLRPCTransaction
{
protected:
	LOG_CLASS(LLXMLRPCTransaction);

public:
	LLXMLRPCTransaction(const std::string& uri, const std::string& method,
						const LLSD& params);

	~LLXMLRPCTransaction();

	typedef enum
	{
		StatusNotStarted,
		StatusStarted,
		StatusDownloading,	// Not used any more (downloading done in request)
		StatusComplete,
		StatusCURLError,
		StatusXMLRPCError,
		StatusOtherError
	} EStatus;

	// Runs the request a little, returns true when done
	bool process();

	// Returns status, and extended CURL code, if curl_code is not NULL.
	EStatus status(S32* curl_code);
	// Returns a message string, suitable for showing the user.
	std::string statusMessage();
	// Returns a URI for the user with more information. Can be empty.
	std::string statusURI();

	// Only non-empty if StatusComplete, otherwise Undefined
	const LLSD& response();

	// Used from newview to set various URLs, parameters and messages,
	// depending on the grid we logged into, the user preferences and the UI
	// language. This avoids dependencies on newview and llui classes. HB
	static void setSupportURL(const std::string& url);
	static void setWebsiteURL(const std::string& url);
	static void setVerifyCert(bool verify);
	static void setMessages(const std::string& server_down,
							const std::string& not_resolving,
							const std::string& not_verified,
							const std::string& connect_error);

private:
	class Handler;
	class Impl;

	Impl& impl;
};

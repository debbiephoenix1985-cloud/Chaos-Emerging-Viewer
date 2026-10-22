/**
 * @file hbfloatertextinput.h
 * @brief HBFloaterTextInput class definition
 *
 * $LicenseInfo:firstyear=2012&license=viewergpl$
 *
 * Copyright (c) 2012-2025, Henri Beauchamp.
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

#include "llfloater.h"

class LLLineEditor;
class LLTextEditor;

class HBFloaterTextInput final : public LLFloater
{
protected:
	LOG_CLASS(HBFloaterTextInput);

public:
	HBFloaterTextInput(LLLineEditor* input_linep, const std::string& dest,
					   void (*typing_callback)(void*, bool),
					   void* callback_datap);
	~HBFloaterTextInput() override;

	static HBFloaterTextInput* show(LLLineEditor* input_linep,
									const std::string& dest = LLStringUtil::null,
									void (*typing_callback)(void*, bool) = NULL,
									void* callback_datap = NULL);

	static void abort(LLLineEditor* input_linep);

	static bool hasFloaterFor(LLLineEditor* input_linep);

private:
	bool postBuild() override;
	void draw() override;

	static void onTextEditorFocusLost(LLFocusableElement* callerp,
									  void* userdatap);
	static void onTextEditorKeystroke(LLTextEditor* callerp, void* userdatap);
	static bool onHandleKeyCallback(KEY key, MASK mask, LLTextEditor* callerp,
									void* userdatap);

private:
	static std::map<LLLineEditor*, HBFloaterTextInput*> sInstancesMap;

	void			(*mTypingCallback)(void*, bool);
	void*			mTypingCallbackData;

	LLLineEditor*	mCallerLineEditor;
	LLTextEditor*	mTextEditor;

	std::string 	mRectControl;
	bool			mIsChatInput;
	bool			mMustClose;
};

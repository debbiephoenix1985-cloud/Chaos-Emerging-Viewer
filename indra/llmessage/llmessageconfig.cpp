/**
 * @file llmessageconfig.cpp
 * @brief Live file handling for messaging
 *
 * $LicenseInfo:firstyear=2000&license=viewergpl$
 *
 * Copyright (c) 2000-2009, Linden Research, Inc.
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

#include "llmessageconfig.h"

#include "llmessage.h"
#include "llsdutil.h"
#include "llsdserialize.h"

static const char MESSAGE_CONFIG_FILENAME[] = "message.xml";
static const char SERVER_NAME[] = "viewer";

//---------------------------------------------------------------
// LLMessageConfigFile class
//---------------------------------------------------------------

class LLMessageConfigFile
{
protected:
	LOG_CLASS(LLMessageConfigFile);

private:
	// Use createInstance() only
	LLMessageConfigFile()
	{
		sInstance = this;
	}

public:
	~LLMessageConfigFile()
	{
		sInstance = NULL;
	}

	// Used to instantiate the singleton class
	static void createInstance(const std::string& config_dir);

	static LLMessageConfigFile* getInstance();

	bool loadFile(const std::string& filename);

	void loadServerDefaults(const LLSD& data);
	void loadMessages(const LLSD& data);
	void loadMessageBans(const LLSD& blacklist);

public:
	LLSD						mMessages;
	std::string					mServerDefault;

private:
	static LLMessageConfigFile* sInstance;
};

LLMessageConfigFile* LLMessageConfigFile::sInstance = NULL;

//static
void LLMessageConfigFile::createInstance(const std::string& config_dir)
{
	if (sInstance)
	{
		llerrs << "Instance already exists !" << llendl;
	}
	LL_DEBUGS("AppInit") << "Config file: " << config_dir << "/"
						 << MESSAGE_CONFIG_FILENAME << LL_ENDL;
	sInstance = new LLMessageConfigFile;
	sInstance->loadFile(config_dir + LL_DIR_DELIM_STR +
						MESSAGE_CONFIG_FILENAME);
}

LLMessageConfigFile* LLMessageConfigFile::getInstance()
{
	if (!sInstance)
	{
		llerrs << "Call done before class initialization !" << llendl;
	}
	return sInstance;
}

bool LLMessageConfigFile::loadFile(const std::string& filename)
{
	LLSD data;
    {
        llifstream file(filename.c_str());
        if (file.is_open())
        {
			LL_DEBUGS("AppInit") << "Loading: " << filename << LL_ENDL;
            LLSDSerialize::fromXML(data, file);
        }

        if (data.isUndefined())
        {
            llwarns << "File missing, ill-formed, or simply undefined; not changing the file."
					<< llendl;
            return false;
        }
    }
	loadServerDefaults(data);
	loadMessages(data);
	loadMessageBans(data);
	return true;
}

void LLMessageConfigFile::loadServerDefaults(const LLSD& data)
{
	mServerDefault = data["serverDefaults"][SERVER_NAME].asString();
}

void LLMessageConfigFile::loadMessages(const LLSD& data)
{
	mMessages = data["messages"];
	LL_DEBUGS("AppInit") << "Loading...\n";
	std::ostringstream out;
	LLSDXMLFormatter* formatter = new LLSDXMLFormatter;
	formatter->format(mMessages, out);
	LL_CONT << out.str() << "\nLoaded: " << mMessages.size() << " messages."
			<< LL_ENDL;
}

void LLMessageConfigFile::loadMessageBans(const LLSD& data)
{
    LLSD bans = data["messageBans"];
    if (!bans.isMap())
    {
        llwarns << "Missing messageBans section" << llendl;
        return;
    }
	gMessageSystemp->setMessageBans(bans["trusted"], bans["untrusted"]);
}

//---------------------------------------------------------------
// LLMessageConfig class
//---------------------------------------------------------------

//static
void LLMessageConfig::initClass(const std::string& config_dir)
{
	LLMessageConfigFile::createInstance(config_dir);
}

//static
LLMessageConfig::Flavor LLMessageConfig::getServerDefaultFlavor()
{
	LLMessageConfigFile* file = LLMessageConfigFile::getInstance();
	if (file->mServerDefault == "llsd")
	{
		return LLSD_FLAVOR;
	}
	if (file->mServerDefault == "template")
	{
		return TEMPLATE_FLAVOR;
	}
	return NO_FLAVOR;
}

//static
LLMessageConfig::Flavor LLMessageConfig::getMessageFlavor(const std::string& msg_name)
{
	LLSD config = LLMessageConfigFile::getInstance()->mMessages[msg_name];
	if (config["flavor"].asString() == "llsd")
	{
		return LLSD_FLAVOR;
	}
	if (config["flavor"].asString() == "template")
	{
		return TEMPLATE_FLAVOR;
	}
	return NO_FLAVOR;
}

//static
bool LLMessageConfig::isValidMessage(const std::string& msg_name)
{
	return LLMessageConfigFile::getInstance()->mMessages.has(msg_name);
}

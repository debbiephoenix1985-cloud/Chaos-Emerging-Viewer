/**
 * @file hbfileselector.cpp
 * @brief The HBFileSelector class definition
 *
 * $LicenseInfo:firstyear=2014&license=viewergpl$
 *
 * Copyright (c) 2014-2025, Henri Beauchamp
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

#include "hbfileselector.h"

#include "llapp.h"				// For isExiting()
#include "llcallbacklist.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lldate.h"
#include "lldir.h"
#include "lldiriterator.h"
#include "hbfastset.h"
#include "lllineeditor.h"
#include "llscrolllistctrl.h"
#include "llsdserialize.h"
#include "llstring.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"

#if LL_WINDOWS
// Native Explorer file chooser, using the same Windows common-dialog API as
// Firestorm llfilepicker.cpp. Only explicit upload requests use this path.
# include <windows.h>
# include <commdlg.h>
# include <shobjidl.h>
# include <shlobj.h>
# pragma comment(lib, "Comdlg32.lib")
# pragma comment(lib, "Ole32.lib")
# pragma comment(lib, "Shell32.lib")
#endif

// Position in a path string where the root delimiter is found, i.e. position
// of "\" in (e.g.) "C:\" for Windows and position of "/" (0) for other OSes
#if LL_WINDOWS
# define ROOT_DELIMITER_POS 2
#else
# define ROOT_DELIMITER_POS 0
#endif

// Static variables
HBFileSelector* HBFileSelector::sInstance = NULL;
HBFileSelector::context_map_t HBFileSelector::sContextToPathMap;
HBFileSelector::used_path_set_t HBFileSelector::sUsedPaths;
std::string HBFileSelector::sLastPath;

HBFileSelector::HBFileSelector(ELoadFilter filter,
							   HBLoadFileCallback callback, void* userdatap)
:	mLoadFilter(filter),
	mSaveFilter(FFSAVE_NONE),
	mMultiple(false),
	mSavePicker(false),
	mDirPicker(false),
	mLoadFileCallback(callback),
	mLoadFilesCallback(NULL),
	mSaveFileCallback(NULL),
	mDirPickCallback(NULL)
{
	init(userdatap);
}

HBFileSelector::HBFileSelector(ELoadFilter filter,
							   HBLoadFilesCallback callback, void* userdatap)
:	mLoadFilter(filter),
	mSaveFilter(FFSAVE_NONE),
	mMultiple(true),
	mSavePicker(false),
	mDirPicker(false),
	mLoadFileCallback(NULL),
	mLoadFilesCallback(callback),
	mSaveFileCallback(NULL),
	mDirPickCallback(NULL)
{
	init(userdatap);
}

HBFileSelector::HBFileSelector(ESaveFilter filter, const std::string& suggestion,
							   HBSaveFileCallback callback, void* userdatap)
:	mLoadFilter(FFLOAD_NONE),
	mSaveFilter(filter),
	mMultiple(false),
	mSavePicker(true),
	mDirPicker(false),
	mLoadFileCallback(NULL),
	mLoadFilesCallback(NULL),
	mSaveFileCallback(callback),
	mDirPickCallback(NULL),
	mCurrentEntry(suggestion)
{
	init(userdatap);
}

HBFileSelector::HBFileSelector(const std::string& suggestion,
							   HBDirPickCallback callback, void* userdatap)
:	mLoadFilter(FFLOAD_NONE),
	mSaveFilter(FFSAVE_NONE),
	mMultiple(false),
	mSavePicker(false),
	mDirPicker(true),
	mLoadFileCallback(NULL),
	mLoadFilesCallback(NULL),
	mSaveFileCallback(NULL),
	mDirPickCallback(callback),
	mCurrentEntry(suggestion)
{
	init(userdatap);
}

void HBFileSelector::init(void* userdatap)
{
	sInstance = this;

	mCallbackUserData = userdatap;
	mCallbackDone = mIsDirty = mCreatingDirectory = false;
	mContext = CONTEXT_UNKNOWN;

	std::string xui_file = mDirPicker ? "floater_dirselector.xml"
									  : "floater_fileselector.xml";
    LLUICtrlFactory::getInstance()->buildFloater(this, xui_file);
}

HBFileSelector::~HBFileSelector()
{
	if (!mCallbackDone && !LLApp::isExiting())
	{
		mCurrentSelection.clear();
		mFiles.clear();
		doCallback();
	}

	sInstance = NULL;
}

bool HBFileSelector::postBuild()
{
	mDirectoriesList = getChild<LLScrollListCtrl>("directories");
	if (mDirPicker)
	{
		mDirectoriesList->setCommitOnSelectionChange(true);
		mDirectoriesList->setCommitCallback(onSelectDirectory);
	}
	mDirectoriesList->setDoubleClickCallback(onLevelDown);
	mDirectoriesList->setCallbackUserData(this);

	if (mDirPicker)
	{
		mFilesList = NULL;
		mShowAllTypesCheck = NULL;
	}
	else
	{
		mFilesList = getChild<LLScrollListCtrl>("files");
		mFilesList->setAllowMultipleSelection(mMultiple);
		mFilesList->setCommitOnSelectionChange(true);
		mFilesList->setCommitCallback(onSelectFile);
		mFilesList->setDoubleClickCallback(onButtonOK);
		mFilesList->setCallbackUserData(this);

		mShowAllTypesCheck = getChild<LLCheckBoxCtrl>("all_files");
		mShowAllTypesCheck->setCommitCallback(onCommitCheckBox);
		mShowAllTypesCheck->setCallbackUserData(this);

		setValidExtensions();
	}

	mPromptTextBox = getChild<LLTextBox>("prompt");

	mPathComboBox = getChild<LLComboBox>("path");
	mPathComboBox->setCommitCallback(onPathComboCommit);
	mPathComboBox->setCallbackUserData(this);

	mInputLine = getChild<LLLineEditor>("selection");
	mInputLine->setOnHandleKeyCallback(onHandleKeyCallback, this);
	mInputLine->setKeystrokeCallback(onKeystrokeCallback);
	mInputLine->setCallbackUserData(this);
	mInputLine->setEnabled(mSavePicker);

	mShowHiddenCheck = getChild<LLCheckBoxCtrl>("show_hidden");
	mShowHiddenCheck->setCommitCallback(onCommitCheckBox);
	mShowHiddenCheck->setCallbackUserData(this);

	mDirLevelFlyoutBtn = getChild<LLFlyoutButton>("dir_level");
	mDirLevelFlyoutBtn->setCommitCallback(onButtonDirLevel);
	mDirLevelFlyoutBtn->setCallbackUserData(this);

	mCreateBtn = getChild<LLButton>("create");
	mCreateBtn->setClickedCallback(onButtonCreate, this);
	mCreateBtn->setEnabled(mSavePicker || mDirPicker);

	mRefreshBtn = getChild<LLButton>("refresh");
	mRefreshBtn->setClickedCallback(onButtonRefresh, this);

	mCancelBtn = getChild<LLButton>("cancel");
	mCancelBtn->setClickedCallback(onButtonCancel, this);

	mOKBtn = getChild<LLButton>("ok");
	mOKBtn->setClickedCallback(onButtonOK, this);

	setPathFromContext();
	if (!mCurrentPath.empty() && LLFile::isdir(mCurrentPath))
	{
		// Remember the suggested path as having been used (or at least seen)
		// during this viewer session.
		sUsedPaths.insert(mCurrentPath);
	}

	setPrompt();

	mIsDirty = true;

	return true;
}

void HBFileSelector::refreshPathCombo()
{
	if (mCurrentPath == mOldPath)
	{
		return;
	}
	mOldPath = mCurrentPath;

	// Avoid duplicate entries in the combo by tracking already added paths.
	fast_hset<std::string> paths;

	mPathComboBox->clearRows();

	// Current path added first (at top, selected by default), unconditionally.
	mPathComboBox->add(mCurrentPath, LLSD(mCurrentPath));
	if (!mCurrentPath.empty())
	{
		paths.insert(mCurrentPath);
	}

	// Last used path added in second position, if different from current path.
	bool first_addition = true;
	if (!sLastPath.empty() && sLastPath != mCurrentPath &&
		LLFile::isdir(sLastPath))
	{
		first_addition = false;
		mPathComboBox->add(getString("last_paths"), ADD_BOTTOM, false);
		mPathComboBox->add(sLastPath, LLSD(sLastPath));
		paths.insert(sLastPath);
	}
	// Then, add all the paths we happened to use during this session, when not
	// already there.
	for (used_path_set_t::const_iterator it = sUsedPaths.begin(),
										 end = sUsedPaths.end();
		 it != end; ++it)
	{
		const std::string& path = *it;
		if (!path.empty() && !paths.count(path) && LLFile::isdir(path))
		{
			if (first_addition)
			{
				first_addition = false;
				mPathComboBox->add(getString("last_paths"), ADD_BOTTOM, false);
			}
			mPathComboBox->add(path, LLSD(path));
			paths.insert(path);
		}
	}

	// Finally, add the per-context default paths, when not already there.
	first_addition = true;
	for (context_map_t::const_iterator it = sContextToPathMap.begin(),
									   end = sContextToPathMap.end();
		 it != end; ++it)
	{
		const std::string& path = it->second;
		if (!path.empty() && !paths.count(path) && LLFile::isdir(path))
		{
			if (first_addition)
			{
				first_addition = false;
				mPathComboBox->add(getString("context_paths"), ADD_BOTTOM,
								   false);
			}
			mPathComboBox->add(path, LLSD(path));
			paths.insert(path);
		}
	}
}

void HBFileSelector::draw()
{
	if (mIsDirty)
	{
		refreshPathCombo();

		mDirectoriesList->deleteAllItems();
		if (mFilesList)
		{
			mFilesList->deleteAllItems();
		}
		std::string timeformat;
		if (LLUI::sConfigGroup)
		{
			timeformat = LLUI::sConfigGroup->getString("ShortDateFormat") +
						 " " +
						 LLUI::sConfigGroup->getString("LongTimeFormat");
		}
#if !LL_WINDOWS
		std::string path = mCurrentPath + LL_DIR_DELIM_STR;
#else
		std::string path;
		if (!mCurrentPath.empty())
		{
			path = mCurrentPath + LL_DIR_DELIM_STR;
		}
#endif
		std::string filename;
		LLSD element;
		LLUUID id, selected_id;
		bool selection_is_dir = false;
#if !LL_WINDOWS
		if (!mCurrentEntry.empty() && mCurrentEntry[0] == '.')
		{
			mShowHiddenCheck->set(true);
		}
#endif
		bool with_hidden = mShowHiddenCheck->get();
		LLDirIterator iter(path, NULL, DI_ALL);
		while (iter.next(filename))
		{
			if (!with_hidden && iter.isHidden())
			{
				continue;	// Do not list hidden entries
			}

			bool is_dir = iter.isDirectory();

			element.clear();
			id.generate();
			if (filename == mCurrentEntry)
			{
				selected_id = id;
				selection_is_dir = is_dir;
			}
			element["id"] = id;
			if (is_dir)
			{
				element["columns"][0]["column"] = "dirname_col";
				element["columns"][0]["value"] = filename;
				if (iter.isLink())
				{
					element["columns"][0]["font-style"] = "ITALIC";
				}
				mDirectoriesList->addElement(element, ADD_SORTED);
			}
			else if (mFilesList && isFileExtensionValid(filename))
			{
				element["columns"][0]["column"] = "name_col";
				element["columns"][0]["value"] = filename;
				if (iter.isLink())
				{
					element["columns"][0]["font-style"] = "ITALIC";
				}
				element["columns"][1]["column"] = "size_col";
				element["columns"][1]["value"] = (LLSD::Integer)iter.getSize();
				element["columns"][2]["column"] = "date_col";
				element["columns"][2]["type"] = "date";
				element["columns"][2]["format"] = timeformat;
				element["columns"][2]["value"] = LLDate(iter.getTimeStamp());
				mFilesList->addElement(element, ADD_SORTED);
			}
		}

		// Set the current selection, if any:
		if (mCurrentEntry.empty())
		{
			mInputLine->clear();
		}
		else
		{
			mInputLine->setText(mCurrentEntry);
			mInputLine->setCursorToEnd();
			bool got_it = mSavePicker;
			if (selected_id.notNull())
			{
				if (selection_is_dir)
				{
					if (mDirPicker &&
						mDirectoriesList->selectByID(selected_id))
					{
						mDirectoriesList->scrollToShowSelected();
						got_it = true;
					}
				}
				else if (mFilesList && mFilesList->selectByID(selected_id))
				{
					mFilesList->scrollToShowSelected();
					got_it = true;
				}
			}
			if (!got_it)
			{
				mCurrentEntry.clear();
			}
		}

		mIsDirty = false;
	}

	LLFloater::draw();
}

void HBFileSelector::setValidExtensions()
{
	mContext = CONTEXT_DEFAULT;
	mValidExtensions.clear();
	if (mSavePicker)
	{
		mFileTypeDescription = getString("any_file") + " (*.*)";
	}
	else if (!mDirPicker)
	{
		mFileTypeDescription = getString("all_files") + " (*.*)";
	}

	if (!mShowAllTypesCheck || mShowAllTypesCheck->get())
	{
		return;
	}

	if (mSavePicker && mSaveFilter != FFSAVE_NONE && mSaveFilter != FFSAVE_ALL)
	{
		switch (mSaveFilter)
		{
			case FFSAVE_TXT:
				mValidExtensions.emplace_back("txt");
				mFileTypeDescription = getString("txt_file") + " (*.txt)";
				mContext = CONTEXT_TXT;
				break;

			case FFSAVE_XML:
				mValidExtensions.emplace_back("xml");
				mFileTypeDescription = getString("xml_file") + " (*.xml)";
				mContext = CONTEXT_XML;
				break;

			case FFSAVE_XUI:
				mValidExtensions.emplace_back("xml");
				mFileTypeDescription = getString("xui_file") + " (*.xml)";
				mContext = CONTEXT_XUI;
				break;

			case FFSAVE_LSL:
				mValidExtensions.emplace_back("lsl");
				mFileTypeDescription = getString("lsl_file") + " (*.lsl)";
				mContext = CONTEXT_LSL;
				break;

			case FFSAVE_SLUA:
				mValidExtensions.emplace_back("slua");
				mFileTypeDescription = getString("slua_file") + " (*.slua)";
				mContext = CONTEXT_LSL;
				break;

			case FFSAVE_WAV:
				mValidExtensions.emplace_back("wav");
				mFileTypeDescription = getString("wav_file") + " (*.wav)";
				mContext = CONTEXT_SOUND;
				break;

			case FFSAVE_BVH:
				mValidExtensions.emplace_back("bvh");
				mFileTypeDescription = getString("bvh_file") + " (*.bvh)";
				mContext = CONTEXT_ANIM;
				break;

			case FFSAVE_DAE:
				mValidExtensions.emplace_back("dae");
				mFileTypeDescription = getString("dae_file") + " (*.dae)";
				mContext = CONTEXT_MODEL;
				break;

			case FFSAVE_OBJ:
				mValidExtensions.emplace_back("obj");
				mFileTypeDescription = getString("obj_file") + " (*.obj)";
				mContext = CONTEXT_MODEL;
				break;

			case FFSAVE_RAW:
				mValidExtensions.emplace_back("raw");
				mFileTypeDescription = getString("raw_file") + " (*.raw)";
				mContext = CONTEXT_RAW;
				break;

			case FFSAVE_TGA:
				mValidExtensions.emplace_back("tga");
				mFileTypeDescription = getString("tga_file") + " (*.tga)";
				mContext = CONTEXT_IMAGE;
				break;

			case FFSAVE_PNG:
				mValidExtensions.emplace_back("png");
				mFileTypeDescription = getString("png_file") + " (*.png)";
				mContext = CONTEXT_IMAGE;
				break;

			case FFSAVE_JPG:
				mValidExtensions.emplace_back("jpg");
				mFileTypeDescription = getString("jpg_file") +
									   " (*.jpg;*.jpeg)";
				mContext = CONTEXT_IMAGE;
				break;

			case FFSAVE_J2C:
				mValidExtensions.emplace_back("j2c");
				mFileTypeDescription = getString("j2c_file") + " (*.j2c)";
				mContext = CONTEXT_IMAGE;
				break;

			case FFSAVE_BMP:
				mValidExtensions.emplace_back("bmp");
				mFileTypeDescription = getString("bmp_file") + " (*.bmp)";
				mContext = CONTEXT_IMAGE;
				break;

			case FFSAVE_GLTF:
				mValidExtensions.emplace_back("gltf");
				mFileTypeDescription = getString("gltf_file") + " (*.gltf)";
				mContext = CONTEXT_MATERIAL;
				break;

			default:
				break;
		}
	}
	else if (mLoadFilter != FFLOAD_NONE && mLoadFilter != FFLOAD_ALL)
	{
		switch (mLoadFilter)
		{
			case FFLOAD_TEXT:
				mValidExtensions.emplace_back("txt");
				mFileTypeDescription = getString("text_files") + " (*.txt)";
				mContext = CONTEXT_TXT;
				break;

			case FFLOAD_XML:
				mValidExtensions.emplace_back("xml");
				mFileTypeDescription = getString("xml_files") + " (*.xml)";
				mContext = CONTEXT_XML;
				break;

			case FFLOAD_XUI:
				mValidExtensions.emplace_back("xml");
				mFileTypeDescription = getString("xui_files") + " (*.xml)";
				mContext = CONTEXT_XUI;
				break;

			case FFLOAD_SCRIPT:
				mValidExtensions.emplace_back("lsl");
				mValidExtensions.emplace_back("slua");
				mFileTypeDescription = getString("script_files") + " (*.lsl;*.slua)";
				mContext = CONTEXT_LSL;
				break;

			case FFLOAD_SOUND:
				mValidExtensions.emplace_back("wav");
				mValidExtensions.emplace_back("dsf");
				mValidExtensions.emplace_back("ogg");
				mFileTypeDescription = getString("sound_files") +
									   " (*.wav;*.dsf;*.ogg)";
				mContext = CONTEXT_SOUND;
				break;

			case FFLOAD_ANIM:
				mValidExtensions.emplace_back("bvh");
				mValidExtensions.emplace_back("anim");
				mFileTypeDescription = getString("animation_files") +
									   " (*.bvh;*.anim)";
				mContext = CONTEXT_ANIM;
				break;

			case FFLOAD_MODEL:
				mValidExtensions.emplace_back("dae");
				mValidExtensions.emplace_back("glb");
				mValidExtensions.emplace_back("gltf");
				mFileTypeDescription = getString("model_files") +
									   " (*.dae;*.glb;*.gltf)";
				mContext = CONTEXT_MODEL;
				break;

			case FFLOAD_TERRAIN:
				mValidExtensions.emplace_back("raw");
				mFileTypeDescription = getString("raw_files") + " (*.raw)";
				mContext = CONTEXT_RAW;
				break;

			case FFLOAD_IMAGE:
				mValidExtensions.emplace_back("tga");
				mValidExtensions.emplace_back("png");
				mValidExtensions.emplace_back("jpg");
				mValidExtensions.emplace_back("jpeg");
				mValidExtensions.emplace_back("j2c");
				mValidExtensions.emplace_back("bmp");
				mFileTypeDescription = getString("image_files") +
									   " (*.tga;*.png;*.jpg;*.jpeg;*.j2c;*.bmp)";
				mContext = CONTEXT_IMAGE;
				break;

			case FFLOAD_HDRI:
				mValidExtensions.emplace_back("exr");
				mFileTypeDescription = getString("exr_file") + " (*.exr)";
				mContext = CONTEXT_IMAGE;
				break;

			case FFLOAD_LUA:
				mValidExtensions.emplace_back("lua");
				mValidExtensions.emplace_back("luac");
				mFileTypeDescription = getString("lua_files") +
									   " (*.lua;*.luac)";
				mContext = CONTEXT_LUA;
				break;

			case FFLOAD_GLTF:
				mValidExtensions.emplace_back("glb");
				mValidExtensions.emplace_back("gltf");
				mFileTypeDescription = getString("gltf_files") +
									   " (*.glb;*.gltf)";
				mContext = CONTEXT_MATERIAL;
				break;

			default:
				break;
		}
	}
}

void HBFileSelector::setPrompt()
{
	std::string prompt;
	bool got_file_info = !mFileTypeDescription.empty();
	if (mCreatingDirectory)
	{
		prompt = getString("new_directory");
	}
	else if (mSavePicker && got_file_info)
	{
		prompt = getString("prompt_save") + " " + mFileTypeDescription;
	}
	else if (mMultiple && got_file_info)
	{
		prompt = getString("prompt_load_multiple") + " " +
				 mFileTypeDescription;
	}
	else if (!mMultiple && !mSavePicker && got_file_info)
	{
		prompt = getString("prompt_load_one") + " " + mFileTypeDescription;
	}
	else
	{
		prompt = getString("default_prompt");
	}
	mPromptTextBox->setText(prompt);
	if (mSaveFilter == FFSAVE_NONE)
	{
		mPromptTextBox->setColor(LLColor4::green);
	}
	else
	{
		mPromptTextBox->setColor(LLColor4::yellow);
	}
}

bool HBFileSelector::isFileExtensionValid(const std::string& filename)
{
	if (mValidExtensions.empty() ||
		(mShowAllTypesCheck && mShowAllTypesCheck->get()))
	{
		return true;
	}

	std::string tmp = filename;
	LLStringUtil::toLower(tmp);
	size_t j = tmp.rfind('.');
	if (j == std::string::npos || j == tmp.length() - 1)
	{
		return false;
	}
	tmp = tmp.substr(j + 1);
	for (U32 i = 0, count = mValidExtensions.size(); i < count; ++i)
	{
		if (mValidExtensions[i] == tmp)
		{
			return true;
		}
	}

	return false;
}

void HBFileSelector::setPathFromContext()
{
	if (mDirPicker)
	{
		std::string tmp = mCurrentEntry;
		if (!tmp.empty())
		{
			// Remove trailing delimiter(s)
			while (tmp.length() &&
				   tmp.rfind(LL_DIR_DELIM_CHR) == tmp.length() - 1)
			{
				tmp = tmp.substr(0, tmp.length() - 1);
			}

			size_t i = tmp.rfind(LL_DIR_DELIM_CHR);
			if (i != std::string::npos && i >= 1)
			{
				// Suggested directory selection
				mCurrentEntry = tmp.substr(i + 1);
				// Parent directory path
				mCurrentPath = tmp.substr(0, i);
			}
			else
			{
				// Suggested directory selection = root directory
				mCurrentEntry = tmp;
				mCurrentPath.clear();
			}
		}
		else
		{
			mCurrentPath = sLastPath;
		}
	}
	else
	{
		context_map_t::iterator it = sContextToPathMap.find(mContext);
		if (it != sContextToPathMap.end())
		{
			mCurrentPath = it->second;
		}
		else
		{
			mCurrentPath.clear();
		}
		// If the saved path is not valid any more, try to find the deepest
		// directory that used to contain it...
		while (!mCurrentPath.empty() && !LLFile::isdir(mCurrentPath))
		{
			size_t i = mCurrentPath.rfind(LL_DIR_DELIM_CHR);
			if (i == std::string::npos || i == ROOT_DELIMITER_POS)
			{
				mCurrentPath.clear();
				break;
			}
			mCurrentPath.erase(i);
		}
		if (mCurrentPath.empty())
		{
			if (mContext == CONTEXT_XUI)
			{
				#define XUI_DIR LL_DIR_DELIM_STR "xui" LL_DIR_DELIM_STR "en-us"
				mCurrentPath = gDirUtil.getSkinDir() + XUI_DIR;
			}
			else
			{
				mCurrentPath = sLastPath;
			}
		}
	}
	if (mCurrentPath.empty() || !LLFile::isdir(mCurrentPath))
	{
		if (!sLastPath.empty() && LLFile::isdir(sLastPath))
		{
			mCurrentPath = sLastPath;
		}
		else
		{
			mCurrentPath = gDirUtil.getOSUserDir();
		}
	}
	isCurrentPathAtRoot();
}

bool HBFileSelector::isCurrentPathAtRoot()
{
	// Remove trailing delimiter(s)
	mCurrentPath.erase(mCurrentPath.find_last_not_of(LL_DIR_DELIM_CHR) + 1);

	return mCurrentPath.empty();
}

void HBFileSelector::setSelectionData()
{
	mFiles.clear();
#if !LL_WINDOWS
	std::string path = mCurrentPath + LL_DIR_DELIM_STR;
#else
	std::string path;
	if (!mCurrentPath.empty())
	{
		path = mCurrentPath + LL_DIR_DELIM_STR;
	}
#endif
	std::string filename;
	LLScrollListItem* itemp = NULL;
	if (mDirPicker)
	{
		itemp = mDirectoriesList->getFirstSelected();
	}
	else if (mFilesList)
	{
		if (mMultiple)
		{
			LLScrollListCtrl::items_vec_t items = mFilesList->getAllSelected();
			for (size_t i = 0, count = items.size(); i < count; ++i)
			{
				itemp = items[i];	// Cannot be NULL. HB
				if (itemp->getColumn(0))
				{
					filename = itemp->getColumn(0)->getValue().asString();
					mFiles.emplace_back(path + filename);
				}
			}
		}
		itemp = mFilesList->getFirstSelected();
	}

	if (itemp && itemp->getColumn(0))
	{
		filename = itemp->getColumn(0)->getValue().asString();
		mCurrentSelection = path + filename;
		mInputLine->setText(filename);
	}
	else if ((mDirPicker || mSavePicker) && !mInputLine->getText().empty())
	{
		mCurrentSelection = path + mInputLine->getText();
	}
}

void HBFileSelector::doCallback()
{
	if (!mCallbackDone)
	{
		mCallbackDone = true;
		if (mLoadFileCallback)
		{
			mLoadFileCallback(mLoadFilter, mCurrentSelection,
							  mCallbackUserData);
		}
		else if (mLoadFilesCallback)
		{
			mLoadFilesCallback(mLoadFilter, mFiles, mCallbackUserData);
		}
		else if (mSaveFileCallback)
		{
			if (!mCurrentSelection.empty() &&
				!isFileExtensionValid(mCurrentSelection))
			{
				mCurrentSelection += "." + mValidExtensions[0];
			}
			mSaveFileCallback(mSaveFilter, mCurrentSelection,
							  mCallbackUserData);
		}
		else if (mDirPickCallback)
		{
			mDirPickCallback(mCurrentSelection, mCallbackUserData);
		}
	}
}

//static
// Native upload selection only. Save dialogs, inventory file browsing and
// other HBFileSelector users retain their original in-viewer selector.
#if LL_WINDOWS
static bool native_upload_file_dialog(HBFileSelector::ELoadFilter filter,
									  bool multiple,
									  std::deque<std::string>& files)
{
	const wchar_t* extensions = L"All files (*.*)\0*.*\0\0";
	const wchar_t* title = L"Select files to upload";
	switch (filter)
	{
		case HBFileSelector::FFLOAD_IMAGE:
			extensions = L"Image files (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.j2c)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.j2c\0All files (*.*)\0*.*\0\0";
			title = L"Select image to upload";
			break;
		case HBFileSelector::FFLOAD_SOUND:
			extensions = L"Sound files (*.wav;*.ogg;*.dsf)\0*.wav;*.ogg;*.dsf\0All files (*.*)\0*.*\0\0";
			title = L"Select sound to upload";
			break;
		case HBFileSelector::FFLOAD_ANIM:
			extensions = L"Animation files (*.bvh;*.anim)\0*.bvh;*.anim\0All files (*.*)\0*.*\0\0";
			title = L"Select animation to upload";
			break;
		case HBFileSelector::FFLOAD_MODEL:
			extensions = L"Mesh models (*.dae)\0*.dae\0All files (*.*)\0*.*\0\0";
			title = L"Select mesh model to upload";
			break;
		case HBFileSelector::FFLOAD_GLTF:
			extensions = L"Material files (*.gltf;*.glb)\0*.gltf;*.glb\0All files (*.*)\0*.*\0\0";
			title = L"Select material to upload";
			break;
		default: break;
	}

	// Firestorm's native Windows chooser uses a wide-character buffer and
	// OFN_EXPLORER for standard Windows navigation and multiple selection.
	std::vector<wchar_t> file_buffer(65536, L'\0');
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = GetActiveWindow();
	ofn.lpstrFile = file_buffer.data();
	ofn.nMaxFile = (DWORD)file_buffer.size();
	ofn.lpstrFilter = extensions;
	ofn.nFilterIndex = 1;
	ofn.lpstrTitle = title;
	ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_FILEMUSTEXIST |
				OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (multiple)
	{
		ofn.Flags |= OFN_ALLOWMULTISELECT;
	}
	if (!GetOpenFileNameW(&ofn))
	{
		return false;
	}

	const wchar_t* first = file_buffer.data();
	const auto from_native = [](const std::wstring& wide) {
		return utf16str_to_utf8str(llutf16string(wide.begin(), wide.end()));
	};
	if (!multiple)
	{
		files.emplace_back(from_native(std::wstring(first)));
		return true;
	}

	const wchar_t* next = first + wcslen(first) + 1;
	if (!*next)
	{
		files.emplace_back(from_native(std::wstring(first)));
	}
	else
	{
		std::wstring folder(first);
		if (!folder.empty() && folder.back() != L'\\' && folder.back() != L'/')
		{
			folder += L'\\';
		}
		while (*next)
		{
			files.emplace_back(from_native(folder + next));
			next += wcslen(next) + 1;
		}
	}
	return !files.empty();
}
#endif

//static
void HBFileSelector::loadUploadFile(ELoadFilter filter,
								   HBLoadFileCallback callback, void* userdatap)
{
#if LL_WINDOWS
	std::deque<std::string> files;
	native_upload_file_dialog(filter, false, files);
	std::string filename = files.empty() ? std::string() : files.front();
	if (callback)
	{
		callback(filter, filename, userdatap);
	}
#else
	loadFile(filter, callback, userdatap);
#endif
}

//static
void HBFileSelector::loadUploadFiles(ELoadFilter filter,
									HBLoadFilesCallback callback, void* userdatap)
{
#if LL_WINDOWS
	std::deque<std::string> files;
	native_upload_file_dialog(filter, true, files);
	if (callback)
	{
		callback(filter, files, userdatap);
	}
#else
	loadFiles(filter, callback, userdatap);
#endif
}
void HBFileSelector::loadFile(ELoadFilter filter,
							  HBLoadFileCallback callback, void* userdatap)
{
	if (sInstance)
	{
		llwarns << "Call done while a file selector instance already exists !  Aborting."
				<< llendl;
		llassert(false);
	}
	else
	{
		new HBFileSelector(filter, callback, userdatap);
	}
}

//static
void HBFileSelector::loadFiles(ELoadFilter filter,
							   HBLoadFilesCallback callback, void* userdatap)
{
	if (sInstance)
	{
		llwarns << "Call done while a file selector instance already exists !  Aborting."
				<< llendl;
		llassert(false);
	}
	else
	{
		new HBFileSelector(filter, callback, userdatap);
	}
}

//static
void HBFileSelector::saveFile(ESaveFilter filter, std::string suggestion,
							  HBSaveFileCallback callback, void* userdatap)
{
#if LL_WINDOWS
	// Use precisely the Windows Explorer common-dialog family employed by
	// loadUploadFile(), with Save As semantics for EVERY save-file caller.
	// Keep each caller's file type, default extension and callback contract.
	if (sInstance)
	{
		llwarns << "Cannot open native Save As while another file selector is open."
				<< llendl;
		return;
	}
	if (!callback) return;

	const wchar_t* file_filter = L"All files (*.*)\0*.*\0\0";
	const wchar_t* default_ext = NULL;
	EContext context = CONTEXT_DEFAULT;
	switch (filter)
	{
		case FFSAVE_TXT:
			file_filter = L"Text files (*.txt)\0*.txt\0\0";
			default_ext = L"txt";
			context = CONTEXT_TXT;
			break;
		case FFSAVE_XML:
			file_filter = L"XML files (*.xml)\0*.xml\0\0";
			default_ext = L"xml";
			context = CONTEXT_XML;
			break;
		case FFSAVE_XUI:
			file_filter = L"XUI files (*.xml)\0*.xml\0\0";
			default_ext = L"xml";
			context = CONTEXT_XUI;
			break;
		case FFSAVE_LSL:
			file_filter = L"LSL scripts (*.lsl)\0*.lsl\0\0";
			default_ext = L"lsl";
			context = CONTEXT_LSL;
			break;
		case FFSAVE_SLUA:
			file_filter = L"SLua scripts (*.slua)\0*.slua\0\0";
			default_ext = L"slua";
			context = CONTEXT_LSL;
			break;
		case FFSAVE_WAV:
			file_filter = L"WAV audio (*.wav)\0*.wav\0\0";
			default_ext = L"wav";
			context = CONTEXT_SOUND;
			break;
		case FFSAVE_BVH:
			file_filter = L"BVH animations (*.bvh)\0*.bvh\0\0";
			default_ext = L"bvh";
			context = CONTEXT_ANIM;
			break;
		case FFSAVE_DAE:
			file_filter = L"Collada mesh (*.dae)\0*.dae\0\0";
			default_ext = L"dae";
			context = CONTEXT_MODEL;
			break;
		case FFSAVE_OBJ:
			file_filter = L"Wavefront objects (*.obj)\0*.obj\0\0";
			default_ext = L"obj";
			context = CONTEXT_MODEL;
			break;
		case FFSAVE_RAW:
			file_filter = L"RAW terrain (*.raw)\0*.raw\0\0";
			default_ext = L"raw";
			context = CONTEXT_RAW;
			break;
		case FFSAVE_TGA:
			file_filter = L"TGA images (*.tga)\0*.tga\0\0";
			default_ext = L"tga";
			context = CONTEXT_IMAGE;
			break;
		case FFSAVE_PNG:
			file_filter = L"PNG images (*.png)\0*.png\0\0";
			default_ext = L"png";
			context = CONTEXT_IMAGE;
			break;
		case FFSAVE_JPG:
			file_filter = L"JPEG images (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";
			default_ext = L"jpg";
			context = CONTEXT_IMAGE;
			break;
		case FFSAVE_J2C:
			file_filter = L"JPEG2000 images (*.j2c)\0*.j2c\0\0";
			default_ext = L"j2c";
			context = CONTEXT_IMAGE;
			break;
		case FFSAVE_BMP:
			file_filter = L"BMP images (*.bmp)\0*.bmp\0\0";
			default_ext = L"bmp";
			context = CONTEXT_IMAGE;
			break;
		case FFSAVE_GLTF:
			file_filter = L"glTF scenes (*.gltf)\0*.gltf\0\0";
			default_ext = L"gltf";
			context = CONTEXT_MATERIAL;
			break;
		default:
			break;
	}

	// Preserve the old selector's per-file-type starting directories.
	std::string initial_path;
	context_map_t::const_iterator existing = sContextToPathMap.find(context);
	if (existing != sContextToPathMap.end() && LLFile::isdir(existing->second))
	{
		initial_path = existing->second;
	}
	else if (!sLastPath.empty() && LLFile::isdir(sLastPath))
	{
		initial_path = sLastPath;
	}
	else if (context == CONTEXT_XUI)
	{
		initial_path = gDirUtil.getSkinDir() + LL_DIR_DELIM_STR +
					   "xui" + LL_DIR_DELIM_STR + "en-us";
	}
	llutf16string utf16_dir = utf8str_to_utf16str(initial_path);
	std::wstring native_dir(utf16_dir.begin(), utf16_dir.end());

	std::vector<wchar_t> file_buffer(32768, L'\0');
	llutf16string utf16_name = utf8str_to_utf16str(suggestion);
	size_t length = llmin(utf16_name.size(), file_buffer.size() - 1);
	for (size_t i = 0; i < length; ++i)
	{
		file_buffer[i] = (wchar_t)utf16_name[i];
	}

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = GetActiveWindow();
	ofn.lpstrFile = file_buffer.data();
	ofn.nMaxFile = (DWORD)file_buffer.size();
	ofn.lpstrFilter = file_filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = default_ext;
	ofn.lpstrInitialDir = native_dir.empty() ? NULL : native_dir.c_str();
	ofn.lpstrTitle = L"Save As";
	ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST |
				OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	std::string filename;
	if (GetSaveFileNameW(&ofn))
	{
		std::wstring native_name(file_buffer.data());
		filename = utf16str_to_utf8str(llutf16string(native_name.begin(),
													 native_name.end()));

		// Like the previous HBFileSelector, ensure the selected export type
		// matches the resulting extension. JPEG accepts both jpg and jpeg.
		if (default_ext)
		{
			std::string lower = filename;
			LLStringUtil::toLower(lower);
			std::string suffix = "." + utf16str_to_utf8str(
				llutf16string(default_ext, default_ext + wcslen(default_ext)));
			bool valid = lower.size() >= suffix.size() &&
						 lower.compare(lower.size() - suffix.size(),
									   suffix.size(), suffix) == 0;
			if (!valid && filter == FFSAVE_JPG && lower.size() >= 5)
			{
				valid = lower.compare(lower.size() - 5, 5, ".jpeg") == 0;
			}
			if (!valid)
			{
				filename += suffix;
				// The native dialog's overwrite prompt covered the selected
				// name; when fixing a different extension, check again.
				if (LLFile::isfile(filename))
				{
					llutf16string utf16_final = utf8str_to_utf16str(filename);
					std::wstring native_final(utf16_final.begin(),
											  utf16_final.end());
					if (MessageBoxW(ofn.hwndOwner, native_final.c_str(),
									L"Overwrite existing file?",
									MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
					{
						filename.clear();
					}
				}
			}
		}

		if (!filename.empty())
		{
			size_t pos = filename.find_last_of("\\/");
			if (pos != std::string::npos)
			{
				sLastPath = filename.substr(0, pos);
				sContextToPathMap[context] = sLastPath;
				sUsedPaths.insert(sLastPath);
			}
		}
	}

	// Existing save callbacks also receive an empty filename on Cancel.
	callback(filter, filename, userdatap);
#else
	if (sInstance)
	{
		llwarns << "Call done while a file selector instance already exists !  Aborting."
				<< llendl;
		llassert(false);
	}
	else
	{
		new HBFileSelector(filter, suggestion, callback, userdatap);
	}
#endif
}

//static
void HBFileSelector::pickDirectory(std::string suggestion,
								   HBDirPickCallback callback, void* userdatap)
{
#if LL_WINDOWS
	// Native Explorer folder chooser, consistent with the upload and Save As
	// file dialogs. IFileOpenDialog(FOS_PICKFOLDERS) allows selecting drives,
	// mapped/network folders and creating a new folder in the Windows shell.
	// Keep the original prefs callback contract, including Cancel = empty.
	if (!callback) return;
	std::string selected_path;
	const HRESULT init_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED |
											 COINIT_DISABLE_OLE1DDE);
	const bool uninit_com = SUCCEEDED(init_hr);
	if (SUCCEEDED(init_hr) || init_hr == RPC_E_CHANGED_MODE)
	{
		IFileOpenDialog* dialog = NULL;
		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL,
									 CLSCTX_INPROC_SERVER,
									 IID_PPV_ARGS(&dialog));
		if (SUCCEEDED(hr) && dialog)
		{
			FILEOPENDIALOGOPTIONS flags = 0;
			if (SUCCEEDED(dialog->GetOptions(&flags)))
			{
				dialog->SetOptions(flags | FOS_PICKFOLDERS |
									  FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
			}

			if (!suggestion.empty() && LLFile::isdir(suggestion))
			{
				llutf16string utf16_path = utf8str_to_utf16str(suggestion);
				std::wstring initial_path(utf16_path.begin(), utf16_path.end());
				IShellItem* folder_item = NULL;
				if (SUCCEEDED(SHCreateItemFromParsingName(initial_path.c_str(),
									 NULL, IID_PPV_ARGS(&folder_item))) &&
					folder_item)
				{
					dialog->SetFolder(folder_item);
					folder_item->Release();
				}
			}
			if (SUCCEEDED(dialog->Show(GetActiveWindow())))
			{
				IShellItem* result = NULL;
				if (SUCCEEDED(dialog->GetResult(&result)) && result)
				{
					PWSTR path = NULL;
					if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH,
															&path)) && path)
					{
						std::wstring wide_path(path);
						llutf16string utf16_path(wide_path.begin(), wide_path.end());
						selected_path = utf16str_to_utf8str(utf16_path);
						CoTaskMemFree(path);
					}
					result->Release();
				}
			}
			dialog->Release();
		}
		else
		{
			llwarns << "Windows folder selection dialog unavailable (HRESULT "
					<< std::hex << hr << std::dec << ")." << llendl;
		}
	}
	else
	{
		llwarns << "Cannot initialize native Windows folder selector."
				<< llendl;
	}
	if (uninit_com) CoUninitialize();
	callback(selected_path, userdatap);
#else
	if (sInstance)
	{
		llwarns << "Call done while a file selector instance already exists !  Aborting."
				<< llendl;
		llassert(false);
	}
	else
	{
		new HBFileSelector(suggestion, callback, userdatap);
	}
#endif
}

//static
void HBFileSelector::onButtonRefresh(void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self)
	{
		if (self->mSavePicker && !self->mInputLine->getText().empty())
		{
			self->mCurrentEntry = self->mInputLine->getText();
		}
		self->mIsDirty = true;
	}
}

//static
void HBFileSelector::onPathComboCommit(LLUICtrl*, void* user_datap)
{
	HBFileSelector* self = (HBFileSelector*)user_datap;
	if (self)
	{
		std::string path = self->mPathComboBox->getSelectedValue().asString();
		if (!path.empty() && LLFile::isdir(path))
		{
			self->mCurrentPath = path;
			self->mIsDirty = true;
		}
	}
}

//static
void HBFileSelector::onButtonDirLevel(LLUICtrl* ctrlp, void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self && ctrlp)
	{
		if (self->mDirPicker)
		{
			self->mCurrentEntry.clear();
		}
		std::string operation = ctrlp->getValue().asString();
		if (operation == "home")
		{
			self->mCurrentPath = gDirUtil.getOSUserDir();
			self->isCurrentPathAtRoot();
			self->mIsDirty = true;
		}
		else if (operation == "suggested")
		{
			// *TODO: implement for directory selector as well ?
			if (!self->mDirPicker)
			{
				self->setPathFromContext();
				self->mIsDirty = true;
			}
		}
		else if (operation == "last")
		{
			if (!sLastPath.empty() && LLFile::isdir(sLastPath))
			{
				self->mCurrentPath = sLastPath;
				self->mIsDirty = true;
			}
		}
		else if (operation == "root")
		{
			if (!self->isCurrentPathAtRoot())
			{
#if LL_WINDOWS
				self->mCurrentPath.clear();
#else
				self->mCurrentPath = LL_DIR_DELIM_STR;
#endif
				self->isCurrentPathAtRoot();
				self->mIsDirty = true;
			}
		}
		else if (!self->isCurrentPathAtRoot())
		{
			// "level_up" operation
			size_t i = self->mCurrentPath.rfind(LL_DIR_DELIM_CHR);
			if (i != std::string::npos)
			{
				self->mCurrentPath = self->mCurrentPath.substr(0, i);
				self->mIsDirty = true;
			}
#if LL_WINDOWS
			else
			{
				self->mCurrentPath.clear();
				self->mIsDirty = true;
			}
#endif
			self->isCurrentPathAtRoot();
		}
	}
}

//static
void HBFileSelector::onButtonCreate(void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self)
	{
		std::string entry = self->mInputLine->getText();
		if (!entry.empty())
		{
			self->mInputLine->clear();
			self->mCurrentEntry = entry;
		}
		self->mInputLine->setEnabled(true);
		self->mCreatingDirectory = true;
		self->mDirLevelFlyoutBtn->setEnabled(false);
		self->mCreateBtn->setEnabled(false);
		self->mRefreshBtn->setEnabled(false);
		self->mOKBtn->setEnabled(false);
		self->mShowHiddenCheck->setEnabled(false);
		if (self->mShowAllTypesCheck)
		{
			self->mShowAllTypesCheck->setEnabled(false);
		}
		self->mDirectoriesList->setEnabled(false);
		if (self->mFilesList)
		{
			self->mFilesList->setEnabled(false);
		}
		self->setPrompt();
	}
}

static void close_selector(HBFileSelector* floaterp)
{
	floaterp->close();
}

//static
void HBFileSelector::onButtonOK(void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self)
	{
		self->setSelectionData();
		sLastPath = self->mCurrentPath;
		if (!sLastPath.empty())
		{
			sContextToPathMap[self->mContext] = sLastPath;
			// Remember the selected path as having been used during this
			// viewer session.
			sUsedPaths.insert(sLastPath);
		}
#if LL_WINDOWS
		// If we are at the drive selection level, we cannot return a selection
		else
		{
			self->mFiles.clear();
			self->mCurrentSelection.clear();
		}
#endif

		self->doCallback();
		// We cannot close the floater now, because it would mean destroying
		// this instance while this method is also called for keyboard events
		// occurring at a point its member variables would be used later on,
		// causing a crash. So we instead hide the floater (so that nothing any
		// more can be performed using it) and we register a one-shot idle
		// callback to the close_selector() function.
		self->setVisible(false);
		doOnIdleOneTime(std::bind(&close_selector, self));
	}
}

//static
void HBFileSelector::onButtonCancel(void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self)
	{
		self->close();
	}
}

//static
void HBFileSelector::onSelectDirectory(LLUICtrl*, void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self)
	{
		self->mCurrentEntry = self->mInputLine->getText();
		self->setSelectionData();
	}
}

//static
void HBFileSelector::onLevelDown(void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self && self->mDirectoriesList)
	{
		if (self->mDirPicker)
		{
			self->mCurrentEntry.clear();
		}
		LLScrollListItem* itemp = self->mDirectoriesList->getFirstSelected();
		if (itemp && itemp->getColumn(0))
		{
#if LL_WINDOWS
			if (!self->mCurrentPath.empty())
#endif
			{
				self->mCurrentPath += LL_DIR_DELIM_STR;
			}
			self->mCurrentPath += itemp->getColumn(0)->getValue().asString();
			self->mDirectoriesList->deselectAllItems(true);
			self->isCurrentPathAtRoot();
			self->mIsDirty = true;
		}
	}
}

//static
void HBFileSelector::onSelectFile(LLUICtrl*, void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self)
	{
		self->mCurrentEntry = self->mInputLine->getText();
		self->setSelectionData();
	}
}

//static
void HBFileSelector::onCommitCheckBox(LLUICtrl*, void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self)
	{
		self->mIsDirty = true;
	}
}

//static
bool HBFileSelector::onHandleKeyCallback(KEY key, MASK mask,
										 LLLineEditor* caller, void* userdatap)
{
	bool handled = false;

	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self && key == KEY_RETURN && mask == MASK_NONE)
	{
		if (self->mCreatingDirectory)
		{
			self->mCreatingDirectory = false;
			std::string new_dir = self->mInputLine->getText();
			if (!new_dir.empty())
			{
				std::string dir_name = self->mInputLine->getText();
				std::string new_dir = self->mCurrentPath + LL_DIR_DELIM_STR +
									  dir_name;
				LLFile::mkdir(new_dir);
				if (LLFile::isdir(new_dir))
				{
					if (self->mDirPicker)
					{
						// Adopt the new directory as the current entry
						self->mCurrentEntry = dir_name;
					}
					else
					{
						// Change to the newly created directory
						self->mCurrentPath = new_dir;
						self->mDirectoriesList->deselectAllItems(true);
						self->isCurrentPathAtRoot();
						// Restore file suggestion, if any
						self->mInputLine->setText(self->mCurrentEntry);
					}
				}
			}
			if (!self->mSavePicker)
			{
				self->mInputLine->setEnabled(false);
			}
			self->mDirLevelFlyoutBtn->setEnabled(true);
			self->mCreateBtn->setEnabled(true);
			self->mRefreshBtn->setEnabled(true);
			self->mOKBtn->setEnabled(true);
			self->mShowHiddenCheck->setEnabled(true);
			if (self->mShowAllTypesCheck)
			{
				self->mShowAllTypesCheck->setEnabled(true);
			}
			self->mDirectoriesList->setEnabled(true);
			if (self->mFilesList)
			{
				self->mFilesList->setEnabled(true);
			}
			self->setPrompt();
			self->mIsDirty = true;

			handled = true;
		}
		else if (self->mSavePicker)
		{
			self->setSelectionData();
			onButtonOK(self);
			handled = true;
		}
	}

	return handled;
}

//static
void HBFileSelector::onKeystrokeCallback(LLLineEditor* caller, void* userdatap)
{
	HBFileSelector* self = (HBFileSelector*)userdatap;
	if (self && caller && caller->getEnabled())
	{
		// We must deselect any selected entry if we just typed a new letter,
		// else the selected entry would override what the user entered in the
		// input line whenever a suggested file/dir name corresponds to an
		// existing file/dir...
		if (self->mDirPicker)
		{
			self->mDirectoriesList->deselectAllItems();
		}
		else if (self->mSavePicker && !self->mCreatingDirectory)
		{
			self->mFilesList->deselectAllItems();
		}
	}
}

//static
void HBFileSelector::saveDefaultPaths(const std::string& filename)
{
	std::string fullpath = gDirUtil.getFullPath(LL_PATH_USER_SETTINGS,
												filename);
	llofstream out(fullpath.c_str());
	if (!out.is_open())
	{
		llwarns << "Unable to open \"" << fullpath
				<< "\" for writing. Default paths not saved." << llendl;
		return;
	}

	llinfos << "Saving default selector paths to: " << fullpath << llendl;

	LLSD data;
	context_map_t::iterator end = sContextToPathMap.end();
	for (S32 context = CONTEXT_DEFAULT; context < CONTEXT_END; ++context)
	{
		std::string path;
		context_map_t::iterator it = sContextToPathMap.find(context);
		if (it != end)
		{
			path = it->second;
		}
		data.set(context, path);
	}
	LLSDSerialize::toPrettyXML(data, out);
	out.close();
}

//static
void HBFileSelector::loadDefaultPaths(const std::string& filename)
{
	std::string fullpath = gDirUtil.getFullPath(LL_PATH_USER_SETTINGS,
												filename);
	LLSD data;
	llifstream input(fullpath.c_str());
	if (input.is_open())
	{
		llinfos << "Loading default selector paths from: " << fullpath
				<< llendl;
		LLSDSerialize::fromXML(data, input);
	}

	if (data.isUndefined() || !data.isArray())
	{
		llinfos << "Default selector paths file \"" << fullpath
				<< "\" is missing, ill-formed, or simply undefined." << llendl;
		return;
	}

	for (S32 context = CONTEXT_DEFAULT; context < CONTEXT_END; ++context)
	{
		std::string path = data.get(context).asString();
		if (!path.empty())
		{
			sContextToPathMap.emplace(context, path);
		}
	}

	input.close();
}

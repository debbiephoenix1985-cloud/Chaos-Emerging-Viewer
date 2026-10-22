/**
 * @file llfontregistry.h
 * @author Brad Payne
 * @brief Storage for fonts.
 *
 * $LicenseInfo:firstyear=2008&license=viewergpl$
 *
 * Copyright (c) 2008-2009, Linden Research, Inc.
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

#include <vector>

#include "llpointer.h"
#include "llstring.h"

class LLFontGL;

struct LLFontFileInfo
{
	LL_INLINE LLFontFileInfo(const std::string& file_name,
							 const std::function<bool(llwchar)>& ftor = NULL)
	:	mFileName(file_name),
		mCharFunctor(ftor)
	{
	}

	LL_INLINE LLFontFileInfo(const LLFontFileInfo& ffi)
	:	mFileName(ffi.mFileName),
		mCharFunctor(ffi.mCharFunctor)
	{
	}

	std::string						mFileName;
	std::function<bool(llwchar)>	mCharFunctor;
};
typedef std::vector<LLFontFileInfo> font_file_info_vec_t;

class LLFontDescriptor
{
public:
	LLFontDescriptor();
	LLFontDescriptor(const std::string& name, const std::string& size,
					 U8 style = 0);	// 0 = NORMAL style
	LLFontDescriptor(const std::string& name, const std::string& size,
					 U8 style, const font_file_info_vec_t& font_list);

	LLFontDescriptor normalize() const;

	bool operator<(const LLFontDescriptor& b) const;

	bool isTemplate() const;

	LL_INLINE const std::string& getName() const			{ return mName; }
	LL_INLINE void setName(const std::string& name)			{ mName = name; }
	LL_INLINE const std::string& getSize() const			{ return mSize; }
	LL_INLINE void setSize(const std::string& size)			{ mSize = size; }

	void addFontFile(const std::string& file_name,
					 const std::string& char_functor = LLStringUtil::null);

	LL_INLINE void setFontFiles(const font_file_info_vec_t& font_files)
	{
		mFontFiles = font_files;
	}

	LL_INLINE const font_file_info_vec_t& getFontFiles() const
	{
		return mFontFiles;
	}

	LL_INLINE font_file_info_vec_t& getFontFiles()			{ return mFontFiles; }

	LL_INLINE U8 getStyle() const							{ return mStyle; }
	LL_INLINE void setStyle(U8 style)						{ mStyle = style; }

private:
	std::string					mName;
	std::string					mSize;
	font_file_info_vec_t		mFontFiles;
	U8							mStyle;

	typedef std::map<std::string, std::function<bool(llwchar)>,
					 std::less<> > char_functor_map_t;
	static char_functor_map_t	sCharFunctors;
};

class LLFontRegistry
{
	friend bool init_from_xml(LLFontRegistry*, LLPointer<class LLXMLNode>);

protected:
	LOG_CLASS(LLFontRegistry);

public:
	LLFontRegistry(const strings_vec_t& xui_paths,
				   bool create_gl_textures = true);
	~LLFontRegistry();

	// Load standard font info from XML file(s).
	bool parseFontInfo(const std::string& xml_filename);

	// Clear cached glyphs for all fonts.
	void reset();

	// Destroy all fonts.
	void clear();

	// GL cleanup
	void destroyGL();

	LLFontGL* getFont(const LLFontDescriptor& desc, bool normalize = true);
	const LLFontDescriptor* getMatchingFontDesc(const LLFontDescriptor& desc);
	const LLFontDescriptor* getClosestFontTemplate(const LLFontDescriptor& desc);

	bool nameToSize(const std::string& size_name, F32& size);

	void dump();

	LL_INLINE const strings_vec_t& getUltimateFallbackList() const
	{
		return mUltimateFallbackList;
	}

private:
	LLFontRegistry(const LLFontRegistry& other);	// no-copy
	LLFontGL* createFont(const LLFontDescriptor& desc);

private:
	// Given a descriptor, look up specific font instantiation.
	typedef std::map<LLFontDescriptor, LLFontGL*> font_reg_map_t;
	font_reg_map_t	mFontMap;

	// Given a size name, look up the point size.
	typedef std::map<std::string, F32, std::less<> > font_size_map_t;
	font_size_map_t	mFontSizes;

	strings_vec_t	mUltimateFallbackList;
	strings_vec_t	mXUIPaths;
	bool			mCreateGLTextures;
};

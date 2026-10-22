/**
 * @file llimagepng.cpp
 * @brief LLImageFormatted glue to encode / decode PNG files.
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

#include "linden_common.h"

#include "llpngwrapper.h"

#include "llimagepng.h"

LLImagePNG::LLImagePNG()
:	LLImageFormatted(IMG_CODEC_PNG),
	mTmpWriteBuffer(NULL)
{
}

LLImagePNG::~LLImagePNG()
{
	if (mTmpWriteBuffer)
	{
		delete[] mTmpWriteBuffer;
	}
}

// Parses PNG image information and set the appropriate width, height and
// components (channels) information.
//virtual
bool LLImagePNG::updateData()
{
    resetLastError();

    // Check to make sure that this instance has been initialized with data
    if (!getData() || getDataSize() == 0)
    {
        setLastError("Uninitialized instance of LLImagePNG");
        return false;
    }

	// Decode the PNG data and extract sizing information
	LLPngWrapper pngWrapper;
	if (!pngWrapper.isValidPng(getData()))
	{
		setLastError("LLImagePNG data does not have a valid PNG header!");
		return false;
	}

	LLPngWrapper::ImageInfo infop;
	if (!pngWrapper.readPng(getData(), getDataSize(), NULL, &infop))
	{
		setLastError(pngWrapper.getErrorMessage());
		return false;
	}

	setSize(infop.mWidth, infop.mHeight, infop.mComponents);

	return true;
}

// Decodes an in-memory PNG image into the raw RGB or RGBA format used within
// SecondLife.
//virtual
bool LLImagePNG::decode(LLImageRaw* raw_imagep)
{
    resetLastError();

	if (!raw_imagep)
	{
		llwarns << "Attempted to decode a NULL raw image buffer address"
				<< llendl;
		llassert(false);
		return false;
	}

    // Check to make sure that this instance has been initialized with data
    if (!getData() || getDataSize() == 0)
    {
        setLastError("LLImagePNG trying to decode an image with no data !");
        return false;
    }

	// Decode the PNG data into the raw image
	LLPngWrapper pngWrapper;
	if (!pngWrapper.isValidPng(getData()))
	{
		setLastError("LLImagePNG data does not have a valid PNG header !");
		return false;
	}

	if (!pngWrapper.readPng(getData(), getDataSize(), raw_imagep))
	{
		setLastError(pngWrapper.getErrorMessage());
		return false;
	}

	return true;
}

// Encodes the in memory RGB image into PNG format.
//virtual
bool LLImagePNG::encode(const LLImageRaw* raw_imagep)
{
	if (!raw_imagep)
	{
		llwarns << "Attempted to decode a NULL raw image" << llendl;
		llassert(false);
		return false;
	}
	if (raw_imagep->isBufferInvalid())
	{
		setLastError("Invalid input, no buffer");
		return false;
	}

    resetLastError();

	// Image logical size
	setSize(raw_imagep->getWidth(), raw_imagep->getHeight(),
			raw_imagep->getComponents());

	// Temporary buffer to hold the encoded image. Note: the final image
	// size should be much smaller due to compression.
	if (mTmpWriteBuffer)
	{
		delete[] mTmpWriteBuffer;
	}
	U32 bufferSize = getWidth() * getHeight() * getComponents() + 1024;
	U8* mTmpWriteBuffer = new (std::nothrow) U8[bufferSize];
	if (!mTmpWriteBuffer)
	{
		LLMemory::allocationFailed(bufferSize);
		setLastError("Unable to encode a PNG image: out of memory.");
		return false;
	}

	// Delegate actual encoding work to wrapper
	LLPngWrapper pngWrapper;
	if (!pngWrapper.writePng(raw_imagep, mTmpWriteBuffer))
	{
		setLastError(pngWrapper.getErrorMessage());
		return false;
	}

	// Resize internal buffer and copy from temp
	bool res = false;
	U32 encodedSize = pngWrapper.getFinalSize();
	if (allocateData(encodedSize))
	{
		memcpy(getData(), mTmpWriteBuffer, encodedSize);
		res = true;
	}

	delete[] mTmpWriteBuffer;

	return res;
}

// Helper function
static S32 read_s32(LLFile& file)
{
	U8 p[4];
	file.read(&p[0], 4);
	return (S32(p[3]) & 0x000000FF) | ((S32(p[2]) << 8) & 0x0000FF00) |
		   ((S32(p[1]) << 16) & 0x00FF0000) | ((S32(p[0]) << 24) & 0xFF000000);
}

//static
bool LLImagePNG::getDimensions(LLFile& file, S32& width, S32& height)
{
	constexpr S64 PNG_MAGIC_SIZE = 8;
	static const U8 PNG_MAGIC[PNG_MAGIC_SIZE] =
		{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

	constexpr S64 HEADER_SIZE = PNG_MAGIC_SIZE + 16;
	bool too_short = LLFile::getFileSize(file.getFileName()) < HEADER_SIZE;

	// Make sure this is a PNG file.
	U8 bufferp[PNG_MAGIC_SIZE];
	file.seek(0);	// Ensure we are at the start of file. HB
	if (too_short || file.read(bufferp, PNG_MAGIC_SIZE) != PNG_MAGIC_SIZE)
	{
		LLImage::setLastError("Premature end of file: " + file.getFileName());
		return false;
	}
	if (memcmp((const void*)bufferp, (const void*)PNG_MAGIC, PNG_MAGIC_SIZE))
	{
		LLImage::setLastError("Not a PNG file: " + file.getFileName());
		return false;
	}

	// Read image dimensions.
	file.seek(8);
	width = read_s32(file);
	height = read_s32(file);
	return true;
}

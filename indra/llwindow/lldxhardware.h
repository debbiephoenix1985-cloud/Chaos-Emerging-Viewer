/**
 * @file lldxhardware.h
 * @brief LLDXHardware definition
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

#pragma once

#include "llsd.h"

extern void (*gWriteDebug)(const char* msg);

class LLDXHardware
{
protected:
	LOG_CLASS(LLDXHardware);

public:
	LLDXHardware() = default;

	// Returns info found via DirectX about the display adapter.
	LLSD getDisplayInfo();

	// Gets the VRAM amount of best GPU in MB: returns memory on success, 0 on
	// failure.
	static S32 getMBVideoMemoryViaDXGI();

	LL_INLINE static void setWriteDebugFunc(void (*func)(const char*))
	{
		gWriteDebug = func;
	}

	// Methods to request and hold a high-performance GPU on Windows 10+
	// Laptops can dynamically switch between integrated and discrete GPUs.
	// The viewer has GPU-specific optimizations, and this switching can cause
	// problems and crashes. The login screen requires low performance, which
	// can lead to the OS deciding to switch to the integrated GPU; to avoid
	// this, we request and hold a high-performance GPU using A D3D11 context
	// until login. For diagnostics, we also log GPU changes.
	static void requestHighPerformanceGPU();
	static void detectGPUChange();
	static void clearHighPerformanceGPURequest();

private:
	LLSD mInfo;
};

extern LLDXHardware gDXHardware;

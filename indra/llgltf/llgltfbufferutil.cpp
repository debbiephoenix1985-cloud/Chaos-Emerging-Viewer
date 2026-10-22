/**
 * @file llgltfbufferutil.h
 * @brief LLGLTF Implementation
 *
 * $LicenseInfo:firstyear=2024&license=viewergpl$
 *
 * Copyright (c) 2024-2025, Henri Beauchamp.
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

#include "llgltfbufferutil.h"

namespace LLGLTF
{
	LL_NO_INLINE void not_implemented(const char* func_sig)
	{
		// This may indeed happen: do not crash a release viewer, just warn. HB
		llwarns << "TODO: implement " << func_sig << llendl;
		llassert(false);
	}

	LL_NO_INLINE void unsupported_accessor_type(const char* func_sig, U8 type)
	{
		llerrs << func_sig << ": Unsupported accessor type: " << type
			   << llendl;
	}

	LL_NO_INLINE void invalid_component_type(const char* func_sig, U8 type)
	{
		llerrs << func_sig << ": Invalid component type: " << type << llendl;
	}

	LL_NO_INLINE void json_error(const char* func_sig,
								 const lljson::exception& e)
	{
		llwarns << func_sig << " encountered nlohmann::json error: "
				<< e.what() << llendl;
	}

	LL_NO_INLINE void invalid_buffer(const char* func_sig)
	{
		llwarns << func_sig << " was passed an invalid buffer." << llendl;
	}

	LL_NO_INLINE void invalid_buffer_view(const char* func_sig)
	{
		llwarns << func_sig << " was passed an invalid buffer view." << llendl;
	}
}

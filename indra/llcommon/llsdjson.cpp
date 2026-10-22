/**
 * @file llsdjson.cpp
 * @brief LLSD flexible data system
 *
 * $LicenseInfo:firstyear=2015&license=viewergpl$
 *
 * Copyright (c) 2015, Linden Research, Inc.
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

#include "llsdjson.h"

LLSD llsd_from_json(const lljson& val)
{
	LLSD result;

	switch (val.type())
	{
		case lljson::value_t::null:
		default:
			break;

		case lljson::value_t::boolean:
			result = LLSD(val.get<bool>());
			break;

		case lljson::value_t::number_integer:
			result = LLSD(val.get<int>());
			break;

		case lljson::value_t::number_unsigned:
			result = LLSD((S32)val.get<unsigned int>());
			break;

		case lljson::value_t::number_float:
			result = LLSD(val.get<double>());
			break;

		case lljson::value_t::object:
			result = LLSD::emptyMap();
			for (lljson::const_iterator it = val.cbegin(), end = val.cend();
				 it != end; ++it)
			{
				result[it.key()] = llsd_from_json(it.value());
			}
			break;

		case lljson::value_t::array:
			result = LLSD::emptyArray();
			for (lljson::const_iterator it = val.cbegin(), end = val.cend();
				 it != end; ++it)
			{
				result.append(llsd_from_json(it.value()));
			}
			break;

		case lljson::value_t::string:
			// Not using val.get<std::string>() here to avoid a string copy. HB
			result = LLSD(val.template get_ref<const lljson::string_t&>());
			break;
	}

	LL_DEBUGS("Json") << "Converted from:\n" << val << "\nto:\n" << result
					  << LL_ENDL;

	return result;
}

lljson llsd_to_json(const LLSD& val)
{
	lljson result;

	switch (val.type())
	{
		case LLSD::TypeUndefined:
			// 'result' is already initialized to a null json value. HB
			break;

		case LLSD::TypeBoolean:
			result = val.asBoolean();
			break;

		case LLSD::TypeInteger:
			result = val.asInteger();
			break;

		case LLSD::TypeReal:
			result = val.asReal();
			break;

		case LLSD::TypeURI:
		case LLSD::TypeDate:
		case LLSD::TypeUUID:
		case LLSD::TypeString:
			result = val.asString();
			break;

		case LLSD::TypeMap:
			result = lljson::object();
			for (LLSD::map_const_iterator it = val.beginMap(),
										  end = val.endMap();
				 it != end; ++it)
			{
				result[it->first] = llsd_to_json(it->second);
			}
			break;

		case LLSD::TypeArray:
			result = lljson::array();
			for (LLSD::array_const_iterator it = val.beginArray(),
											end = val.endArray();
				 it != end; ++it)
			{
				result.emplace_back(llsd_to_json(*it));
			}
			break;

		case LLSD::TypeBinary:
		default:
			llerrs << "Unsupported conversion to JSON from LLSD type: "
				   << val.type() << llendl;
	}

	LL_DEBUGS("Json") << "Converted from:\n" << val << "\nto:\n" << result
					  << LL_ENDL;

	return result;
}

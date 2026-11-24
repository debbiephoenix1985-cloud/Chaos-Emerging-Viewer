/**
 * @file llloadingindicator.cpp
 * @brief Perpetual loading indicator
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llloadingindicator.h"

// Linden library includes
#include "llsingleton.h"

// Project includes
#include "lluictrlfactory.h"
#include "llui.h"
#include "llui.h"
#include "llframetimer.h"

// Register the original 12-frame Firestorm spinner in Cool VL's widget factory.
static const std::string LOADING_INDICATOR_TAG = "loading_indicator";
static LLRegisterWidget<LLLoadingIndicator> register_loading_indicator(LOADING_INDICATOR_TAG);

LLLoadingIndicator::LLLoadingIndicator(const std::string& name,
                                       const LLRect& rect, F32 images_per_sec)
: LLUICtrl(name, rect, false, NULL, NULL, FOLLOWS_LEFT | FOLLOWS_TOP),
  mImagesPerSec(images_per_sec > 0.f ? images_per_sec : 1.f),
  mCurImageIdx(0)
{
    for (S32 i = 1; i <= 12; ++i)
        mImages.push_back(LLUI::getUIImage("Progress_" + std::to_string(i)));
    start();
}

const std::string& LLLoadingIndicator::getTag() const { return LOADING_INDICATOR_TAG; }

LLXMLNodePtr LLLoadingIndicator::getXML(bool save_children) const
{
    LLXMLNodePtr node = LLUICtrl::getXML(save_children);
    node->setName(LOADING_INDICATOR_TAG);
    return node;
}

LLView* LLLoadingIndicator::fromXML(LLXMLNodePtr node, LLView* parent,
                                     LLUICtrlFactory* factory)
{
    std::string name = LOADING_INDICATOR_TAG;
    node->getAttributeString("name", name);
    LLRect rect;
    createRect(node, rect, parent, LLRect(0, 20, 20, 0));
    F32 rate = 1.0f;
    node->getAttributeF32("images_per_sec", rate);
    auto* indicator = new LLLoadingIndicator(name, rect, rate);
    indicator->initFromXML(node, parent);
    return indicator;
}

void LLLoadingIndicator::draw()
{
    // Time to switch to the next image?
    if (mImageSwitchTimer.getStarted() && mImageSwitchTimer.hasExpired())
    {
        // Switch to the next image.
        if (!mImages.empty())
        {
            mCurImageIdx = (mCurImageIdx + 1) % mImages.size();
        }

        // Restart timer.
        start();
    }

    LLUIImagePtr cur_image = mImages.empty() ? LLUIImagePtr(NULL) : mImages[mCurImageIdx];

    // Draw current image.
    if( cur_image.notNull() )
    {
        cur_image->draw(getLocalRect(), LLColor4::white);
    }

    LLUICtrl::draw();
}

void LLLoadingIndicator::stop()
{
    mImageSwitchTimer.stop();
}

void LLLoadingIndicator::start()
{
    mImageSwitchTimer.start();
    F32 period = mImages.empty() ? 1.0f : 1.0f / (mImages.size() * mImagesPerSec);
    mImageSwitchTimer.setTimerExpirySec(period);
}

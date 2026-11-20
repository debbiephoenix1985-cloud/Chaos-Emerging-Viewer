// Chaos Emerging Favorites bar: inventory-backed landmarks with Firestorm-style copy drag/drop.
// Reference: FirestormViewer/phoenix-firestorm indra/newview/llfavoritesbar.cpp
#include "llviewerprecompiledheaders.h"
#include "llchaosfavoritesbar.h"

#include "llagent.h"
#include "llstartup.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llfloaterworldmap.h"
#include "lltextbox.h"
#include "lltooldraganddrop.h"
#include "llviewerinventory.h"
#include "llstring.h"
#include "llviewercontrol.h"
#include "llviewerfoldertype.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

LLChaosFavoritesBar* gChaosFavoritesBarp = nullptr;

namespace
{
std::vector<std::string> readFavoriteOrder()
{
    std::vector<std::string> order;
    std::istringstream stream(gSavedPerAccountSettings.getString("ChaosFavoritesBarOrder"));
    std::string id;
    while (std::getline(stream, id, ','))
    {
        if (!id.empty()) order.push_back(id);
    }
    return order;
}

void writeFavoriteOrder(const std::vector<std::string>& order)
{
    std::string joined;
    for (const auto& id : order)
    {
        if (!joined.empty()) joined += ",";
        joined += id;
    }
    gSavedPerAccountSettings.setString("ChaosFavoritesBarOrder", joined);
}

class LLChaosFavoriteCopyCallback final : public LLInventoryCallback
{
public:
    explicit LLChaosFavoriteCopyCallback(const LLUUID& before) : mBefore(before) {}
    void fire(const LLUUID& id) override
    {
        if (gChaosFavoritesBarp && id.notNull())
        {
            gChaosFavoritesBarp->moveItemBefore(id, mBefore);
        }
    }
private:
    LLUUID mBefore;
};
}

LLChaosFavoritesBar::LLChaosFavoritesBar(const LLRect& rect)
: LLPanel("chaos_favorites_bar", rect, false)
{
    gChaosFavoritesBarp = this;
    // The root UI is constructed before login: never expose this in the splash.
    setVisible(false);
    setFollows(FOLLOWS_TOP | FOLLOWS_LEFT | FOLLOWS_RIGHT);
    setIsChrome(true);
    setMouseOpaque(true);
    setBackgroundVisible(true);
    setBackgroundOpaque(true);
    setBackgroundColor(LLColor4(0.16f, 0.16f, 0.16f, 1.f));

    // Show the label only when no landmarks are present.
    // Do not reserve a permanent left gutter before the first favourite.
    mTitle = new LLTextBox("favorites_label", LLRect(9, 16, 160, 1),
                           "Favourites Bar", LLFontGL::getFontSansSerifSmall(), false);
    mTitle->setToolTip("Drag landmarks here from Inventory for quick teleport access");
    addChild(mTitle);

    mOverflow = new LLComboBox("favorites_overflow", LLRect(0, 16, 56, 1),
                               "More", onOverflowCommit, this);
    addChild(mOverflow);
    mOverflow->setVisible(false);

    gInventory.addObserver(this);
    mDirty = true;
}

LLChaosFavoritesBar::~LLChaosFavoritesBar()
{
    gInventory.removeObserver(this);
    if (gChaosFavoritesBarp == this) gChaosFavoritesBarp = nullptr;
}

void LLChaosFavoritesBar::changed(U32)
{
    // Observe the same account inventory folder as Firestorm; the viewer
    // receives its landmarks from the grid, not from another viewer's cache.
    mDirty = true;
}

void LLChaosFavoritesBar::updateState()
{
    // Visibility requires a COMPLETED login, not merely enabled preferences.
    // gAgentID can already be populated while the login/splash UI is visible.
    const bool logged_in = LLStartUp::isLoggedIn() &&
                           gAgentID.notNull() && gAgent.getRegion() != nullptr;
    const bool enabled = logged_in &&
                         gSavedSettings.getBool("ShowFavoritesBar") &&
                         !gAgent.cameraMouselook();
    if (getVisible() != enabled) setVisible(enabled);
    if (!enabled) return;

    if (mFolderID.isNull())
    {
        mFolderID = gInventory.findCategoryUUIDForType(LLFolderType::FT_FAVORITE, true);
        if (mFolderID.notNull())
        {
            gInventory.fetchDescendentsOf(mFolderID);
            mDirty = true;
        }
    }
    if (mFolderID.notNull() && mDirty) rebuildFromInventory();
}

void LLChaosFavoritesBar::rebuildFromInventory()
{
    LLInventoryModel::cat_array_t* folders = nullptr;
    LLInventoryModel::item_array_t* items = nullptr;
    gInventory.getDirectDescendentsOf(mFolderID, folders, items);
    if (!items) return; // Inventory still loading from the server.

    favorites_t next;
    next.reserve(items->size());
    for (const auto& entry : *items)
    {
        LLViewerInventoryItem* item = entry.get();
        if (!item) continue;
        if (item->getType() != LLAssetType::AT_LANDMARK) continue;
        Favorite fav;
        fav.id = item->getUUID();
        fav.name = item->getName();
        fav.created = item->getCreationDate();
        next.push_back(fav);
    }

    const auto saved = readFavoriteOrder();
    std::unordered_map<std::string, size_t> positions;
    for (size_t i = 0; i < saved.size(); ++i) positions[saved[i]] = i;

    std::stable_sort(next.begin(), next.end(),
        [&positions](const Favorite& a, const Favorite& b)
        {
            const auto ia = positions.find(a.id.asString());
            const auto ib = positions.find(b.id.asString());
            if (ia != positions.end() && ib != positions.end())
                return ia->second < ib->second;
            if (ia != positions.end()) return true;
            if (ib != positions.end()) return false;
            if (a.created != b.created) return a.created < b.created;
            return a.name < b.name;
        });

    bool altered = next.size() != mItems.size();
    if (!altered)
    {
        for (size_t i = 0; i < next.size(); ++i)
        {
            if (next[i].id != mItems[i].id || next[i].name != mItems[i].name)
            {
                altered = true;
                break;
            }
        }
    }
    mDirty = false;
    if (!altered) return;

    mItems.swap(next);
    for (LLButton* button : mButtons)
    {
        removeChild(button);
        delete button;
    }
    mButtons.clear();
    mButtonData.clear();

    for (const Favorite& fav : mItems)
    {
        auto data = std::make_unique<ButtonData>();
        data->bar = this;
        data->item_id = fav.id;
        LLButton* button = new LLButton("favorite_place", LLRect(0, 16, 140, 1),
                                        nullptr, onFavoriteClick, data.get());
        // Firestorm Default skin Favorites button artwork instead of
        // the viewer's generic blue rounded toolbar button images.
        button->setImages("ChaosFavoriteButtonOff");
        button->setHoverImages("ChaosFavoriteButtonOver", "ChaosFavoriteButtonOver");
        button->setImageDisabled("ChaosFavoriteButtonOff");
        button->setImageDisabledSelected("ChaosFavoriteButtonOff");
        button->setScaleImage(true);
        button->setFont(LLFontGL::getFontSansSerifSmall());
        button->setDropShadowedText(false);
        button->setLeftHPad(10);
        button->setRightHPad(10);
        button->setUnselectedLabelColor(LLColor4::white);
        button->setSelectedLabelColor(LLColor4::white);
        button->setLabel(fav.name);
        button->setToolTip(fav.name + "\nClick to teleport; drag a landmark here to add it.");
        button->setFollows(FOLLOWS_LEFT | FOLLOWS_TOP);
        addChild(button);
        mButtonData.push_back(std::move(data));
        mButtons.push_back(button);
    }
    layoutButtons();
}

void LLChaosFavoritesBar::layoutButtons()
{
    if (!mOverflow) return;
    // Firestorm uses 15px-high landmark buttons, up to 140px wide,
    // with a 20px text allowance and no permanent label gutter.
    if (mTitle) mTitle->setVisible(mItems.empty());

    constexpr S32 BUTTON_GAP = 2;
    constexpr S32 BUTTON_TEXT_PADDING = 20;
    constexpr S32 MAX_BUTTON_WIDTH = 140;
    constexpr S32 MIN_BUTTON_WIDTH = 34;
    constexpr S32 OVERFLOW_WIDTH = 56;
    constexpr S32 LEFT_INSET = 1;
    constexpr S32 RIGHT_INSET = 2;
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    const S32 right_edge = llmax(0, getRect().getWidth() - RIGHT_INSET);

    std::vector<S32> widths;
    widths.reserve(mItems.size());
    S32 total_width = LEFT_INSET;
    for (const Favorite& item : mItems)
    {
        const S32 width = llclamp(font->getWidth(item.name) +
                                  BUTTON_TEXT_PADDING,
                                  MIN_BUTTON_WIDTH, MAX_BUTTON_WIDTH);
        widths.push_back(width);
        total_width += width + BUTTON_GAP;
    }

    const bool needs_overflow = total_width > right_edge;
    const S32 button_edge = right_edge -
                            (needs_overflow ? OVERFLOW_WIDTH + BUTTON_GAP : 0);
    S32 x = LEFT_INSET;
    size_t visible_count = 0;
    for (size_t i = 0; i < mItems.size(); ++i)
    {
        if (x + widths[i] > button_edge) break;
        x += widths[i] + BUTTON_GAP;
        ++visible_count;
    }

    x = LEFT_INSET;
    for (size_t i = 0; i < mButtons.size(); ++i)
    {
        LLButton* button = mButtons[i];
        const bool shown = i < visible_count;
        button->setVisible(shown);
        if (!shown) continue;

        const S32 width = widths[i];
        button->setRect(LLRect(x, 16, x + width, 1));

        // Firestorm uses ellipses for landmarks longer than the 140px cap.
        if (font->getWidth(mItems[i].name) + BUTTON_TEXT_PADDING > width)
        {
            LLWString short_label = utf8str_to_wstring(mItems[i].name);
            while (!short_label.empty() &&
                   font->getWidth(wstring_to_utf8str(short_label) + "...") >
                       width - BUTTON_TEXT_PADDING)
            {
                short_label.pop_back();
            }
            button->setLabel(wstring_to_utf8str(short_label) + "...");
        }
        else
        {
            button->setLabel(mItems[i].name);
        }
        x += width + BUTTON_GAP;
    }

    mOverflow->clearRows();
    const bool more = visible_count < mItems.size();
    mOverflow->setVisible(more);
    if (more)
    {
        const S32 more_x = llmax(LEFT_INSET, llmin(x,
                                    right_edge - OVERFLOW_WIDTH));
        mOverflow->setRect(LLRect(more_x, 16,
                                  more_x + OVERFLOW_WIDTH, 1));
        for (size_t i = visible_count; i < mItems.size(); ++i)
        {
            mOverflow->addSimpleElement(mItems[i].name, ADD_BOTTOM,
                                        LLSD(mItems[i].id.asString()));
        }
    }
    mLayoutWidth = getRect().getWidth();
}

void LLChaosFavoritesBar::reshape(S32 width, S32 height, bool from_parent)
{
    LLPanel::reshape(width, height, from_parent);
    if (mLayoutWidth != width) layoutButtons();
}

void LLChaosFavoritesBar::draw()
{
    LLPanel::draw();
}

void LLChaosFavoritesBar::teleportTo(const LLUUID& item_id)
{
    LLViewerInventoryItem* item = gInventory.getItem(item_id);
    if (!item) return;
    if (item->getActualType() == LLAssetType::AT_LINK)
    {
        item = item->getLinkedItem();
    }
    if (!item || item->getType() != LLAssetType::AT_LANDMARK) return;

    gAgent.teleportViaLandmark(item->getAssetUUID());
    if (gFloaterWorldMapp) gFloaterWorldMapp->trackLandmark(item_id);
}

void LLChaosFavoritesBar::onFavoriteClick(void* user_data)
{
    ButtonData* data = static_cast<ButtonData*>(user_data);
    if (data && data->bar) data->bar->teleportTo(data->item_id);
}

void LLChaosFavoritesBar::onOverflowCommit(LLUICtrl* ctrl, void* data)
{
    auto* bar = static_cast<LLChaosFavoritesBar*>(data);
    if (bar && ctrl) bar->teleportTo(LLUUID(ctrl->getValue().asString()));
}

LLUUID LLChaosFavoritesBar::destinationAt(S32 x) const
{
    for (size_t i = 0; i < mButtons.size(); ++i)
    {
        if (mButtons[i]->getVisible() && mButtons[i]->getRect().pointInRect(x, 12))
            return mItems[i].id;
    }
    return LLUUID::null;
}

void LLChaosFavoritesBar::moveItemBefore(const LLUUID& item_id,
                                         const LLUUID& before_id)
{
    // Preserve drag/reorder across sessions of this account.
    std::vector<std::string> order;
    for (const Favorite& fav : mItems)
    {
        if (fav.id != item_id) order.push_back(fav.id.asString());
    }
    const std::string source = item_id.asString();
    const std::string target = before_id.asString();
    auto it = std::find(order.begin(), order.end(), target);
    order.insert(it, source);
    writeFavoriteOrder(order);
    mDirty = true;
}

bool LLChaosFavoritesBar::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
                                            EDragAndDropType cargo_type,
                                            void* cargo_data, EAcceptance* accept,
                                            std::string& tooltip)
{
    *accept = ACCEPT_NO;
    if (cargo_type != DAD_LANDMARK || !cargo_data) return true;

    const auto source = gToolDragAndDrop.getSource();
    if (source != LLToolDragAndDrop::SOURCE_AGENT &&
        source != LLToolDragAndDrop::SOURCE_LIBRARY) return true;

    LLInventoryItem* item = static_cast<LLInventoryItem*>(cargo_data);
    if (!item || mFolderID.isNull()) return true;
    const LLUUID before = destinationAt(x);

    if (item->getParentUUID() == mFolderID)
    {
        *accept = ACCEPT_YES_SINGLE;
        if (drop && item->getUUID() != before)
            moveItemBefore(item->getUUID(), before);
    }
    else
    {
        *accept = ACCEPT_YES_COPY_MULTI;
        if (drop)
        {
            // Firestorm semantics: copy rather than moving the original
            // landmark out of Inventory. Callback retains drop position.
            LLPointer<LLInventoryCallback> callback =
                new LLChaosFavoriteCopyCallback(before);
            copy_inventory_item(item->getPermissions().getOwner(),
                                item->getUUID(), mFolderID, std::string(), callback);
        }
    }
    return true; // Prevent dropping landmarks through the bar into the world.
}

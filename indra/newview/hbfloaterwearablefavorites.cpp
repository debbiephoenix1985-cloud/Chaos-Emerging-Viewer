/*
 * Chaos Emerging Viewer - Favourite Wearables
 * Inspired by Firestorm's 2018 Favorite Wearables by Ansariel Hiller.
 * Only inventory links are stored; user originals remain intact.
 */
#include "llviewerprecompiledheaders.h"
#include "hbfloaterwearablefavorites.h"

#include "llagent.h"
#include "llagentwearables.h"
#include "llappearancemgr.h"
#include "llfloaterinventory.h"
#include "llfloaterproperties.h"
#include "llfolderview.h"
#include "lltrans.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llmenugl.h"
#include "llviewermenu.h"
#include "llnotifications.h"
#include <algorithm>
#include <set>
#include "llinventorymodel.h"
#include "llscrolllistctrl.h"
#include "lllineeditor.h"
#include "lluictrlfactory.h"
#include "llviewerinventory.h"
#include "llvoavatarself.h"
#include "llviewerwearable.h"
#include "mkrlinterface.h"

namespace
{
    const char* const FS_ROOT = "#Firestorm";
    const char* const FAVORITES = "#Wearable Favorites";
    bool sCreatingFolder = false;
    LLUUID sCreatedFolderID;
}

HBFloaterWearableFavorites::HBFloaterWearableFavorites(const LLSD&)
{
    LLUICtrlFactory::getInstance()->buildFloater(
        this, "floater_wearable_favorites.xml");
}

HBFloaterWearableFavorites::~HBFloaterWearableFavorites()
{
    gInventory.removeObserver(this);
}

bool HBFloaterWearableFavorites::postBuild()
{
    mList = getChild<LLScrollListCtrl>("favorites");
    mList->setDoubleClickCallback(onDoubleClick);
    mList->setCallbackUserData(this);
    // Original Firestorm 7.2.5 Assignment-to-Folder action is a real
    // right-click context menu; it does not alter the existing favourites list.
    mList->setRightMouseDownCallback([this](LLUICtrl*, S32 x, S32 y, MASK)
    {
        // Right-click selects the row under the pointer, not the previous
        // left-click selection (LLScrollListCtrl does not do this for us).
        if (mList->selectItemAt(x, y, MASK_NONE))
            showFolderAssignMenu(x, y);
    });
    mFolderCombo = getChild<LLComboBox>("folder_combo");
    mFolderCombo->setCommitCallback(onFolderChanged);
    mFolderCombo->setCallbackUserData(this);
    childSetAction("new_folder_btn", onNewFolder, this);
    childSetAction("rename_folder_btn", onRenameFolder, this);
    childSetAction("delete_folder_btn", onDeleteFolder, this);
    mRenameFolderBtn = getChild<LLButton>("rename_folder_btn");
    mDeleteFolderBtn = getChild<LLButton>("delete_folder_btn");
    mSearch = getChild<LLSearchEditor>("filter");
    mSearch->setSearchCallback(onSearch, this);
    mRemove = getChild<LLButton>("remove");
    mRemove->setClickedCallback(onRemove, this);
    childSetAction("refresh", onRefresh, this);
    gInventory.addObserver(this);
    return true;
}

void HBFloaterWearableFavorites::changed(U32)
{
    mDirty = true;
}

bool HBFloaterWearableFavorites::isFavType(
    const LLViewerInventoryItem* item)
{
    if (!item) return false;
    LLAssetType::EType type = item->getType();
    return type == LLAssetType::AT_CLOTHING ||
           type == LLAssetType::AT_BODYPART ||
           type == LLAssetType::AT_OBJECT;
}

void HBFloaterWearableFavorites::ensureFolder()
{
    if (!gInventory.isInventoryUsable()) return;
    if (mFolderID.notNull() && gInventory.getCategory(mFolderID)) return;
    mFolderID.setNull();

    LLUUID fs_root;
    LLInventoryModel::cat_array_t* root_cats = NULL;
    LLInventoryModel::item_array_t* root_items = NULL;
    gInventory.getDirectDescendentsOf(
        gInventory.getRootFolderID(), root_cats, root_items);
    if (!root_cats) return;
    for (const auto& folder : *root_cats)
    {
        if (folder && folder->getName() == FS_ROOT)
        {
            fs_root = folder->getUUID();
            break;
        }
    }
    if (fs_root.notNull())
    {
        // Wait until the server has returned this folder's children before
        // deciding that the Firestorm favourites subfolder is missing.
        LLViewerInventoryCategory* root_folder = gInventory.getCategory(fs_root);
        if (root_folder && !gInventory.isCategoryComplete(fs_root))
        {
            root_folder->fetch();
            return;
        }
        LLInventoryModel::cat_array_t* cats = NULL;
        LLInventoryModel::item_array_t* items = NULL;
        gInventory.getDirectDescendentsOf(fs_root, cats, items);
        if (cats)
        {
            for (const auto& folder : *cats)
            {
                if (folder && folder->getName() == FAVORITES)
                {
                    mFolderID = folder->getUUID();
                    folder->fetch();
                    mDirty = true;
                    return;
                }
            }
        }
    }

    if (sCreatedFolderID.notNull() &&
        gInventory.getCategory(sCreatedFolderID))
    {
        mFolderID = sCreatedFolderID;
        gInventory.getCategory(mFolderID)->fetch();
        mDirty = true;
        return;
    }
    if (sCreatingFolder) return;

    sCreatingFolder = true;
    if (fs_root.isNull())
    {
        gInventory.createNewCategory(gInventory.getRootFolderID(),
            LLFolderType::FT_NONE, FS_ROOT,
            [](const LLUUID& root_id)
            {
                if (root_id.isNull())
                {
                    sCreatingFolder = false;
                    return;
                }
                gInventory.createNewCategory(root_id, LLFolderType::FT_NONE,
                    FAVORITES, [](const LLUUID& id)
                    {
                        sCreatedFolderID = id;
                        sCreatingFolder = false;
                    });
            });
    }
    else
    {
        gInventory.createNewCategory(fs_root, LLFolderType::FT_NONE,
            FAVORITES, [](const LLUUID& id)
            {
                sCreatedFolderID = id;
                sCreatingFolder = false;
            });
    }
}

void HBFloaterWearableFavorites::updateList()
{
    if (!mList) return;
    mList->deleteAllItems();
    if (mFolderID.isNull()) return;

    // Firestorm 7.2.5: All Favourites is the union of the parent folder
    // and its named folder views, with duplicate wearable links removed.
    // A named view shows only links assigned to that folder.
    std::set<LLUUID> shown_originals;
    auto append_folder = [this, &shown_originals](const LLUUID& folder_id)
    {
        LLInventoryModel::cat_array_t* cats = NULL;
        LLInventoryModel::item_array_t* items = NULL;
        gInventory.getDirectDescendentsOf(folder_id, cats, items);
        if (!items) return;
        std::string needle = mFilter;
        LLStringUtil::toLower(needle);

        for (const auto& link : *items)
        {
            if (!link || !link->getIsLinkType() || !isFavType(link))
                continue;
            const LLUUID original_id = link->getLinkedUUID();
            LLViewerInventoryItem* original = gInventory.getItem(original_id);
            if (!original || !isFavType(original) ||
                !shown_originals.insert(original_id).second)
                continue;

            const std::string name = original->getName();
            std::string lower = name;
            LLStringUtil::toLower(lower);
            if (!needle.empty() && lower.find(needle) == std::string::npos)
                continue;

            LLSD row;
            row["id"] = link->getUUID();
            row["columns"][0]["column"] = "name";
            row["columns"][0]["value"] = name;
            row["columns"][1]["column"] = "type";
            row["columns"][1]["value"] =
                original->getType() == LLAssetType::AT_OBJECT ? "Attachment / HUD"
              : original->getType() == LLAssetType::AT_BODYPART ? "Body part"
                                                                : "Clothing";
            row["columns"][2]["column"] = "worn";
            row["columns"][2]["value"] =
                get_is_item_worn(original_id, false) ? "Worn" : "";
            mList->addElement(row, ADD_BOTTOM);
        }
    };

    const LLUUID active_id = mSelectedFolderID.notNull() ?
                             mSelectedFolderID : mFolderID;
    if (active_id == mFolderID)
    {
        append_folder(mFolderID);
        LLInventoryModel::cat_array_t* cats = NULL;
        LLInventoryModel::item_array_t* items = NULL;
        gInventory.getDirectDescendentsOf(mFolderID, cats, items);
        if (cats)
        {
            for (const auto& folder : *cats)
            {
                if (folder) append_folder(folder->getUUID());
            }
        }
    }
    else
    {
        append_folder(active_id);
    }
    mList->sortByColumn("name", true);
}

void HBFloaterWearableFavorites::draw()
{
    ensureFolder();
    if (mDirty && mFolderID.notNull())
    {
        refreshFolderCombo();
        updateList();
        mDirty = false;
    }
    const bool has_selection = mList && mList->getFirstSelected();
    if (mRemove) mRemove->setEnabled(has_selection);
    const bool can_change_folder = mFolderID.notNull() &&
        mSelectedFolderID.notNull() && mSelectedFolderID != mFolderID;
    if (mRenameFolderBtn) mRenameFolderBtn->setEnabled(can_change_folder);
    if (mDeleteFolderBtn) mDeleteFolderBtn->setEnabled(can_change_folder);
    LLFloater::draw();
}

bool HBFloaterWearableFavorites::handleDragAndDrop(
    S32, S32, MASK, bool drop, EDragAndDropType cargo_type,
    void* cargo_data, EAcceptance* accept, std::string&)
{
    *accept = ACCEPT_NO;
    if (mFolderID.isNull() || !cargo_data) return true;
    if (cargo_type != DAD_OBJECT && cargo_type != DAD_CLOTHING &&
        cargo_type != DAD_BODYPART) return true;

    LLInventoryItem* dragged = (LLInventoryItem*)cargo_data;
    LLViewerInventoryItem* original =
        gInventory.getItem(dragged->getLinkedUUID());
    if (!original || !isFavType(original)) return true;

    const LLUUID target = mSelectedFolderID.notNull() ?
                          mSelectedFolderID : mFolderID;
    auto contains_link = [original](const LLUUID& folder_id)
    {
        LLInventoryModel::cat_array_t* cats = NULL;
        LLInventoryModel::item_array_t* items = NULL;
        gInventory.getDirectDescendentsOf(folder_id, cats, items);
        if (!items) return false;
        for (const auto& link : *items)
        {
            if (link && link->getLinkedUUID() == original->getUUID())
                return true;
        }
        return false;
    };
    if (contains_link(target)) return true;
    *accept = ACCEPT_YES_SINGLE;
    if (drop)
    {
        // Preserve an All Favourites link even if the custom folder is
        // removed later. Every custom folder stores inventory links only.
        if (target != mFolderID && !contains_link(mFolderID))
            link_inventory_object(mFolderID, original->getUUID());
        link_inventory_object(target, original->getUUID());
        mDirty = true;
    }
    return true;
}

void HBFloaterWearableFavorites::toggleSelected()
{
    LLScrollListItem* selected = mList ? mList->getFirstSelected() : NULL;
    if (!selected) return;
    LLViewerInventoryItem* link = gInventory.getItem(selected->getUUID());
    if (!link || !link->getIsLinkType()) return;
    LLViewerInventoryItem* original = gInventory.getItem(link->getLinkedUUID());
    if (!original || !isFavType(original) ||
        !gAgentWearables.areWearablesLoaded()) return;
    const LLUUID id = original->getUUID();

    if (get_is_item_worn(id, false))
    {
        if (original->getType() == LLAssetType::AT_OBJECT)
        {
            if (!gRLenabled || gRLInterface.canDetach(original))
                LLVOAvatarSelf::detachAttachmentIntoInventory(id);
        }
        else if (original->getType() == LLAssetType::AT_CLOTHING)
        {
            if (gRLenabled && !gRLInterface.canUnwear(original)) return;
            LLViewerWearable* wearable =
                gAgentWearables.getWearableFromItemID(id);
            U32 index = 0;
            if (wearable && gAgentWearables.getWearableIndex(wearable, index))
            {
                gAgentWearables.userRemoveWearable(wearable->getType(), index);
                gInventory.notifyObservers();
            }
        }
    }
    else if (original->getType() == LLAssetType::AT_OBJECT)
    {
        if (!gRLenabled || gRLInterface.canAttach(original))
            gAppearanceMgr.rezAttachment(original, NULL, false);
    }
    else if (!gRLenabled || gRLInterface.canWear(original))
    {
        gAppearanceMgr.wearItemOnAvatar(
            id, original->getType() == LLAssetType::AT_BODYPART);
    }
    mDirty = true;
}

void HBFloaterWearableFavorites::removeSelected()
{
    LLScrollListItem* selected = mList ? mList->getFirstSelected() : NULL;
    if (!selected) return;
    LLViewerInventoryItem* link = gInventory.getItem(selected->getUUID());
    if (!link || !link->getIsLinkType() || mFolderID.isNull()) return;
    const LLUUID selected_original = link->getLinkedUUID();
    const LLUUID active = mSelectedFolderID.notNull() ? mSelectedFolderID : mFolderID;
    if (active != mFolderID)
    {
        // Named folder: remove that folder's favourite link only.
        if (link->getParentUUID() == active)
            remove_inventory_item(link->getUUID());
    }
    else
    {
        // All Favourites: remove every favourite link for this wearable,
        // including links in named folders, but never its original asset.
        std::vector<LLUUID> folders{mFolderID};
        LLInventoryModel::cat_array_t* categories = NULL;
        LLInventoryModel::item_array_t* items = NULL;
        gInventory.getDirectDescendentsOf(mFolderID, categories, items);
        if (categories)
        {
            for (const auto& category : *categories)
                if (category) folders.push_back(category->getUUID());
        }
        for (const LLUUID& folder_id : folders)
        {
            LLInventoryModel::cat_array_t* subfolders = NULL;
            LLInventoryModel::item_array_t* links = NULL;
            gInventory.getDirectDescendentsOf(folder_id, subfolders, links);
            if (!links) continue;
            for (const auto& candidate : *links)
            {
                if (candidate && candidate->getIsLinkType() &&
                    candidate->getLinkedUUID() == selected_original)
                    remove_inventory_item(candidate->getUUID());
            }
        }
    }
    mDirty = true;
}

void HBFloaterWearableFavorites::onDoubleClick(void* userdata)
{
    HBFloaterWearableFavorites* self = (HBFloaterWearableFavorites*)userdata;
    if (self) self->toggleSelected();
}
void HBFloaterWearableFavorites::onRemove(void* userdata)
{
    HBFloaterWearableFavorites* self = (HBFloaterWearableFavorites*)userdata;
    if (self) self->removeSelected();
}
void HBFloaterWearableFavorites::onRefresh(void* userdata)
{
    HBFloaterWearableFavorites* self = (HBFloaterWearableFavorites*)userdata;
    if (self) self->mDirty = true;
}
void HBFloaterWearableFavorites::onSearch(
    const std::string& text, void* userdata)
{
    HBFloaterWearableFavorites* self = (HBFloaterWearableFavorites*)userdata;
    if (self)
    {
        self->mFilter = text;
        self->mDirty = true;
    }
}
// Firestorm 7.2.5 feature: Favourite Wearables named folder views.
// Kept as additions to the original Chaos HBFloaterWearableFavorites class.
void HBFloaterWearableFavorites::refreshFolderCombo()
{
    if (!mFolderCombo || mFolderID.isNull()) return;
    const LLUUID desired = mSelectedFolderID.notNull() ?
                           mSelectedFolderID : mFolderID;
    mFolderCombo->clearRows();
    mFolderCombo->add("All Favourites", LLSD(mFolderID.asString()));

    LLViewerInventoryCategory* root_category = gInventory.getCategory(mFolderID);
    if (root_category && !gInventory.isCategoryComplete(mFolderID))
    {
        root_category->fetch();
        return; // Retain the selected folder while child categories load.
    }
    LLInventoryModel::cat_array_t* cats = NULL;
    LLInventoryModel::item_array_t* items = NULL;
    gInventory.getDirectDescendentsOf(mFolderID, cats, items);
    std::vector<LLViewerInventoryCategory*> sorted;
    if (cats)
    {
        for (const auto& cat : *cats)
        {
            if (cat) sorted.push_back(cat);
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const LLViewerInventoryCategory* a,
               const LLViewerInventoryCategory* b)
            {
                return LLStringUtil::compareInsensitive(a->getName(),
                                                        b->getName()) < 0;
            });
    }
    bool found = (desired == mFolderID);
    for (const auto* cat : sorted)
    {
        mFolderCombo->add(cat->getName(), LLSD(cat->getUUID().asString()));
        if (cat->getUUID() == desired) found = true;
    }
    mSelectedFolderID = found ? desired : mFolderID;
    mFolderCombo->selectByValue(LLSD(mSelectedFolderID.asString()));
}

void HBFloaterWearableFavorites::onFolderChanged(LLUICtrl*, void* userdata)
{
    HBFloaterWearableFavorites* self =
        static_cast<HBFloaterWearableFavorites*>(userdata);
    if (!self || !self->mFolderCombo || self->mFolderID.isNull()) return;
    const LLUUID next(self->mFolderCombo->getSelectedValue().asString());
    if (next.isNull()) return;
    if (self->mSelectedFolderID != next)
    {
        self->mSelectedFolderID = next;
        self->updateList();
    }
}

void HBFloaterWearableFavorites::showFolderAssignMenu(S32 x, S32 y)
{
    if (!mList || mFolderID.isNull()) return;
    LLScrollListItem* selected = mList->getFirstSelected();
    if (!selected) return;
    LLViewerInventoryItem* link = gInventory.getItem(selected->getUUID());
    if (!link || !link->getIsLinkType()) return;
    LLViewerInventoryItem* original = gInventory.getItem(link->getLinkedUUID());
    if (!original || !isFavType(original)) return;
    mContextOriginalID = original->getUUID();

    // Reuse native menus and preserve the existing Assign-to-Folder actions.
    if (!mAssignmentMenu)
    {
        mAssignmentMenu = new LLMenuGL("Favourite Wearables");
        mAssignmentMenu->setVisible(false);
    }
    // LLMenuItemBranchGL owns and deletes its submenu in its destructor.
    // Clearing the main menu destroys previous Attach to/HUD submenus:
    // never retain pointers to them across right-clicks.
    mAssignmentMenu->empty();
    mAssignmentTargets.clear();
    mAttachmentTargets.clear();

    const bool object = original->getType() == LLAssetType::AT_OBJECT;
    const bool bodypart = original->getType() == LLAssetType::AT_BODYPART;
    const bool permitted = !gRLenabled ||
        (object ? gRLInterface.canAttach(original)
                : gRLInterface.canWear(original));
    const bool ready = gAgentWearables.areWearablesLoaded();
    const bool can_wear = ready && permitted;
    const bool can_add = can_wear && !bodypart &&
        (!object || (isAgentAvatarValid() &&
                    gAgentAvatarp->canAttachMoreObjects()));

    // The favourites list already uses get_is_item_worn() for its Worn
    // column and double-click toggle. Use exactly the same live inventory/
    // avatar state whenever the right-click menu opens.
    const bool is_worn = get_is_item_worn(original->getUUID(), false);
    const bool can_remove_worn = ready && is_worn && !bodypart &&
        (object ? (!gRLenabled || gRLInterface.canDetach(original))
                : (!gRLenabled || gRLInterface.canUnwear(original)));
    const bool show_remove = is_worn && !bodypart;

    auto* wear = new LLMenuItemCallGL("Wear", onContextWear, NULL, this);
    wear->setEnabled(can_wear);
    mAssignmentMenu->append(wear);
    auto* add_or_remove = new LLMenuItemCallGL(
        show_remove ? (object ? "Detach" : "Take Off") : "Add",
        show_remove ? onContextRemoveWorn : onContextAdd, NULL, this);
    add_or_remove->setEnabled(show_remove ? can_remove_worn : can_add);
    mAssignmentMenu->append(add_or_remove);

    if (object && isAgentAvatarValid())
    {
        // These submenus belong to this menu invocation only. appendMenu()
        // transfers their lifetime to LLMenuItemBranchGL, which deletes
        // them when mAssignmentMenu->empty() rebuilds the next right-click.
        LLMenuGL* attach_to_menu = new LLMenuGL("Attach to", "Attach to");
        LLMenuGL* attach_hud_menu = new LLMenuGL("Attach to HUD", "Attach to HUD");
        attach_to_menu->setVisible(false);
        attach_hud_menu->setVisible(false);

        // Use the same avatar attachment-point list used by the existing
        // Inventory > Attach to / Attach to HUD context menus.
        for (const auto& entry : gAgentAvatarp->mAttachmentPoints)
        {
            LLViewerJointAttachment* point = entry.second;
            if (!point) continue;
            std::unique_ptr<AttachmentTarget> target(new AttachmentTarget);
            target->owner = this;
            target->original_id = mContextOriginalID;
            target->attachment_point = entry.first;
            LLMenuGL* submenu = point->getIsHUDAttachment() ?
                                  attach_hud_menu : attach_to_menu;
            const std::string label = LLTrans::getString(point->getName());
            auto* menu_item = new LLMenuItemCallGL(label,
                onContextAttachPoint, NULL, target.get());
            menu_item->setEnabled(can_wear);
            submenu->append(menu_item);
            mAttachmentTargets.push_back(std::move(target));
        }

        // When populated, the parent menu owns each submenu via its branch.
        // An empty submenu is never appended and must be deleted here.
        if (attach_to_menu->getChildCount())
        {
            attach_to_menu->buildDrawLabels();
            mAssignmentMenu->appendMenu(attach_to_menu);
        }
        else
        {
            delete attach_to_menu;
        }
        if (attach_hud_menu->getChildCount())
        {
            attach_hud_menu->buildDrawLabels();
            mAssignmentMenu->appendMenu(attach_hud_menu);
        }
        else
        {
            delete attach_hud_menu;
        }
    }

    mAssignmentMenu->append(new LLMenuItemCallGL("Item Profile",
        onContextProfile, NULL, this));
    mAssignmentMenu->append(new LLMenuItemCallGL("Show Original",
        onContextShowOriginal, NULL, this));

    // Existing Firestorm 7.2.5 favourite-folder actions remain available.
    LLInventoryModel::cat_array_t* cats = NULL;
    LLInventoryModel::item_array_t* items = NULL;
    gInventory.getDirectDescendentsOf(mFolderID, cats, items);
    if (cats)
    {
        for (const auto& cat : *cats)
        {
            if (!cat) continue;
            std::unique_ptr<AssignmentTarget> target(new AssignmentTarget);
            target->owner = this;
            target->folder_id = cat->getUUID();
            mAssignmentMenu->append(
                new LLMenuItemCallGL("Assign to Folder: " + cat->getName(),
                                     onAssignFolder, NULL, target.get()));
            mAssignmentTargets.push_back(std::move(target));
        }
    }
    mAssignmentMenu->buildDrawLabels();
    mAssignmentMenu->updateParent(gMenuHolderp);
    LLMenuGL::showPopup(mList, mAssignmentMenu, x, y);
}

LLViewerInventoryItem* HBFloaterWearableFavorites::getContextOriginal() const
{
    LLViewerInventoryItem* item = gInventory.getItem(mContextOriginalID);
    return isFavType(item) ? item : NULL;
}

void HBFloaterWearableFavorites::onContextWear(void* userdata)
{
    auto* self = static_cast<HBFloaterWearableFavorites*>(userdata);
    LLViewerInventoryItem* item = self ? self->getContextOriginal() : NULL;
    if (!item || !gAgentWearables.areWearablesLoaded()) return;
    if (item->getType() == LLAssetType::AT_OBJECT)
    {
        if (!gRLenabled || gRLInterface.canAttach(item))
            gAppearanceMgr.rezAttachment(item, NULL, true);
    }
    else if (!gRLenabled || gRLInterface.canWear(item))
    {
        gAppearanceMgr.wearItemOnAvatar(item->getUUID(), true);
    }
    self->mDirty = true;
}

void HBFloaterWearableFavorites::onContextAdd(void* userdata)
{
    auto* self = static_cast<HBFloaterWearableFavorites*>(userdata);
    LLViewerInventoryItem* item = self ? self->getContextOriginal() : NULL;
    if (!item || !gAgentWearables.areWearablesLoaded() ||
        item->getType() == LLAssetType::AT_BODYPART) return;
    if (item->getType() == LLAssetType::AT_OBJECT)
    {
        if ((!gRLenabled || gRLInterface.canAttach(item)) &&
            isAgentAvatarValid() && gAgentAvatarp->canAttachMoreObjects())
            gAppearanceMgr.rezAttachment(item, NULL, false);
    }
    else if (!gRLenabled || gRLInterface.canWear(item))
    {
        gAppearanceMgr.wearItemOnAvatar(item->getUUID(), false);
    }
    self->mDirty = true;
}

// Existing double-click behaviour uses the same detach/take-off actions;
// this menu callback merely exposes them when the inventory reports "Worn".
void HBFloaterWearableFavorites::onContextRemoveWorn(void* userdata)
{
    auto* self = static_cast<HBFloaterWearableFavorites*>(userdata);
    LLViewerInventoryItem* item = self ? self->getContextOriginal() : NULL;
    if (!item || !gAgentWearables.areWearablesLoaded() ||
        !get_is_item_worn(item->getUUID(), false)) return;

    const LLUUID id = item->getUUID();
    if (item->getType() == LLAssetType::AT_OBJECT)
    {
        if (!gRLenabled || gRLInterface.canDetach(item))
            LLVOAvatarSelf::detachAttachmentIntoInventory(id);
    }
    else if (item->getType() == LLAssetType::AT_CLOTHING)
    {
        if (gRLenabled && !gRLInterface.canUnwear(item)) return;
        LLViewerWearable* wearable =
            gAgentWearables.getWearableFromItemID(id);
        U32 index = 0;
        if (wearable && gAgentWearables.getWearableIndex(wearable, index))
        {
            gAgentWearables.userRemoveWearable(wearable->getType(), index);
            gInventory.notifyObservers();
        }
    }
    self->mDirty = true;
}

void HBFloaterWearableFavorites::onContextAttachPoint(void* userdata)
{
    auto* target = static_cast<AttachmentTarget*>(userdata);
    if (!target || !target->owner || !isAgentAvatarValid()) return;
    LLViewerInventoryItem* item = gInventory.getItem(target->original_id);
    if (!item || item->getType() != LLAssetType::AT_OBJECT ||
        !gAgentWearables.areWearablesLoaded() ||
        (gRLenabled && !gRLInterface.canAttach(item)) ||
        !gAgentAvatarp->canAttachMoreObjects()) return;
    auto iter = gAgentAvatarp->mAttachmentPoints.find(target->attachment_point);
    if (iter == gAgentAvatarp->mAttachmentPoints.end() || !iter->second)
        return;
    gAppearanceMgr.rezAttachment(item, iter->second, false);
    target->owner->mDirty = true;
}

void HBFloaterWearableFavorites::onContextProfile(void* userdata)
{
    auto* self = static_cast<HBFloaterWearableFavorites*>(userdata);
    LLViewerInventoryItem* item = self ? self->getContextOriginal() : NULL;
    if (item)
        LLFloaterProperties::show(item->getUUID(), LLUUID::null, self);
}

void HBFloaterWearableFavorites::onContextShowOriginal(void* userdata)
{
    auto* self = static_cast<HBFloaterWearableFavorites*>(userdata);
    LLViewerInventoryItem* item = self ? self->getContextOriginal() : NULL;
    if (!item) return;
    if (LLFloaterInventory* inventory = LLFloaterInventory::showAgentInventory())
    {
        if (LLInventoryPanel* panel = inventory->getPanel())
            panel->setSelection(item->getUUID(), true);
    }
}

void HBFloaterWearableFavorites::onAssignFolder(void* userdata)
{
    AssignmentTarget* target = static_cast<AssignmentTarget*>(userdata);
    if (target && target->owner)
        target->owner->assignSelectedToFolder(target->folder_id);
}

void HBFloaterWearableFavorites::assignSelectedToFolder(const LLUUID& folder_id)
{
    if (mFolderID.isNull() || folder_id.isNull() ||
        folder_id == mFolderID || !mList)
        return;

    LLViewerInventoryCategory* folder = gInventory.getCategory(folder_id);
    if (!folder || folder->getParentUUID() != mFolderID) return;
    LLScrollListItem* selected = mList->getFirstSelected();
    if (!selected) return;
    LLViewerInventoryItem* selected_link = gInventory.getItem(selected->getUUID());
    if (!selected_link || !selected_link->getIsLinkType()) return;

    LLViewerInventoryItem* original =
        gInventory.getItem(selected_link->getLinkedUUID());
    if (!original || !isFavType(original)) return;

    auto contains = [original](const LLUUID& search_folder)
    {
        LLInventoryModel::cat_array_t* cats = NULL;
        LLInventoryModel::item_array_t* items = NULL;
        gInventory.getDirectDescendentsOf(search_folder, cats, items);
        if (items)
        {
            for (const auto& link : *items)
            {
                if (link && link->getLinkedUUID() == original->getUUID())
                    return true;
            }
        }
        return false;
    };
    // Keep the original item's All Favourites link even if a subfolder is
    // removed later. Never move/delete the original inventory wearable.
    if (!contains(mFolderID))
        link_inventory_object(mFolderID, original->getUUID());
    if (!contains(folder_id))
        link_inventory_object(folder_id, original->getUUID());
    mDirty = true; // Leave the current selected view unchanged.
}

void HBFloaterWearableFavorites::onNewFolder(void* userdata)
{
    HBFloaterWearableFavorites* self =
        static_cast<HBFloaterWearableFavorites*>(userdata);
    if (!self || self->mFolderID.isNull()) return;
    const LLHandle<LLFloater> handle = self->getHandle();
    gNotifications.add("ChaosWearableFavoritesNewFolder", LLSD(), LLSD(),
        [handle](const LLSD& n, const LLSD& r) -> bool
        {
            auto* live = dynamic_cast<HBFloaterWearableFavorites*>(handle.get());
            return live ? live->confirmNewFolder(n, r) : false;
        });
}

bool HBFloaterWearableFavorites::confirmNewFolder(
    const LLSD& notification, const LLSD& response)
{
    if (LLNotification::getSelectedOption(notification, response) != 0 ||
        mFolderID.isNull()) return false;

    std::string name = response["message"].asString();
    LLStringUtil::trim(name);
    if (name.empty() || name.find_first_of("/\\") != std::string::npos)
        return false;
    LLInventoryModel::cat_array_t* cats = NULL;
    LLInventoryModel::item_array_t* items = NULL;
    gInventory.getDirectDescendentsOf(mFolderID, cats, items);
    if (cats)
    {
        for (const auto& cat : *cats)
        {
            if (cat && LLStringUtil::compareInsensitive(cat->getName(), name) == 0)
                return false;
        }
    }
    const LLHandle<LLFloater> handle = getHandle();
    gInventory.createNewCategory(mFolderID, LLFolderType::FT_NONE, name,
        [handle](const LLUUID& new_folder)
        {
            auto* live = dynamic_cast<HBFloaterWearableFavorites*>(handle.get());
            if (live && new_folder.notNull())
            {
                live->mSelectedFolderID = new_folder;
                live->mDirty = true;
            }
        });
    return false;
}

void HBFloaterWearableFavorites::onRenameFolder(void* userdata)
{
    HBFloaterWearableFavorites* self =
        static_cast<HBFloaterWearableFavorites*>(userdata);
    if (!self || self->mSelectedFolderID.isNull() ||
        self->mSelectedFolderID == self->mFolderID) return;
    LLSD args;
    if (auto* cat = gInventory.getCategory(self->mSelectedFolderID))
        args["FOLDER_NAME"] = cat->getName();
    const LLHandle<LLFloater> handle = self->getHandle();
    gNotifications.add("ChaosWearableFavoritesRenameFolder", args, LLSD(),
        [handle](const LLSD& n, const LLSD& r) -> bool
        {
            auto* live = dynamic_cast<HBFloaterWearableFavorites*>(handle.get());
            return live ? live->confirmRenameFolder(n, r) : false;
        });
}

bool HBFloaterWearableFavorites::confirmRenameFolder(
    const LLSD& notification, const LLSD& response)
{
    if (LLNotification::getSelectedOption(notification, response) != 0 ||
        mSelectedFolderID.isNull() || mSelectedFolderID == mFolderID)
        return false;
    LLViewerInventoryCategory* cat = gInventory.getCategory(mSelectedFolderID);
    if (!cat || cat->getParentUUID() != mFolderID) return false;
    std::string name = response["message"].asString();
    LLStringUtil::trim(name);
    if (name.empty() || name.find_first_of("/\\") != std::string::npos)
        return false;
    LLSD updates;
    updates["name"] = name;
    update_inventory_category(mSelectedFolderID, updates);
    mDirty = true;
    return false;
}

void HBFloaterWearableFavorites::onDeleteFolder(void* userdata)
{
    HBFloaterWearableFavorites* self =
        static_cast<HBFloaterWearableFavorites*>(userdata);
    if (!self || self->mSelectedFolderID.isNull() ||
        self->mSelectedFolderID == self->mFolderID) return;
    const LLHandle<LLFloater> handle = self->getHandle();
    gNotifications.add("ChaosWearableFavoritesDeleteFolder", LLSD(), LLSD(),
        [handle](const LLSD& n, const LLSD& r) -> bool
        {
            auto* live = dynamic_cast<HBFloaterWearableFavorites*>(handle.get());
            return live ? live->confirmDeleteFolder(n, r) : false;
        });
}

bool HBFloaterWearableFavorites::confirmDeleteFolder(
    const LLSD& notification, const LLSD& response)
{
    if (LLNotification::getSelectedOption(notification, response) != 0 ||
        mSelectedFolderID.isNull() || mSelectedFolderID == mFolderID)
        return false;
    LLViewerInventoryCategory* cat = gInventory.getCategory(mSelectedFolderID);
    if (!cat || cat->getParentUUID() != mFolderID) return false;
    const LLUUID deleted_id = mSelectedFolderID;
    mSelectedFolderID = mFolderID;
    // The folder contains only inventory links, so originals are preserved.
    remove_inventory_category(deleted_id);
    mDirty = true;
    return false;
}


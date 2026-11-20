/*
 * Chaos Emerging Viewer - Favourite Wearables
 * Behaviour based on Firestorm's Favorite Wearables (Ansariel Hiller, 2018).
 * This implementation is adapted to the Cool VL inventory/floater APIs.
 * Uses inventory links in #Firestorm/#Wearable Favorites, not duplicated assets.
 */
#pragma once

#include "llfloater.h"
#include "llinventorymodel.h"
#include <memory>
#include <vector>

class LLComboBox;
class LLMenuGL;
class LLScrollListCtrl;
class LLSearchEditor;
class LLButton;
class LLViewerInventoryItem;

class HBFloaterWearableFavorites final : public LLFloater,
    public LLFloaterSingleton<HBFloaterWearableFavorites>,
    public LLInventoryObserver
{
    friend class LLUISingleton<HBFloaterWearableFavorites,
                               VisibilityPolicy<LLFloater> >;

private:
    HBFloaterWearableFavorites(const LLSD&);
    ~HBFloaterWearableFavorites() override;
    bool postBuild() override;
    void draw() override;
    void changed(U32 mask) override;
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
                           EDragAndDropType cargo_type, void* cargo_data,
                           EAcceptance* accept, std::string& tooltip) override;

    void ensureFolder();
    void updateList();
    // Firestorm 7.2.5: custom folder views are inventory LINK collections.
    void refreshFolderCombo();
    // Preserve Assign to Folder and add Firestorm's wearable context actions.
    void showFolderAssignMenu(S32 x, S32 y);
    LLViewerInventoryItem* getContextOriginal() const;
    static void onContextWear(void* userdata);
    static void onContextAdd(void* userdata);
    static void onContextRemoveWorn(void* userdata);
    static void onContextProfile(void* userdata);
    static void onContextShowOriginal(void* userdata);
    static void onContextAttachPoint(void* userdata);
    struct AttachmentTarget
    {
        HBFloaterWearableFavorites* owner;
        LLUUID original_id;
        S32 attachment_point;
    };
    std::vector<std::unique_ptr<AttachmentTarget>> mAttachmentTargets;
    LLUUID mContextOriginalID;
    void assignSelectedToFolder(const LLUUID& folder_id);
    bool confirmNewFolder(const LLSD& notification, const LLSD& response);
    bool confirmRenameFolder(const LLSD& notification, const LLSD& response);
    bool confirmDeleteFolder(const LLSD& notification, const LLSD& response);
    static void onFolderChanged(LLUICtrl*, void*);
    static void onNewFolder(void*);
    static void onRenameFolder(void*);
    static void onDeleteFolder(void*);
    struct AssignmentTarget
    {
        HBFloaterWearableFavorites* owner;
        LLUUID folder_id;
    };
    static void onAssignFolder(void*);
    std::vector<std::unique_ptr<AssignmentTarget>> mAssignmentTargets;
    void toggleSelected();
    void removeSelected();
    static void onDoubleClick(void* userdata);
    static void onRemove(void* userdata);
    static void onRefresh(void* userdata);
    static void onSearch(const std::string& text, void* userdata);
    static bool isFavType(const LLViewerInventoryItem* item);

    LLComboBox* mFolderCombo = NULL;
    LLButton* mRenameFolderBtn = NULL;
    LLButton* mDeleteFolderBtn = NULL;
    LLMenuGL* mAssignmentMenu = NULL;
    LLUUID mSelectedFolderID;
    LLScrollListCtrl* mList = NULL;
    LLSearchEditor* mSearch = NULL;
    LLButton* mRemove = NULL;
    bool mDirty = true;
    bool mCreating = false;
    std::string mFilter;
    LLUUID mFolderID;
};

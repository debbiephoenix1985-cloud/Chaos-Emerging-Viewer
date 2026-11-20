// Chaos Emerging: Firestorm-style Favorites bar backed by the shared inventory Favorites folder.
#pragma once

#include "llinventorymodel.h"
#include "llpanel.h"
#include "lluuid.h"

#include <memory>
#include <string>
#include <vector>

class LLButton;
class LLTextBox;
class LLComboBox;
class LLUICtrl;

class LLChaosFavoritesBar final : public LLPanel, public LLInventoryObserver
{
public:
    explicit LLChaosFavoritesBar(const LLRect& rect);
    ~LLChaosFavoritesBar() override;

    void draw() override;
    void reshape(S32 width, S32 height, bool from_parent = true) override;
    void changed(U32 mask) override;
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
                           EDragAndDropType cargo_type, void* cargo_data,
                           EAcceptance* accept, std::string& tooltip) override;
    void updateState();
    void moveItemBefore(const LLUUID& item_id, const LLUUID& before_id);

private:
    struct Favorite
    {
        LLUUID id;
        std::string name;
        time_t created = 0;
    };
    struct ButtonData
    {
        LLChaosFavoritesBar* bar = nullptr;
        LLUUID item_id;
    };
    using favorites_t = std::vector<Favorite>;
    favorites_t mItems;
    std::vector<LLButton*> mButtons;
    std::vector<std::unique_ptr<ButtonData>> mButtonData;
    LLTextBox* mTitle = nullptr;
    LLComboBox* mOverflow = nullptr;
    LLUUID mFolderID;
    bool mDirty = true;
    S32 mLayoutWidth = -1;
    void rebuildFromInventory();
    void layoutButtons();
    void teleportTo(const LLUUID& item_id);
    LLUUID destinationAt(S32 x) const;
    static void onFavoriteClick(void* data);
    static void onOverflowCommit(LLUICtrl* ctrl, void* data);
};
extern LLChaosFavoritesBar* gChaosFavoritesBarp;

// Chaos Emerging Build 171 â€” Firestorm Contact Sets integration.
// Maintains Firestorm's per-account contact-set storage schema.
#pragma once

#include "lleventtimer.h"
#include "llfloater.h"
#include "lluuid.h"
#include "llcolor4.h"

#include <functional>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>

class LLMenuGL;
class LLButton;
class LLComboBox;
class LLScrollListCtrl;
class LLCheckBoxCtrl;
class LLColorSwatchCtrl;
class LLTextBox;
class LLUICtrl;

class LLFloaterContactSets final : public LLFloater,
                                   public LLFloaterSingleton<LLFloaterContactSets>,
                                   public LLEventTimer
{
    friend class LLUISingleton<LLFloaterContactSets, VisibilityPolicy<LLFloater> >;

protected:
    LOG_CLASS(LLFloaterContactSets);

public:
    ~LLFloaterContactSets() override = default;
    bool postBuild() override;
    bool tick() override;

private:
    explicit LLFloaterContactSets(const LLSD& key);

    struct ContactSet
    {
        std::set<LLUUID> members;
        LLColor4 color = LLColor4::grey;
        bool notify = false;
        bool sort_online = true;
    };

    std::map<std::string, ContactSet> mSets;
    // Retain Firestorm fields not exposed in this window (e.g. autoresponses).
    std::map<std::string, LLSD> mOtherSetSettings;
    LLSD mGlobalSettings;
    std::map<LLUUID, std::string> mPseudonyms;
    std::set<LLUUID> mExtraAvatars;
    std::map<LLUUID, bool> mLastOnline;
    std::string mSelectedSet = "__all__";
    std::string mPickerTargetSet = "__all__";
    LLUUID mLoadedAgent;
    bool mUpdating = false;

    LLMenuGL* mGearMenu = nullptr;
    LLButton* mGearButton = nullptr;
    struct GearAction { std::function<void()> fn; };
    std::vector<std::unique_ptr<GearAction>> mGearActions;
    static void gearMenuCallback(void* p) { static_cast<GearAction*>(p)->fn(); }
    void setupGearMenu();
    static void onGear(void* data);
    LLComboBox* mSetSelector = nullptr;
    LLScrollListCtrl* mContacts = nullptr;
    std::string mFilterString;
    LLCheckBoxCtrl* mNotifications = nullptr;
    LLColorSwatchCtrl* mSetColor = nullptr;
    LLTextBox* mStatus = nullptr;

    std::string filename() const;
    void loadForCurrentAgent();
    void save() const;
    void refreshSets();
    void refreshContacts();
    void refreshSettings();
    void setStatus(const std::string& message);
    bool isNormalSet() const;
    std::vector<LLUUID> selectedAvatars() const;
    std::string resolveName(const LLUUID& id) const;
    bool isInAnySet(const LLUUID& id) const;

    static void onSearchEdit(const std::string& text, void* userdata);
    static void onSetSelected(LLUICtrl* ctrl, void* userdata);
    static void onContactSelected(LLUICtrl* ctrl, void* userdata);
    static void onNew(void* userdata);
    static void onRename(void* userdata);
    static void onDelete(void* userdata);
    static void onAdd(void* userdata);
    static void onRemove(void* userdata);
    static void onIM(void* userdata);
    static void onTeleport(void* userdata);
    static void onProfile(void* userdata);
    static void onSetAlias(void* userdata);
    static void onClearAlias(void* userdata);
    static void onRemoveDN(void* userdata);
    bool confirmNew(const LLSD& notification, const LLSD& response);
    bool confirmRename(const LLSD& notification, const LLSD& response);
    bool confirmDelete(const LLSD& notification, const LLSD& response);
    bool confirmAlias(const LLSD& notification, const LLSD& response);
    static void onNotifications(LLUICtrl* ctrl, void* userdata);
    static void onColor(LLUICtrl* ctrl, void* userdata);
    static void onPicker(const std::vector<std::string>& names,
                         const uuid_vec_t& ids, void* userdata);
};

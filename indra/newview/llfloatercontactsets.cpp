// Chaos Emerging Build 171 â€” Contact Sets, integrated with the existing
// OpenSim conference IM and WebRTC call handling. Firestorm 7.2.5 set file schema.
#include "llviewerprecompiledheaders.h"
#include "llfloatercontactsets.h"

#include "llagent.h"
#include "llavataractions.h"
#include "llavatarnamecache.h"
#include "llavatartracker.h"
#include "llbutton.h"
#include "llcachename.h"
#include "llcheckboxctrl.h"
#include "llcolorswatch.h"
#include "llcombobox.h"
#include "lldir.h"
#include "llfile.h"
#include "llfloateravatarinfo.h"
#include "llfloateravatarpicker.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llviewermenu.h"
#include "llnotifications.h"
#include "llscrolllistctrl.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llviewermenu.h"
#include "llsdserialize.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace
{
constexpr char SETS_FILENAME[] = "settings_friends_groups.xml";
constexpr char ALL_CONTACTS[] = "__all__";
constexpr char NO_SET[] = "__none__";
constexpr char PSEUDONYMS[] = "__aliases__";
constexpr size_t MAX_CONFERENCE_CONTACTS = 20;

bool reservedName(const std::string& name)
{
    return name.empty() || name == "extraAvs" ||
           name == "Pseudonyms" || name == "globalSettings" ||
           name == ALL_CONTACTS || name == NO_SET || name == PSEUDONYMS;
}
}

LLFloaterContactSets::LLFloaterContactSets(const LLSD&)
:   LLEventTimer(5.f)
{
    LLUICtrlFactory::getInstance()->buildFloater(this, "floater_contact_sets.xml");
}

bool LLFloaterContactSets::postBuild()
{
    mSetSelector = getChild<LLComboBox>("sets");
    mSetSelector->setCommitCallback(onSetSelected);
    mSetSelector->setCallbackUserData(this);

    mContacts = getChild<LLScrollListCtrl>("contact_list");
    mContacts->setMaxSelectable(MAX_CONFERENCE_CONTACTS);
    mContacts->setCommitOnSelectionChange(true);
    mContacts->setCommitCallback(onContactSelected);
    mContacts->setCallbackUserData(this);
    mContacts->setDoubleClickCallback(onIM);

    mNotifications = getChild<LLCheckBoxCtrl>("set_notifications");
    mNotifications->setCommitCallback(onNotifications);
    mNotifications->setCallbackUserData(this);
    mSetColor = getChild<LLColorSwatchCtrl>("set_color");
    mSetColor->setCommitCallback(onColor);
    mSetColor->setCallbackUserData(this);
    mStatus = getChild<LLTextBox>("contact_status");

    childSetAction("new_set", onNew, this);
    mGearButton = getChild<LLButton>("config_btn");
    childSetAction("config_btn", onGear, this);
    setupGearMenu();
    childSetAction("delete_set", onDelete, this);
    childSetAction("add_contact", onAdd, this);
    childSetAction("remove_contact", onRemove, this);
    childSetAction("im_call", onIM, this);
    childSetAction("offer_tp", onTeleport, this);
    childSetAction("profile", onProfile, this);
    childSetAction("set_alias", onSetAlias, this);
    childSetAction("clear_alias", onClearAlias, this);
    childSetAction("remove_dn", onRemoveDN, this);

    loadForCurrentAgent();
    refreshSets();
    setStatus("Ctrl-click names to select multiple contacts for a conference.");
    return true;
}

bool LLFloaterContactSets::tick()
{
    if (mLoadedAgent != gAgentID)
    {
        loadForCurrentAgent();
        refreshSets();
    }
    else if (getVisible() && mContacts && mContacts->getAllSelected().empty())
    {
        refreshContacts(); // Update names and online states without losing selection.
    }
    // Firestorm-style per-set online/offline notifications. Observe once per
    // timer tick even when the floater is hidden; the first observation is
    // baseline only, never an unsolicited login-notice burst.
    if (!mLoadedAgent.isNull())
    {
        std::set<LLUUID> watched;
        for (const auto& entry : mSets)
            if (entry.second.notify)
                for (const LLUUID& id : entry.second.members)
                    if (gAvatarTracker.isBuddy(id)) watched.insert(id);
        for (const LLUUID& id : watched)
        {
            const bool online = gAvatarTracker.isBuddyOnline(id);
            auto prior = mLastOnline.emplace(id, online);
            if (!prior.second && prior.first->second != online)
            {
                LLSD args;
                args["NAME"] = resolveName(id);
                gNotifications.add(online ? "FriendOnline" : "FriendOffline", args);
                prior.first->second = online;
            }
        }
        for (auto it = mLastOnline.begin(); it != mLastOnline.end(); )
        {
            if (!watched.count(it->first)) it = mLastOnline.erase(it);
            else ++it;
        }
    }
    return false;
}

std::string LLFloaterContactSets::filename() const
{
    return gDirUtil.getFullPath(LL_PATH_PER_ACCOUNT, SETS_FILENAME);
}

void LLFloaterContactSets::loadForCurrentAgent()
{
    mLoadedAgent = gAgentID;
    mSets.clear();
    mOtherSetSettings.clear();
    mGlobalSettings = LLSD();
    mPseudonyms.clear();
    mExtraAvatars.clear();
    mLastOnline.clear();
    mSelectedSet = ALL_CONTACTS;
    mPickerTargetSet = ALL_CONTACTS;
    if (mLoadedAgent.isNull()) return;

    llifstream in(filename().c_str());
    if (!in.is_open()) return;

    LLSD data;
    if (LLSDSerialize::fromXML(data, in) <= 0) return;
    if (!data.isMap()) return;

    for (LLSD::map_const_iterator it = data.beginMap(), end = data.endMap(); it != end; ++it)
    {
        const std::string& name = it->first;
        const LLSD& value = it->second;
        if (name == "globalSettings")
        {
            mGlobalSettings = value;
            continue;
        }
        if (name == "extraAvs" || name == "Pseudonyms")
        {
            if (!value.isMap()) continue;
            for (LLSD::map_const_iterator item = value.beginMap(), last = value.endMap(); item != last; ++item)
            {
                LLUUID id(item->first);
                if (id.isNull()) continue;
                if (name == "extraAvs") mExtraAvatars.insert(id);
                else mPseudonyms[id] = item->second.asString();
            }
            continue;
        }
        if (!value.isMap() || reservedName(name)) continue;
        ContactSet set;
        if (value.has("color")) set.color = LLColor4(value["color"]);
        if (value.has("notify")) set.notify = value["notify"].asBoolean();
        if (value.has("sort_by_online_status")) set.sort_online = value["sort_by_online_status"].asBoolean();
        if (value.has("friends") && value["friends"].isMap())
        {
            const LLSD& friends = value["friends"];
            for (LLSD::map_const_iterator item = friends.beginMap(), last = friends.endMap(); item != last; ++item)
            {
                LLUUID id(item->first);
                if (!id.isNull()) set.members.insert(id);
            }
        }
        mSets.emplace(name, std::move(set));
        mOtherSetSettings[name] = value;
    }
}

void LLFloaterContactSets::save() const
{
    if (mLoadedAgent.isNull() || mLoadedAgent != gAgentID) return;
    LLSD data;
    data["globalSettings"] = mGlobalSettings;
    if (!data["globalSettings"].has("defaultColor"))
        data["globalSettings"]["defaultColor"] = LLColor4::grey.getValue();
    for (const LLUUID& id : mExtraAvatars) data["extraAvs"][id.asString()] = "";
    for (const auto& pair : mPseudonyms)
        data["Pseudonyms"][pair.first.asString()] = pair.second;
    for (const auto& pair : mSets)
    {
        const ContactSet& set = pair.second;
        LLSD& item = data[pair.first];
        auto old = mOtherSetSettings.find(pair.first);
        if (old != mOtherSetSettings.end()) item = old->second;
        item["friends"] = LLSD::emptyMap();
        item["color"] = set.color.getValue();
        item["notify"] = set.notify;
        item["sort_by_online_status"] = set.sort_online;
        for (const LLUUID& id : set.members) item["friends"][id.asString()] = "";
    }
    llofstream out(filename().c_str());
    if (out.is_open()) LLSDSerialize::toPrettyXML(data, out);
}

bool LLFloaterContactSets::isNormalSet() const
{
    return mSets.find(mSelectedSet) != mSets.end();
}

bool LLFloaterContactSets::isInAnySet(const LLUUID& id) const
{
    for (const auto& set : mSets)
        if (set.second.members.find(id) != set.second.members.end()) return true;
    return false;
}

void LLFloaterContactSets::refreshSets()
{
    if (!mSetSelector) return;
    mUpdating = true;
    mSetSelector->removeall();
    mSetSelector->add("All sets", LLSD(ALL_CONTACTS), ADD_BOTTOM, true);
    mSetSelector->add("No Set", LLSD(NO_SET), ADD_BOTTOM, true);
    mSetSelector->add("Pseudonyms", LLSD(PSEUDONYMS), ADD_BOTTOM, true);
    for (const auto& entry : mSets)
        mSetSelector->add(entry.first, LLSD(entry.first), ADD_BOTTOM, true);
    if (mSelectedSet != ALL_CONTACTS && mSelectedSet != NO_SET &&
        mSelectedSet != PSEUDONYMS && !isNormalSet()) mSelectedSet = ALL_CONTACTS;
    mSetSelector->setValue(mSelectedSet);
    mUpdating = false;
    refreshSettings();
    refreshContacts();
}

std::string LLFloaterContactSets::resolveName(const LLUUID& id) const
{
    std::string name;
    LLAvatarName avatar_name;
    if (LLAvatarNameCache::get(id, &avatar_name)) name = avatar_name.getNames();
    if (name.empty() && gCacheNamep) gCacheNamep->getFullName(id, name);
    if (name.empty()) name = id.asString();
    auto alias = mPseudonyms.find(id);
    if (alias != mPseudonyms.end())
    {
        if (alias->second == "--- ---")
        {
            std::string legacy;
            if (gCacheNamep) gCacheNamep->getFullName(id, legacy);
            if (legacy.empty()) legacy = avatar_name.getLegacyName();
            if (!legacy.empty()) name = legacy;
        }
        else if (!alias->second.empty())
        {
            name = alias->second + " (" + name + ")";
        }
    }
    return name;
}

void LLFloaterContactSets::refreshContacts()
{
    if (!mContacts) return;
    mUpdating = true;
    mContacts->deleteAllItems();
    LLAvatarTracker::buddy_map_t buddies;
    gAvatarTracker.copyBuddyList(buddies);
    std::set<LLUUID> visible;
    if (mSelectedSet == ALL_CONTACTS || mSelectedSet == NO_SET || mSelectedSet == PSEUDONYMS)
    {
        for (const auto& buddy : buddies)
        {
            if (mSelectedSet == NO_SET && isInAnySet(buddy.first)) continue;
            if (mSelectedSet == PSEUDONYMS && !mPseudonyms.count(buddy.first)) continue;
            visible.insert(buddy.first);
        }
        if (mSelectedSet == ALL_CONTACTS)
        {
            for (const auto& id : mExtraAvatars) visible.insert(id);
            for (const auto& entry : mSets)
                for (const auto& id : entry.second.members) visible.insert(id);
        }
        if (mSelectedSet == PSEUDONYMS)
            for (const auto& alias : mPseudonyms) visible.insert(alias.first);
    }
    else
    {
        auto found = mSets.find(mSelectedSet);
        if (found != mSets.end()) visible = found->second.members;
    }

    size_t displayed = 0;
    for (const LLUUID& id : visible)
    {
        std::string display_name = resolveName(id);
        if (!mFilterString.empty())
        {
            std::string compare = display_name;
            LLStringUtil::toLower(compare);
            if (compare.find(mFilterString) == std::string::npos) continue;
        }
        LLSD row;
        row["id"] = id;
        const bool is_friend = gAvatarTracker.isBuddy(id);
        const bool online = is_friend && gAvatarTracker.isBuddyOnline(id);
        LLSD& status = row["columns"][0];
        status["column"] = "online";
        status["type"] = "icon";
        if (online) status["value"] = "icon_avatar_online.tga";
        LLSD& label = row["columns"][1];
        label["column"] = "name";
        label["value"] = display_name;
        label["font"] = "SANSSERIF";
        label["font-style"] = online ? "BOLD" : "NORMAL";
        label["font-color"] = online ? LLColor4(0.94f, 0.94f, 0.95f, 1.f).getValue()
                                     : LLColor4(0.66f, 0.67f, 0.70f, 1.f).getValue();
        if (isNormalSet()) label["font-color"] = mSets.at(mSelectedSet).color.getValue();

        // Use the existing friend-rights indicators. These are small action/status
        // icons, never profile portraits; rights are display-only here.
        const LLRelationship* rel = gAvatarTracker.getBuddyInfo(id);
        const auto icon = [&](int index, const char* column, const char* image)
        {
            LLSD& cell = row["columns"][index];
            cell["column"] = column;
            cell["type"] = "icon";
            if (image && *image) cell["value"] = image;
        };
        icon(2, "show_online",
             rel && rel->isRightGrantedTo(LLRelationship::GRANT_ONLINE_STATUS)
                  ? "ff_visible_online.tga" : "");
        icon(3, "show_map",
             rel && rel->isRightGrantedTo(LLRelationship::GRANT_MAP_LOCATION)
                  ? "ff_visible_map.tga" : "");
        icon(4, "edit_mine",
             rel && rel->isRightGrantedTo(LLRelationship::GRANT_MODIFY_OBJECTS)
                  ? "ff_edit_mine.tga" : "");
        icon(5, "see_theirs",
             rel && rel->isRightGrantedFrom(LLRelationship::GRANT_MAP_LOCATION)
                  ? "ff_visible_map_theirs.tga" :
             rel && (online || rel->isRightGrantedFrom(LLRelationship::GRANT_ONLINE_STATUS))
                  ? "ff_visible_online_theirs.tga" : "");
        icon(6, "edit_theirs",
             rel && rel->isRightGrantedFrom(LLRelationship::GRANT_MODIFY_OBJECTS)
                  ? "ff_edit_theirs.tga" : "");
        mContacts->addElement(row, ADD_BOTTOM);
        ++displayed;
    }
    mContacts->sortByColumn("name", true);
    if (isNormalSet() && mSets.at(mSelectedSet).sort_online)
        mContacts->sortByColumn("online", false);
    mUpdating = false;
    setStatus(llformat("Members: %d", static_cast<S32>(displayed)));
}

void LLFloaterContactSets::refreshSettings()
{
    const auto found = mSets.find(mSelectedSet);
    const bool real_set = found != mSets.end();
    mUpdating = true;
    if (mNotifications)
    {
        mNotifications->setEnabled(real_set);
        mNotifications->setValue(real_set && found->second.notify);
    }
    if (mSetColor)
    {
        mSetColor->setEnabled(real_set);
        mSetColor->set(real_set ? found->second.color : LLColor4::grey);
    }
    childSetEnabled("config_btn", real_set);
    childSetEnabled("delete_set", real_set);
    childSetEnabled("add_contact", !mLoadedAgent.isNull() && mLoadedAgent == gAgentID);
    childSetEnabled("remove_contact", real_set);
    mUpdating = false;
}

void LLFloaterContactSets::setStatus(const std::string& message)
{
    if (mStatus) mStatus->setText(message);
}

std::vector<LLUUID> LLFloaterContactSets::selectedAvatars() const
{
    std::vector<LLUUID> ids;
    if (!mContacts) return ids;
    for (LLScrollListItem* item : mContacts->getAllSelected())
    {
        if (item && !item->getUUID().isNull()) ids.push_back(item->getUUID());
        if (ids.size() >= MAX_CONFERENCE_CONTACTS) break;
    }
    return ids;
}

void LLFloaterContactSets::onSearchEdit(const std::string& search, void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self || self->mUpdating) return;
    self->mFilterString = search;
    LLStringUtil::trim(self->mFilterString);
    LLStringUtil::toLower(self->mFilterString);
    self->refreshContacts();
}

void LLFloaterContactSets::setupGearMenu()
{
    if (!gMenuHolderp || mGearMenu) return;
    mGearMenu = new LLMenuGL("chaos_contact_set_options", "Contact Set");
    gMenuHolderp->addChild(mGearMenu);
    mGearMenu->setVisible(false);
    auto add = [this](const std::string& name, const std::string& label,
                      const std::function<void()>& action)
    {
        auto item_action = std::make_unique<GearAction>();
        item_action->fn = action;
        LLMenuItemCallGL* item = new LLMenuItemCallGL(
            name, label, &LLFloaterContactSets::gearMenuCallback,
            nullptr, item_action.get(), KEY_NONE, MASK_NONE, true);
        mGearActions.push_back(std::move(item_action));
        mGearMenu->append(item);
    };
    add("contact_set_rename", "Rename Contact Set...",
        [this]() { onRename(this); });
    add("contact_set_notify", "Toggle Set Online Notifications",
        [this]()
        {
            if (!isNormalSet()) return;
            ContactSet& set = mSets.at(mSelectedSet);
            set.notify = !set.notify;
            save();
            refreshSettings();
        });
    add("contact_set_color", "Set Contact Colour...",
        [this]()
        {
            if (isNormalSet() && mSetColor) mSetColor->showPicker(true);
        });
}
void LLFloaterContactSets::onGear(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self || !self->isNormalSet()) return;
    self->setupGearMenu();
    if (!self->mGearMenu) return;
    LLMenuGL::showPopup(self->mGearButton, self->mGearMenu, 0, 0);
}

void LLFloaterContactSets::onSetSelected(LLUICtrl*, void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self || self->mUpdating) return;
    self->mSelectedSet = self->mSetSelector->getValue().asString();
    self->refreshSettings();
    self->refreshContacts();
}

void LLFloaterContactSets::onContactSelected(LLUICtrl*, void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self || self->mUpdating) return;
    auto ids = self->selectedAvatars();
    self->childSetEnabled("set_alias", ids.size() == 1);
    self->childSetEnabled("clear_alias",
        ids.size() == 1 && self->mPseudonyms.count(ids.front()) != 0);
    self->childSetEnabled("remove_dn", ids.size() == 1 &&
        (!self->mPseudonyms.count(ids.front()) ||
         self->mPseudonyms.at(ids.front()) != "--- ---"));
    self->childSetEnabled("profile", ids.size() == 1);
    self->childSetEnabled("im_call", !ids.empty());
    self->childSetEnabled("offer_tp", !ids.empty());
}

void LLFloaterContactSets::onNew(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    LLHandle<LLFloater> handle = self->getHandle();
    gNotifications.add("ChaosContactSetNew", LLSD(), LLSD(),
        [handle](const LLSD& notification, const LLSD& response) -> bool
        {
            auto* live = dynamic_cast<LLFloaterContactSets*>(handle.get());
            return live ? live->confirmNew(notification, response) : false;
        });
}

bool LLFloaterContactSets::confirmNew(const LLSD& notification, const LLSD& response)
{
    if (LLNotification::getSelectedOption(notification, response) != 0) return false;
    std::string name = response["message"].asString();
    LLStringUtil::trim(name);
    if (name.size() > 64 || reservedName(name) || mSets.count(name))
    {
        setStatus("Enter a unique set name, up to 64 characters.");
        return false;
    }
    mSets[name] = ContactSet();
    mSelectedSet = name;
    save();
    refreshSets();
    return false;
}

void LLFloaterContactSets::onRename(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self->isNormalSet()) return;
    LLHandle<LLFloater> handle = self->getHandle();
    LLSD payload;
    payload["set"] = self->mSelectedSet;
    LLSD args;
    args["SET"] = self->mSelectedSet;
    gNotifications.add("ChaosContactSetRename", args, payload,
        [handle](const LLSD& notification, const LLSD& response) -> bool
        {
            auto* live = dynamic_cast<LLFloaterContactSets*>(handle.get());
            return live ? live->confirmRename(notification, response) : false;
        });
}

bool LLFloaterContactSets::confirmRename(const LLSD& notification, const LLSD& response)
{
    if (LLNotification::getSelectedOption(notification, response) != 0) return false;
    std::string previous = notification["payload"]["set"].asString();
    if (!mSets.count(previous)) return false;
    std::string name = response["message"].asString();
    LLStringUtil::trim(name);
    if (name.size() > 64 || reservedName(name) ||
        (name != previous && mSets.count(name)))
    {
        setStatus("Enter a different unused set name.");
        return false;
    }
    if (name == previous) return false;
    ContactSet moved = std::move(mSets.at(previous));
    auto extras = mOtherSetSettings.find(previous);
    if (extras != mOtherSetSettings.end())
    {
        mOtherSetSettings[name] = extras->second;
        mOtherSetSettings.erase(extras);
    }
    mSets.erase(previous);
    mSets.emplace(name, std::move(moved));
    mSelectedSet = name;
    save();
    refreshSets();
    return false;
}

void LLFloaterContactSets::onDelete(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self->isNormalSet()) return;
    LLHandle<LLFloater> handle = self->getHandle();
    LLSD payload;
    payload["set"] = self->mSelectedSet;
    LLSD args;
    args["SET"] = self->mSelectedSet;
    gNotifications.add("ChaosContactSetDelete", args, payload,
        [handle](const LLSD& notification, const LLSD& response) -> bool
        {
            auto* live = dynamic_cast<LLFloaterContactSets*>(handle.get());
            return live ? live->confirmDelete(notification, response) : false;
        });
}

bool LLFloaterContactSets::confirmDelete(const LLSD& notification, const LLSD& response)
{
    if (LLNotification::getSelectedOption(notification, response) != 0) return false;
    std::string previous = notification["payload"]["set"].asString();
    if (!mSets.erase(previous)) return false;
    mOtherSetSettings.erase(previous);
    mSelectedSet = "__all__";
    save();
    refreshSets();
    return false;
}


void LLFloaterContactSets::onPicker(const std::vector<std::string>&,
                                     const uuid_vec_t& ids, void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self || self->mLoadedAgent.isNull() || self->mLoadedAgent != gAgentID) return;
    // Firestorm behaviour: a named set receives the selected UUIDs; when
    // "All sets" or another internal category is selected, non-friends are
    // retained as extra avatars, without inventing an unnamed contact set.
    auto target = self->mSets.find(self->mPickerTargetSet);
    bool changed = false;
    for (const LLUUID& id : ids)
    {
        if (id.isNull() || id == gAgentID) continue;
        if (target != self->mSets.end())
            changed = target->second.members.insert(id).second || changed;
        if (!gAvatarTracker.isBuddy(id))
            changed = self->mExtraAvatars.insert(id).second || changed;
    }
    if (changed) self->save();
    self->refreshContacts();
    if (changed && target != self->mSets.end())
        self->setStatus("Contacts added to " + self->mPickerTargetSet + ".");
    else if (changed)
        self->setStatus("Additional contacts added to All sets.");
    else if (!ids.empty())
        self->setStatus("Already listed. Select a named set to group friends.");
}

void LLFloaterContactSets::onAdd(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self || self->mLoadedAgent.isNull() || self->mLoadedAgent != gAgentID)
        return;
    self->mPickerTargetSet = self->mSelectedSet;
    // This original picker has real Search, Friends, Calling Cards (inventory),
    // and Near me tabs; no substitute or custom search is needed.
    LLFloaterAvatarPicker* picker = LLFloaterAvatarPicker::show(onPicker, self, true, true);
    if (picker) self->addDependentFloater(picker);
}

void LLFloaterContactSets::onRemove(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self->isNormalSet()) return;
    auto ids = self->selectedAvatars();
    ContactSet& set = self->mSets.at(self->mSelectedSet);
    for (const LLUUID& id : ids) set.members.erase(id);
    self->save();
    self->refreshContacts();
    self->setStatus("Removed selected contacts from this set.");
}

void LLFloaterContactSets::onIM(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    auto ids = self->selectedAvatars();
    if (ids.empty()) { self->setStatus("Select one or more contacts."); return; }
    if (ids.size() > MAX_CONFERENCE_CONTACTS) ids.resize(MAX_CONFERENCE_CONTACTS);
    LLAvatarActions::startIM(ids, true);
    self->setStatus("IM opened. Choose Start Call in the conference IM for voice.");
}

void LLFloaterContactSets::onTeleport(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    auto ids = self->selectedAvatars();
    if (!ids.empty()) LLAvatarActions::offerTeleport(ids);
}

void LLFloaterContactSets::onProfile(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    auto ids = self->selectedAvatars();
    if (ids.size() == 1) LLFloaterAvatarInfo::show(ids.front());
}

void LLFloaterContactSets::onSetAlias(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    auto ids = self->selectedAvatars();
    if (ids.size() != 1) return;
    LLHandle<LLFloater> handle = self->getHandle();
    LLSD payload, args;
    payload["id"] = ids.front();
    args["NAME"] = self->resolveName(ids.front());
    gNotifications.add("ChaosContactSetAlias", args, payload,
        [handle](const LLSD& notification, const LLSD& response) -> bool
        {
            auto* live = dynamic_cast<LLFloaterContactSets*>(handle.get());
            return live ? live->confirmAlias(notification, response) : false;
        });
}

bool LLFloaterContactSets::confirmAlias(const LLSD& notification, const LLSD& response)
{
    if (LLNotification::getSelectedOption(notification, response) != 0) return false;
    LLUUID id = notification["payload"]["id"].asUUID();
    if (id.isNull()) return false;
    std::string alias = response["message"].asString();
    LLStringUtil::trim(alias);
    if (alias.size() > 64)
    {
        setStatus("Alias must be 64 characters or fewer.");
        return false;
    }
    if (alias.empty()) mPseudonyms.erase(id);
    else mPseudonyms[id] = alias;
    save();
    refreshContacts();
    return false;
}

void LLFloaterContactSets::onClearAlias(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    for (const LLUUID& id : self->selectedAvatars())
        self->mPseudonyms.erase(id);
    self->save();
    self->refreshContacts();
}

void LLFloaterContactSets::onRemoveDN(void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    for (const LLUUID& id : self->selectedAvatars())
        self->mPseudonyms[id] = "--- ---";
    self->save();
    self->refreshContacts();
}


void LLFloaterContactSets::onNotifications(LLUICtrl*, void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self->isNormalSet() || self->mUpdating) return;
    self->mSets.at(self->mSelectedSet).notify = self->mNotifications->getValue().asBoolean();
    self->save();
}

void LLFloaterContactSets::onColor(LLUICtrl*, void* userdata)
{
    auto* self = static_cast<LLFloaterContactSets*>(userdata);
    if (!self->isNormalSet() || self->mUpdating) return;
    self->mSets.at(self->mSelectedSet).color = self->mSetColor->get();
    self->save();
}

/**
 * @file llappearancelistener.cpp
 * Chaos Emerging Viewer 184: Firestorm 7.2.5 LLAppearance LEAP port.
 *
 * Based on FirestormViewer/phoenix-firestorm, branch Firestorm_7.2.5:
 * indra/newview/llappearancelistener.cpp.
 * Original copyright (C) 2024 Linden Research, Inc.; LGPL v2.1.
 *
 * All five Firestorm event operation names, arguments and response shapes
 * are retained. Only the appearance-manager calls are adapted, because the
 * OpenSim/Cool VL-era Chaos codebase implements wearing and removing through
 * a different LLAppearanceMgr and LLAgentWearables workflow.
 */
#include "llviewerprecompiledheaders.h"
#include "llappearancelistener.h"

#include "llappearancemgr.h"
#include "llagentwearables.h"
#include "llfoldertype.h"
#include "llgesturemgr.h"
#include "llinventorybridge.h"
#include "llinventorymodel.h"
#include "llstartup.h"
#include "llviewerinventory.h"
#include "llviewerwearable.h"
#include "llvoavatarself.h"

// Preserve Chaos's own RLV/RLVa restrictions for every action.
#include "mkrlinterface.h"

#include <vector>

namespace
{
// In Firestorm, items_id accepts either a single UUID or an array of UUIDs.
bool unpackItemIDs(const LLSD& data, uuid_vec_t& ids, std::string& error)
{
    if (!data.has("items_id"))
    {
        error = "items_id is required";
        return false;
    }

    const LLSD& raw = data["items_id"];
    if (raw.isArray())
    {
        for (LLSD::array_const_iterator it = raw.beginArray(),
                                      end = raw.endArray();
             it != end; ++it)
        {
            const LLUUID id = it->asUUID();
            if (id.isNull())
            {
                error = "items_id array contains an invalid UUID";
                return false;
            }
            ids.push_back(id);
        }
    }
    else
    {
        const LLUUID id = raw.asUUID();
        if (id.isNull())
        {
            error = "items_id is not a valid UUID";
            return false;
        }
        ids.push_back(id);
    }
    return true;
}

class LLIsChaosOutfitFolder final : public LLInventoryCollectFunctor
{
public:
    bool operator()(LLInventoryCategory* category,
                    LLInventoryItem*) override
    {
        return category &&
               category->getPreferredType() == LLFolderType::FT_OUTFIT;
    }
};
}

LLAppearanceListener::LLAppearanceListener()
: LLEventAPI("LLAppearance",
             "API to wear a specified outfit and wear/remove individual items")
{
    // Same five operations as Firestorm 7.2.5 llappearancelistener.cpp.
    add("wearOutfit",
        "Wear outfit by folder id: [\"folder_id\"] OR by folder name: [\"folder_name\"]\n"
        "When [\"append\"] is true, outfit will be added to COF\n"
        "otherwise it will replace current outfit",
        &LLAppearanceListener::wearOutfit);

    add("wearItems",
        "Wear items by id: [items_id]",
        &LLAppearanceListener::wearItems);

    add("detachItems",
        "Detach items by id: [items_id]",
        &LLAppearanceListener::detachItems);

    add("getOutfitsList",
        "Return the table with Outfits info(id and name)",
        &LLAppearanceListener::getOutfitsList);

    add("getOutfitItems",
        "Return items (id : name, wearable_type, is_worn) from an outfit folder",
        &LLAppearanceListener::getOutfitItems);
}

void LLAppearanceListener::wearOutfit(const LLSD& data)
{
    Response response(LLSD(), data);
    if (!LLStartUp::isLoggedIn() || !gAgentWearables.areWearablesLoaded())
    {
        response.error("Outfit cannot be changed before avatar wearables load");
        return;
    }
    if (!data.has("folder_id") && !data.has("folder_name"))
    {
        response.error("Either [folder_id] or [folder_name] is required");
        return;
    }

    const bool append = data.has("append") ? data["append"].asBoolean() : false;
    LLViewerInventoryCategory* category = nullptr;
    // Firestorm's name lookup searches the Library first. Preserve that,
    // then allow exact-name user inventory lookup as an OpenSim convenience.
    if (data.has("folder_name"))
    {
        const std::string name = data["folder_name"].asString();
        if (!name.empty())
        {
            LLInventoryModel::cat_array_t cats;
            LLInventoryModel::item_array_t items;
            LLNameCategoryCollector matches(name);
            gInventory.collectDescendentsIf(gInventory.getLibraryRootFolderID(),
                                            cats, items,
                                            LLInventoryModel::EXCLUDE_TRASH,
                                            matches);
            if (cats.empty())
            {
                gInventory.collectDescendentsIf(gInventory.getRootFolderID(),
                                                cats, items,
                                                LLInventoryModel::EXCLUDE_TRASH,
                                                matches);
            }
            if (!cats.empty())
            {
                category = cats[0];
            }
        }
    }
    if (!category && data.has("folder_id"))
    {
        category = gInventory.getCategory(data["folder_id"].asUUID());
    }
    if (!category)
    {
        response.error("Failed to wear outfit: folder not found");
        return;
    }

    const LLUUID id = category->getUUID();
    const LLUUID user_root = gInventory.getRootFolderID();
    const LLUUID library_root = gInventory.getLibraryRootFolderID();
    const bool from_user = gInventory.isObjectDescendentOf(id, user_root);
    const bool from_library = gInventory.isObjectDescendentOf(id, library_root);
    if ((!from_user && !from_library) ||
        id == user_root || id == library_root ||
        gInventory.isInTrash(id))
    {
        response.error("Failed to wear outfit: invalid folder location");
        return;
    }
    if (gRLenabled &&
        (!gRLInterface.canAttachCategory(category) ||
         (!append && !gRLInterface.canDetachCategory(category))))
    {
        response.error("Cannot wear outfit due to RLV restrictions");
        return;
    }

    // The native Chaos category path fetches inventory asynchronously and
    // preserves its COF, BoM, multiwear, RLV and attachment handling.
    // For Library categories, the original copy-before-wear path is required.
    gAppearanceMgr.wearInventoryCategory(category, from_library, append);
    response["accepted"] = true; // queued, not a bake-complete signal
}

void LLAppearanceListener::wearItems(const LLSD& data)
{
    Response response(LLSD(), data);
    if (!LLStartUp::isLoggedIn() || !gAgentWearables.areWearablesLoaded())
    {
        response.error("Cannot wear items before avatar wearables load");
        return;
    }
    uuid_vec_t ids;
    std::string error;
    if (!unpackItemIDs(data, ids, error))
    {
        response.error(error);
        return;
    }

    const bool replace = data.has("replace") && data["replace"].asBoolean();
    bool first_object = true;
    std::vector<bool> first_type(LLWearableType::WT_COUNT, true);
    U32 attempted = 0;

    // Firestorm batches COF links; Chaos adds through the established
    // wearItemOnAvatar() path. For replace=true, match Firestorm's logic:
    // replace only the first object and first clothing layer of each type.
    for (const LLUUID& id : ids)
    {
        LLViewerInventoryItem* item = gInventory.getItem(id);
        if (!item || gInventory.isInTrash(item->getUUID()))
        {
            continue;
        }
        bool replace_this = false;
        const LLAssetType::EType type = item->getType();
        if (type == LLAssetType::AT_OBJECT)
        {
            replace_this = first_object && replace;
            first_object = false;
        }
        else if (type == LLAssetType::AT_CLOTHING)
        {
            const LLWearableType::EType wtype = item->getWearableType();
            if (wtype >= 0 && wtype < LLWearableType::WT_COUNT)
            {
                replace_this = first_type[wtype] && replace;
                first_type[wtype] = false;
            }
        }
        else if (type == LLAssetType::AT_BODYPART)
        {
            replace_this = true;
        }
        else
        {
            continue;
        }

        // Native function is void-like: its bool return is false even on
        // successfully started operations; do not treat it as a failure.
        gAppearanceMgr.wearItemOnAvatar(item->getUUID(), replace_this);
        ++attempted;
    }
    response["accepted"] = attempted > 0;
    response["requested"] = (S32)attempted;
}

void LLAppearanceListener::detachItems(const LLSD& data)
{
    Response response(LLSD(), data);
    if (!LLStartUp::isLoggedIn() || !gAgentWearables.areWearablesLoaded())
    {
        response.error("Cannot detach items before avatar wearables load");
        return;
    }
    uuid_vec_t ids;
    std::string error;
    if (!unpackItemIDs(data, ids, error))
    {
        response.error(error);
        return;
    }

    U32 removed = 0;
    for (const LLUUID& requested : ids)
    {
        LLViewerInventoryItem* link = gInventory.getItem(requested);
        if (!link)
        {
            continue;
        }
        LLViewerInventoryItem* item =
            gInventory.getItem(link->getLinkedUUID());
        if (!item)
        {
            item = link;
        }
        const LLUUID id = item->getLinkedUUID();
        if (!get_is_item_worn(item->getUUID(), false))
        {
            continue;
        }
        if (item->getType() == LLAssetType::AT_OBJECT)
        {
            if (!gRLenabled || gRLInterface.canDetach(item))
            {
                if (LLVOAvatarSelf::detachAttachmentIntoInventory(id))
                {
                    ++removed;
                }
            }
        }
        else if (item->getType() == LLAssetType::AT_CLOTHING)
        {
            if (gRLenabled && !gRLInterface.canUnwear(item))
            {
                continue;
            }
            LLViewerWearable* wearable =
                gAgentWearables.getWearableFromItemID(id);
            U32 index = 0;
            if (wearable && gAgentWearables.getWearableIndex(wearable, index))
            {
                gAgentWearables.userRemoveWearable(wearable->getType(), index);
                ++removed;
            }
        }
        // Body parts are not detachable, as in Firestorm.
    }
    if (removed)
    {
        gInventory.notifyObservers();
    }
    response["detached"] = (S32)removed;
}

void LLAppearanceListener::getOutfitsList(const LLSD& data)
{
    Response response(LLSD(), data);
    const LLUUID outfits_id =
        gInventory.findCategoryUUIDForType(LLFolderType::FT_MY_OUTFITS);
    LLSD outfit_map = LLSD::emptyMap();
    if (outfits_id.notNull())
    {
        LLInventoryModel::cat_array_t categories;
        LLInventoryModel::item_array_t items;
        LLIsChaosOutfitFolder is_outfit;
        gInventory.collectDescendentsIf(outfits_id, categories, items,
                                        LLInventoryModel::EXCLUDE_TRASH,
                                        is_outfit);
        for (const auto& cat : categories)
        {
            if (cat)
            {
                outfit_map[cat->getUUID().asString()] = cat->getName();
            }
        }
    }
    response["outfits"] = outfit_map;
}

void LLAppearanceListener::getOutfitItems(const LLSD& data)
{
    Response response(LLSD(), data);
    const LLUUID outfit_id = data["outfit_id"].asUUID();
    LLViewerInventoryCategory* cat = gInventory.getCategory(outfit_id);
    if (!cat || cat->getPreferredType() != LLFolderType::FT_OUTFIT)
    {
        response.error("Couldn't find outfit " + outfit_id.asString());
        return;
    }
    LLInventoryModel::cat_array_t categories;
    LLInventoryModel::item_array_t items;
    LLFindOutfitItems collector;
    gInventory.collectDescendentsIf(outfit_id, categories, items,
                                    LLInventoryModel::EXCLUDE_TRASH,
                                    collector);

    LLSD item_map = LLSD::emptyMap();
    for (const auto& item : items)
    {
        if (!item) continue;
        LLSD detail = LLSD::emptyMap();
        detail["name"] = item->getName();
        const bool wearable =
            item->getType() == LLAssetType::AT_CLOTHING ||
            item->getType() == LLAssetType::AT_BODYPART;
        detail["wearable_type"] =
            LLWearableType::getTypeName(wearable ?
                                       item->getWearableType() :
                                       LLWearableType::WT_NONE);
        detail["is_worn"] = get_is_item_worn(item->getUUID());
        item_map[item->getUUID().asString()] = detail;
    }
    response["items"] = item_map;
}

void initAppearanceLeapAPI()
{
    static LLAppearanceListener listener;
}

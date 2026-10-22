#pragma once

// Firestorm-style emoji picker integration for the classic Chaos Emerging UI.
// Emoji database and category layout derive from Firestorm's original assets.

#include "llfloater.h"
#include "llhandle.h"
#include <string>
#include <vector>
#include <map>

class LLLineEditor;
class LLScrollableContainer;
class LLPanel;
class LLTextBox;
class DPEmojiGrid;

struct DPEmojiEntry
{
    llwchar character = 0;
    std::string short_code;
    std::vector<std::string> categories;
};

class DPEmojiPicker : public LLFloater
{
public:
    DPEmojiPicker();
    ~DPEmojiPicker() override;
    bool postBuild() override;
    static void showFor(LLLineEditor* editor);
    static std::string expandShortcodes(const std::string& text);
    void insertEmoji(size_t index);
    std::string tooltipFor(size_t index) const;
    const DPEmojiEntry* entryAt(size_t index) const;
private:
    void showCategory(S32 category);
    void rebuildGrid();
    void loadRecent();
    void saveRecent();
    static void onCategory(LLUICtrl* ctrl, void* userdata);
    static void onSearch(LLLineEditor* editor, void* userdata);
    static DPEmojiPicker* sPicker;
    static void loadDictionary();
    static std::vector<DPEmojiEntry> sDictionary;
    static std::vector<llwchar> sRecent;
    static bool sLoaded;
    S32 mCategory = 0;
    LLHandle<LLView> mInputHandle;
    LLScrollableContainer* mScroll = NULL;
    DPEmojiGrid* mGrid = NULL;
    LLLineEditor* mSearch = NULL;
    LLTextBox* mPreview = NULL;
};

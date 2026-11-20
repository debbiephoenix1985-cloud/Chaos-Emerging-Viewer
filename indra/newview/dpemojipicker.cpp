// Adaptation of Firestorm's emoji picker/dictionary to the legacy LLFloater UI.
// Firestorm's original emoji_characters.xml and emoji_groups.xml supply the data.
#include "llviewerprecompiledheaders.h"
#include "dpemojipicker.h"
#include "llbutton.h"
#include "lldir.h"
#include "llfontgl.h"
#include "lllineeditor.h"
#include "llpanel.h"
#include "llscrollcontainer.h"
#include "llscrollingpanellist.h"
#include "llsdserialize.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "llview.h"
#include <algorithm>
#include <fstream>
#include <cctype>

DPEmojiPicker* DPEmojiPicker::sPicker = NULL;
std::vector<DPEmojiEntry> DPEmojiPicker::sDictionary;
std::vector<llwchar> DPEmojiPicker::sRecent;
bool DPEmojiPicker::sLoaded = false;
static std::map<std::string, std::string> sEmojiShortCodes;

static const char* const sEmojiCategories[] = {
    "", "", "smileys & emotion", "animals & nature", "food & drink",
    "travel & places", "activities", "objects", "symbols", ""
};
static const S32 DP_EMOJI_CELL = 31;
static const S32 DP_EMOJI_COLUMNS = 10;

// Use the viewer's native scrolling-panel list, as Firestorm does.
// Each row is a proper child panel; the list handles viewport visibility.
class DPEmojiRow final : public LLScrollingPanel
{
public:
    DPEmojiRow(DPEmojiPicker* picker, const std::vector<size_t>& items)
    : LLScrollingPanel("emoji_row", LLRect(0, DP_EMOJI_CELL,
                                          DP_EMOJI_CELL * DP_EMOJI_COLUMNS, 0)),
      mPicker(picker), mItems(items)
    {
    }

    void updatePanel(bool allow_modify) override {}

    void draw() override
    {
        LLScrollingPanel::draw();
        LLFontGL* emoji_font = LLFontGL::getFontEmoji();
        if (!emoji_font) return;
        for (size_t column = 0; column < mItems.size(); ++column)
        {
            const DPEmojiEntry* entry = mPicker->entryAt(mItems[column]);
            if (!entry) continue;
            const F32 x = (F32)(column * DP_EMOJI_CELL + DP_EMOJI_CELL / 2);
            const F32 y = (F32)(DP_EMOJI_CELL / 2);
            // Cool VL LLFontGL::render takes (style, max_chars, max_pixels).
            // Firestorm uses (style, shadow, max_chars) here. Passing the
            // Firestorm arguments made max_pixels=1 and hid every glyph.
            emoji_font->render(LLWString(1, entry->character), 0, x, y,
                               LLColor4::white, LLFontGL::HCENTER,
                               LLFontGL::VCENTER, LLFontGL::NORMAL,
                               1, S32_MAX);
        }
    }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        if (x < 0 || y < 0 || y >= DP_EMOJI_CELL) return false;
        size_t column = (size_t)(x / DP_EMOJI_CELL);
        if (column >= mItems.size()) return false;
        mPicker->insertEmoji(mItems[column]);
        return true;
    }

    bool handleToolTip(S32 x, S32 y, std::string& msg, LLRect* rect) override
    {
        if (x < 0 || y < 0 || y >= DP_EMOJI_CELL) return false;
        size_t column = (size_t)(x / DP_EMOJI_CELL);
        if (column >= mItems.size()) return false;
        msg = mPicker->tooltipFor(mItems[column]);
        return !msg.empty();
    }

private:
    DPEmojiPicker* mPicker;
    std::vector<size_t> mItems;
};

class DPEmojiGrid final : public LLScrollingPanelList
{
public:
    DPEmojiGrid(DPEmojiPicker* picker)
    : LLScrollingPanelList("emoji_grid", LLRect(0, 310, 310, 0)),
      mPicker(picker)
    {
    }

    void update(const std::vector<size_t>& entries)
    {
        clearPanels();
        const size_t rows = (entries.size() + DP_EMOJI_COLUMNS - 1) /
                            DP_EMOJI_COLUMNS;
        // Native scrolling-panel list inserts each new panel at the top.
        // Add bottom-up to retain the original Firestorm emoji ordering.
        for (size_t row = rows; row > 0; --row)
        {
            const size_t first = (row - 1) * DP_EMOJI_COLUMNS;
            const size_t last = llmin(entries.size(), first + DP_EMOJI_COLUMNS);
            std::vector<size_t> row_items(entries.begin() + first,
                                          entries.begin() + last);
            addPanel(new DPEmojiRow(mPicker, row_items));
        }
    }

private:
    DPEmojiPicker* mPicker;
};

DPEmojiPicker::DPEmojiPicker()
: LLFloater("dp_emoji_picker", LLRect(0, 394, 363, 0), "Choose Emoji",
            RESIZE_YES, 363, 394, DRAG_ON_TOP, MINIMIZE_NO, CLOSE_YES)
{
    loadDictionary();
    loadRecent();
    LLUICtrlFactory::getInstance()->buildFloater(this, "floater_dp_emoji_picker.xml");
}

DPEmojiPicker::~DPEmojiPicker()
{
    if (sPicker == this) sPicker = NULL;
}

void DPEmojiPicker::loadDictionary()
{
    if (sLoaded) return;
    sLoaded = true;
    std::string filename = gDirUtil.getFullPath(LL_PATH_APP_SETTINGS, "", "",
                                                        "emoji_characters.xml");
    llifstream input(filename.c_str());
    if (!input.is_open())
    {
        llwarns << "Firestorm emoji character data not found: " << filename
                << llendl;
        return;
    }
    LLSD all;
    LLSDSerialize::fromXML(all, input);
    if (!all.isArray()) return;
    for (LLSD::array_const_iterator it = all.beginArray();
         it != all.endArray(); ++it)
    {
        const LLSD& record = *it;
        if (!record.has("Character") || !record.has("ShortCodes")) continue;
        LLWString word = utf8str_to_wstring(record["Character"].asString());
        if (word.size() != 1) continue;
        DPEmojiEntry entry;
        entry.character = word[0];
        const LLSD& shortcodes = record["ShortCodes"];
        if (shortcodes.isArray())
        {
            for (LLSD::array_const_iterator code = shortcodes.beginArray();
                 code != shortcodes.endArray(); ++code)
            {
                std::string label = code->asString();
                LLStringUtil::toLower(label);
                if (label.empty()) continue;
                sEmojiShortCodes[label] = record["Character"].asString();
                if (entry.short_code.empty()) entry.short_code = label;
            }
        }
        if (entry.short_code.empty()) continue;
        if (record.has("Categories") && record["Categories"].isArray())
        {
            const LLSD& categories = record["Categories"];
            for (LLSD::array_const_iterator cat = categories.beginArray();
                 cat != categories.endArray(); ++cat)
            {
                std::string name = cat->asString();
                LLStringUtil::toLower(name);
                entry.categories.push_back(name);
            }
        }
        sDictionary.push_back(entry);
    }
    llinfos << "Loaded " << sDictionary.size() <<
               " original Firestorm emoji definitions." << llendl;
}

void DPEmojiPicker::loadRecent()
{
    sRecent.clear();
    std::string filename = gDirUtil.getFullPath(LL_PATH_PER_ACCOUNT, "", "",
                                                        "emoji_recent.xml");
    llifstream file(filename.c_str());
    if (!file.is_open()) return;
    LLSD data;
    LLSDSerialize::fromXML(data, file);
    if (!data.isArray()) return;
    for (LLSD::array_const_iterator it = data.beginArray();
         it != data.endArray() && sRecent.size() < 60; ++it)
    {
        LLWString value = utf8str_to_wstring(it->asString());
        if (value.size() == 1) sRecent.push_back(value[0]);
    }
}

void DPEmojiPicker::saveRecent()
{
    std::string filename = gDirUtil.getFullPath(LL_PATH_PER_ACCOUNT, "", "",
                                                        "emoji_recent.xml");
    llofstream output(filename.c_str());
    if (!output.is_open()) return;
    LLSD data = LLSD::emptyArray();
    for (llwchar emoji : sRecent)
        data.append(wstring_to_utf8str(LLWString(1, emoji)));
    LLSDSerialize::toPrettyXML(data, output);
}

bool DPEmojiPicker::postBuild()
{
    mSearch = getChild<LLLineEditor>("emoji_search");
    mPreview = getChild<LLTextBox>("emoji_preview");
    mSearch->setKeystrokeCallback(onSearch);
    mSearch->setCallbackUserData(this);

    for (S32 i = 0; i < 10; ++i)
    {
        std::string name = llformat("emoji_category_%d", i);
        childSetCommitCallback(name.c_str(), onCategory, this);
    }

    mGrid = new DPEmojiGrid(this);
    mScroll = new LLScrollableContainer("emoji_scroller",
                                        LLRect(7, 314, 355, 34), mGrid);
    addChild(mScroll);
    rebuildGrid();
    return true;
}

void DPEmojiPicker::showFor(LLLineEditor* editor)
{
    if (!editor) return;
    if (!sPicker) sPicker = new DPEmojiPicker();
    sPicker->mLiveSuggestions = false;
    sPicker->mLiveMatches.clear();
    sPicker->mInputHandle = editor->getHandle();
    sPicker->mSearch->setText("");
    // Show the full catalogue first; an empty recent list must not hide every emoji.
    sPicker->showCategory(sRecent.empty() ? 1 : 0);
    sPicker->open();
}

void DPEmojiPicker::showCategory(S32 category)
{
    mCategory = llclamp(category, 0, 9);
    rebuildGrid();
}

void DPEmojiPicker::onCategory(LLUICtrl* control, void* userdata)
{
    DPEmojiPicker* picker = (DPEmojiPicker*)userdata;
    if (!picker || !control) return;
    std::string name = control->getName();
    S32 idx = -1;
    if (sscanf(name.c_str(), "emoji_category_%d", &idx) == 1)
        picker->showCategory(idx);
}

void DPEmojiPicker::onSearch(LLLineEditor*, void* userdata)
{
    DPEmojiPicker* picker = (DPEmojiPicker*)userdata;
    if (picker) picker->rebuildGrid();
}

const DPEmojiEntry* DPEmojiPicker::entryAt(size_t index) const
{
    return index < sDictionary.size() ? &sDictionary[index] : NULL;
}

std::string DPEmojiPicker::tooltipFor(size_t index) const
{
    const DPEmojiEntry* entry = entryAt(index);
    return entry ? entry->short_code : "";
}

void DPEmojiPicker::rebuildGrid()
{
    if (!mGrid) return;
    std::vector<size_t> entries;
    std::string needle = mSearch ? mSearch->getText() : "";
    LLStringUtil::toLower(needle);
    for (size_t i = 0; i < sDictionary.size(); ++i)
    {
        const DPEmojiEntry& item = sDictionary[i];
        if (!needle.empty())
        {
            if (item.short_code.find(needle) == std::string::npos)
                continue;
        }
        else if (mCategory == 0)
        {
            if (std::find(sRecent.begin(), sRecent.end(), item.character) ==
                sRecent.end()) continue;
        }
        else if (mCategory >= 2 && mCategory <= 8)
        {
            const std::string required = sEmojiCategories[mCategory];
            const bool in_category =
                std::find(item.categories.begin(), item.categories.end(),
                          required) != item.categories.end();
            const bool is_people = mCategory == 2 &&
                std::find(item.categories.begin(), item.categories.end(),
                          "people & body") != item.categories.end();
            if (!in_category && !is_people) continue;
        }
        else if (mCategory == 9 && !item.categories.empty())
        {
            continue;
        }
        entries.push_back(i);
    }
    if (mCategory == 0 && needle.empty())
    {
        std::stable_sort(entries.begin(), entries.end(), [](size_t lhs, size_t rhs)
        {
            const auto left = std::find(sRecent.begin(), sRecent.end(),
                                        sDictionary[lhs].character);
            const auto right = std::find(sRecent.begin(), sRecent.end(),
                                         sDictionary[rhs].character);
            return left < right;
        });
    }
    mGrid->update(entries);
    if (mScroll) mScroll->goToTop();
    if (mPreview)
        mPreview->setText(llformat("%d emojis", (S32)entries.size()));
}

void DPEmojiPicker::insertEmoji(size_t index)
{
    const DPEmojiEntry* entry = entryAt(index);
    if (!entry) return;
    LLView* view = mInputHandle.get();
    if (view)
    {
        LLLineEditor* editor = static_cast<LLLineEditor*>(view);
        bool replaced = false;
        if (mLiveSuggestions && mLiveStart >= 0 && mLiveStart < mLiveEnd &&
            editor->getCursor() == mLiveEnd)
        {
            LLWString text = editor->getWText();
            if (mLiveEnd <= (S32)text.size() && text[mLiveStart] == L':')
            {
                text.replace(mLiveStart, mLiveEnd - mLiveStart,
                             LLWString(1, entry->character));
                editor->setText(wstring_to_utf8str(text));
                editor->setCursor(mLiveStart + 1);
                replaced = true;
            }
        }
        if (!replaced)
        {
            editor->insert(wstring_to_utf8str(LLWString(1, entry->character)),
                           editor->getCursor());
        }
        editor->setFocus(true);
    }
    if (mLiveSuggestions) hideLiveSuggestions();
    sRecent.erase(std::remove(sRecent.begin(), sRecent.end(),
                               entry->character), sRecent.end());
    sRecent.insert(sRecent.begin(), entry->character);
    if (sRecent.size() > 60) sRecent.resize(60);
    saveRecent();
    if (mCategory == 0) rebuildGrid();
}

// Firestorm-style emoji autocomplete using Build 192's existing emoji
// definitions and picker; the caller is solely LLChatBar's chat input.
void DPEmojiPicker::hideLiveSuggestions(LLLineEditor* editor)
{
    if (!sPicker || !sPicker->mLiveSuggestions) return;
    if (editor && sPicker->mInputHandle.get() != editor) return;
    sPicker->mLiveSuggestions = false;
    sPicker->mLiveMatches.clear();
    sPicker->mLiveStart = sPicker->mLiveEnd = -1;
    sPicker->setVisible(false);
}

bool DPEmojiPicker::handleLiveSuggestionKey(LLLineEditor* editor,
                                            KEY key, MASK mask)
{
    if (!editor || mask != MASK_NONE || !sPicker ||
        !sPicker->mLiveSuggestions || sPicker->mInputHandle.get() != editor)
        return false;

    if (key == KEY_ESCAPE)
    {
        hideLiveSuggestions(editor);
        return true;
    }
    const size_t count = sPicker->mLiveMatches.size();
    if (!count) return false;
    if (key == KEY_UP || key == KEY_DOWN)
    {
        sPicker->mLiveSelected = (sPicker->mLiveSelected +
            (key == KEY_DOWN ? 1 : count - 1)) % count;
        if (sPicker->mPreview)
        {
            sPicker->mPreview->setText(
                sDictionary[sPicker->mLiveMatches[sPicker->mLiveSelected]].short_code);
        }
        return true;
    }
    if (key == KEY_TAB || key == KEY_RETURN)
    {
        sPicker->insertEmoji(sPicker->mLiveMatches[sPicker->mLiveSelected]);
        return true;
    }
    return false;
}

void DPEmojiPicker::updateLiveSuggestions(LLLineEditor* editor)
{
    if (!editor) return;
    const LLWString text = editor->getWText();
    const S32 cursor = editor->getCursor();
    if (cursor < 2 || cursor > (S32)text.size())
    {
        hideLiveSuggestions(editor);
        return;
    }
    auto isCodeChar = [](llwchar ch) -> bool
    {
        return (ch >= L'a' && ch <= L'z') ||
               (ch >= L'A' && ch <= L'Z') ||
               (ch >= L'0' && ch <= L'9') ||
               ch == L'_' || ch == L'-' || ch == L'+';
    };

    S32 start = cursor - 1;
    const bool closing_colon = text[start] == L':';
    if (closing_colon) --start;
    while (start >= 0 && isCodeChar(text[start])) --start;
    if (start < 0 || text[start] != L':' ||
        cursor - start > 55 || cursor - start < 3 ||
        (start > 0 && text[start - 1] >= L'0' && text[start - 1] <= L'9'))
    {
        hideLiveSuggestions(editor);
        return;
    }

    std::string prefix = wstring_to_utf8str(text.substr(start, cursor - start));
    LLStringUtil::toLower(prefix);
    if (closing_colon)
    {
        // Insertion occurs immediately when a full :emoji_shortcode: is typed.
        const std::string replacement = expandShortcodes(prefix);
        if (replacement != prefix)
        {
            LLWString next(text);
            const LLWString emoji = utf8str_to_wstring(replacement);
            next.replace(start, cursor - start, emoji);
            editor->setText(wstring_to_utf8str(next));
            editor->setCursor(start + (S32)emoji.size());
        }
        hideLiveSuggestions(editor);
        return;
    }

    if (!sPicker) sPicker = new DPEmojiPicker();
    sPicker->mLiveMatches.clear();
    for (size_t i = 0; i < sDictionary.size(); ++i)
    {
        if (sDictionary[i].short_code.compare(0, prefix.size(), prefix) == 0)
        {
            sPicker->mLiveMatches.push_back(i);
            if (sPicker->mLiveMatches.size() == 30) break;
        }
    }
    if (sPicker->mLiveMatches.empty())
    {
        hideLiveSuggestions(editor);
        return;
    }
    sPicker->mInputHandle = editor->getHandle();
    sPicker->mLiveSuggestions = true;
    sPicker->mLiveStart = start;
    sPicker->mLiveEnd = cursor;
    sPicker->mLiveSelected = 0;
    if (sPicker->mGrid) sPicker->mGrid->update(sPicker->mLiveMatches);
    if (sPicker->mScroll) sPicker->mScroll->goToTop();
    if (sPicker->mPreview)
        sPicker->mPreview->setText(
            sDictionary[sPicker->mLiveMatches.front()].short_code);
    if (!sPicker->getVisible())
    {
        sPicker->open();
        editor->setFocus(true); // Suggestions must not take chat-input focus.
    }
}

std::string DPEmojiPicker::expandShortcodes(const std::string& input)
{
    loadDictionary();
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); )
    {
        if (input[i] == ':')
        {
            size_t end = input.find(':', i + 1);
            if (end != std::string::npos && end - i <= 55)
            {
                std::string code = input.substr(i, end - i + 1);
                LLStringUtil::toLower(code);
                std::map<std::string, std::string>::const_iterator found =
                    sEmojiShortCodes.find(code);
                if (found != sEmojiShortCodes.end())
                {
                    result += found->second;
                    i = end + 1;
                    continue;
                }
            }
        }
        result += input[i++];
    }
    return result;
}

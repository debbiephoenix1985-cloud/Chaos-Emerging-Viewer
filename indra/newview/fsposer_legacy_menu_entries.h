// Firestorm 7.2.5 menu_fs_poser_poses_btn.xml exact actions & labels.
#pragma once
struct FSPoserLegacyMenuEntry {const char* name;const char* label;const char* parameter;bool separator;};
static const FSPoserLegacyMenuEntry FS_POSER_LEGACY_MENU_ENTRIES[] = {
    {"load_selective", "Load Selective", "selective", false},
    {"rotations_positions", "Load Selective - Rotations Only", "selective_rot", false},
    {"", "", "", true},
    {"rotations", "Load - Rotations Only", "rotation", false},
    {"load_all", "Load", "all", false},
};

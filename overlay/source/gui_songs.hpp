#pragma once
// Library-driven browser (same rule as snag-ctl): only library entries
// whose audio file is on disk, shown with real names. On-disk orphans
// (caches) are deliberately hidden.
#include <tesla.hpp>
#include <dirent.h>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "library_lookup.hpp"
#include "../../sys-snag/include/snag_client.hpp"

struct SongEntry {
    std::string title;
    std::string artist;
    std::string path;
};

static bool HasAudioExt(const char* name)
{
    const size_t len = std::strlen(name);
    return len > 4 && (std::strcmp(name + len - 5, ".opus") == 0 ||
                       std::strcmp(name + len - 5, ".webm") == 0 ||
                       std::strcmp(name + len - 4, ".ogg") == 0 ||
                       std::strcmp(name + len - 4, ".m4a") == 0);
}

class GuiSongList : public tsl::Gui {
public:
    GuiSongList()
    {
        ScanSongs();
    }

    virtual tsl::elm::Element* createUI() override
    {
        auto* frame = new tsl::elm::OverlayFrame("Library", "StreamSnagNX Tracks");
        auto* list = new tsl::elm::List();

        if (songs_.empty())
        {
            list->addItem(new tsl::elm::CategoryHeader("No Songs Found"));
            list->addItem(new tsl::elm::ListItem("Download in StreamSnagNX first"));
        }
        else
        {
            list->addItem(new tsl::elm::CategoryHeader(std::to_string(songs_.size()) + " Songs Available"));
            for (const auto& song : songs_)
            {
                auto* item = new tsl::elm::ListItem(song.title);
                item->setClickListener([song](u64 keys) {
                    if (keys & HidNpadButton_A)
                    {
                        SnagClient::Instance().PlayFile(song.path.c_str(),
                            song.title.c_str(), song.artist.c_str());
                        tsl::goBack(); // one screen back to the player
                        return true;
                    }
                    return false;
                });
                list->addItem(item);
            }
        }

        frame->setContent(list);
        return frame;
    }

private:
    std::vector<SongEntry> songs_;

    void ScanSongs()
    {
        songs_.clear();
        ssnx::LibraryDb db = ssnx::LibraryDb::Load();

        DIR* dir = opendir("sdmc:/switch/StreamSnagNX/music");
        if (!dir)
            return;
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr)
        {
            if (ent->d_name[0] == '.')
                continue;
            if (!HasAudioExt(ent->d_name))
                continue;
            std::string vid = ssnx::VideoIdFromPath(ent->d_name);
            std::string title, artist;
            if (!db.lookup(vid, title, artist) || title.empty())
                continue; // not in library: hidden cache/orphan
            SongEntry e;
            e.title = title;
            e.artist = artist;
            e.path = std::string("sdmc:/switch/StreamSnagNX/music/") + ent->d_name;
            songs_.push_back(e);
        }
        closedir(dir);
        std::sort(songs_.begin(), songs_.end(),
                  [](const SongEntry& a, const SongEntry& b) { return a.title < b.title; });
    }
};

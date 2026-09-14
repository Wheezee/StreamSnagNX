#pragma once
// Tiny library.json reader for the overlay (no JSON lib linked here).
// Same approach as snag-ctl: find "video_id":"<id>", parse the surrounding
// object for title/artist(/subtitle). Works whether library.json is a bare
// array or an object containing one.
#include <string>
#include <cstdio>

namespace ssnx {

// Whitespace-tolerant "key" : "value" finder. library.json is written with
// JSON_INDENT(2), so `"key":"value"` never appears verbatim.
inline bool JsonFindString(const std::string& json, const char* key, size_t from,
                           std::string& out_val, size_t& out_end, size_t& out_keypos)
{
    std::string qk = std::string("\"") + key + "\"";
    size_t search = from;
    while ((search = json.find(qk, search)) != std::string::npos)
    {
        out_keypos = search;
        size_t p = search + qk.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r'))
            p++;
        if (p >= json.size() || json[p] != ':')
        {
            search += qk.size();
            continue;
        }
        p++;
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r'))
            p++;
        if (p >= json.size() || json[p] != '"')
        {
            search += qk.size();
            continue;
        }
        p++;
        std::string val;
        while (p < json.size() && json[p] != '"')
        {
            if (json[p] == '\\' && p + 1 < json.size())
            {
                p++;
                if (json[p] == 'n')      val += '\n';
                else if (json[p] == 't') val += '\t';
                else                     val += json[p];
            }
            else
            {
                val += json[p];
            }
            p++;
        }
        if (p >= json.size())
            return false;
        out_val = val;
        out_end = p + 1;
        return true;
    }
    return false;
}

// "sdmc:/.../QoQt4_G6DsI.opus" -> "QoQt4_G6DsI"
inline std::string VideoIdFromPath(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.rfind('.');
    if (dot != std::string::npos)
        base = base.substr(0, dot);
    return base;
}

class LibraryDb {
public:
    static LibraryDb Load()
    {
        LibraryDb db;
        FILE* fp = std::fopen("sdmc:/switch/StreamSnagNX/library.json", "rb");
        if (!fp)
            return db;
        std::fseek(fp, 0, SEEK_END);
        long sz = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (sz > 0 && sz <= 512 * 1024)
        {
            db.data_.resize(static_cast<size_t>(sz));
            size_t got = std::fread(&db.data_[0], 1, static_cast<size_t>(sz), fp);
            db.data_.resize(got);
        }
        std::fclose(fp);
        return db;
    }

    bool lookup(const std::string& video_id, std::string& out_title, std::string& out_artist) const
    {
        if (video_id.empty() || data_.empty())
            return false;
        std::string id, dummy;
        size_t id_end = 0, keypos = 0, search = 0, e1 = 0, e2 = 0;
        while (JsonFindString(data_, "video_id", search, id, id_end, keypos))
        {
            search = id_end;
            if (id != video_id)
                continue;
            size_t obj_start = data_.rfind('{', keypos);
            if (obj_start == std::string::npos)
                return false;
            size_t obj_end = data_.find('}', id_end);
            if (obj_end == std::string::npos)
                return false;
            std::string obj = data_.substr(obj_start, obj_end - obj_start + 1);
            JsonFindString(obj, "title", 0, out_title, e1, e2);
            JsonFindString(obj, "artist", 0, out_artist, e1, e2);
            if (out_artist.empty())
                JsonFindString(obj, "subtitle", 0, out_artist, e1, e2);
            return !out_title.empty();
        }
        return false;
    }

private:
    std::string data_;
};

} // namespace ssnx

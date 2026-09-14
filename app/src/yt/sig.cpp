#include "sig.hpp"
#include "../util/log.hpp"

#include <curl/curl.h>

extern "C" {
#include <quickjs.h>
}

#include <cstdio>
#include <mutex>
#include <optional>
#include <regex>
#include <string>

namespace ssnx::yt
{
namespace
{

constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36";

std::mutex g_sig_mutex;
std::string g_cached_player_js_url;
std::string g_cached_function_body;

void LogSig(const std::string& line)
{
    ssnx::Log(line);
}

size_t WriteToStringCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::optional<std::string> HttpGet(const std::string& url)
{
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl)
        return std::nullopt;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || http_code != 200 || response.empty())
        return std::nullopt;

    return response;
}

std::optional<std::string> FindQueryValue(const std::string& url, const std::string& key)
{
    const std::string pattern = key + "=";
    size_t search_from = 0;
    while (true)
    {
        const size_t pos = url.find(pattern, search_from);
        if (pos == std::string::npos)
            return std::nullopt;

        if (pos == 0 || url[pos - 1] == '?' || url[pos - 1] == '&')
        {
            const size_t value_start = pos + pattern.size();
            const size_t value_end = url.find_first_of("&#", value_start);
            return url.substr(value_start,
                              value_end == std::string::npos ? std::string::npos
                                                             : value_end - value_start);
        }
        search_from = pos + 1;
    }
}

std::string ReplaceQueryValue(const std::string& url, const std::string& key,
                              const std::string& new_value)
{
    const std::string pattern = key + "=";
    size_t search_from = 0;
    while (true)
    {
        const size_t pos = url.find(pattern, search_from);
        if (pos == std::string::npos)
            return url;

        if (pos == 0 || url[pos - 1] == '?' || url[pos - 1] == '&')
        {
            const size_t value_start = pos + pattern.size();
            const size_t value_end = url.find_first_of("&#", value_start);
            const size_t len = value_end == std::string::npos ? std::string::npos
                                                              : value_end - value_start;
            std::string result = url;
            result.replace(value_start, len, new_value);
            return result;
        }
        search_from = pos + 1;
    }
}

std::optional<std::string> FetchPlayerJsUrl(const std::string& video_id)
{
    const std::string embed_url = "https://www.youtube.com/embed/" + video_id;
    const auto page = HttpGet(embed_url);
    if (!page.has_value())
    {
        LogSig("throttle: failed to fetch embed page for " + video_id);
        return std::nullopt;
    }

    static const std::regex kPlayerJsPattern(R"xxx("jsUrl"\s*:\s*"(/s/player/[^"]+/base\.js)")xxx");
    std::smatch match;
    if (std::regex_search(*page, match, kPlayerJsPattern))
        return "https://www.youtube.com" + match[1].str();

    static const std::regex kPlayerJsPattern2(R"xxx(src="(/s/player/[^"]+/base\.js)")xxx");
    if (std::regex_search(*page, match, kPlayerJsPattern2))
        return "https://www.youtube.com" + match[1].str();

    LogSig("throttle: player JS URL not found in embed page");
    return std::nullopt;
}

std::optional<std::string> ExtractThrottleFunction(const std::string& player_js)
{
    // Pattern A: Modern caller format
    // &&(b=a.get("n"))&&(b=FUNCNAME(b),a.set("n",b))
    {
        static const std::regex kCallerPattern(
            R"(&&\(b=a\.get\("n"\)\)&&\(b=([a-zA-Z0-9$_]+)(?:\[(\d+)\])?\(b\))");
        std::smatch caller_match;
        if (std::regex_search(player_js, caller_match, kCallerPattern))
        {
            std::string func_name = caller_match[1].str();

            if (caller_match[2].matched)
            {
                // Array reference: funcArray[index](b)
                const std::string array_pattern_str =
                    "var " + std::regex_replace(func_name, std::regex(R"(\$)"), R"(\$)") +
                    R"(\s*=\s*\[([a-zA-Z0-9$_]+(?:\s*,\s*[a-zA-Z0-9$_]+)*)\])";
                std::regex array_pattern(array_pattern_str);
                std::smatch array_match;
                if (std::regex_search(player_js, array_match, array_pattern))
                {
                    const int index = std::stoi(caller_match[2].str());
                    std::string elements = array_match[1].str();
                    int current = 0;
                    size_t pos = 0;
                    while (current < index)
                    {
                        pos = elements.find(',', pos);
                        if (pos == std::string::npos)
                            break;
                        ++pos;
                        ++current;
                    }
                    if (current == index)
                    {
                        size_t end = elements.find(',', pos);
                        func_name = elements.substr(
                            pos, end == std::string::npos ? std::string::npos : end - pos);
                        func_name.erase(0, func_name.find_first_not_of(" \t"));
                        func_name.erase(func_name.find_last_not_of(" \t") + 1);
                    }
                }
            }

            LogSig("throttle: found n-transform function name: " + func_name);

            const std::string escaped_name =
                std::regex_replace(func_name, std::regex(R"(\$)"), R"(\$)");
            const std::string body_pattern_str =
                escaped_name + R"(\s*=\s*function\s*\([a-zA-Z0-9_$]\))";
            std::regex body_start_pattern(body_pattern_str);
            std::smatch body_start_match;
            if (std::regex_search(player_js, body_start_match, body_start_pattern))
            {
                const size_t func_keyword =
                    player_js.find("function", body_start_match.position());
                if (func_keyword != std::string::npos)
                {
                    const size_t open_brace = player_js.find('{', func_keyword);
                    if (open_brace != std::string::npos)
                    {
                        int depth = 1;
                        size_t pos = open_brace + 1;
                        while (pos < player_js.size() && depth > 0)
                        {
                            if (player_js[pos] == '{')
                                ++depth;
                            else if (player_js[pos] == '}')
                                --depth;
                            ++pos;
                        }
                        if (depth == 0)
                        {
                            const std::string full_func =
                                player_js.substr(func_keyword, pos - func_keyword);
                            LogSig("throttle: extracted function body (" +
                                   std::to_string(full_func.size()) + " bytes)");
                            return full_func;
                        }
                    }
                }
            }
        }
    }

    // Pattern B: Direct function pattern
    // function FUNCNAME(a){var b=a.split("")...b.join("")}
    {
        static const std::regex kDirectPattern(
            R"(\b([a-zA-Z0-9$_]+)\s*=\s*function\s*\(\s*[a-zA-Z]\s*\)\s*\{)"
            R"(\s*var\s+[a-zA-Z]\s*=\s*[a-zA-Z]\s*\.\s*split\s*\(\s*""\s*\))");
        std::smatch direct_match;
        std::string::const_iterator search_start = player_js.cbegin();
        while (std::regex_search(search_start, player_js.cend(), direct_match, kDirectPattern))
        {
            const size_t func_pos =
                direct_match.position() + std::distance(player_js.cbegin(), search_start);
            const size_t func_keyword = player_js.find("function", func_pos);
            if (func_keyword != std::string::npos)
            {
                const size_t open_brace = player_js.find('{', func_keyword);
                if (open_brace != std::string::npos)
                {
                    int depth = 1;
                    size_t pos = open_brace + 1;
                    while (pos < player_js.size() && depth > 0)
                    {
                        if (player_js[pos] == '{')
                            ++depth;
                        else if (player_js[pos] == '}')
                            --depth;
                        ++pos;
                    }
                    if (depth == 0)
                    {
                        const std::string body =
                            player_js.substr(func_keyword, pos - func_keyword);
                        if (body.find("join(\"\")") != std::string::npos ||
                            body.find("join('')") != std::string::npos)
                        {
                            LogSig("throttle: extracted via direct pattern (" +
                                   std::to_string(body.size()) + " bytes)");
                            return body;
                        }
                    }
                }
            }
            search_start = direct_match.suffix().first;
        }
    }

    return std::nullopt;
}

bool EnsureFunctionLoaded(const std::string& video_id)
{
    if (!g_cached_function_body.empty())
        return true;

    const auto player_js_url = FetchPlayerJsUrl(video_id);
    if (!player_js_url.has_value())
        return false;

    if (*player_js_url == g_cached_player_js_url && !g_cached_function_body.empty())
        return true;

    const auto player_js = HttpGet(*player_js_url);
    if (!player_js.has_value())
    {
        LogSig("throttle: failed to fetch player JS: " + *player_js_url);
        return false;
    }

    const auto func_body = ExtractThrottleFunction(*player_js);
    if (!func_body.has_value())
    {
        LogSig("throttle: failed to extract throttle function from player JS");
        return false;
    }

    g_cached_player_js_url = *player_js_url;
    g_cached_function_body = *func_body;
    LogSig("throttle: cached n transform function (" +
           std::to_string(g_cached_function_body.size()) + " bytes) from " + *player_js_url);
    return true;
}

std::optional<std::string> TransformN(const std::string& n_value)
{
    JSRuntime* rt = JS_NewRuntime();
    if (!rt)
        return std::nullopt;

    JS_SetMemoryLimit(rt, 64 * 1024 * 1024);
    JS_SetMaxStackSize(rt, 1024 * 1024);

    JSContext* ctx = JS_NewContext(rt);
    if (!ctx)
    {
        JS_FreeRuntime(rt);
        return std::nullopt;
    }

    const std::string script = "(" + g_cached_function_body + ")(\"" + n_value + "\")";

    JSValue result =
        JS_Eval(ctx, script.c_str(), script.size(), "<throttle>", JS_EVAL_TYPE_GLOBAL);

    std::optional<std::string> output;
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(ctx);
        const char* err_str = JS_ToCString(ctx, exception);
        if (err_str)
        {
            LogSig(std::string("throttle: QuickJS exception: ") + err_str);
            JS_FreeCString(ctx, err_str);
        }
        JS_FreeValue(ctx, exception);
    }
    else
    {
        const char* str = JS_ToCString(ctx, result);
        if (str)
        {
            output = std::string(str);
            JS_FreeCString(ctx, str);
        }
    }

    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return output;
}

} // namespace

std::string SolveN(const std::string& url, const std::string& video_id)
{
    const auto n_value = FindQueryValue(url, "n");
    if (!n_value.has_value() || n_value->empty())
        return url;

    std::string vid = video_id;
    if (vid.empty())
    {
        const auto id_param = FindQueryValue(url, "id");
        if (id_param.has_value())
            vid = *id_param;
    }

    std::lock_guard<std::mutex> lock(g_sig_mutex);

    if (!EnsureFunctionLoaded(vid))
    {
        LogSig("throttle: failed to load n transform for video=" + vid);
        return url;
    }

    const auto transformed = TransformN(*n_value);
    if (!transformed.has_value() || transformed->empty())
    {
        LogSig("throttle: n transform failed for n=" + *n_value);
        return url;
    }

    if (*transformed == *n_value)
        return url;

    LogSig("throttle: n transformed " + *n_value + " -> " + *transformed);
    return ReplaceQueryValue(url, "n", *transformed);
}

void InvalidateSigCache()
{
    std::lock_guard<std::mutex> lock(g_sig_mutex);
    g_cached_player_js_url.clear();
    g_cached_function_body.clear();
}

} // namespace ssnx::yt

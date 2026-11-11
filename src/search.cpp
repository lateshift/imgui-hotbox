#include "search.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace hotbox::detail {
namespace {

struct Folded {
    char32_t ch;
    bool start; // First character of a word.
};

// Base letters for U+00C0–U+00FF. '*' keeps the original character (× and ÷).
constexpr std::string_view latin1 = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTsaaaaaaaceeeeiiiidnooooo*ouuuuyty";
// Base letters for U+0100–U+017F (Latin Extended-A).
constexpr std::string_view latin_a =
    "AaAaAa" "CcCcCcCc" "DdDd" "EeEeEeEeEe" "GgGgGgGg" "HhHh" "IiIiIiIiIi" "Ii" "Jj" "Kkk"
    "LlLlLlLlLl" "NnNnNnnNn" "OoOoOo" "Oo" "RrRrRr" "SsSsSsSs" "TtTtTt" "UuUuUuUuUuUu" "Ww" "YyY"
    "ZzZzZz" "s";
static_assert(latin1.size() == 0x40);
static_assert(latin_a.size() == 0x80);

char32_t Decode(std::string_view text, std::size_t& i) {
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned lead = byte(i);
    const int length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || i + static_cast<std::size_t>(length) > text.size()) {
        ++i;
        return 0xFFFD;
    }
    char32_t value = length == 1 ? lead : lead & (0x7Fu >> length);
    for (int k = 1; k < length; ++k) {
        const unsigned next = byte(i + static_cast<std::size_t>(k));
        if ((next & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        value = (value << 6) | (next & 0x3F);
    }
    i += static_cast<std::size_t>(length);
    return value;
}

/// Base letter and case of a character. Non-letters return themselves.
std::pair<char32_t, bool> Base(char32_t ch) {
    char letter = 0;
    if (ch >= 'A' && ch <= 'Z')
        letter = static_cast<char>(ch);
    else if (ch >= 'a' && ch <= 'z')
        letter = static_cast<char>(ch);
    else if (ch >= 0xC0 && ch <= 0xFF && latin1[ch - 0xC0] != '*')
        letter = latin1[ch - 0xC0];
    else if (ch >= 0x100 && ch <= 0x17F)
        letter = latin_a[ch - 0x100];
    if (!letter)
        return {ch, false};
    const bool upper = letter >= 'A' && letter <= 'Z';
    return {static_cast<char32_t>(upper ? letter - 'A' + 'a' : letter), upper};
}

bool WordCharacter(char32_t ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch >= 0x80;
}

void Fold(std::string_view text, std::vector<Folded>& out) {
    out.clear();
    bool previous_word = false, previous_lower = false;
    for (std::size_t i = 0; i < text.size();) {
        const auto [ch, upper] = Base(Decode(text, i));
        const bool word = WordCharacter(ch);
        out.push_back({ch, word && (!previous_word || (upper && previous_lower))});
        previous_word = word;
        previous_lower = word && !upper && ch >= 'a' && ch <= 'z';
    }
}

float Subsequence(const std::vector<Folded>& query, const std::vector<Folded>& text, bool prefer_starts) {
    std::size_t at = 0, starts = 0, adjacent = 0, last = 0;
    for (std::size_t q = 0; q < query.size(); ++q) {
        std::size_t found = text.size();
        for (std::size_t i = at; i < text.size(); ++i) {
            if (text[i].ch != query[q].ch)
                continue;
            if (found == text.size())
                found = i;
            if (!prefer_starts || text[i].start) {
                found = i;
                break;
            }
        }
        if (found == text.size())
            return 0;
        starts += text[found].start;
        adjacent += q > 0 && found == last + 1;
        last = found;
        at = found + 1;
    }
    const float n = static_cast<float>(query.size());
    return 0.25f + 0.3f * static_cast<float>(starts) / n +
        (query.size() > 1 ? 0.1f * static_cast<float>(adjacent) / (n - 1) : 0.1f);
}

thread_local std::vector<Folded> folded_query, folded_text;

} // namespace

std::optional<float> MatchScore(std::string_view token, std::string_view text) {
    auto& query = folded_query;
    auto& target = folded_text;
    Fold(token, query);
    Fold(text, target);
    if (query.empty())
        return 1.0f;
    if (query.size() > target.size())
        return std::nullopt;
    float best = 0;
    for (std::size_t i = 0; i + query.size() <= target.size(); ++i) {
        const bool same = std::equal(query.begin(), query.end(), target.begin() + static_cast<std::ptrdiff_t>(i),
            [](const Folded& a, const Folded& b) { return a.ch == b.ch; });
        if (same)
            best = std::max(best, i == 0 ? 1.0f : target[i].start ? 0.9f : 0.7f);
    }
    if (best == 0) {
        // A word-start-first walk can skip characters a plain walk needs, so keep the better one.
        best = std::max(Subsequence(query, target, true), Subsequence(query, target, false));
        if (best == 0)
            return std::nullopt;
    }
    const float extra = static_cast<float>(target.size() - query.size());
    return best * (1 - 0.15f * std::min(1.0f, extra / 32));
}

History::Uses* History::find(std::string_view id) {
    const auto match = std::ranges::find(uses_, id, &Uses::id);
    return match == uses_.end() ? nullptr : &*match;
}

const History::Uses* History::find(std::string_view id) const {
    const auto match = std::ranges::find(uses_, id, &Uses::id);
    return match == uses_.end() ? nullptr : &*match;
}

void History::record(std::string_view id, std::int64_t now) {
    if (id.empty())
        return;
    const auto existing = std::ranges::find(recent_, id);
    if (existing != recent_.end())
        recent_.erase(existing);
    recent_.insert(recent_.begin(), CommandId(id));
    if (recent_.size() > recent_limit)
        recent_.resize(recent_limit);

    auto* uses = find(id);
    if (!uses) {
        if (uses_.size() >= command_limit) {
            const auto weakest = std::ranges::min_element(uses_, {},
                [&](const Uses& entry) { return frecency(entry.id, now); });
            uses_.erase(weakest);
        }
        uses = &uses_.emplace_back(Uses{CommandId(id), {}});
    }
    uses->times.insert(std::ranges::upper_bound(uses->times, now), now);
    if (uses->times.size() > uses_per_command)
        uses->times.erase(uses->times.begin());
}

float History::frecency(std::string_view id, std::int64_t now) const {
    const auto* uses = find(id);
    if (!uses)
        return 0;
    constexpr std::int64_t hour = 3600, day = 24 * hour, week = 7 * day;
    float score = 0;
    for (const auto time : uses->times) {
        const auto age = now - time;
        score += age <= 4 * hour ? 4.0f : age <= day ? 2.0f : age <= week ? 1.0f : 0.5f;
    }
    return score;
}

void History::clear() {
    recent_.clear();
    uses_.clear();
}

void History::write(std::string& out) const {
    for (const auto& id : recent_) {
        out += "Recent=";
        out += id;
        out += '\n';
    }
    for (const auto& uses : uses_) {
        out += "Use=";
        for (std::size_t i = 0; i < uses.times.size(); ++i) {
            if (i)
                out += ',';
            out += std::to_string(uses.times[i]);
        }
        out += ' ';
        out += uses.id;
        out += '\n';
    }
}

bool History::read(std::string_view line) {
    constexpr std::string_view recent_key = "Recent=", use_key = "Use=";
    if (line.starts_with(recent_key)) {
        const auto id = line.substr(recent_key.size());
        if (!id.empty() && recent_.size() < recent_limit && std::ranges::find(recent_, id) == recent_.end())
            recent_.emplace_back(id);
        return true;
    }
    if (!line.starts_with(use_key))
        return false;
    line.remove_prefix(use_key.size());
    const auto space = line.find(' ');
    if (space == std::string_view::npos || space + 1 >= line.size())
        return true;
    const auto id = line.substr(space + 1);
    if (find(id) || uses_.size() >= command_limit)
        return true;
    Uses uses{CommandId(id), {}};
    auto times = line.substr(0, space);
    while (!times.empty()) {
        const auto comma = times.find(',');
        const auto field = times.substr(0, comma);
        std::int64_t value = 0;
        const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value);
        if (error == std::errc() && end == field.data() + field.size())
            uses.times.push_back(value);
        times = comma == std::string_view::npos ? std::string_view{} : times.substr(comma + 1);
    }
    std::ranges::sort(uses.times);
    if (uses.times.size() > uses_per_command)
        uses.times.erase(uses.times.begin(), uses.times.end() - static_cast<std::ptrdiff_t>(uses_per_command));
    if (!uses.times.empty())
        uses_.push_back(std::move(uses));
    return true;
}

void RankSearch(std::span<const SearchEntry> entries, std::string_view query, const History& history,
    std::int64_t now, std::size_t limit, std::vector<SearchHit>& hits) {
    hits.clear();
    std::vector<std::string_view> tokens;
    for (std::size_t start = 0; start < query.size();) {
        const auto end = std::min(query.find(' ', start), query.size());
        if (end > start)
            tokens.push_back(query.substr(start, end - start));
        start = end + 1;
    }
    if (tokens.empty() || limit == 0)
        return;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        float total = 0;
        bool matched = true;
        for (const auto token : tokens) {
            float best = 0;
            const auto consider = [&](std::string_view text, float weight) {
                if (const auto score = text.empty() ? std::nullopt : MatchScore(token, text))
                    best = std::max(best, *score * weight);
            };
            consider(entry.label, 1.0f);
            consider(entry.alt_text, 0.85f);
            for (const auto& keyword : entry.keywords)
                consider(keyword, 0.8f);
            consider(entry.path, 0.6f);
            if (best == 0) {
                matched = false;
                break;
            }
            total += best;
        }
        if (!matched)
            continue;
        const float quality = total / static_cast<float>(tokens.size());
        const float boost = 1 + 0.15f * std::log2(1 + history.frecency(entry.id, now));
        hits.push_back({index, quality, quality * boost});
    }
    // Scattered letters are noise next to a prefix or word match, so they show only when nothing
    // matches at least half as well.
    float best = 0;
    for (const auto& hit : hits)
        best = std::max(best, hit.quality);
    std::erase_if(hits, [&](const SearchHit& hit) { return hit.quality < best * 0.5f; });
    const auto order = [&](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        const auto& left = entries[a.entry];
        const auto& right = entries[b.entry];
        return left.label != right.label ? left.label < right.label : left.id < right.id;
    };
    const auto keep = std::min(limit, hits.size());
    std::partial_sort(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(keep), hits.end(), order);
    hits.resize(keep);
}

} // namespace hotbox::detail

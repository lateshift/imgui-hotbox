#pragma once

#include <hotbox/item.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Search and command history. Pure logic: no ImGui calls.
namespace hotbox::detail {

/// How well `token` matches `text`, in (0, 1], or nothing when it is not a subsequence.
/// Matching ignores case and folds Latin diacritics, so "cafe" matches "Café".
std::optional<float> MatchScore(std::string_view token, std::string_view text);

struct SearchEntry {
    std::string_view id, label, alt_text, path;
    std::span<const std::string> keywords;
};

/// Recently used commands and their use times, in seconds since the Unix epoch.
class History {
public:
    static constexpr std::size_t recent_limit = 8;
    static constexpr std::size_t uses_per_command = 10;
    static constexpr std::size_t command_limit = 256;

    void record(std::string_view id, std::int64_t now);
    /// Sum of age weights: ≤ 4 h ×4, ≤ 1 day ×2, ≤ 1 week ×1, older ×0.5.
    float frecency(std::string_view id, std::int64_t now) const;
    /// Most recent first.
    std::span<const CommandId> recent() const { return recent_; }
    bool empty() const { return recent_.empty() && uses_.empty(); }
    void clear();

    /// Append `Recent=` and `Use=` lines.
    void write(std::string& out) const;
    /// Read one line written by write(). Returns false for lines it does not own.
    bool read(std::string_view line);

private:
    struct Uses {
        CommandId id;
        std::vector<std::int64_t> times; // Oldest first.
    };
    Uses* find(std::string_view id);
    const Uses* find(std::string_view id) const;

    std::vector<CommandId> recent_;
    std::vector<Uses> uses_;
};

struct SearchHit {
    std::size_t entry;
    float quality; // Match quality alone.
    float score;   // Quality boosted by frecency; the ranking key.
};

/// Rank `entries` for `query`. Every space-separated token must match the label, alt text,
/// a keyword or the menu path. Match quality is boosted by frecency. Writes at most `limit` hits.
void RankSearch(std::span<const SearchEntry> entries, std::string_view query, const History& history,
    std::int64_t now, std::size_t limit, std::vector<SearchHit>& hits);

} // namespace hotbox::detail

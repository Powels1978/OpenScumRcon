#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace openscumrcon
{
// Input is UTF-8. Never cut the continuation bytes of a code point.
inline std::size_t utf8_prefix_size(std::string_view text, std::size_t limit)
{
    auto size = std::min(text.size(), limit);
    while (size && size < text.size() && (static_cast<unsigned char>(text[size]) & 0xc0) == 0x80) --size;
    return size;
}
class ResponseScope
{
    inline static thread_local ResponseScope* current = nullptr;
    ResponseScope* previous;
    const void* command;
    std::string output;
    std::size_t count = 0;
    bool cut = false, failed = false;
public:
    static constexpr std::size_t max_bytes = 65536, max_messages = 128;
    explicit ResponseScope(const void* key) : previous(current), command(key) { current = this; }
    ~ResponseScope() { current = previous; }
    ResponseScope(const ResponseScope&) = delete;
    ResponseScope& operator=(const ResponseScope&) = delete;
    static bool matches(const void* key) { return key && current && current->command == key; }
    static void failure() noexcept { if (current) current->failed = true; }
    static bool accept(const void* key, std::string_view text) noexcept
    {
        if (!matches(key)) return false;
        auto& scope = *current;
        try
        {
            if (++scope.count > max_messages) { scope.cut = true; return true; }
            if (!scope.output.empty() && !text.empty() && scope.output.back() != '\n')
            {
                if (scope.output.size() == max_bytes) { scope.cut = true; return true; }
                scope.output += '\n';
            }
            const auto available = max_bytes - scope.output.size();
            const auto n = utf8_prefix_size(text, available);
            if (n) scope.output.append(text.data(), n);
            scope.cut = scope.cut || n != text.size();
        }
        catch (...) { scope.failed = true; }
        return true;
    }
    const std::string& text() const { return output; }
    bool truncated() const { return cut; }
    bool has_failed() const { return failed; }
    std::size_t messages() const { return count; }
};
}

#pragma once
#include "winrecomp/core.hpp"
#include <variant>
#include <tuple>

namespace wr {
using ResourceName = std::variant<std::uint32_t, std::u16string>;
struct ResourceData {
    std::uint32_t language{}, codepage{}, address{};
    std::vector<std::uint8_t> bytes;
};
// The table owns bytes copied from the validated original image. No native DLL
// load or execution is involved, and resource pointers never alias host memory.
class ResourceTable {
    using Key = std::tuple<ResourceName, ResourceName, std::uint32_t>;
    std::map<Key,ResourceData> entries_;
public:
    static ResourceTable parse(const Image& image);
    const ResourceData* find(const ResourceName& type, const ResourceName& name,
                             std::uint32_t language = 0) const;
    std::size_t size() const { return entries_.size(); }
};
}

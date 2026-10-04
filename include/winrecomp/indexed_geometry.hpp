#pragma once
// Bounded DX7 WORD-indexed triangle lists. This is format validation only,
// independent of a graphics driver; callers must first own the input buffers.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace wr::geometry {
constexpr std::uint32_t MaxVertices = 65535;
constexpr std::uint32_t MaxIndices = 65535;
struct IndexRange { std::uint32_t first{}, vertices{}, primitives{}; };
inline std::optional<IndexRange> index_range(std::size_t vertices,
    std::span<const std::uint16_t> indices, std::uint32_t max_index = MaxVertices - 1,
    std::uint32_t max_primitives = MaxIndices / 3) {
    if (!vertices || vertices > MaxVertices || indices.empty() ||
        indices.size() > MaxIndices || indices.size() % 3 || indices.size() / 3 > max_primitives)
        return {};
    const auto [low, high] = std::minmax_element(indices.begin(), indices.end());
    if (*high >= vertices || *high > max_index) return {};
    return IndexRange{*low, std::uint32_t(*high) - *low + 1, std::uint32_t(indices.size() / 3)};
}
template<class Vertex> inline bool referenced_vertices_valid(std::span<const Vertex> vertices,
    std::span<const std::uint16_t> indices) {
    // Do not inspect unused vertex payloads: a hole in an index list is not a draw.
    for (auto index : indices) {
        if (index >= vertices.size()) return false;
        const auto& v = vertices[index];
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
            !std::isfinite(v.rhw) || v.rhw <= 0) return false;
        if constexpr (requires { v.u; v.v; })
            if (!std::isfinite(v.u) || !std::isfinite(v.v)) return false;
    }
    return true;
}
}

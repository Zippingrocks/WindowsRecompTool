#pragma once
// Version-neutral host boundary: d3d.h and d3d9.h define incompatible public
// names and must never be included in the same translation unit. No guest
// pointer is accepted here; the checked legacy bridge supplies owned copies.
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace wr::host9 {
using Status=std::uint32_t;
constexpr Status Ok=0,Invalid=0x8876086cu,Unavailable=0x8876086au;
constexpr bool failed(Status hr){return (hr&0x80000000u)!=0;}
struct Vertex {float x,y,z,rhw;std::uint32_t diffuse;};
struct Viewport {std::uint32_t x,y,width,height;float min_z,max_z;};
struct Capabilities {std::uint32_t misc{},shade{},max_primitives{};float max_w{};};
static_assert(sizeof(Vertex)==20 && sizeof(Viewport)==24);
class Device {
public:
    virtual ~Device()=default;
    virtual bool in_scene() const=0;
    virtual Status begin()=0;
    virtual Status end()=0;
    virtual Status clear(std::uint32_t color)=0;
    virtual Status set_viewport(const Viewport&)=0;
    virtual Status get_viewport(Viewport&)=0;
    virtual Status set_state(std::uint32_t,std::uint32_t)=0;
    virtual Status get_state(std::uint32_t,std::uint32_t&)=0;
    virtual Status triangles(std::span<const Vertex>)=0;
    virtual Status upload(std::span<const std::uint8_t>)=0;
    virtual Status readback(std::vector<std::uint8_t>&)=0;
};
class Factory {
public:
    virtual ~Factory()=default;
    virtual Status capabilities(Capabilities&)=0;
    virtual Status create(std::uintptr_t window,std::uint32_t width,std::uint32_t height,
                          std::unique_ptr<Device>&)=0;
};
std::unique_ptr<Factory> make_factory();
}

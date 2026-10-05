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
struct TexturedVertex {float x,y,z,rhw;std::uint32_t diffuse;float u,v;};
struct TextureLimits {std::uint32_t width{},height{},aspect{},ops{},filters{},address{},caps{};bool alpha{};};
struct Viewport {std::uint32_t x,y,width,height;float min_z,max_z;};
struct Capabilities {std::uint32_t misc{},shade{},max_primitives{};float max_w{};TextureLimits texture{};std::uint32_t depth_compare{};bool depth16{};std::uint32_t src_blend{},dst_blend{},alpha_compare{};};
static_assert(sizeof(Vertex)==20 && sizeof(TexturedVertex)==28 && sizeof(Viewport)==24);
class Depth {
public:
    virtual ~Depth()=default;
};
class Device {
public:
    virtual ~Device()=default;
    virtual bool in_scene() const=0;
    virtual Status begin()=0;
    virtual Status end()=0;
    virtual Status clear(std::uint32_t color)=0;
    // Depth storage belongs to the creating native device. Rebinding on that
    // device preserves its contents; copying between devices is not implicit.
    virtual Status create_depth(std::shared_ptr<Depth>&)=0;
    virtual Status bind_depth(const std::shared_ptr<Depth>&)=0;
    virtual Status clear_buffers(std::uint32_t flags,std::uint32_t color,float z)=0;
    virtual Status set_viewport(const Viewport&)=0;
    virtual Status get_viewport(Viewport&)=0;
    virtual Status set_state(std::uint32_t,std::uint32_t)=0;
    virtual Status get_state(std::uint32_t,std::uint32_t&)=0;
    virtual Status triangles(std::span<const Vertex>)=0;
    // One stage, one level. Pixels are host-owned copies, never guest pointers.
    virtual Status indexed_triangles(std::span<const Vertex>,std::span<const std::uint16_t>)=0;
    virtual Status indexed_textured_triangles(std::span<const TexturedVertex>,std::span<const std::uint16_t>)=0;
    virtual Status texture(std::uint32_t width,std::uint32_t height,bool alpha,
                           std::span<const std::uint8_t>)=0;
    virtual Status unbind_texture()=0;
    virtual Status set_stage(std::uint32_t state,std::uint32_t value)=0;
    virtual Status get_stage(std::uint32_t state,std::uint32_t& value)=0;
    virtual Status textured_triangles(std::span<const TexturedVertex>)=0;
    virtual Status upload(std::span<const std::uint8_t>)=0;
    virtual Status readback(std::vector<std::uint8_t>&)=0;
    // Copy the owned render target to the device swap chain and display it.
    // Presentation is legal only outside BeginScene/EndScene.
    virtual Status present()=0;
};
class Factory {
public:
    virtual ~Factory()=default;
    // Native HMONITOR identity for D3D9 adapter 0, represented opaquely.
    virtual std::uintptr_t adapter_monitor() const=0;
    virtual Status capabilities(Capabilities&)=0;
    virtual Status create(std::uintptr_t window,std::uint32_t width,std::uint32_t height,
                          std::unique_ptr<Device>&)=0;
};
std::unique_ptr<Factory> make_factory();
}

#include "winrecomp/resources.hpp"
#include <functional>
#include <limits>
#include <stdexcept>
namespace wr {
ResourceTable ResourceTable::parse(const Image& image) {
    ResourceTable result;
    const auto directory = image.directories[2];
    if (!directory.rva && !directory.size) return result;
    if (!directory.rva || directory.size < 16) throw std::runtime_error("invalid PE resource directory");
    auto read = [&](std::uint64_t relative, std::size_t length) {
        if (relative > directory.size || length > directory.size-relative)
            throw std::runtime_error("resource directory offset out of bounds");
        const auto at=std::uint64_t(image.base)+directory.rva+relative;
        if(at>0xffffffffu)throw std::runtime_error("resource directory address wraps");
        const auto offset=image.offset(Address(at),length);
        if(!offset)throw std::runtime_error("resource directory is not file-backed");
        return std::span(image.bytes).subspan(*offset,length);
    };
    auto u16=[&](std::uint64_t offset){auto b=read(offset,2);return unsigned(b[0])|(unsigned(b[1])<<8);};
    auto u32=[&](std::uint64_t offset){auto b=read(offset,4);return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);};
    auto name=[&](std::uint32_t value)->ResourceName {
        if(!(value&0x80000000u))return value;
        const auto at=value&0x7fffffffu;const auto count=u16(at);
        if(count>32767)throw std::runtime_error("resource name exceeds bound");
        read(std::uint64_t(at)+2,std::size_t(count)*2);std::u16string s;
        for(unsigned k=0;k<count;++k)s.push_back(char16_t(u16(std::uint64_t(at)+2+2*k)));
        return s;
    };
    std::size_t visited{},copied{};std::set<std::uint32_t> ancestors;
    std::array<ResourceName,3> path;
    std::function<void(std::uint32_t,unsigned)> visit=[&](std::uint32_t at,unsigned depth){
        if(depth>=3 || !ancestors.insert(at).second)throw std::runtime_error("cyclic or excessive resource directory depth");
        read(at,16);const auto named=u16(std::uint64_t(at)+12), ids=u16(std::uint64_t(at)+14), count=named+ids;
        if(visited+count>65536)throw std::runtime_error("resource entry budget exhausted");visited+=count;
        read(std::uint64_t(at)+16,std::size_t(count)*8);std::set<ResourceName> names;
        for(unsigned k=0;k<count;++k){
            const auto entry=std::uint64_t(at)+16+8*k;const auto raw_name=u32(entry), child=u32(entry+4);
            if(bool(raw_name&0x80000000u)!=(k<named))throw std::runtime_error("resource named/id partition is malformed");
            path[depth]=name(raw_name);
            if(!names.insert(path[depth]).second)throw std::runtime_error("duplicate resource key");
            if(depth<2){if(!(child&0x80000000u))throw std::runtime_error("resource leaf before language level");visit(child&0x7fffffffu,depth+1);}
            else {
                if((child&0x80000000u) || !std::holds_alternative<std::uint32_t>(path[2]))throw std::runtime_error("invalid resource language leaf");
                const auto language=std::get<std::uint32_t>(path[2]);if(language>65535)throw std::runtime_error("invalid resource language id");
                read(child,16);const auto rva=u32(child),size=u32(std::uint64_t(child)+4),cp=u32(std::uint64_t(child)+8);
                if(size>64u*1024*1024 || copied+size>64u*1024*1024)throw std::runtime_error("resource byte budget exhausted");
                const auto address=std::uint64_t(image.base)+rva;
                if(address>0xffffffffu)throw std::runtime_error("resource data address wraps");
                const auto offset=image.offset(Address(address),size);
                if(!offset)throw std::runtime_error("resource data is not file-backed");
                ResourceData data{language,cp,Address(address),{}};
                data.bytes.assign(image.bytes.begin()+std::ptrdiff_t(*offset),image.bytes.begin()+std::ptrdiff_t(*offset+size));copied+=size;
                if(!result.entries_.emplace(Key{path[0],path[1],language},std::move(data)).second)throw std::runtime_error("duplicate resource leaf");
            }
        }
        ancestors.erase(at);
    };
    visit(0,0);return result;
}
const ResourceData* ResourceTable::find(const ResourceName& type,const ResourceName& name,std::uint32_t language)const {
    auto exact=entries_.find(Key{type,name,language});if(exact!=entries_.end())return &exact->second;
    // Deterministic profile: exact, neutral, US English, first available. This is
    // not Windows' complete per-thread/user UI language fallback algorithm.
    for(auto fallback:{0u,0x409u}){auto it=entries_.find(Key{type,name,fallback});if(it!=entries_.end())return &it->second;}
    auto it=entries_.lower_bound(Key{type,name,0u});
    if(it!=entries_.end() && std::get<0>(it->first)==type && std::get<1>(it->first)==name)return &it->second;
    return nullptr;
}
}

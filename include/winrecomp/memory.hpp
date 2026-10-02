#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace wr {
enum class FaultKind { memory, divide, unsupported, untranslated, budget, breakpoint, floating };
class GuestFault : public std::runtime_error {
public:
    FaultKind kind;
    std::uint32_t address;
    GuestFault(FaultKind k,std::uint32_t a,const std::string& message)
        :std::runtime_error(message+" at guest address "+std::to_string(a)),kind(k),address(a){}
};
// Guest addresses never become native pointers. Reservations own sparse 4 KiB
// pages; commits/protection changes are validated before changing any page.
class Memory {
public:
    using U32=std::uint32_t;
    enum Permission : unsigned { Read=1, Write=2, Execute=4 };
    static constexpr std::size_t PAGE=4096;
    struct RegionInfo { U32 base; std::size_t size; };
private:
    struct Page { std::unique_ptr<std::array<std::uint8_t,PAGE>> data; unsigned permissions{}; };
    struct Region { U32 base; std::size_t size; std::vector<Page> pages; };
    std::map<U32,Region> regions_;
    mutable std::recursive_mutex mutex_;
    static void bounds(U32 a,std::size_t n) {
        if(n>0x100000000ull || std::uint64_t(a)+n>0x100000000ull)
            throw GuestFault(FaultKind::memory,a,"guest range wraps");
    }
    Region& region(U32 a) {
        auto it=regions_.upper_bound(a);
        if(it!=regions_.begin()) {
            auto& r=std::prev(it)->second;
            if(std::uint64_t(a)-r.base<r.size)return r;
        }
        throw GuestFault(FaultKind::memory,a,"unmapped guest memory");
    }
    Region& one_region(U32 a,std::size_t n) {
        bounds(a,n);if(!n)throw GuestFault(FaultKind::memory,a,"empty page range");
        auto& r=region(a);
        if(n>r.size-std::size_t(a-r.base))throw GuestFault(FaultKind::memory,a,"range crosses a reservation");
        return r;
    }
    template<class F> void spans(U32 a,std::size_t n,F&& f) {
        bounds(a,n);
        std::size_t done=0;
        while(done<n) {
            const U32 p=a+U32(done);auto& r=region(p);const std::size_t off=p-r.base;
            auto& page=r.pages.at(off/PAGE);
            const auto len=std::min({n-done,PAGE-off%PAGE,r.size-off});
            f(page,off%PAGE,len,done,p);done+=len;
        }
    }
    void validate(U32 a,std::size_t n,unsigned access) {
        spans(a,n,[&](Page& p,std::size_t,std::size_t,std::size_t,U32 at){
            if(!p.data)throw GuestFault(FaultKind::memory,at,"uncommitted guest memory");
            if((p.permissions&access)!=access)throw GuestFault(FaultKind::memory,at,"guest memory permission fault");
        });
    }
    static void width_check(unsigned w) {
        if(w!=8 && w!=16 && w!=32)throw std::runtime_error("unsupported memory width");
    }
public:
    auto lock() { return std::unique_lock<std::recursive_mutex>(mutex_); }
    bool available(U32 base,std::size_t size) {
        auto guard=lock();bounds(base,size);if(!size)return false;
        auto it=regions_.lower_bound(base);
        if(it!=regions_.end() && it->first<std::uint64_t(base)+size)return false;
        if(it!=regions_.begin()){const auto& p=std::prev(it)->second;if(std::uint64_t(p.base)+p.size>base)return false;}
        return true;
    }
    void reserve(U32 base,std::size_t size) {
        auto guard=lock();
        if(!size || size>512ull*1024*1024 || !available(base,size))throw GuestFault(FaultKind::memory,base,"invalid or overlapping reservation");
        regions_.emplace(base,Region{base,size,std::vector<Page>((size+PAGE-1)/PAGE)});
    }
    void commit(U32 base,std::size_t size,unsigned permissions) {
        auto guard=lock();auto& r=one_region(base,size);
        const auto first=(base-r.base)/PAGE,last=(std::size_t(base-r.base)+size-1)/PAGE;
        // Allocate everything first: allocation failure must not partially commit.
        std::vector<std::pair<std::size_t,std::unique_ptr<std::array<std::uint8_t,PAGE>>>> pending;
        for(auto i=first;i<=last;++i)if(!r.pages[i].data)pending.emplace_back(i,std::make_unique<std::array<std::uint8_t,PAGE>>());
        for(auto& [i,p]:pending){r.pages[i].data=std::move(p);r.pages[i].permissions=permissions;}
        // Already-committed pages retain contents AND protection (VirtualAlloc).
    }
    void map(U32 base,std::size_t size,unsigned permissions) {
        auto guard=lock();reserve(base,size);
        try{commit(base,size,permissions);}catch(...){regions_.erase(base);throw;}
    }
    void release(U32 base) {
        auto guard=lock();if(regions_.erase(base)!=1)throw GuestFault(FaultKind::memory,base,"release requires allocation base");
    }
    void decommit(U32 base,std::size_t size) {
        auto guard=lock();auto& r=one_region(base,size);
        for(auto i=(base-r.base)/PAGE;i<=(std::size_t(base-r.base)+size-1)/PAGE;++i){r.pages[i].data.reset();r.pages[i].permissions=0;}
    }
    unsigned protect(U32 base,std::size_t size,unsigned permissions) {
        auto guard=lock();auto& r=one_region(base,size);validate(base,size,0);
        const auto first=(base-r.base)/PAGE,last=(std::size_t(base-r.base)+size-1)/PAGE;
        const unsigned old=r.pages[first].permissions;
        for(auto i=first;i<=last;++i)r.pages[i].permissions=permissions;
        return old;
    }
    RegionInfo allocation(U32 at) {auto guard=lock();const auto& r=region(at);return {r.base,r.size};}
    unsigned permissions(U32 at) {auto guard=lock();auto& r=region(at);auto& p=r.pages[(at-r.base)/PAGE];if(!p.data)throw GuestFault(FaultKind::memory,at,"uncommitted guest memory");return p.permissions;}
    std::vector<RegionInfo> regions() {auto guard=lock();std::vector<RegionInfo> v;for(const auto& [a,r]:regions_)v.push_back({a,r.size});return v;}
    void check(U32 base,std::size_t size,unsigned access) {auto guard=lock();validate(base,size,access);}
    void initialize(U32 base,std::span<const std::uint8_t> bytes) {
        auto guard=lock();validate(base,bytes.size(),0);
        spans(base,bytes.size(),[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){std::copy_n(bytes.data()+done,n,p.data->data()+off);});
    }
    void copy_in(U32 base,std::span<const std::uint8_t> bytes) {
        auto guard=lock();validate(base,bytes.size(),Write);initialize(base,bytes);
    }
    void copy_out(U32 base,std::span<std::uint8_t> bytes) {
        auto guard=lock();validate(base,bytes.size(),Read);
        spans(base,bytes.size(),[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){std::copy_n(p.data->data()+off,n,bytes.data()+done);});
    }
    void fetch(U32 base,std::span<std::uint8_t> bytes) {
        auto guard=lock();validate(base,bytes.size(),Execute);
        spans(base,bytes.size(),[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){std::copy_n(p.data->data()+off,n,bytes.data()+done);});
    }
    U32 load(U32 a,unsigned w) {
        auto guard=lock();width_check(w);validate(a,w/8,Read);
        U32 v=0;spans(a,w/8,[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){for(std::size_t i=0;i<n;++i)v|=U32((*p.data)[off+i])<<unsigned(8*(done+i));});return v;
    }
    void store(U32 a,U32 v,unsigned w) {
        auto guard=lock();width_check(w);validate(a,w/8,Write);
        spans(a,w/8,[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){for(std::size_t i=0;i<n;++i)(*p.data)[off+i]=std::uint8_t(v>>unsigned(8*(done+i)));});
    }
};
}

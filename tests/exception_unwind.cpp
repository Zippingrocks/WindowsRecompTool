#include "winrecomp/memory.hpp"
#include <exception>
#include <functional>
#include <iostream>
#include <thread>

namespace {
unsigned checks=0;
void check(bool ok,const char* message) {
    ++checks;
    if(!ok)throw std::runtime_error(message);
}
struct Lifetime {
    unsigned& destroyed;
    ~Lifetime() { ++destroyed; }
};
// The automatic object is intentionally outside a local try block. MSVC's
// partial default EH can catch the throw without running this destructor.
void throw_with_lifetime(unsigned& destroyed) {
    Lifetime scope{destroyed};
    throw std::runtime_error("unwind probe");
}
void other_thread_can_enter(wr::Memory& memory) {
    std::recursive_mutex* mutex=nullptr;
    { auto guard=memory.lock();mutex=guard.mutex(); }
    bool acquired=false;
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            // Never wait for a leaked lock: report a regression, not a timeout.
            std::unique_lock<std::recursive_mutex> guard(*mutex,std::try_to_lock);
            if(!guard.owns_lock())return;
            acquired=true;
            check(memory.load(0x10000,32)==0x13572468,"memory damaged after fault");
            memory.store(0x10004,0x24681357,32);
        } catch(...) { failure=std::current_exception(); }
    });
    worker.join();
    if(failure)std::rethrow_exception(failure);
    check(acquired,"handled exception retained the guest memory mutex");
    check(memory.load(0x10004,32)==0x24681357,"post-fault cross-thread write failed");
}
void guest_fault(wr::Memory& memory,const std::function<void()>& operation) {
    bool caught=false;
    try { operation(); }
    catch(const wr::GuestFault& error) {
        check(error.kind==wr::FaultKind::memory,"wrong guest fault kind");
        caught=true;
    }
    check(caught,"expected guest memory fault");
    other_thread_can_enter(memory);
}
}
int main() {
    try {
        unsigned destroyed=0;
        bool caught=false;
        try { throw_with_lifetime(destroyed); }
        catch(const std::runtime_error&) { caught=true; }
        check(caught && destroyed==1,"C++ automatic-object unwinding is not enabled");
        wr::Memory memory;
        memory.reserve(0x10000,0x3000);
        memory.commit(0x10000,0x1000,wr::Memory::Read|wr::Memory::Write);
        memory.commit(0x11000,0x1000,wr::Memory::Read);
        memory.store(0x10000,0x13572468,32);
        std::array<std::uint8_t,4> bytes{1,2,3,4};
        guest_fault(memory,[&]{(void)memory.load(0,32);});
        guest_fault(memory,[&]{memory.store(0x11000,0,32);});
        guest_fault(memory,[&]{memory.store(0x10fff,0,32);});
        guest_fault(memory,[&]{memory.check(0x11fff,2,wr::Memory::Read);});
        guest_fault(memory,[&]{memory.copy_in(0x11000,bytes);});
        guest_fault(memory,[&]{memory.copy_out(0x12000,bytes);});
        guest_fault(memory,[&]{memory.fetch(0x10000,bytes);});
        guest_fault(memory,[&]{memory.release(0x10004);});
        guest_fault(memory,[&]{memory.reserve(0x10000,0x1000);});
        guest_fault(memory,[&]{memory.map(0x10000,0x1000,wr::Memory::Read);});
        guest_fault(memory,[&]{memory.protect(0x12000,0x1000,wr::Memory::Read);});
        guest_fault(memory,[&]{memory.store(0xfffffffeu,0,32);});
        guest_fault(memory,[&]{
            auto outer=memory.lock();
            auto inner=memory.lock();
            memory.store(0x11000,0,32);
        });
        bool width_fault=false;
        try {(void)memory.load(0x10000,7);}
        catch(const std::runtime_error&) {width_fault=true;}
        check(width_fault,"invalid-width exception missing");
        other_thread_can_enter(memory);
        std::cout<<checks<<" exception-unwind and post-fault cross-thread assertions passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}

#include "winrecomp/core.hpp"
#include <algorithm>
#include <functional>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace wr {
Graph discover(const Image& image,Address entry,std::size_t budget,bool include_image_roots) {
    if (image.code(entry).empty()) throw std::runtime_error("seed is not file-backed executable code: "+hex(entry));
    if(!budget)throw std::runtime_error("instruction budget must be positive");
    Graph g;g.entry=entry;g.roots.insert(entry);
    if(include_image_roots){g.roots.insert(image.exports.begin(),image.exports.end());g.roots.insert(image.tls_callbacks.begin(),image.tls_callbacks.end());}
    g.function_candidates=g.roots;
    Decoder decoder;
    std::priority_queue<Address,std::vector<Address>,std::greater<Address>> work;
    std::set<Address> queued,leaders=g.roots;
    auto enqueue=[&](Address a){if(queued.insert(a).second)work.push(a);};
    for(auto a:g.roots)enqueue(a);
    auto edge=[&](Address from,std::optional<Address> to,const std::string& kind,const std::string& detail="") {
        g.edges.push_back({from,to,kind,detail});if(to)enqueue(*to);
    };
    while(!work.empty()) {
        if(g.instructions.size()>=budget){g.budget_exhausted=true;g.diagnostics.push_back("instruction budget exhausted; pending targets="+std::to_string(work.size()));break;}
        const auto a=work.top();work.pop();
        auto successor=g.instructions.lower_bound(a);
        if(successor!=g.instructions.end() && successor->first==a)continue;
        if(successor!=g.instructions.begin()) {
            const auto& p=std::prev(successor)->second;
            if(std::uint64_t(p.address)+p.decoded.length>a){g.diagnostics.push_back("target overlaps instruction: "+hex(a));continue;}
        }
        const auto bytes=image.code(a);
        if(bytes.empty()){g.diagnostics.push_back("non-code or unbacked target: "+hex(a));continue;}
        auto decoded=decoder.decode(bytes,a);
        if(!decoded){g.diagnostics.push_back("decode failure: "+hex(a));continue;}
        if(successor!=g.instructions.end() && std::uint64_t(a)+decoded->decoded.length>successor->first){g.diagnostics.push_back("instruction overlaps established code: "+hex(a));continue;}
        const auto i=*decoded;g.instructions.emplace(a,std::move(*decoded));
        const auto next=i.next();
        if(i.flow==Flow::normal){edge(a,next,"fallthrough");continue;}
        if(i.flow==Flow::ret){edge(a,{},"return");continue;}
        if(i.flow==Flow::stop){edge(a,{},"stop",i.text);continue;}
        if(i.flow==Flow::call || i.flow==Flow::conditional){leaders.insert(next);edge(a,next,i.flow==Flow::call?"call_continuation":"branch_not_taken");}
        if(i.target){
            leaders.insert(*i.target);
            const auto kind=i.flow==Flow::call?"call_direct":i.flow==Flow::conditional?"branch_taken":"jump_direct";
            edge(a,i.target,kind);
            if(i.flow==Flow::call && !image.code(*i.target).empty())g.function_candidates.insert(*i.target);
        } else {
            const auto* imp=i.absolute_slot?image.import_at(*i.absolute_slot):nullptr;
            if(imp)edge(a,{},i.flow==Flow::call?"call_import":"jump_import",imp->dll+"!"+imp->name);
            else {
                edge(a,{},i.flow==Flow::call?"call_indirect":"jump_indirect",i.text);
                if(i.flow==Flow::jump && i.decoded.operand_count_visible && i.operands[0].type==ZYDIS_OPERAND_TYPE_MEMORY && i.operands[0].mem.index!=ZYDIS_REGISTER_NONE)g.jump_table_candidates.push_back(a);
            }
        }
    }
    // A second pass splits blocks at every discovered target. No stale overlapping blocks.
    std::set<Address> assigned;
    for(const auto& [address,unused]:g.instructions) {
        (void)unused;if(assigned.contains(address))continue;
        Block b;b.start=address;auto a=address;
        for(;;) {
            const auto it=g.instructions.find(a);
            if(it==g.instructions.end() || assigned.contains(a))break;
            const auto& i=it->second;assigned.insert(a);b.instructions.push_back(a);b.end=i.next();
            if(i.flow!=Flow::normal || leaders.contains(i.next()))break;
            a=i.next();
        }
        g.blocks.emplace(b.start,std::move(b));
    }
    std::sort(g.edges.begin(),g.edges.end(),[](const Edge& a,const Edge& b){return std::tie(a.from,a.kind,a.to,a.detail)<std::tie(b.from,b.kind,b.to,b.detail);});
    std::sort(g.diagnostics.begin(),g.diagnostics.end());
    return g;
}
std::string Graph::json(const Image& image) const {
    std::ostringstream o;
    o<<"{\"schema\":\"winrecomp.cfg.v1\",\"decoder\":\"Zydis 4.1.1\",\"guest_bits\":32,\"entry\":"<<entry
     <<",\"budget_exhausted\":"<<(budget_exhausted?"true":"false")<<",\"whole_program_complete\":false,\"image\":"<<image.json();
    o<<",\"roots\":[";bool first=true;for(auto a:roots){if(!first)o<<',';first=false;o<<a;}
    o<<"],\"function_candidates\":[";first=true;for(auto a:function_candidates){if(!first)o<<',';first=false;o<<a;}
    o<<"],\"blocks\":[";first=true;
    for(const auto& [a,b]:blocks){if(!first)o<<',';first=false;o<<"{\"start\":"<<a<<",\"end\":"<<b.end<<",\"instructions\":[";bool f=true;for(auto pc:b.instructions){if(!f)o<<',';f=false;o<<pc;}o<<"]}";}
    o<<"],\"instructions\":[";first=true;
    for(const auto& [a,i]:instructions){
        if(!first)o<<',';
        first=false;
        o<<"{\"address\":"<<a<<",\"size\":"<<unsigned(i.decoded.length)<<",\"text\":"<<quote(i.text)<<",\"mnemonic\":"<<quote(ZydisMnemonicGetString(i.decoded.mnemonic))<<",\"bytes\":";
        std::string bs;constexpr char digits[]="0123456789abcdef";for(auto b:i.bytes){bs+=digits[b>>4];bs+=digits[b&15];}o<<quote(bs)<<",\"operands\":[";
        for(unsigned n=0;n<i.decoded.operand_count_visible;++n){
            if(n)o<<',';
            const auto& op=i.operands[n];o<<"{\"width\":"<<op.size;
            if(op.type==ZYDIS_OPERAND_TYPE_REGISTER)o<<",\"kind\":\"register\",\"register\":"<<quote(ZydisRegisterGetString(op.reg.value));
            else if(op.type==ZYDIS_OPERAND_TYPE_IMMEDIATE)o<<",\"kind\":\"immediate\",\"value_hex\":"<<quote(hex(op.imm.value.u))<<",\"signed\":"<<(op.imm.is_signed?"true":"false")<<",\"relative\":"<<(op.imm.is_relative?"true":"false");
            else if(op.type==ZYDIS_OPERAND_TYPE_MEMORY)o<<",\"kind\":\"memory\",\"base\":"<<quote(ZydisRegisterGetString(op.mem.base))<<",\"index\":"<<quote(ZydisRegisterGetString(op.mem.index))<<",\"scale\":"<<unsigned(op.mem.scale)<<",\"displacement\":"<<op.mem.disp.value<<",\"segment\":"<<quote(ZydisRegisterGetString(op.mem.segment))<<",\"address_width\":"<<unsigned(i.decoded.address_width);
            else o<<",\"kind\":\"unsupported\"";
            o<<'}';
        }
        o<<"]}";
    }
    o<<"],\"edges\":[";first=true;for(const auto& e:edges){if(!first)o<<',';first=false;o<<"{\"from\":"<<e.from<<",\"to\":";if(e.to)o<<*e.to;else o<<"null";o<<",\"kind\":"<<quote(e.kind)<<",\"detail\":"<<quote(e.detail)<<'}';}
    o<<"],\"jump_table_candidates\":[";first=true;for(auto a:jump_table_candidates){if(!first)o<<',';first=false;o<<a;}
    o<<"],\"diagnostics\":[";first=true;for(const auto& d:diagnostics){if(!first)o<<',';first=false;o<<quote(d);}o<<"]}\n";return o.str();
}
}

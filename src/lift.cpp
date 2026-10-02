#include "winrecomp/core.hpp"
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace wr {
namespace {
struct Register { unsigned index,width,shift; };
Register reginfo(ZydisRegister r) {
    switch(r) {
#define R(name,idx,w,s) case ZYDIS_REGISTER_##name:return {idx,w,s};
    R(EAX,0,32,0) R(ECX,1,32,0) R(EDX,2,32,0) R(EBX,3,32,0) R(ESP,4,32,0) R(EBP,5,32,0) R(ESI,6,32,0) R(EDI,7,32,0)
    R(AX,0,16,0) R(CX,1,16,0) R(DX,2,16,0) R(BX,3,16,0) R(SP,4,16,0) R(BP,5,16,0) R(SI,6,16,0) R(DI,7,16,0)
    R(AL,0,8,0) R(CL,1,8,0) R(DL,2,8,0) R(BL,3,8,0) R(AH,0,8,8) R(CH,1,8,8) R(DH,2,8,8) R(BH,3,8,8)
#undef R
    default:throw std::runtime_error("unsupported register");
    }
}
std::string number(std::uint32_t v){return hex(v)+"u";}
std::string readreg(ZydisRegister r) {
    const auto x=reginfo(r);return "wr::reg(s,"+std::to_string(x.index)+","+std::to_string(x.width)+","+std::to_string(x.shift)+")";
}
std::string address(const Instruction& i,const ZydisDecodedOperand& op,bool lea=false) {
    if(op.type!=ZYDIS_OPERAND_TYPE_MEMORY)throw std::runtime_error("expected memory operand");
    if(!lea && (op.mem.segment==ZYDIS_REGISTER_FS || op.mem.segment==ZYDIS_REGISTER_GS))throw std::runtime_error("FS/GS memory requires a TEB/segment runtime");
    if(i.decoded.address_width!=16 && i.decoded.address_width!=32)throw std::runtime_error("unsupported address width");
    std::string e=number(std::uint32_t(op.mem.disp.value));
    if(op.mem.base!=ZYDIS_REGISTER_NONE)e+="+"+readreg(op.mem.base);
    if(op.mem.index!=ZYDIS_REGISTER_NONE)e+="+"+readreg(op.mem.index)+"*"+std::to_string(op.mem.scale)+"u";
    return "(("+e+")&"+(i.decoded.address_width==16?"0xffffu":"0xffffffffu")+")";
}
std::string read(const Instruction& i,const ZydisDecodedOperand& op) {
    switch(op.type) {
    case ZYDIS_OPERAND_TYPE_REGISTER:return readreg(op.reg.value);
    case ZYDIS_OPERAND_TYPE_IMMEDIATE:if(op.imm.is_relative)throw std::runtime_error("relative immediate used as data");return number(std::uint32_t(op.imm.value.u));
    case ZYDIS_OPERAND_TYPE_MEMORY:
        if(op.size!=8 && op.size!=16 && op.size!=32)throw std::runtime_error("unsupported memory operand width");
        return "m.load("+address(i,op)+","+std::to_string(op.size)+")";
    default:throw std::runtime_error("unsupported operand");
    }
}
std::string write(const Instruction& i,const ZydisDecodedOperand& op,const std::string& v) {
    if(op.type==ZYDIS_OPERAND_TYPE_REGISTER) {
        const auto x=reginfo(op.reg.value);
        return "wr::set_reg(s,"+std::to_string(x.index)+","+v+","+std::to_string(x.width)+","+std::to_string(x.shift)+");";
    }
    if(op.type==ZYDIS_OPERAND_TYPE_MEMORY && (op.size==8 || op.size==16 || op.size==32))return "m.store("+address(i,op)+","+v+","+std::to_string(op.size)+");";
    throw std::runtime_error("unsupported destination");
}
std::string condition(ZydisMnemonic mnemonic) {
    switch(mnemonic){
    case ZYDIS_MNEMONIC_JO:return "(s.flags&wr::OF)!=0";
    case ZYDIS_MNEMONIC_JNO:return "(s.flags&wr::OF)==0";
    case ZYDIS_MNEMONIC_JB:return "(s.flags&wr::CF)!=0";
    case ZYDIS_MNEMONIC_JNB:return "(s.flags&wr::CF)==0";
    case ZYDIS_MNEMONIC_JZ:return "(s.flags&wr::ZF)!=0";
    case ZYDIS_MNEMONIC_JNZ:return "(s.flags&wr::ZF)==0";
    case ZYDIS_MNEMONIC_JBE:return "(s.flags&(wr::CF|wr::ZF))!=0";
    case ZYDIS_MNEMONIC_JNBE:return "(s.flags&(wr::CF|wr::ZF))==0";
    case ZYDIS_MNEMONIC_JS:return "(s.flags&wr::SF)!=0";
    case ZYDIS_MNEMONIC_JNS:return "(s.flags&wr::SF)==0";
    case ZYDIS_MNEMONIC_JP:return "(s.flags&wr::PF)!=0";
    case ZYDIS_MNEMONIC_JNP:return "(s.flags&wr::PF)==0";
    case ZYDIS_MNEMONIC_JL:return "bool(s.flags&wr::SF)!=bool(s.flags&wr::OF)";
    case ZYDIS_MNEMONIC_JNL:return "bool(s.flags&wr::SF)==bool(s.flags&wr::OF)";
    case ZYDIS_MNEMONIC_JLE:return "(s.flags&wr::ZF)!=0 || bool(s.flags&wr::SF)!=bool(s.flags&wr::OF)";
    case ZYDIS_MNEMONIC_JNLE:return "(s.flags&wr::ZF)==0 && bool(s.flags&wr::SF)==bool(s.flags&wr::OF)";
    case ZYDIS_MNEMONIC_JECXZ:return "s.r[wr::ECX]==0";
    case ZYDIS_MNEMONIC_JCXZ:return "(s.r[wr::ECX]&0xffffu)==0";
    default:throw std::runtime_error("unsupported conditional branch");
    }
}
std::string translate(const Instruction& i) {
    if(i.decoded.attributes&(ZYDIS_ATTRIB_HAS_LOCK|ZYDIS_ATTRIB_HAS_REP|ZYDIS_ATTRIB_HAS_REPE|ZYDIS_ATTRIB_HAS_REPNE))throw std::runtime_error("LOCK/REP semantics not implemented");
    if(i.decoded.meta.branch_type==ZYDIS_BRANCH_TYPE_FAR)throw std::runtime_error("far control flow not implemented");
    const auto& d=i.operands[0];const auto& src=i.operands[1];const auto op=i.decoded.mnemonic;
    auto width=std::to_string(d.size);
    if(i.flow==Flow::conditional) {
        if(!i.target)throw std::runtime_error("conditional branch without a target");
        return "s.eip=("+condition(op)+")?"+number(*i.target)+":"+number(i.next())+";";
    }
    if(op==ZYDIS_MNEMONIC_JMP || op==ZYDIS_MNEMONIC_CALL) {
        if(!i.target)throw std::runtime_error("indirect transfers need recovered targets/import thunks");
        if(i.decoded.operand_width!=32)throw std::runtime_error("16-bit control transfer not implemented");
        return (op==ZYDIS_MNEMONIC_CALL?"wr::push(s,m,"+number(i.next())+"); ":"")+"s.eip="+number(*i.target)+";";
    }
    if(op==ZYDIS_MNEMONIC_RET) {
        if(i.decoded.operand_width!=32)throw std::runtime_error("16-bit return not implemented");
        auto result=std::string("s.eip=wr::pop(s,m);");
        if(i.decoded.operand_count_visible)result+="s.r[wr::ESP]+="+number(std::uint32_t(d.imm.value.u))+";";
        return result;
    }
    if(op==ZYDIS_MNEMONIC_NOP)return ";";
    if(op==ZYDIS_MNEMONIC_MOV || op==ZYDIS_MNEMONIC_MOVZX || op==ZYDIS_MNEMONIC_MOVSX) {
        auto value=read(i,src);
        if(op==ZYDIS_MNEMONIC_MOVSX)value="wr::sign_extend("+value+","+std::to_string(src.size)+")";
        return "{auto value="+value+";"+write(i,d,"value")+"}";
    }
    if(op==ZYDIS_MNEMONIC_LEA)return write(i,d,address(i,src,true));
    if(op==ZYDIS_MNEMONIC_PUSH) {
        if(i.decoded.operand_width!=16 && i.decoded.operand_width!=32)throw std::runtime_error("unsupported push width");
        return "{auto value="+read(i,d)+";wr::push(s,m,value,"+std::to_string(i.decoded.operand_width)+");}";
    }
    if(op==ZYDIS_MNEMONIC_POP) {
        if(d.type!=ZYDIS_OPERAND_TYPE_REGISTER)throw std::runtime_error("POP to memory not implemented");
        return "{auto value=wr::pop(s,m,"+std::to_string(i.decoded.operand_width)+");"+write(i,d,"value")+"}";
    }
    if(op==ZYDIS_MNEMONIC_LEAVE) {
        if(i.decoded.operand_width!=32)throw std::runtime_error("16-bit LEAVE not implemented");
        return "{auto p=s.r[wr::EBP];auto v=m.load(p,32);s.r[wr::ESP]=p+4;s.r[wr::EBP]=v;}";
    }
    if(op==ZYDIS_MNEMONIC_NOT)return write(i,d,"~("+read(i,d)+")");
    if(op==ZYDIS_MNEMONIC_CLC)return "s.flags&=~wr::CF;s.defined_flags|=wr::CF;";
    if(op==ZYDIS_MNEMONIC_STC)return "s.flags|=wr::CF;s.defined_flags|=wr::CF;";
    if(op==ZYDIS_MNEMONIC_CMC)return "s.flags^=wr::CF;";
    std::string operation;
    bool store=true;
    switch(op) {
    case ZYDIS_MNEMONIC_ADD:operation="wr::add(f,a,b,"+width+")";break;
    case ZYDIS_MNEMONIC_ADC:operation="wr::add(f,a,b,"+width+",s.flags&wr::CF)";break;
    case ZYDIS_MNEMONIC_SUB:operation="wr::sub(f,a,b,"+width+")";break;
    case ZYDIS_MNEMONIC_SBB:operation="wr::sub(f,a,b,"+width+",s.flags&wr::CF)";break;
    case ZYDIS_MNEMONIC_CMP:operation="wr::sub(f,a,b,"+width+")";store=false;break;
    case ZYDIS_MNEMONIC_AND:operation="wr::logic(f,a&b,"+width+")";break;
    case ZYDIS_MNEMONIC_OR:operation="wr::logic(f,a|b,"+width+")";break;
    case ZYDIS_MNEMONIC_XOR:operation="wr::logic(f,a^b,"+width+")";break;
    case ZYDIS_MNEMONIC_TEST:operation="wr::logic(f,a&b,"+width+")";store=false;break;
    case ZYDIS_MNEMONIC_NEG:operation="wr::sub(f,0,a,"+width+")";break;
    case ZYDIS_MNEMONIC_INC:operation="wr::incdec(f,a,"+width+",false)";break;
    case ZYDIS_MNEMONIC_DEC:operation="wr::incdec(f,a,"+width+",true)";break;
    default:throw std::runtime_error("instruction semantics not implemented");
    }
    auto result="{auto a="+read(i,d)+";";
    if(i.decoded.operand_count_visible>1)result+="auto b="+read(i,src)+";";
    result+="auto f=s;auto v="+operation+";";
    result+=store?write(i,d,"v"):"(void)v;";
    result+="s.flags=f.flags;s.defined_flags=f.defined_flags;}";
    return result;
}
}
std::string emit_cpp(const Graph& g,const std::string& name) {
    if(name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0]=='_'))throw std::runtime_error("invalid C++ function name");
    for(unsigned char c:name)if(!(std::isalnum(c)||c=='_'))throw std::runtime_error("invalid C++ function name");
    if(g.budget_exhausted || !g.diagnostics.empty())throw std::runtime_error("refusing to lift a graph with discovery errors or an exhausted budget");
    static const std::set<std::string> keywords={"alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool","break","case","catch","char","char8_t","char16_t","char32_t","class","compl","concept","const","consteval","constexpr","constinit","const_cast","continue","co_await","co_return","co_yield","decltype","default","delete","do","double","dynamic_cast","else","enum","explicit","export","extern","false","float","for","friend","goto","if","inline","int","long","mutable","namespace","new","noexcept","not","not_eq","nullptr","operator","or","or_eq","private","protected","public","register","reinterpret_cast","requires","return","short","signed","sizeof","static","static_assert","static_cast","struct","switch","template","this","thread_local","throw","true","try","typedef","typeid","typename","union","unsigned","using","virtual","void","volatile","wchar_t","while","xor","xor_eq"};
    if(keywords.contains(name) || name[0]=='_' || name.find("__")!=std::string::npos)throw std::runtime_error("reserved C++ function name");
    std::ostringstream out;
    out<<"// Generated by WinRecomp. Guest addresses remain 32-bit.\n#include <winrecomp/runtime.hpp>\n";
    out<<"void "<<name<<"(wr::Cpu& s,wr::Memory& m,wr::U32 stop,std::uint64_t budget) {\nwhile(s.eip!=stop) {\nif(!budget--)throw std::runtime_error(\"translated block budget exhausted\");\nswitch(s.eip) {\n";
    for(const auto& [a,b]:g.blocks) {
        out<<"case "<<number(a)<<": {\n";
        for(auto pc:b.instructions) {
            const auto& i=g.instructions.at(pc);std::string statement;
            try{statement=translate(i);}catch(const std::exception& e){throw std::runtime_error("cannot lift "+hex(pc)+" ("+i.text+"): "+e.what());}
            out<<"s.eip="<<number(pc)<<"; "<<statement<<"\n";
        }
        const auto& last=g.instructions.at(b.instructions.back());
        if(last.flow==Flow::normal)out<<"s.eip="<<number(last.next())<<";\n";
        out<<"break; }\n";
    }
    out<<"default:throw std::runtime_error(\"untranslated guest target \"+std::to_string(s.eip));\n}\n}\n}\n";return out.str();
}
}

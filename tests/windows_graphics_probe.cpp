// Environment observation only: absence of legacy x64 D3D is NOT a game pass.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d9.h>
#include <d3d11.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdint>
namespace {
constexpr GUID draw7_iid{0x15e65ec0,0x3b9c,0x11d2,{0xb9,0x2f,0x00,0x60,0x97,0x97,0xea,0x5b}};
constexpr GUID d3d7_iid{0xf5049e77,0x4861,0x11d2,{0xa4,0x07,0x00,0xa0,0xc9,0x06,0x29,0xa8}};
std::string hex(HRESULT value){std::ostringstream out;out<<"\"0x"<<std::hex<<std::setfill('0')<<std::setw(8)<<std::uint32_t(value)<<"\"";return out.str();}
template<class T>struct Com {T* p{};~Com(){if(p)p->Release();}T** out(){return &p;}T* operator->(){return p;}};
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    auto window=CreateWindowExA(0,"STATIC","WinRecomp graphics availability probe",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    const auto window_error=window?0:GetLastError();
    Com<IDirectDraw7> draw;
    const auto dd=DirectDrawCreateEx(nullptr,reinterpret_cast<void**>(draw.out()),draw7_iid,nullptr);
    HRESULT coop=E_UNEXPECTED,legacy=E_UNEXPECTED;Com<IUnknown> d3d7;
    if(SUCCEEDED(dd) && draw.p){coop=draw->SetCooperativeLevel(window,DDSCL_NORMAL);legacy=draw->QueryInterface(d3d7_iid,reinterpret_cast<void**>(d3d7.out()));}
    Com<IDirect3D9> modern;modern.p=Direct3DCreate9(D3D_SDK_VERSION);
    HRESULT caps_result=E_UNEXPECTED,device_result=E_UNEXPECTED,clear_result=E_UNEXPECTED;D3DCAPS9 caps{};
    Com<IDirect3DDevice9> device;
    if(modern.p){
        caps_result=modern->GetDeviceCaps(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,&caps);
        if(window){D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=16;pp.BackBufferHeight=16;pp.BackBufferFormat=D3DFMT_UNKNOWN;
            device_result=modern->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,device.out());
            if(SUCCEEDED(device_result) && device.p)clear_result=device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(16,32,64),1.0f,0);
        }
    }
    Com<ID3D11Device> warp;Com<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    const auto warp_result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,warp.out(),&level,context.out());
    std::cout<<"{\n  \"schema\": \"winrecomp.native-graphics-observation.v1\",\n  \"pointer_bits\": "<<sizeof(void*)*8
        <<",\n  \"window_error\": "<<window_error<<",\n  \"ddraw7_create\": "<<hex(dd)<<",\n  \"ddraw7_cooperative\": "<<hex(coop)
        <<",\n  \"d3d7_query\": "<<hex(legacy)<<",\n  \"d3d7_object\": "<<(d3d7.p?"true":"false")
        <<",\n  \"d3d9_object\": "<<(modern.p?"true":"false")<<",\n  \"d3d9_adapters\": "<<(modern.p?modern->GetAdapterCount():0)
        <<",\n  \"d3d9_caps\": "<<hex(caps_result)<<",\n  \"d3d9_hal_device\": "<<hex(device_result)<<",\n  \"d3d9_clear\": "<<hex(clear_result)
        <<",\n  \"d3d11_warp_device\": "<<hex(warp_result)<<",\n  \"d3d11_feature_level\": "<<unsigned(level)
        <<",\n  \"scope\": \"Host API observations only; not a renderer implementation or game test.\"\n}\n";
    if(device.p){device.p->Release();device.p=nullptr;}if(window)DestroyWindow(window);
    return 0;
}

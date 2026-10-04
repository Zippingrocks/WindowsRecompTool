// Included inside Draw: a bounded IDirect3D7 capability-enumeration boundary.
// Original implementations remain in the native DirectDraw DLL. Only guest ABI
// structures, borrowed payloads, callbacks and COM ownership are adapted here.
static constexpr U32 DeviceDesc7Size=236;
void write_device(U32 at,const D3DDEVICEDESC7& d){
    // The structure has no pointers; assert the legacy ABI and copy named fields
    // only. The reserved tail is never read from the driver or exposed to guest.
    static_assert(sizeof(D3DDEVICEDESC7)==DeviceDesc7Size && sizeof(D3DPRIMCAPS)==56);
    static_assert(offsetof(D3DDEVICEDESC7,deviceGUID)==196 && offsetof(D3DDEVICEDESC7,dwReserved1)==220);
    std::array<std::uint8_t,DeviceDesc7Size> out{};
    auto put=[&](std::size_t offset,const auto& value){std::memcpy(out.data()+offset,&value,sizeof(value));};
    put(0,d.dwDevCaps);put(4,d.dpcLineCaps);put(60,d.dpcTriCaps);
    put(116,d.dwDeviceRenderBitDepth);put(120,d.dwDeviceZBufferBitDepth);
    put(124,d.dwMinTextureWidth);put(128,d.dwMinTextureHeight);
    put(132,d.dwMaxTextureWidth);put(136,d.dwMaxTextureHeight);
    put(140,d.dwMaxTextureRepeat);put(144,d.dwMaxTextureAspectRatio);put(148,d.dwMaxAnisotropy);
    put(152,d.dvGuardBandLeft);put(156,d.dvGuardBandTop);put(160,d.dvGuardBandRight);put(164,d.dvGuardBandBottom);
    put(168,d.dvExtentsAdjust);put(172,d.dwStencilCaps);put(176,d.dwFVFCaps);put(180,d.dwTextureOpCaps);
    put(184,d.wMaxTextureBlendStages);put(186,d.wMaxSimultaneousTextures);put(188,d.dwMaxActiveLights);
    put(192,d.dvMaxVertexW);put(196,d.deviceGUID);put(212,d.wMaxUserClipPlanes);
    put(214,d.wMaxVertexBlendMatrices);put(216,d.dwVertexProcessingCaps);
    p.memory.copy_in(at,out);
}
struct D3DEnumeration {Draw* self;U32 callback,context;std::exception_ptr failure;unsigned visits{};};
static HRESULT CALLBACK enumerate_device(LPSTR description,LPSTR name,D3DDEVICEDESC7* caps,void* context) noexcept {
    auto& call=*static_cast<D3DEnumeration*>(context);auto& self=*call.self;
    try {
        self.enter();if(call.failure || self.p.exited())return D3DENUMRET_CANCEL;
        if(!caps || !description || !name)self.stop("null native Direct3D device descriptor/name");
        if(++call.visits>256)self.stop("Direct3D device enumeration budget exceeded");
        Allocation scratch(self.p,12288);self.write_device(scratch.address,*caps);
        U32 used=DeviceDesc7Size;
        auto string=[&](const char* text){
            std::size_t n=0;while(n<4096 && text[n])++n;
            if(n==4096)self.stop("unterminated Direct3D device string");
            const auto address=scratch.address+used;used+=(U32(n)+4)&~3u;
            self.p.memory.copy_in(address,std::span(reinterpret_cast<const std::uint8_t*>(text),n+1));return address;
        };
        const std::array<U32,4> args{string(description),string(name),scratch.address,call.context};
        self.p.memory.protect(scratch.address,12288,Memory::Read);
        ++self.device_callbacks;const auto result=self.p.callback(call.callback,args);
        if(self.p.exited())return D3DENUMRET_CANCEL;
        if(result!=D3DENUMRET_OK && result!=D3DENUMRET_CANCEL)self.stop("invalid Direct3D device callback result");
        return HRESULT(result);
    }catch(...){if(!call.failure)call.failure=std::current_exception();return D3DENUMRET_CANCEL;}
}
U32 enum_devices(Args a){
    auto receiver=static_cast<IDirect3D7*>(object(a[0],Interface::direct3d7).native);
    p.memory.check(a[1],1,Memory::Execute);
    receiver->AddRef();NativeOwner lifetime{receiver};
    D3DEnumeration call{this,a[1],a[2],{},0};
    const auto hr=receiver->EnumDevices(enumerate_device,&call);
    if(call.failure)std::rethrow_exception(call.failure);return U32(hr);
}
static HRESULT CALLBACK enumerate_zformat(DDPIXELFORMAT* format,void* context) noexcept {
    auto& call=*static_cast<D3DEnumeration*>(context);auto& self=*call.self;
    try {
        self.enter();if(call.failure || self.p.exited())return D3DENUMRET_CANCEL;
        if(!format || format->dwSize!=32)self.stop("invalid native z-buffer pixel format");
        if(++call.visits>4096)self.stop("z-buffer enumeration budget exceeded");
        constexpr U32 allowed=DDPF_ZBUFFER|DDPF_STENCILBUFFER;
        if(!(format->dwFlags&DDPF_ZBUFFER) || (format->dwFlags&~allowed))self.stop("unhandled native z-buffer format flags");
        // DDPIXELFORMAT consists of eight DWORDs, several of which are unions.
        // Reserved/non-depth members are zero. Copy only the selected meanings.
        std::array<U32,8> out{};out[0]=32;out[1]=format->dwFlags;
        out[3]=format->dwZBufferBitDepth;out[5]=format->dwZBitMask;
        if(format->dwFlags&DDPF_STENCILBUFFER){out[4]=format->dwStencilBitDepth;out[6]=format->dwStencilBitMask;}
        Allocation scratch(self.p,32);self.p.memory.copy_in(scratch.address,std::span(reinterpret_cast<const std::uint8_t*>(out.data()),32));
        self.p.memory.protect(scratch.address,4096,Memory::Read);
        const std::array<U32,2> args{scratch.address,call.context};++self.zformat_callbacks;
        const auto result=self.p.callback(call.callback,args);
        if(self.p.exited())return D3DENUMRET_CANCEL;
        if(result!=D3DENUMRET_OK && result!=D3DENUMRET_CANCEL)self.stop("invalid z-buffer callback result");
        return HRESULT(result);
    }catch(...){if(!call.failure)call.failure=std::current_exception();return D3DENUMRET_CANCEL;}
}
U32 enum_zformats(Args a){
    auto receiver=static_cast<IDirect3D7*>(object(a[0],Interface::direct3d7).native);
    const auto iid=guid(a[1]);p.memory.check(a[2],1,Memory::Execute);
    receiver->AddRef();NativeOwner lifetime{receiver};
    D3DEnumeration call{this,a[2],a[3],{},0};
    const auto hr=receiver->EnumZBufferFormats(iid,enumerate_zformat,&call);
    if(call.failure)std::rethrow_exception(call.failure);return U32(hr);
}
// The x86 DDDEVICEIDENTIFIER2 has 1068 defined bytes. The native 64-bit
// structure can have extra trailing alignment padding: never copy sizeof(d).
U32 device_identifier(Args a){
    auto receiver=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);
    p.memory.check(a[1],1068,Memory::Write);
    DDDEVICEIDENTIFIER2 d{};const auto hr=receiver->GetDeviceIdentifier(&d,a[2]);
    if(FAILED(hr))return U32(hr);
    static_assert(sizeof(d.szDriver)==512 && sizeof(d.szDescription)==512);
    static_assert(offsetof(DDDEVICEIDENTIFIER2,liDriverVersion)==1024);
    static_assert(offsetof(DDDEVICEIDENTIFIER2,dwWHQLLevel)==1064);
    std::array<std::uint8_t,1068> out{};
    auto text=[&](std::size_t offset,const char* s){
        std::size_t n=0;while(n<512 && s[n])++n;
        if(n==512)stop("unterminated native device-identifier string");
        std::memcpy(out.data()+offset,s,n);
    };
    text(0,d.szDriver);text(512,d.szDescription);
    auto put=[&](std::size_t at,const auto& v){std::memcpy(out.data()+at,&v,sizeof(v));};
    put(1024,d.liDriverVersion.LowPart);put(1028,d.liDriverVersion.HighPart);
    put(1032,d.dwVendorId);put(1036,d.dwDeviceId);put(1040,d.dwSubSysId);put(1044,d.dwRevision);
    put(1048,d.guidDeviceIdentifier);put(1064,d.dwWHQLLevel);
    p.memory.copy_in(a[1],out);return U32(hr);
}
U32 draw_caps(Args a){
    auto receiver=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);
    static_assert(sizeof(DDCAPS_DX7)==380 && sizeof(DDCAPS)==380);
    static_assert(offsetof(DDCAPS_DX7,ddsCaps)==364 && sizeof(DDSCAPS2)==16);
    DDCAPS hw{},sw{};hw.dwSize=sw.dwSize=380;
    for(U32 at:{a[1],a[2]})if(at){
        p.memory.check(at,380,Memory::Read|Memory::Write);
        if(p.memory.load(at,32)!=380)stop("GetCaps requires a 380-byte DX7 DDCAPS");
    }
    if(a[1] && a[2] && std::uint64_t(a[1])<std::uint64_t(a[2])+380 && std::uint64_t(a[2])<std::uint64_t(a[1])+380)
        stop("overlapping GetCaps output buffers are outside the current profile");
    const auto hr=receiver->GetCaps(a[1]?&hw:nullptr,a[2]?&sw:nullptr);
    if(FAILED(hr))return U32(hr);
    auto output=[&](U32 at,DDCAPS& caps){
        if(!at)return;caps.dwReserved1=caps.dwReserved2=caps.dwReserved3=0;
        // All fields are pointer-free DWORDs. Zero-initialization plus the
        // explicit reserved-field clearing excludes native indeterminate data.
        p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&caps),380));
    };
    output(a[1],hw);output(a[2],sw);return U32(hr);
}
DDSURFACEDESC2 mode7_filter(U32 at){
    p.memory.check(at,DescSize,Memory::Read);
    if(p.memory.load(at,32)!=DescSize)stop("EnumDisplayModes7 requires a 124-byte DDSURFACEDESC2");
    DDSURFACEDESC2 d{};d.dwSize=sizeof(d);d.dwFlags=p.memory.load(at+4,32);
    constexpr U32 allowed=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;
    if(d.dwFlags&~allowed)stop("unmodelled DX7 display-mode filter fields");
    if(p.memory.load(at+36,32))stop("DX7 mode filter must not contain a surface pointer");
    if(d.dwFlags&DDSD_WIDTH)d.dwWidth=p.memory.load(at+12,32);
    if(d.dwFlags&DDSD_HEIGHT)d.dwHeight=p.memory.load(at+8,32);
    if(d.dwFlags&DDSD_REFRESHRATE)d.dwRefreshRate=p.memory.load(at+24,32);
    if(d.dwFlags&DDSD_PIXELFORMAT){
        p.memory.copy_out(at+72,std::span(reinterpret_cast<std::uint8_t*>(&d.ddpfPixelFormat),32));
        if(d.ddpfPixelFormat.dwSize!=32)stop("DX7 mode filter pixel format size");
    }
    return d;
}
void write_mode7(U32 at,const DDSURFACEDESC2& d){
    if(d.dwSize!=sizeof(d) || d.lpSurface || (d.dwFlags&DDSD_LPSURFACE))stop("unhandled DX7 display-mode descriptor");
    std::array<U32,31> out{};out[0]=DescSize;out[1]=d.dwFlags;
    if(d.dwFlags&DDSD_HEIGHT)out[2]=d.dwHeight;
    if(d.dwFlags&DDSD_WIDTH)out[3]=d.dwWidth;
    if(d.dwFlags&(DDSD_PITCH|DDSD_LINEARSIZE))out[4]=U32(d.lPitch);
    if(d.dwFlags&DDSD_BACKBUFFERCOUNT)out[5]=d.dwBackBufferCount;
    if(d.dwFlags&(DDSD_MIPMAPCOUNT|DDSD_REFRESHRATE))out[6]=d.dwRefreshRate;
    if(d.dwFlags&DDSD_ALPHABITDEPTH)out[7]=d.dwAlphaBitDepth;
    if(d.dwFlags&DDSD_CKDESTOVERLAY)std::memcpy(out.data()+10,&d.ddckCKDestOverlay,8);
    if(d.dwFlags&DDSD_CKDESTBLT)std::memcpy(out.data()+12,&d.ddckCKDestBlt,8);
    if(d.dwFlags&DDSD_CKSRCOVERLAY)std::memcpy(out.data()+14,&d.ddckCKSrcOverlay,8);
    if(d.dwFlags&DDSD_CKSRCBLT)std::memcpy(out.data()+16,&d.ddckCKSrcBlt,8);
    if(d.dwFlags&DDSD_PIXELFORMAT)std::memcpy(out.data()+18,&d.ddpfPixelFormat,32);
    if(d.dwFlags&DDSD_CAPS)std::memcpy(out.data()+26,&d.ddsCaps,16);
    if(d.dwFlags&DDSD_TEXTURESTAGE)out[30]=d.dwTextureStage;
    p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(out.data()),DescSize));
}
static HRESULT CALLBACK enumerate_mode7(DDSURFACEDESC2* desc,void* context) noexcept {
    auto& call=*static_cast<ModeEnumeration*>(context);auto& self=*call.self;
    try{
        self.enter();if(call.failure || self.p.exited())return DDENUMRET_CANCEL;
        if(!desc || ++call.visits>4096)self.stop("invalid DX7 mode callback or budget");
        Allocation scratch(self.p,DescSize);self.write_mode7(scratch.address,*desc);
        self.p.memory.protect(scratch.address,4096,Memory::Read);
        const std::array<U32,2> args{scratch.address,call.context};++self.mode_callbacks;
        auto result=self.p.callback(call.callback,args);
        if(self.p.exited())return DDENUMRET_CANCEL;
        if(result!=DDENUMRET_OK && result!=DDENUMRET_CANCEL)self.stop("invalid DX7 mode callback result");
        return HRESULT(result);
    }catch(...){if(!call.failure)call.failure=std::current_exception();return DDENUMRET_CANCEL;}
}
U32 enum_modes7(Args a){
    auto receiver=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);
    p.memory.check(a[4],1,Memory::Execute);DDSURFACEDESC2 filter{};if(a[2])filter=mode7_filter(a[2]);
    receiver->AddRef();NativeOwner lifetime{receiver};ModeEnumeration call{this,a[4],a[3],{},0};
    const auto hr=receiver->EnumDisplayModes(a[1],a[2]?&filter:nullptr,&call,enumerate_mode7);
    if(call.failure)std::rethrow_exception(call.failure);return U32(hr);
}

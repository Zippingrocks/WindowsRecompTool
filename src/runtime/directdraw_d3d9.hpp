// Included inside Draw; uses the DX7 ABI here and a version-neutral helper TU
// for DX9. This profile intentionally cannot advertise textures/depth/lights.
    host9::Factory& factory9(){if(!renderer9)renderer9=host9::make_factory();if(!renderer9)stop("D3D9 host unavailable");return *renderer9;}
    std::uintptr_t identity9(IUnknown* native){
        IUnknown* id{};const auto hr=native->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&id));
        if(FAILED(hr) || !id)stop("native object has no controlling IUnknown");
        NativeOwner owner{id};const auto key=reinterpret_cast<std::uintptr_t>(id);
        if(!root_identities.contains(key)){
            auto held=std::shared_ptr<IUnknown>(owner.keep(),[](IUnknown* p){p->Release();});
            root_identities.emplace(key,std::move(held));
        }
        return key;
    }
    void check_target9(IUnknown* target){
        for(const auto& [at,o]:objects){(void)at;if(o.compat && o.compat->target==target && o.compat->gpu->in_scene())stop("render target access during an active D3D9 scene");}
    }
    bool target_locked9(IUnknown* target){for(const auto& [at,o]:objects){(void)at;if(o.native==target && o.locked)return true;}return false;}
    U32 caps9(D3DDEVICEDESC7& caps){
        host9::Capabilities c{};const auto hr=factory9().capabilities(c);if(host9::failed(hr))return hr;
        caps={};caps.dpcLineCaps.dwSize=56;caps.dpcTriCaps.dwSize=56;
        caps.dwDevCaps=D3DDEVCAPS_FLOATTLVERTEX|D3DDEVCAPS_DRAWPRIMTLVERTEX;
        caps.dpcTriCaps.dwMiscCaps=c.misc;caps.dpcTriCaps.dwShadeCaps=c.shade;
        caps.dwDeviceRenderBitDepth=DDBD_32;caps.dvMaxVertexW=c.max_w;
        caps.deviceGUID=IID_IDirect3DHALDevice;
        // No texture formats, Z formats, multitexturing or light slots yet.
        return hr;
    }
    U32 enum_devices9(Args a){
        auto root=object(a[0],Interface::compat3d7).native;
        if(!a[1])return U32(DDERR_INVALIDPARAMS);p.memory.check(a[1],1,Memory::Execute);
        root->AddRef();NativeOwner lifetime{root};D3DDEVICEDESC7 caps{};
        auto hr=caps9(caps);if(host9::failed(hr))return hr;
        DeviceEnumeration call{this,a[1],a[2],{},0};
        char description[]="WinRecomp bounded Direct3D7-on-9 (untextured TL triangles)";
        char name[]="WinRecomp D3D9";
        enumerate_device(description,name,&caps,&call);
        if(call.failure)std::rethrow_exception(call.failure);return U32(S_OK);
    }
    bool rgb32_9(const DDSURFACEDESC2& d){
        const auto& f=d.ddpfPixelFormat;
        return (d.dwFlags&(DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT))==(DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT) &&
            d.dwWidth && d.dwHeight && d.dwWidth<=2048 && d.dwHeight<=2048 && f.dwSize==32 &&
            f.dwFlags==DDPF_RGB && f.dwRGBBitCount==32 && f.dwRBitMask==0xff0000 && f.dwGBitMask==0xff00 && f.dwBBitMask==0xff;
    }
    U32 transfer9(CompatDevice& device,bool download){
        if(target_locked9(device.target))stop("D3D9 transfer while guest render target is locked");
        std::vector<std::uint8_t> bytes(std::size_t(device.width)*device.height*4);
        if(download){auto hr=device.gpu->readback(bytes);if(host9::failed(hr))return hr;}
        DDSURFACEDESC2 desc{};desc.dwSize=sizeof(desc);
        auto hr=device.target->Lock(nullptr,&desc,DDLOCK_WAIT|(download?0:DDLOCK_READONLY),nullptr);
        if(FAILED(hr))return U32(hr);
        struct Unlocker {IDirectDrawSurface7* p;~Unlocker(){if(p)p->Unlock(nullptr);}} cleanup{device.target};
        if(!rgb32_9(desc) || desc.dwWidth!=device.width || desc.dwHeight!=device.height ||
           desc.lPitch<0 || DWORD(desc.lPitch)<device.width*4 || !desc.lpSurface)stop("unsupported native D3D9 transfer layout");
        for(U32 row=0;row<device.height;++row){
            auto native=static_cast<std::uint8_t*>(desc.lpSurface)+std::size_t(row)*desc.lPitch;
            auto buffer=bytes.data()+std::size_t(row)*device.width*4;
            if(download)std::memcpy(native,buffer,device.width*4);else std::memcpy(buffer,native,device.width*4);
        }
        hr=device.target->Unlock(nullptr);cleanup.p=nullptr;if(FAILED(hr))return U32(hr);
        if(download){++renderer9_readbacks;return U32(S_OK);}
        return device.gpu->upload(bytes);
    }
    U32 create_device9(Args a){
        auto root=static_cast<IDirectDraw7*>(object(a[0],Interface::compat3d7).native);
        const auto requested=guid(a[1]);p.memory.check(a[3],4,Memory::Write);
        if(!IsEqualIID(requested,IID_IDirect3DHALDevice))return U32(DDERR_INVALIDPARAMS);
        auto& target=object(a[2],Interface::surface7);if(target.locked)stop("CreateDevice with locked D3D9 target");
        for(const auto& [at,o]:objects){(void)at;if(o.compat && o.compat->target==target.native)stop("one live D3D9 device per render target is supported");}
        const auto pos=cooperative_windows.find(identity9(root));
        if(pos==cooperative_windows.end() || !pos->second)stop("D3D9 creation needs an application-owned cooperative window");
        DDSURFACEDESC2 desc{};desc.dwSize=sizeof(desc);
        auto native_target=static_cast<IDirectDrawSurface7*>(target.native);
        void* parent{};auto parent_hr=native_target->GetDDInterface(&parent);
        if(FAILED(parent_hr))return U32(parent_hr);if(!parent)stop("render target has no DirectDraw owner");
        NativeOwner parent_owner{static_cast<IUnknown*>(parent)};
        if(identity9(parent_owner.value)!=identity9(root))stop("D3D9 render target belongs to another DirectDraw root");
        auto hr=native_target->GetSurfaceDesc(&desc);if(FAILED(hr))return U32(hr);
        if(!rgb32_9(desc))stop("D3D9 profile requires a bounded X8R8G8B8 render target");
        if(objects.size()>=MaxObjects)stop("COM object budget exhausted");
        // Own the native inputs before CreateDevice can reenter through USER32.
        auto d=std::make_shared<CompatDevice>();root->AddRef();d->root=root;native_target->AddRef();d->target=native_target;
        d->width=desc.dwWidth;d->height=desc.dwHeight;
        const auto created=factory9().create(reinterpret_cast<std::uintptr_t>(pos->second),d->width,d->height,d->gpu);
        if(host9::failed(created))return created;
        auto uploaded=transfer9(*d,false);if(host9::failed(uploaded))return uploaded;
        const auto table=vtable(Interface::compatdevice7);Allocation memory(p,4);
        p.memory.store(memory.address,table,32);p.memory.protect(memory.address,4096,Memory::Read);
        auto at=memory.address;objects.emplace(at,Object{Interface::compatdevice7,nullptr,1,{},d});memory.keep();
        ++this->created;++render_devices;++renderer9_devices;p.memory.store(a[3],at,32);return U32(S_OK);
    }
    U32 begin9(Args a){
        auto d=object(a[0],Interface::compatdevice7).compat;
        if(d->gpu->in_scene())return host9::Invalid;
        auto hr=transfer9(*d,false);if(host9::failed(hr))return hr;return d->gpu->begin();
    }
    U32 end9(Args a){auto d=object(a[0],Interface::compatdevice7).compat;auto hr=d->gpu->end();if(host9::failed(hr))return hr;return transfer9(*d,true);}
    U32 clear9(Args a){
        auto d=object(a[0],Interface::compatdevice7).compat;
        if(a[1] || a[2] || a[3]!=D3DCLEAR_TARGET)stop("bounded D3D9 Clear supports whole viewport color only");
        if(!d->gpu->in_scene()){auto hr=transfer9(*d,false);if(host9::failed(hr))return hr;}
        auto hr=d->gpu->clear(a[4]);if(host9::failed(hr))return hr;++renderer9_clears;
        return d->gpu->in_scene()?hr:transfer9(*d,true);
    }
    U32 triangles9(Args a){
        auto d=object(a[0],Interface::compatdevice7).compat;
        if(a[1]!=D3DPT_TRIANGLELIST || a[2]!=(D3DFVF_XYZRHW|D3DFVF_DIFFUSE) || a[5])stop("unsupported D3D9 primitive, FVF or flags");
        if(!a[4] || a[4]%3 || a[4]>3*65536u)return host9::Invalid;
        const auto bytes=std::size_t(a[4])*sizeof(host9::Vertex);p.memory.check(a[3],bytes,Memory::Read);
        std::vector<host9::Vertex> vertices(a[4]);p.memory.copy_out(a[3],std::span(reinterpret_cast<std::uint8_t*>(vertices.data()),bytes));
        auto hr=d->gpu->triangles(vertices);if(!host9::failed(hr))++renderer9_draws;return hr;
    }

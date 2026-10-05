// Included inside Draw; uses the DX7 ABI here and a version-neutral helper TU
// for DX9. One checked texture stage and attached D16 depth; no stencil or presentation.
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
        caps.dpcTriCaps.dwSrcBlendCaps=c.src_blend;caps.dpcTriCaps.dwDestBlendCaps=c.dst_blend;
        caps.dpcTriCaps.dwAlphaCmpCaps=c.alpha_compare;
        caps.dwDeviceRenderBitDepth=DDBD_32;caps.dwDeviceZBufferBitDepth=c.depth16?DDBD_16:0;
        caps.dpcTriCaps.dwZCmpCaps=c.depth_compare;caps.dvMaxVertexW=c.max_w;
        caps.deviceGUID=IID_IDirect3DHALDevice;
        caps.dwDevCaps|=D3DDEVCAPS_TEXTURESYSTEMMEMORY;
        caps.dpcTriCaps.dwTextureCaps=c.texture.caps;
        caps.dpcTriCaps.dwTextureFilterCaps=c.texture.filters;
        caps.dpcTriCaps.dwTextureAddressCaps=c.texture.address;
        caps.dwMinTextureWidth=caps.dwMinTextureHeight=1;
        caps.dwMaxTextureWidth=c.texture.width;caps.dwMaxTextureHeight=c.texture.height;
        caps.dwMaxTextureAspectRatio=c.texture.aspect;caps.dwTextureOpCaps=c.texture.ops;
        caps.wMaxTextureBlendStages=caps.wMaxSimultaneousTextures=1;
        // Single-level textures, bounded alpha blend/test and optional D16; no stencil, multitexturing or lights.
        return hr;
    }
    U32 enum_devices9(Args a){
        auto root=object(a[0],Interface::compat3d7).native;
        if(!a[1])return U32(DDERR_INVALIDPARAMS);p.memory.check(a[1],1,Memory::Execute);
        root->AddRef();NativeOwner lifetime{root};D3DDEVICEDESC7 caps{};
        auto hr=caps9(caps);if(host9::failed(hr))return hr;
        DeviceEnumeration call{this,a[1],a[2],{},0};
        char description[]="WinRecomp bounded Direct3D7-on-9 (single-texture TL triangles)";
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
        if(target.texture_model)stop("texture render targets are outside this profile");
        auto surface_model=target.surface_model;
        if(!surface_model || surface_model->constructing)stop("D3D9 target is unavailable or under construction");
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
        // Own native inputs and metadata before CreateDevice can reenter USER32.
        struct Constructing {SurfaceModel& value;explicit Constructing(SurfaceModel& v):value(v){value.constructing=true;}~Constructing(){value.constructing=false;}} guard(*surface_model);
        auto d=std::make_shared<CompatDevice>();d->surface_model=surface_model;root->AddRef();d->root=root;native_target->AddRef();d->target=native_target;
        d->width=desc.dwWidth;d->height=desc.dwHeight;
        const auto created=factory9().create(reinterpret_cast<std::uintptr_t>(pos->second),d->width,d->height,d->gpu);
        if(host9::failed(created))return created;
        std::shared_ptr<host9::Depth> pending;
        if(surface_model->depth){
            auto hr=prepare_depth9(*d,*surface_model->depth,pending);if(host9::failed(hr))return hr;
            for(auto [state,value]:{std::pair<U32,U32>{D3DRENDERSTATE_ZENABLE,1},{D3DRENDERSTATE_ZWRITEENABLE,1},{D3DRENDERSTATE_ZFUNC,D3DCMP_LESSEQUAL}}){
                hr=d->gpu->set_state(state,value);if(host9::failed(hr))return hr;
            }
        }
        auto uploaded=transfer9(*d,false);if(host9::failed(uploaded))return uploaded;
        const auto table=vtable(Interface::compatdevice7);Allocation memory(p,4);
        p.memory.store(memory.address,table,32);p.memory.protect(memory.address,4096,Memory::Read);
        auto at=memory.address;objects.emplace(at,Object{Interface::compatdevice7,nullptr,1,{},d});memory.keep();
        if(surface_model->depth){if(!surface_model->depth->storage)++renderer9_depth_surfaces;surface_model->depth->storage=std::move(pending);++renderer9_depth_binds;}
        surface_model->device=d;
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
        if(a[1] || a[2] || !a[3] || (a[3]&~U32(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER)))stop("bounded D3D9 Clear supports viewport color/D16 only");
        const float z=std::bit_cast<float>(a[5]);
        if(a[3]&D3DCLEAR_ZBUFFER){
            if(!d->surface_model->depth)stop("depth Clear requires an attached D16 surface");
            if(!std::isfinite(z) || z<0 || z>1)return host9::Invalid;
        }
        if(!d->gpu->in_scene()){auto hr=transfer9(*d,false);if(host9::failed(hr))return hr;}
        auto hr=d->gpu->clear_buffers(a[3],a[4],z);if(host9::failed(hr))return hr;++renderer9_clears;
        return d->gpu->in_scene()?hr:transfer9(*d,true);
    }
    U32 enum_textures9(Args a){
        auto d=object(a[0],Interface::compatdevice7).compat;
        if(!a[1])return U32(DDERR_INVALIDPARAMS);p.memory.check(a[1],1,Memory::Execute);
        host9::Capabilities caps{};auto hr=factory9().capabilities(caps);if(host9::failed(hr))return hr;
        ModeEnumeration call{this,a[1],a[2],{},0};
        for(bool alpha:{false,true}){
            if(alpha && !caps.texture.alpha)continue;
            DDPIXELFORMAT f{};f.dwSize=32;f.dwFlags=DDPF_RGB|(alpha?DDPF_ALPHAPIXELS:0);
            f.dwRGBBitCount=32;f.dwRBitMask=0xff0000;f.dwGBitMask=0xff00;f.dwBBitMask=0xff;f.dwRGBAlphaBitMask=alpha?0xff000000:0;
            auto result=enumerate_texture(&f,&call);
            if(call.failure)std::rethrow_exception(call.failure);
            if(result==D3DENUMRET_CANCEL || p.exited())break;
        }
        return U32(S_OK);
    }
    U32 texture_upload9(CompatDevice& d,IDirectDrawSurface7* source,const TextureModel& model){
        if(source==d.target)stop("render-target texture feedback is not supported");
        if(target_locked9(source))stop("texture upload while the guest surface is locked");
        DDSURFACEDESC2 desc{};desc.dwSize=sizeof(desc);auto hr=source->GetSurfaceDesc(&desc);if(FAILED(hr))return U32(hr);
        desc.ddsCaps=model.caps;desc.dwFlags|=DDSD_CAPS;
        auto& f=desc.ddpfPixelFormat;
        const bool alpha=f.dwFlags==(DDPF_RGB|DDPF_ALPHAPIXELS);
        if((desc.dwFlags&(DDSD_CAPS|DDSD_PIXELFORMAT|DDSD_WIDTH|DDSD_HEIGHT))!=(DDSD_CAPS|DDSD_PIXELFORMAT|DDSD_WIDTH|DDSD_HEIGHT) ||
           !(desc.ddsCaps.dwCaps&DDSCAPS_TEXTURE) || (desc.ddsCaps.dwCaps&(DDSCAPS_MIPMAP|DDSCAPS_COMPLEX)) ||
           desc.ddsCaps.dwCaps2 || desc.ddsCaps.dwCaps3 || desc.ddsCaps.dwCaps4 ||
           f.dwSize!=32 || (f.dwFlags!=DDPF_RGB && !alpha) || f.dwFourCC || f.dwRGBBitCount!=32 ||
           f.dwRBitMask!=0xff0000 || f.dwGBitMask!=0xff00 || f.dwBBitMask!=0xff || f.dwRGBAlphaBitMask!=(alpha?0xff000000u:0u))
            stop("unsupported single-level RGB texture descriptor");
        const auto w=desc.dwWidth,h=desc.dwHeight;
        if(!w || !h || w>2048 || h>2048 || (w&(w-1)) || (h&(h-1)))stop("unsupported texture dimensions");
        void* parent{};hr=source->GetDDInterface(&parent);if(FAILED(hr))return U32(hr);
        if(!parent)stop("texture has no DirectDraw parent");NativeOwner owner{static_cast<IUnknown*>(parent)};
        if(identity9(owner.value)!=identity9(d.root))stop("texture belongs to another DirectDraw root");
        std::vector<std::uint8_t> bytes(std::size_t(w)*h*4);DDSURFACEDESC2 locked{};locked.dwSize=sizeof(locked);
        hr=source->Lock(nullptr,&locked,DDLOCK_WAIT|DDLOCK_READONLY,nullptr);if(FAILED(hr))return U32(hr);
        struct Unlocker {IDirectDrawSurface7* p;~Unlocker(){if(p)p->Unlock(nullptr);}} cleanup{source};
        if(locked.dwWidth!=w || locked.dwHeight!=h || !locked.lpSurface || locked.lPitch<0 || DWORD(locked.lPitch)<w*4)
            stop("unsupported texture lock layout");
        for(U32 y=0;y<h;++y)std::memcpy(bytes.data()+std::size_t(y)*w*4,static_cast<const std::uint8_t*>(locked.lpSurface)+std::size_t(y)*locked.lPitch,w*4);
        hr=source->Unlock(nullptr);cleanup.p=nullptr;if(FAILED(hr))return U32(hr);
        return d.gpu->texture(w,h,alpha,bytes);
    }
    U32 set_texture9(Args a){
        auto d=object(a[0],Interface::compatdevice7).compat;if(a[1])stop("only texture stage zero is implemented");
        if(!a[2]){auto hr=d->gpu->unbind_texture();if(!host9::failed(hr)){d->texture.reset();d->texture_model.reset();}return hr;}
        auto& texture=object(a[2],Interface::surface7);if(!texture.texture_model)stop("SetTexture requires an owned guest texture surface");if(texture.locked)stop("SetTexture with a locked guest surface");
        auto native=static_cast<IDirectDrawSurface7*>(texture.native);native->AddRef();NativeOwner owner{native};
        auto retained=std::shared_ptr<IDirectDrawSurface7>(static_cast<IDirectDrawSurface7*>(owner.keep()),[](auto* p){p->Release();});
        auto model=texture.texture_model;auto hr=texture_upload9(*d,native,*model);if(!host9::failed(hr)){d->texture=std::move(retained);d->texture_model=std::move(model);}return hr;
    }
    U32 get_texture9(Args a){
        auto d=object(a[0],Interface::compatdevice7).compat;if(a[1])stop("only texture stage zero is implemented");
        p.memory.check(a[2],4,Memory::Write);if(!d->texture){p.memory.store(a[2],0,32);return U32(S_OK);}
        d->texture->AddRef();auto at=wrap(d->texture.get(),Interface::surface7,d->texture_model);p.memory.store(a[2],at,32);return U32(S_OK);
    }
    U32 stage9(Args a,bool set){
        auto d=object(a[0],Interface::compatdevice7).compat;if(a[1])stop("only texture stage zero is implemented");
        if(!set)p.memory.check(a[3],4,Memory::Write);
        U32 value=a[3];auto hr=set?d->gpu->set_stage(a[2],value):d->gpu->get_stage(a[2],value);
        if(hr==U32(E_NOTIMPL))stop("texture stage state/value outside the admitted profile");
        if(!set && !host9::failed(hr))p.memory.store(a[3],value,32);return hr;
    }
    U32 triangles9(Args a){
        auto d=object(a[0],Interface::compatdevice7).compat;
        const bool textured=a[2]==(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
        if(a[1]!=D3DPT_TRIANGLELIST || (!textured && a[2]!=(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)) || a[5])stop("unsupported D3D9 primitive, FVF or flags");
        if(!a[4] || a[4]%3 || a[4]>3*65536u)return host9::Invalid;
        const auto bytes=std::size_t(a[4])*(textured?sizeof(host9::TexturedVertex):sizeof(host9::Vertex));p.memory.check(a[3],bytes,Memory::Read);
        U32 hr{};
        if(textured){
            if(!d->texture || !d->gpu->in_scene())return host9::Invalid;
            std::vector<host9::TexturedVertex> vertices(a[4]);p.memory.copy_out(a[3],std::span(reinterpret_cast<std::uint8_t*>(vertices.data()),bytes));
            // Re-upload for each draw so guest edits after SetTexture are visible.
            // This correctness path is intentionally not the final caching strategy.
            hr=texture_upload9(*d,d->texture.get(),*d->texture_model);if(host9::failed(hr))return hr;
            hr=d->gpu->textured_triangles(vertices);
        }else{
            if(d->texture)stop("a bound texture requires explicit TEX1 vertices in this profile");
            std::vector<host9::Vertex> vertices(a[4]);p.memory.copy_out(a[3],std::span(reinterpret_cast<std::uint8_t*>(vertices.data()),bytes));
            hr=d->gpu->triangles(vertices);
        }
        if(!host9::failed(hr))++renderer9_draws;return hr;
    }

    U32 indexed_triangles9(Args a){
        auto d=object(a[0],Interface::compatdevice7).compat;
        const bool textured=a[2]==(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
        if(a[1]!=D3DPT_TRIANGLELIST || (!textured && a[2]!=(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)) || a[7])
            stop("unsupported indexed D3D9 primitive, FVF or flags");
        // Counts are bounded before multiplication/allocation or guest reads.
        if(!d->gpu->in_scene() || !a[4] || a[4]>geometry::MaxVertices ||
           !a[6] || a[6]>geometry::MaxIndices || a[6]%3)return host9::Invalid;
        const auto bytes=std::size_t(a[4])*(textured?sizeof(host9::TexturedVertex):sizeof(host9::Vertex));
        p.memory.check(a[3],bytes,Memory::Read);p.memory.check(a[5],std::size_t(a[6])*2,Memory::Read);
        std::vector<std::uint16_t> indices(a[6]);
        p.memory.copy_out(a[5],std::span(reinterpret_cast<std::uint8_t*>(indices.data()),indices.size()*2));
        if(!geometry::index_range(a[4],indices))return host9::Invalid;
        U32 hr{};
        if(textured){
            if(!d->texture)return host9::Invalid;
            std::vector<host9::TexturedVertex> vertices(a[4]);
            p.memory.copy_out(a[3],std::span(reinterpret_cast<std::uint8_t*>(vertices.data()),bytes));
            if(!geometry::referenced_vertices_valid(std::span<const host9::TexturedVertex>(vertices),indices))return host9::Invalid;
            hr=texture_upload9(*d,d->texture.get(),*d->texture_model);if(host9::failed(hr))return hr;
            hr=d->gpu->indexed_textured_triangles(vertices,indices);
        }else{
            if(d->texture)stop("a bound texture requires explicit TEX1 indexed vertices in this profile");
            std::vector<host9::Vertex> vertices(a[4]);
            p.memory.copy_out(a[3],std::span(reinterpret_cast<std::uint8_t*>(vertices.data()),bytes));
            if(!geometry::referenced_vertices_valid(std::span<const host9::Vertex>(vertices),indices))return host9::Invalid;
            hr=d->gpu->indexed_triangles(vertices,indices);
        }
        if(!host9::failed(hr)){++renderer9_draws;++renderer9_indexed_draws;}return hr;
    }

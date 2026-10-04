// Included inside Draw. A depth surface is a checked guest object with lazily
// allocated, real D3D9 D16 storage. It is not an RGB DirectDraw surrogate.
// One target attachment and one native-device lifetime per depth resource.
    U32 wrap_depth9(std::shared_ptr<DepthModel> model){
        for(auto& [at,value]:objects)if(value.depth_model==model && value.guest_refs){
            if(value.guest_refs==0xffffffffu)stop("depth reference count overflow");
            ++value.guest_refs;return at;
        }
        if(objects.size()>=MaxObjects)stop("depth COM object budget exhausted");
        const auto table=vtable(Interface::depth7);Allocation memory(p,4);
        p.memory.store(memory.address,table,32);p.memory.protect(memory.address,4096,Memory::Read);
        const auto at=memory.address;Object value{};value.kind=Interface::depth7;
        value.guest_refs=1;value.depth_model=std::move(model);
        objects.emplace(at,std::move(value));memory.keep();++created;return at;
    }
    U32 create_depth9(Args a,const DDSURFACEDESC2& description){
        auto root=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);
        host9::Capabilities caps{};auto hr=factory9().capabilities(caps);
        if(host9::failed(hr))return hr;
        if(!caps.depth16)return U32(DDERR_INVALIDPIXELFORMAT);
        auto model=std::make_shared<DepthModel>();root->AddRef();NativeOwner owner{root};
        model->root=std::shared_ptr<IDirectDraw7>(static_cast<IDirectDraw7*>(owner.keep()),[](auto* v){v->Release();});
        model->description=description;
        p.memory.store(a[2],wrap_depth9(std::move(model)),32);return U32(DD_OK);
    }
    U32 enum_depth9(Args a){
        auto root=object(a[0],Interface::compat3d7).native;
        if(!IsEqualIID(guid(a[1]),IID_IDirect3DHALDevice) || !a[2])return U32(DDERR_INVALIDPARAMS);
        p.memory.check(a[2],1,Memory::Execute);
        root->AddRef();NativeOwner lifetime{root};host9::Capabilities caps{};
        auto hr=factory9().capabilities(caps);if(host9::failed(hr))return hr;
        if(caps.depth16){
            DDPIXELFORMAT f{};f.dwSize=32;f.dwFlags=DDPF_ZBUFFER;
            f.dwZBufferBitDepth=16;f.dwZBitMask=0xffff;
            ZFormatEnumeration call{this,a[2],a[3],{},0};enumerate_zformat(&f,&call);
            if(call.failure)std::rethrow_exception(call.failure);
        }
        return U32(S_OK);
    }
    // Do not publish a lazy native allocation in guest state until the complete
    // binding succeeds. A failed operation must leave the old attachment alone.
    U32 prepare_depth9(CompatDevice& device,DepthModel& depth,std::shared_ptr<host9::Depth>& pending){
        if(depth.description.dwWidth!=device.width || depth.description.dwHeight!=device.height)
            return U32(DDERR_CANNOTATTACHSURFACE);
        pending=depth.storage;
        if(!pending){auto hr=device.gpu->create_depth(pending);if(host9::failed(hr))return hr;}
        // The host rejects a depth surface created by a different device. Never
        // replace it with a fresh (uninitialized) depth buffer to hide that case.
        return device.gpu->bind_depth(pending);
    }
    std::shared_ptr<SurfaceModel> target_model9(Object& surface){
        if(!p.options.legacy_d3d9 || !surface.surface_model || surface.texture_model)
            stop("depth attachment requires a D3D9 color render-target surface");
        if(surface.surface_model->constructing)stop("depth mutation during native device construction");
        if(auto device=surface.surface_model->device.lock();device && device->gpu->in_scene())
            stop("depth attachment mutation during active scene");
        if(surface.locked)stop("depth attachment while color surface is locked");
        return surface.surface_model;
    }
    U32 attach_depth9(Args a){
        auto& target=object(a[0],Interface::surface7);auto state=target_model9(target);
        auto depth=object(a[1],Interface::depth7).depth_model;
        if(state->depth==depth)return U32(DDERR_SURFACEALREADYATTACHED);
        if(state->depth || !depth->attached_to.expired())return U32(DDERR_CANNOTATTACHSURFACE);
        auto native=static_cast<IDirectDrawSurface7*>(target.native);
        DDSURFACEDESC2 description{};description.dwSize=sizeof(description);
        auto hr=native->GetSurfaceDesc(&description);if(FAILED(hr))return U32(hr);
        if(!(description.ddsCaps.dwCaps&DDSCAPS_3DDEVICE) || !rgb32_9(description) ||
           description.dwWidth!=depth->description.dwWidth || description.dwHeight!=depth->description.dwHeight)
            return U32(DDERR_CANNOTATTACHSURFACE);
        void* parent{};hr=native->GetDDInterface(&parent);if(FAILED(hr))return U32(hr);
        if(!parent)stop("depth target has no native DirectDraw parent");
        NativeOwner owner{static_cast<IUnknown*>(parent)};
        if(identity9(owner.value)!=identity9(depth->root.get()))return U32(DDERR_CANNOTATTACHSURFACE);
        if(auto device=state->device.lock()){
            std::shared_ptr<host9::Depth> pending;auto result=prepare_depth9(*device,*depth,pending);
            if(host9::failed(result))return result;
            if(!depth->storage)++renderer9_depth_surfaces;
            depth->storage=std::move(pending);++renderer9_depth_binds;
        }
        state->depth=depth;depth->attached_to=state;return U32(DD_OK);
    }
    U32 detach_depth9(Args a){
        auto& target=object(a[0],Interface::surface7);auto state=target_model9(target);
        if(a[1])return U32(DDERR_INVALIDPARAMS);
        auto depth=object(a[2],Interface::depth7).depth_model;
        if(state->depth!=depth)return U32(DDERR_SURFACENOTATTACHED);
        if(auto device=state->device.lock()){
            const auto hr=device->gpu->bind_depth({});if(host9::failed(hr))return hr;
        }
        // Storage is retained by the depth object, so rebinding on this device
        // preserves Z values across scenes and attachment changes.
        state->depth.reset();depth->attached_to.reset();return U32(DD_OK);
    }
    U32 attached_depth9(Args a){
        auto& target=object(a[0],Interface::surface7);
        if(!p.options.legacy_d3d9 || !target.surface_model)stop("unmodelled attachment query");
        DDSCAPS2 caps{};p.memory.copy_out(a[1],std::span(reinterpret_cast<std::uint8_t*>(&caps),16));
        p.memory.check(a[2],4,Memory::Write);
        if(caps.dwCaps!=DDSCAPS_ZBUFFER || caps.dwCaps2 || caps.dwCaps3 || caps.dwCaps4)
            stop("only exact Z-buffer attachment queries are supported");
        auto depth=target.surface_model->depth;
        if(!depth){p.memory.store(a[2],0,32);return U32(DDERR_NOTFOUND);}
        p.memory.store(a[2],wrap_depth9(std::move(depth)),32);return U32(DD_OK);
    }

// Bounded Windows OpenGL startup boundary. Native contexts and DCs never become
// guest pointers. Cross-thread, foreign-context and implicit-lifetime use stops.
HDC acquired_dc(Process& p,U32 id){
    auto& g=gui(p);auto it=g.acquired_dcs.find(id);
    if(it==g.acquired_dcs.end() || !it->second.refs)unsupported(p,"OpenGL requires an acquired guest window DC");
    owned_window(p,it->second.window);
    return native<HDC>(p,id,Kind::dc);
}
void current_gl(Process& p){
    auto& g=gui(p);auto it=g.contexts.find(g.current_context);
    if(it==g.contexts.end() || ::wglGetCurrentContext()!=it->second.native)
        unsupported(p,"OpenGL requires this guest's current native rendering context");
    if(::wglGetCurrentDC()!=acquired_dc(p,it->second.dc))unsupported(p,"OpenGL current DC ownership mismatch");
}
PIXELFORMATDESCRIPTOR pixel_format(Process& p,U32 at){
    static_assert(sizeof(PIXELFORMATDESCRIPTOR)==40);
    PIXELFORMATDESCRIPTOR value{};
    p.memory.copy_out(at,std::span(reinterpret_cast<std::uint8_t*>(&value),sizeof(value)));
    if(value.nSize!=40 || value.nVersion!=1)unsupported(p,"unsupported PIXELFORMATDESCRIPTOR layout/version");
    return value;
}
void install_wgl(Process& p){
    using Fn=std::function<U32(Process&)>;
    auto add=[&](const char* dll,const char* name,unsigned n,Fn fn){
        p.register_api(dll,name,n,[fn=std::move(fn)](Process& q)->U32{
            auto& g=gui(q);rethrow(g);auto value=fn(q);rethrow(g);return value;
        });
    };
    auto gl=[&](const char* name,unsigned n,Fn fn){
        add("opengl32.dll",name,n,[fn=std::move(fn)](Process& q)->U32{current_gl(q);return fn(q);});
    };
    add("user32.dll","GetDC",1,[](auto& q)->U32{
        auto w=owned_window(q,arg(q,0));auto dc=::GetDC(w->hwnd);
        if(!dc){q.set_error(::GetLastError());return 0;}
        auto& g=gui(q);auto id=token(g,Kind::dc,reinterpret_cast<std::uintptr_t>(dc));
        auto& owned=g.acquired_dcs[id];if(owned.refs && owned.window!=w->id)unsupported(q,"native DC ownership collision");
        owned.window=w->id;++owned.refs;return id;
    });
    add("user32.dll","ReleaseDC",2,[](auto& q)->U32{
        auto w=owned_window(q,arg(q,0));auto id=arg(q,1);auto dc=acquired_dc(q,id);auto& g=gui(q);
        auto& owned=g.acquired_dcs.at(id);if(owned.window!=w->id)unsupported(q,"DC belongs to another guest window");
        for(const auto& [key,context]:g.contexts){(void)key;if(context.dc==id)unsupported(q,"delete rendering contexts before releasing their DC");}
        if(w->paint && w->paint->dc==id)unsupported(q,"ReleaseDC during an active paint transaction");
        auto result=::ReleaseDC(w->hwnd,dc);if(!result){q.set_error(::GetLastError());return 0;}
        if(!--owned.refs){g.handles.at(id).native=0;g.acquired_dcs.erase(id);}return 1;
    });
    add("gdi32.dll","ChoosePixelFormat",2,[](auto& q)->U32{auto format=pixel_format(q,arg(q,1));auto value=::ChoosePixelFormat(acquired_dc(q,arg(q,0)),&format);if(!value)q.set_error(::GetLastError());return U32(value);});
    add("gdi32.dll","SetPixelFormat",3,[](auto& q)->U32{auto format=pixel_format(q,arg(q,2));auto value=::SetPixelFormat(acquired_dc(q,arg(q,0)),signed32(arg(q,1)),&format);if(!value)q.set_error(::GetLastError());return value!=FALSE;});
    add("gdi32.dll","GetPixelFormat",1,[](auto& q)->U32{auto value=::GetPixelFormat(acquired_dc(q,arg(q,0)));if(!value)q.set_error(::GetLastError());return U32(value);});
    add("gdi32.dll","DescribePixelFormat",4,[](auto& q)->U32{
        auto at=arg(q,3),count=arg(q,2);PIXELFORMATDESCRIPTOR value{};
        if((at && count!=40) || (!at && count))unsupported(q,"DescribePixelFormat supports a 40-byte buffer or a null query");
        if(at)q.memory.check(at,40,Memory::Write);
        auto result=::DescribePixelFormat(acquired_dc(q,arg(q,0)),signed32(arg(q,1)),count,at?&value:nullptr);
        if(!result){q.set_error(::GetLastError());return 0;}
        if(at)q.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&value),40));return U32(result);
    });
    add("opengl32.dll","wglCreateContext",1,[](auto& q)->U32{
        auto& g=gui(q);auto dc=arg(q,0);auto native_context=::wglCreateContext(acquired_dc(q,dc));
        if(!native_context){q.set_error(::GetLastError());return 0;}
        auto id=token(g,Kind::context,reinterpret_cast<std::uintptr_t>(native_context));
        g.contexts.emplace(id,GuiState::Context{native_context,dc});++g.contexts_created;return id;
    });
    add("opengl32.dll","wglMakeCurrent",2,[](auto& q)->U32{
        auto& g=gui(q);auto dc=arg(q,0),id=arg(q,1);auto host=::wglGetCurrentContext();
        if(host){bool found=false;for(auto& [key,c]:g.contexts){(void)key;found|=c.native==host;}if(!found)unsupported(q,"refusing to replace a foreign native OpenGL context");}
        HGLRC context{};HDC device{};
        if(id){auto it=g.contexts.find(id);if(it==g.contexts.end())unsupported(q,"unknown guest rendering context");if(it->second.dc!=dc)unsupported(q,"cross-DC context binding is outside this profile");context=it->second.native;device=acquired_dc(q,dc);}
        else if(dc)unsupported(q,"context detach requires a null DC in this profile");
        const auto result=::wglMakeCurrent(device,context);g.current_context=result?id:0;
        if(!result)q.set_error(::GetLastError());return result!=FALSE;
    });
    add("opengl32.dll","wglDeleteContext",1,[](auto& q)->U32{
        auto& g=gui(q);auto id=arg(q,0);auto it=g.contexts.find(id);
        if(it==g.contexts.end())unsupported(q,"unknown or stale rendering context");
        if(::wglGetCurrentContext()==it->second.native)unsupported(q,"detach the rendering context before deletion");
        if(!::wglDeleteContext(it->second.native)){q.set_error(::GetLastError());return 0;}
        g.handles.at(id).native=0;g.contexts.erase(it);return 1;
    });
    add("opengl32.dll","wglGetCurrentContext",0,[](auto& q)->U32{if(!::wglGetCurrentContext())return 0;current_gl(q);return gui(q).current_context;});
    add("opengl32.dll","wglGetCurrentDC",0,[](auto& q)->U32{if(!::wglGetCurrentContext())return 0;current_gl(q);auto& g=gui(q);return g.contexts.at(g.current_context).dc;});
    add("gdi32.dll","SwapBuffers",1,[](auto& q)->U32{
        current_gl(q);auto& g=gui(q);auto dc=arg(q,0);
        if(dc!=g.contexts.at(g.current_context).dc)unsupported(q,"SwapBuffers DC differs from the active guest context");
        auto value=::SwapBuffers(acquired_dc(q,dc));if(!value){q.set_error(::GetLastError());return 0;}++g.buffer_swaps;return 1;
    });
    gl("glClearColor",4,[](auto& q)->U32{::glClearColor(std::bit_cast<float>(arg(q,0)),std::bit_cast<float>(arg(q,1)),std::bit_cast<float>(arg(q,2)),std::bit_cast<float>(arg(q,3)));return 0;});
    gl("glClear",1,[](auto& q)->U32{::glClear(arg(q,0));++gui(q).clear_calls;return 0;});
    gl("glViewport",4,[](auto& q)->U32{::glViewport(signed32(arg(q,0)),signed32(arg(q,1)),signed32(arg(q,2)),signed32(arg(q,3)));return 0;});
    gl("glReadBuffer",1,[](auto& q)->U32{::glReadBuffer(arg(q,0));return 0;});
    gl("glFinish",0,[](auto&)->U32{::glFinish();return 0;});
    gl("glFlush",0,[](auto&)->U32{::glFlush();return 0;});
    gl("glGetError",0,[](auto&)->U32{return ::glGetError();});
    gl("glReadPixels",7,[](auto& q)->U32{
        const auto width=arg(q,2),height=arg(q,3),at=arg(q,6);const auto size=std::uint64_t(width)*height*4;
        if(!width || !height || width>4096 || height>4096 || size>16u*1024*1024 || arg(q,4)!=GL_RGBA || arg(q,5)!=GL_UNSIGNED_BYTE)
            unsupported(q,"glReadPixels supports bounded nonempty RGBA8 rectangles only");
        q.memory.check(at,std::size_t(size),Memory::Read|Memory::Write);
        auto data=std::vector<std::uint8_t>(std::size_t(size));q.memory.copy_out(at,data);
        ::glReadPixels(signed32(arg(q,0)),signed32(arg(q,1)),int(width),int(height),GL_RGBA,GL_UNSIGNED_BYTE,data.data());
        q.memory.copy_in(at,data);++gui(q).pixel_read_calls;return 0;
    });
}

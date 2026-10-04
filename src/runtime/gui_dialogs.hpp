// Modal dialogs are created from validated resource bytes, never by loading the
// guest executable as a native module. Handles and callbacks stay width-safe.
U32 Gui::dialog_item(U32 dialog,U32 item){
    auto it=dialogs.find(dialog);if(it==dialogs.end())throw InvalidHandle{1400};
    auto child=GetDlgItem(hwnd(dialog),signed32(item));
    if(!child){p.set_error(GetLastError());return 0;}
    if(!IsChild(it->second->native,child))unsupported("dialog control ownership mismatch");
    return token(Kind::control,reinterpret_cast<std::uintptr_t>(child),dialog);
}
U32 Gui::dialog_long(Args a,bool write){
    auto it=dialogs.find(a[0]);if(it==dialogs.end())unsupported("control window-long access is outside the dialog profile");
    auto& d=*it->second;auto h=hwnd(a[0]);const auto index=signed32(a[1]);
    if(index==4){if(write)unsupported("replacing the dialog procedure is not implemented");return d.procedure;}
    if(index==8){auto old=d.user_data;if(write)d.user_data=a[2];return old;}
    if(index==0){
        // Guest DWL_MSGRESULT stays offset zero; other pointer-sized DWLP fields
        // must NOT be reached using guest offsets on a Win64 window.
        const auto old=GetWindowLongPtrA(h,DWLP_MSGRESULT);
        if(write)SetWindowLongPtrA(h,DWLP_MSGRESULT,signed_param(a[2]));
        return scalar(std::uintptr_t(old));
    }
    if(index==GWLP_HINSTANCE){if(write)unsupported("dialog module replacement");return p.image_base;}
    if(index==GWL_STYLE || index==GWL_EXSTYLE){return write?U32(SetWindowLongA(h,index,signed32(a[2]))):U32(GetWindowLongA(h,index));}
    unsupported("unmodelled dialog window-long field");
}
U32 Gui::control_message(Args a){
    const auto h=hwnd(a[0]);auto& handle=handles.at(a[0]);
    if(handle.kind!=Kind::control)unsupported("control message target is not a dialog child");
    auto owner=dialogs.find(handle.owner);if(owner==dialogs.end())throw InvalidHandle{1400};
    const auto id=std::uint16_t(GetDlgCtrlID(h));std::uint16_t atom=0;
    for(const auto& c:owner->second->shape.controls)if(c.id==id){atom=c.atom;break;}
    if(!atom)unsupported("unregistered internal dialog control");
    const auto m=a[1];const auto wp=a[2],lp=a[3];
    if(m==WM_SETTEXT){auto text=lp?p.read_string(lp):std::string{};return result32(SendMessageA(h,m,wp,lp?reinterpret_cast<LPARAM>(text.c_str()):0));}
    if(m==WM_GETTEXT){
        if(wp>32768)unsupported("control text output limit");if(wp)p.memory.check(lp,wp,Memory::Write);
        std::vector<char> text(std::max(U32(1),wp),0);auto r=SendMessageA(h,m,wp,reinterpret_cast<LPARAM>(text.data()));check();
        if(wp){const auto n=std::min<std::size_t>(text.size(),std::size_t(std::max(LRESULT(0),r))+1);p.memory.copy_in(lp,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),n));}return result32(r);
    }
    if(atom==0x83){
        // Validated templates exclude owner-drawn/no-data list boxes. Every
        // string pointer is copied; item data is guest uint32, not a host ptr.
        switch(m){
        case LB_ADDSTRING:case LB_INSERTSTRING:case LB_FINDSTRING:case LB_FINDSTRINGEXACT:case LB_SELECTSTRING:{
            auto text=p.read_string(lp);return result32(SendMessageA(h,m,signed_param(wp),reinterpret_cast<LPARAM>(text.c_str())));}
        case LB_GETTEXT:{
            const auto n=SendMessageA(h,LB_GETTEXTLEN,signed_param(wp),0);if(n==LB_ERR)return U32(LB_ERR);
            if(n<0 || n>=32768)unsupported("list-box text output limit");p.memory.check(lp,1,Memory::Write);
            std::vector<char> text(std::size_t(n)+1,0);const auto r=SendMessageA(h,m,signed_param(wp),reinterpret_cast<LPARAM>(text.data()));check();
            if(r==LB_ERR)return U32(LB_ERR);if(r<0 || r>n)unsupported("list-box text changed during synchronous query");
            p.memory.copy_in(lp,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),std::size_t(r)+1));return result32(r);}
        case LB_RESETCONTENT:case LB_DELETESTRING:case LB_SETCURSEL:case LB_GETCURSEL:case LB_GETCOUNT:case LB_GETTEXTLEN:
        case LB_SETTOPINDEX:case LB_GETTOPINDEX:case LB_GETSEL:case LB_SETSEL:case LB_GETSELCOUNT:
        case LB_GETITEMDATA:case LB_SETITEMDATA:
            return scalar(std::uintptr_t(SendMessageA(h,m,signed_param(wp),m==LB_SETITEMDATA?LPARAM(lp):signed_param(lp))));
        default:break;
        }
    }
    if(atom==0x80 && (m==BM_GETCHECK || m==BM_SETCHECK || m==BM_GETSTATE || m==BM_SETSTATE || m==BM_CLICK))return result32(SendMessageA(h,m,wp,signed_param(lp)));
    if(m==WM_GETTEXTLENGTH || m==WM_ENABLE || m==WM_SETREDRAW || m==WM_CHANGEUISTATE || m==WM_UPDATEUISTATE || m==WM_QUERYUISTATE)return result32(SendMessageA(h,m,wp,signed_param(lp)));
    unsupported("unmarshalled dialog control message "+hex(m));
}
U32 Gui::dialog_box(Args a){
    if(a[0]!=p.image_base)unsupported("dialog resources from foreign guest modules");
    if(dialogs.size()>=8)unsupported("nested modal dialog limit");
    if(!a[3])unsupported("null guest dialog procedure");p.memory.check(a[3],1,Memory::Execute);
    HWND parent{}; // The legacy API returns zero for an invalid owner.
    try { parent=hwnd(a[2],true); }
    catch(const InvalidHandle&){p.set_error(1400);return 0;}
    ResourceName name=std::uint16_t(a[1]);
    if(a[1]>=65536){auto text=p.read_string(a[1],256);std::u16string wide;for(unsigned char c:text){if(c>=128)unsupported("non-ASCII dialog resource name");wide+=char16_t(c);}name=wide;}
    auto resource=find_resource(p.source_image(),std::uint16_t(5),name);
    if(!resource){p.set_error(1814);return 0xffffffffu;}
    auto bytes=resource_bytes(p.source_image(),*resource);auto shape=inspect_dialog_template(bytes);
    // vector<U32> supplies the required DWORD alignment. The parser checked all
    // variable arrays/control extents before the native dialog manager reads it.
    std::vector<U32> aligned((bytes.size()+3)/4,0);std::copy(bytes.begin(),bytes.end(),reinterpret_cast<std::uint8_t*>(aligned.data()));
    auto d=std::make_shared<Dialog>();d->gui=this;d->procedure=a[3];d->init=a[4];d->shape=std::move(shape);
    d->id=token(Kind::dialog,1,0,false,false);dialogs.emplace(d->id,d);creating_dialogs.push_back(d);
    struct Scope {
        Gui& g;std::shared_ptr<Dialog> d;
        ~Scope(){
            if(!creating_dialogs.empty() && creating_dialogs.back()==d)creating_dialogs.pop_back();
            if(d->native)dialog_owners.erase(d->native);
            for(auto it=g.handles.begin();it!=g.handles.end();){if(it->first==d->id || it->second.owner==d->id)it=g.handles.erase(it);else ++it;}
            g.dialogs.erase(d->id);
        }
    } scope{*this,d};
    const auto result=DialogBoxIndirectParamA(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(aligned.data()),parent,dialog_procedure,reinterpret_cast<LPARAM>(d.get()));
    const auto error=GetLastError();check();if(result==-1)p.set_error(error);else ++dialogs_completed;
    return scalar(std::uintptr_t(result));
}
INT_PTR CALLBACK Gui::dialog_procedure(HWND h,UINT message,WPARAM wp,LPARAM lp) noexcept {
    std::shared_ptr<Dialog> d;
    try{
        if(auto it=dialog_owners.find(h);it!=dialog_owners.end())d=it->second.lock();
        if(!d && !creating_dialogs.empty()){
            d=creating_dialogs.back();
            // Before WM_INITDIALOG the dialog manager may send font messages.
            // Do not associate a second window with an already bound invocation.
            if(d->native && d->native!=h)return FALSE;
            d->native=h;dialog_owners[h]=d;auto& g=*d->gui;
            g.handles.at(d->id).native=reinterpret_cast<std::uintptr_t>(h);++g.dialogs_created;
            d->callback_window=std::make_shared<Window>();d->callback_window->gui=&g;d->callback_window->id=d->id;d->callback_window->native=h;d->callback_window->procedure=d->procedure;
        }
        if(!d)return FALSE;auto& g=*d->gui;
        if(g.stopping || g.failure || g.p.exited()){
            if(message!=WM_NCDESTROY && !d->ending){d->ending=true;EndDialog(h,-1);}return FALSE;
        }
        U32 a=0,b=0,result=0;std::vector<U32> borrowed;
        struct Borrowed {Gui& g;std::vector<U32>& ids;~Borrowed(){for(auto id:ids)g.handles.erase(id);}} borrowed_scope{g,borrowed};
        bool direct=true;
        switch(message){
        case WM_INITDIALOG:
            if(lp!=reinterpret_cast<LPARAM>(d.get()))g.unsupported("dialog initialization context mismatch");
            a=g.window_token(reinterpret_cast<HWND>(wp));b=d->init;break;
        case WM_SETFONT:a=g.token(Kind::font,std::uintptr_t(wp),d->id);b=scalar(std::uintptr_t(lp));break;
        case WM_CTLCOLORMSGBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:case WM_CTLCOLORDLG:case WM_CTLCOLORSCROLLBAR:case WM_CTLCOLORSTATIC:
            a=g.token(Kind::borrowed_dc,std::uintptr_t(wp),d->id,false,false);borrowed.push_back(a);b=g.window_token(reinterpret_cast<HWND>(lp));break;
        default:direct=false;break;
        }
        ++g.dialog_callbacks;
        if(direct){const std::array<U32,4> args{d->id,message,a,b};result=g.p.callback(d->procedure,args);}
        else {Frame frame(g,d->callback_window,message,wp,lp);const std::array<U32,4> args{d->id,message,frame.wp,frame.lp};result=g.p.callback(d->procedure,args);frame.to_native();}
        if(message==WM_NCDESTROY)dialog_owners.erase(h);
        if(message>=WM_CTLCOLORMSGBOX && message<=WM_CTLCOLORSTATIC)return result?reinterpret_cast<INT_PTR>(g.brush(result)):0;
        return signed_param(result);
    }catch(...){
        if(d){auto& g=*d->gui;if(!g.failure)g.failure=std::current_exception();if(message!=WM_NCDESTROY && !d->ending){d->ending=true;EndDialog(h,-1);}if(message==WM_NCDESTROY)dialog_owners.erase(h);}
        return FALSE;
    }
}

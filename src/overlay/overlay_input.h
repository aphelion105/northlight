#pragma once
// Settings overlay input routing (settings_overlay.cpp). No Win32, D3D9 or ImGui dependency: the decisions are
// tested natively (tests/test_settings_overlay.py). The window procedure runs on the game's window thread; it only
// asks route() what to do with a message, queues a copy for the render thread and either passes the message on to
// the game or swallows it. ImGui itself only ever runs on the render thread.
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace NorthlightOverlayInput {
// The Win32 message numbers routed here (static_assert'ed against windows.h in settings_overlay.cpp).
namespace Msg {
constexpr unsigned KillFocus=0x0008,ActivateApp=0x001C,
    KeyDown=0x0100,KeyUp=0x0101,Char=0x0102,SysKeyDown=0x0104,SysKeyUp=0x0105,SysChar=0x0106,
    MouseMove=0x0200,LButtonDown=0x0201,LButtonUp=0x0202,LButtonDblClk=0x0203,RButtonDown=0x0204,RButtonUp=0x0205,RButtonDblClk=0x0206,
    MButtonDown=0x0207,MButtonUp=0x0208,MButtonDblClk=0x0209,MouseWheel=0x020A,XButtonDown=0x020B,XButtonUp=0x020C,XButtonDblClk=0x020D,MouseHWheel=0x020E,
    CaptureChanged=0x0215;
}
constexpr unsigned VkF1=0x70,VkF11=0x7A;

struct Modifiers { bool ctrl=false,shift=false,alt=false; };
// The toggle key ([Renderer] SettingsOverlayKey): F1..F12 with an exact set of modifiers.
struct Hotkey { unsigned vk=VkF11; Modifiers mods{true,true,false}; };
inline bool operator==(const Hotkey& a,const Hotkey& b){return a.vk==b.vk&&a.mods.ctrl==b.mods.ctrl&&a.mods.shift==b.mods.shift&&a.mods.alt==b.mods.alt;}
// "Ctrl+Shift+F11" (case-insensitive, '+' separated, spaces ignored; Control and Alt/Menu accepted). false: malformed, a key
// other than F1..F12, a repeated part, or a combination Northlight already uses (Ctrl+Shift+F7..F10, F12).
inline bool parseHotkey(const std::string& text,Hotkey& out){
    Hotkey k;k.vk=0;k.mods={};std::string part;bool sawCtrl=false,sawShift=false,sawAlt=false;
    auto take=[&](std::string p)->bool{
        std::string t;for(char c:p)if(c!=' '&&c!='\t')t+=char(c>='a'&&c<='z'?c-32:c);
        if(t.empty())return false;
        if(t=="CTRL"||t=="CONTROL"){if(sawCtrl)return false;sawCtrl=k.mods.ctrl=true;return true;}
        if(t=="SHIFT"){if(sawShift)return false;sawShift=k.mods.shift=true;return true;}
        if(t=="ALT"||t=="MENU"){if(sawAlt)return false;sawAlt=k.mods.alt=true;return true;}
        if(k.vk||t.size()<2||t.size()>3||t[0]!='F')return false;
        unsigned n=0;for(std::size_t i=1;i<t.size();++i){if(t[i]<'0'||t[i]>'9')return false;n=n*10+unsigned(t[i]-'0');}
        if(n<1||n>12)return false;k.vk=VkF1+n-1;return true;
    };
    for(char c:text){if(c=='+'){if(!take(part))return false;part.clear();}else part+=c;}
    if(!take(part)||!k.vk)return false;
    if(k.mods.ctrl&&k.mods.shift&&!k.mods.alt&&((k.vk>=VkF1+6&&k.vk<=VkF1+9)||k.vk==VkF1+11))return false;
    out=k;return true;
}
inline std::string hotkeyName(const Hotkey& k){
    std::string s;if(k.mods.ctrl)s+="Ctrl+";if(k.mods.shift)s+="Shift+";if(k.mods.alt)s+="Alt+";
    return s+"F"+std::to_string(k.vk-VkF1+1);
}

// Read by the window procedure. open: the window is shown. captureMouse/captureText: what ImGui wanted at its last
// frame (published by the render thread). heldButtons: buttons whose press went to the overlay. hotkey: the toggle.
struct State { bool open=false,captureMouse=false,captureText=false; unsigned heldButtons=0; Hotkey hotkey; };
// toggle: show/hide the window. queue: copy the message to the render thread. swallow: the game never sees it.
// lost: focus or mouse capture went away (ImGui releases its keys and buttons).
struct Decision { bool toggle=false,queue=false,swallow=false,lost=false; };

// 0 left, 1 right, 2 middle, 3/4 X buttons; -1 not a button message.
inline int button(unsigned msg,std::uintptr_t wparam){
    switch(msg){
    case Msg::LButtonDown:case Msg::LButtonUp:case Msg::LButtonDblClk:return 0;
    case Msg::RButtonDown:case Msg::RButtonUp:case Msg::RButtonDblClk:return 1;
    case Msg::MButtonDown:case Msg::MButtonUp:case Msg::MButtonDblClk:return 2;
    case Msg::XButtonDown:case Msg::XButtonUp:case Msg::XButtonDblClk:return ((wparam>>16)&0xFFFF)==2?4:3;
    default:return -1;
    }
}
inline bool buttonUp(unsigned msg){return msg==Msg::LButtonUp||msg==Msg::RButtonUp||msg==Msg::MButtonUp||msg==Msg::XButtonUp;}
inline bool hotkey(unsigned msg,std::uintptr_t wparam,Modifiers m,const Hotkey& k){
    return (msg==Msg::KeyDown||msg==Msg::SysKeyDown)&&wparam==k.vk&&m.ctrl==k.mods.ctrl&&m.shift==k.mods.shift&&m.alt==k.mods.alt;
}

// The hotkey toggles and never reaches the game (auto-repeats are swallowed without toggling again); a toggle forgets
// held buttons. While the window is open: mouse moves and key-ups always reach the game (no stuck keys or cursor);
// presses, double clicks and the wheel go to ImGui only while it wants the mouse (the cursor is over the window or a drag
// started on it), and the release of a press that went to ImGui stays with ImGui; a press the game gets clears that
// button's mark, so its release always reaches the game. Losing focus or the mouse capture forgets every mark (the
// release may never come). Key downs and characters go to ImGui only while one of its text fields is active.
inline Decision route(unsigned msg,std::uintptr_t wparam,std::intptr_t lparam,Modifiers m,State& s){
    Decision d;
    if(hotkey(msg,wparam,m,s.hotkey)){d.toggle=(lparam&(std::intptr_t(1)<<30))==0;d.swallow=true;if(d.toggle)s.heldButtons=0;return d;}
    const int b=button(msg,wparam);
    if(b>=0){
        const unsigned bit=1u<<b;
        if(buttonUp(msg)){d.swallow=(s.heldButtons&bit)!=0;d.queue=s.open||d.swallow;s.heldButtons&=~bit;return d;}
        d.queue=s.open;d.swallow=s.open&&s.captureMouse;
        if(d.swallow)s.heldButtons|=bit;else s.heldButtons&=~bit;
        return d;
    }
    if(msg==Msg::KillFocus||msg==Msg::CaptureChanged||(msg==Msg::ActivateApp&&wparam==0)){s.heldButtons=0;d.lost=true;d.queue=s.open;return d;}
    if(!s.open)return d;
    switch(msg){
    case Msg::MouseMove:case Msg::KeyUp:case Msg::SysKeyUp:d.queue=true;break;
    case Msg::MouseWheel:case Msg::MouseHWheel:d.queue=true;d.swallow=s.captureMouse;break;
    case Msg::KeyDown:case Msg::SysKeyDown:case Msg::Char:case Msg::SysChar:d.queue=true;d.swallow=s.captureText;break;
    default:break;
    }
    return d;
}

// Client-area pixel -> back buffer pixel (the swap chain may be smaller or larger than the window).
inline float mapCoordinate(int client,int clientSize,unsigned backSize){
    return clientSize>0&&backSize?float(client)*float(backSize)/float(clientSize):float(client);
}

// One queued window message, with what the render thread needs to replay it into ImGui.
struct Event { unsigned msg=0; std::uintptr_t wparam=0; std::intptr_t lparam=0; int clientWidth=0,clientHeight=0; Modifiers mods; };

// Window thread pushes, render thread drains. Bounded: a stalled render thread drops input, never grows memory.
class Queue {
    mutable std::mutex mutex;
    std::vector<Event> events;
public:
    static constexpr std::size_t Capacity=512;
    bool push(const Event& e){std::lock_guard<std::mutex> lock(mutex);if(events.size()>=Capacity)return false;events.push_back(e);return true;}
    void drain(std::vector<Event>& out){out.clear();std::lock_guard<std::mutex> lock(mutex);out.swap(events);}
    void clear(){std::lock_guard<std::mutex> lock(mutex);events.clear();}
    std::size_t size()const{std::lock_guard<std::mutex> lock(mutex);return events.size();}
};
} // namespace NorthlightOverlayInput

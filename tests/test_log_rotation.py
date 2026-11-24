#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Previous-session log rotation (northlight-renderer.prev.log). Native tests of the
production log_rotation.h with (1) a fake filesystem that models the Win32 rules
the adapter relies on (MoveFileExW REPLACE_EXISTING refuses a read-only target
and a source open without delete sharing; a stub log without a device
never replaces the previous session) and (2) real POSIX files in a temporary directory. Plus a source
audit of the renderer.cpp wiring. No Wine, Windows binary or game is run."""

from source_units import source_text
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
source=r'''
#include "log_rotation.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
using namespace NorthlightLogRotation;
static const std::string S1="Northlight renderer 0.3.144; backend=legacy\nLOG previous session log\nCreateDevice HRESULT=0x00000000 flags=0x52\nsession one\n";
static const std::string S2="Northlight renderer 0.3.144; backend=legacy\nLOG previous session log\nCreateDevice HRESULT=0x00000000 flags=0x52\nsession two\n";
static const std::string STUB="Northlight renderer 0.3.144; backend=legacy\nLOG previous session log northlight-renderer.prev.log rotation=moved error=0\n";
// Win32 model: files with contents, read-only attribute and "open by another
// process with FILE_SHARE_READ|WRITE but no FILE_SHARE_DELETE" (msvcrt fopen).
struct FakeFs {
    struct File {std::string data;bool readOnly=false,lockedNoDelete=false,unreadable=false;};
    std::map<std::wstring,File> files;unsigned moves=0,attributes=0,reads=0;
    bool exists(const std::wstring& p){return files.count(p)!=0;}
    bool readOnly(const std::wstring& p){auto f=files.find(p);return f!=files.end()&&f->second.readOnly;}
    bool session(const std::wstring& p){++reads;auto f=files.find(p);if(f==files.end()||f->second.unreadable)return false;
        const std::string head=f->second.data.substr(0,16384);return recordedSession(head.data(),(unsigned long)head.size());}
    bool move(const std::wstring& from,const std::wstring& to){++moves;
        auto s=files.find(from);if(s==files.end()||s->second.lockedNoDelete)return false;
        auto d=files.find(to);if(d!=files.end()&&(d->second.readOnly||d->second.lockedNoDelete))return false;
        File f=s->second;files.erase(s);files[to]=f;return true;}
    bool makeWritable(const std::wstring& p){++attributes;auto f=files.find(p);if(f==files.end())return false;f->second.readOnly=false;return true;}
    // An instance opens the log with "w": truncate and hold it (no delete sharing).
    void open(const std::wstring& p,const std::string& data){auto& f=files[p];f.data=data;f.lockedNoDelete=true;}
    void close(const std::wstring& p){files[p].lockedNoDelete=false;}
};
struct PosixFs {
    bool exists(const std::string& p){struct stat st;return ::stat(p.c_str(),&st)==0;}
    bool move(const std::string& a,const std::string& b){return std::rename(a.c_str(),b.c_str())==0;}
    bool readOnly(const std::string& p){struct stat st;return ::stat(p.c_str(),&st)==0&&S_ISREG(st.st_mode)&&!(st.st_mode&S_IWUSR);}
    bool makeWritable(const std::string& p){return ::chmod(p.c_str(),0644)==0;}
    bool session(const std::string& p){std::ifstream in(p,std::ios::binary);if(!in)return false;char head[16384];in.read(head,sizeof(head));return recordedSession(head,(unsigned long)in.gcount());}
};
static std::string read(const std::string& p){std::ifstream in(p,std::ios::binary);std::stringstream s;s<<in.rdbuf();return s.str();}
static void write(const std::string& p,const std::string& d){std::ofstream(p,std::ios::binary|std::ios::trunc)<<d;}
int main(int argc,char** argv){
    assert(argc==2);
    const std::wstring cur=L"C:\\wow\\northlight-renderer.log",prev=L"C:\\wow\\northlight-renderer.prev.log";
    assert(recordedSession(S1.data(),(unsigned long)S1.size())&&!recordedSession(STUB.data(),(unsigned long)STUB.size())&&!recordedSession("",0));
    {FakeFs fs;assert(rotate(fs,cur,prev)==Result::Missing&&fs.moves==0&&!fs.exists(prev));} // first ever start
    {FakeFs fs;fs.files[cur].data=S1;assert(rotate(fs,cur,prev)==Result::Moved);assert(!fs.exists(cur)&&fs.files[prev].data==S1);}
    {FakeFs fs;fs.files[cur].data=S2;fs.files[prev].data=S1;assert(rotate(fs,cur,prev)==Result::Moved&&fs.files[prev].data==S2&&!fs.exists(cur));}
    {FakeFs fs;fs.files[cur].data=S2;fs.files[prev].data=S1;fs.files[prev].readOnly=true; // read-only previous
     assert(rotate(fs,cur,prev)==Result::Replaced&&fs.files[prev].data==S2&&!fs.exists(cur)&&fs.attributes==1&&fs.moves==2);}
    {FakeFs fs;fs.files[cur].data=S1;fs.files[cur].readOnly=true; // read-only current: rename still works
     assert(rotate(fs,cur,prev)==Result::Moved&&fs.files[prev].readOnly&&!fs.exists(cur));
     fs.files[cur].data=S2;assert(rotate(fs,cur,prev)==Result::Replaced&&fs.files[prev].data==S2);} // and is replaced next time
    // Probe stub (closed or held) never replaces the previous session.
    {FakeFs fs;fs.files[cur].data=STUB;fs.files[prev].data=S1;assert(rotate(fs,cur,prev)==Result::Stub&&fs.files[prev].data==S1&&fs.files[cur].data==STUB&&fs.moves==0);}
    {FakeFs fs;fs.open(cur,STUB);fs.files[prev].data=S1;assert(rotate(fs,cur,prev)==Result::Stub&&fs.files[prev].data==S1&&fs.moves==0);}
    // A real session held by another live instance: previous kept, nothing copied, attributes untouched.
    {FakeFs fs;fs.open(cur,S2);fs.files[prev].data=S1;assert(rotate(fs,cur,prev)==Result::InUse&&fs.files[prev].data==S1&&fs.files[cur].data==S2&&fs.attributes==0);}
    {FakeFs fs;fs.open(cur,S2);fs.files[prev].data=S1;fs.files[prev].readOnly=true;assert(rotate(fs,cur,prev)==Result::InUse&&fs.files[prev].data==S1);}
    {FakeFs fs;fs.files[cur].data=S1;fs.files[cur].unreadable=true;fs.files[prev].data="older";assert(rotate(fs,cur,prev)==Result::Stub&&fs.files[prev].data=="older");}
    // Full launches, both orders: the crash/previous session survives in .prev.
    for(int order=0;order<3;++order){FakeFs fs;fs.files[cur].data=S1; // S1 = the session that just ended (e.g. crashed)
        auto start=[&](const std::string& writes){const Result r=rotate(fs,cur,prev);fs.open(cur,writes);return r;};
        if(order==0){assert(start(STUB)==Result::Moved);assert(start(S2)==Result::Stub);}           // probe first, still running
        else if(order==1){assert(start(STUB)==Result::Moved);fs.close(cur);assert(start(S2)==Result::Stub);} // probe first, already exited
        else{assert(start(S2)==Result::Moved);assert(rotate(fs,cur,prev)==Result::InUse);}          // game first, probe later
        assert(fs.files[prev].data==S1);
        fs.close(cur);if(order!=2)fs.files[cur].data=S2;assert(rotate(fs,cur,prev)==Result::Moved&&fs.files[prev].data==S2);} // next launch keeps S2
    for(auto r:{Result::Missing,Result::Moved,Result::Replaced,Result::Stub,Result::InUse,Result::Failed})assert(name(r)&&*name(r));
    // Real files (POSIX rename semantics: read-only targets are replaceable).
    const std::string dir=argv[1],c=dir+"/northlight-renderer.log",p=dir+"/northlight-renderer.prev.log";
    PosixFs fs;assert(rotate(fs,c,p)==Result::Missing&&!fs.exists(p));
    write(c,S1);assert(rotate(fs,c,p)==Result::Moved&&read(p)==S1&&!fs.exists(c));
    write(c,STUB);assert(rotate(fs,c,p)==Result::Stub&&read(p)==S1&&read(c)==STUB);
    write(c,S2);::chmod(p.c_str(),0444);assert(rotate(fs,c,p)==Result::Moved&&read(p)==S2&&!fs.exists(c));
    write(c,S1);const std::string sub=dir+"/prevdir";::mkdir(sub.c_str(),0755); // target cannot be replaced by a file
    assert(rotate(fs,c,sub)==Result::InUse&&read(c)==S1);
    std::puts("PASS log rotation: missing/moved/replaced/read-only previous and current; probe stub never replaces the previous session (held or closed); live real session kept, nothing copied; launch orders probe-first (running/exited) and game-first keep the ended session in .prev; real files");
}
'''
r=source_text(fp.src('renderer.cpp'))
logger=r[r.index('static void logf('):r.index('static void reportLogCost()')]
checks={
 'rotation once, before the first open, inside the logger lock':re.search(r'AcquireSRWLockExclusive\(&logLock\);.*if \(!logFile\) \{\s*if\(!logRotated\)\{logRotated=true;rotatePreviousLog\(\);\}\s*wchar_t path\[MAX_PATH\];.*_wfopen\(path, L"w"\)',logger,re.S) is not None,
 'Win32 adapter: atomic replace, no reboot-delay or write-through wait':'MoveFileExW(from,to,MOVEFILE_REPLACE_EXISTING)' in r and 'MOVEFILE_DELAY_UNTIL_REBOOT' not in r and 'MOVEFILE_WRITE_THROUGH' not in r,
 'Win32 adapter: only the read-only bit of a read-only file is cleared; nothing is copied':'(a&FILE_ATTRIBUTE_READONLY)&&!(a&FILE_ATTRIBUTE_DIRECTORY)' in r and 'SetFileAttributesW(path,a&~DWORD(FILE_ATTRIBUTE_READONLY))' in r and 'CopyFileW' not in r,
 'Win32 adapter: session check reads with full sharing, never blocks a live writer':'CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING' in r and 'NorthlightLogRotation::recordedSession(head,got)' in r and 'CloseHandle(h)' in r,
 'paths next to the DLL':'L"%lsnorthlight-renderer.log",rootPath' in r and 'L"%lsnorthlight-renderer.prev.log",rootPath' in r,
 'path formatting failure is a no-op':'logRotation=NorthlightLogRotation::Result::Failed;return;' in r,
 'result reported at start-up (after the version line)':r.index('logf("Northlight renderer 0.3.')<r.index('logf("LOG previous session log northlight-renderer.prev.log rotation=%s error=%lu"'),
 'no rotation work in DllMain':'rotate' not in r[r.index('BOOL WINAPI DllMain'):],
}
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-log-rotation-') as tmp:
    t=Path(tmp);(t/'test.cpp').write_text(source);(t/'files').mkdir()
    for label,flags in [('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
        exe=t/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(t/'test.cpp'),'-o',str(exe)],check=True)
        d=t/('files-'+label);d.mkdir()
        out=subprocess.run([str(exe),str(d)],capture_output=True,text=True,timeout=60);print(label+': '+out.stdout+out.stderr,end='');out.check_returncode()

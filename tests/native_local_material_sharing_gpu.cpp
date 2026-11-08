// Differential GPU oracle for the production endpoint sharing transform.
#include <windows.h>
#include <d3d9.h>
#include "native_local_light_patch.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace NorthlightNativeLocalPatch;
static void checked(HRESULT hr,int line){if(FAILED(hr)){printf("HRESULT %08lx line=%d\n",(unsigned long)hr,line);abort();}}
#define ok(hr) checked((hr),__LINE__)
static void emit(std::vector<Word>& p,unsigned op,std::initializer_list<Word> operands){p.push_back(op|(unsigned(operands.size())<<24));p.insert(p.end(),operands);}
static Word sw(Word t,unsigned swizzle){return (t&~0x00ff0000u)|(swizzle<<16);}
int main(int argc,char**argv){assert(argc==2);setvbuf(stdout,nullptr,_IONBF,0);
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NativeSharingOracle";assert(RegisterClassW(&wc));auto window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
 auto lib=LoadLibraryA(argv[1]);assert(lib);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(lib,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9*d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 std::vector<Word> v{0xfffe0300u};emit(v,31,{0x80000000u,dst(1,0)});emit(v,31,{0x80000005u,dst(1,1)});emit(v,31,{0x80000000u,dst(6,0)});emit(v,31,{0x8000000au,dst(6,1)});for(unsigned i=0;i<4;++i)emit(v,31,{0x80000005u|(i<<16),dst(6,i+2)});emit(v,1,{dst(6,0),src(1,0)});emit(v,1,{dst(6,2),src(1,1)});emit(v,1,{dst(0,0),src(2,1)});emit(v,4,{dst(6,1),src(1,1),src(2,0),src(0,0)});for(unsigned i=0;i<3;++i)emit(v,4,{dst(6,i+3),src(1,1),src(2,i+2),src(0,0)});v.push_back(65535);IDirect3DVertexShader9*vs=nullptr;ok(d->CreateVertexShader((DWORD*)v.data(),&vs));ok(d->SetVertexShader(vs));
 D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9*vd=nullptr;ok(d->CreateVertexDeclaration(decl,&vd));ok(d->SetVertexDeclaration(vd));const float quad[][8]={{-1,1,0,1,0,0,.3f,1},{1,1,0,1,1,0,.3f,1},{-1,-1,0,1,0,1,.3f,1},{1,-1,0,1,1,1,.3f,1}};
 float vc[5][4]={{.8f,.6f,.4f,0},{.1f,.2f,.3f,.7f},{.3f,.4f,.2f,0},{.6f,.1f,.5f,0},{.2f,.7f,.3f,0}};ok(d->SetVertexShaderConstantF(0,vc[0],5));float pc[11][4]{};pc[0][0]=.25f;pc[0][1]=.125f;pc[0][2]=.5f;pc[0][3]=1;for(float&x:pc[10])x=.25f;ok(d->SetPixelShaderConstantF(0,pc[0],11));
 IDirect3DTexture9*tex=nullptr;ok(d->CreateTexture(32,32,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tex,nullptr));for(unsigned level=0;level<tex->GetLevelCount();++level){D3DSURFACE_DESC desc;tex->GetLevelDesc(level,&desc);D3DLOCKED_RECT lock;ok(tex->LockRect(level,&lock,nullptr,0));for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x)((DWORD*)((char*)lock.pBits+y*lock.Pitch))[x]=0xff000000u|((x*17+level*35)%256)<<16|((y*19+level*22)%256)<<8|((x+y)*13%256);ok(tex->UnlockRect(level));}ok(d->SetTexture(0,tex));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER})ok(d->SetSamplerState(0,key,D3DTEXF_LINEAR));
 IDirect3DSurface9*rt=nullptr,*copy=nullptr;ok(d->CreateRenderTarget(64,64,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));ok(d->CreateOffscreenPlainSurface(64,64,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&copy,nullptr));ok(d->SetRenderTarget(0,rt));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
 unsigned tests=0,negative=0;for(unsigned mode=0;mode<12;++mode)for(unsigned count:{2u,4u}){
 std::vector<Word> p{0xffff0300u};emit(p,31,{0x8000000au,dst(1,0)});emit(p,31,{0x80000005u,dst(1,1)});emit(p,31,{0x90000000u,dst(10,0)});
 emit(p,1,{dst(0,0),src(1,1)});
 if(mode==1){emit(p,1,{dst(0,0,1),src(2,0)});emit(p,1,{dst(0,0,2),sw(src(1,0),0x55)});} // mixed aliases
 if(mode==2)emit(p,4,{dst(0,0),src(1,1),src(2,0),src(2,0)});
 if(mode==3)emit(p,2,{dst(0,0,3),src(0,0),src(1,0)}); // dependent sample
 if(mode==4){emit(p,91,{dst(0,1),src(1,1)});emit(p,92,{dst(0,2),src(1,1)});emit(p,93,{dst(0,3),src(0,0),src(10,0),src(0,1),src(0,2)});}
 else if(mode==5){emit(p,1,{dst(0,0,8),sw(src(2,0),0x00)});emit(p,95,{dst(0,3),src(0,0),src(10,0)});}
 else emit(p,66,{dst(0,3),src(0,0),src(10,0)});
 if(mode==6){emit(p,91,{dst(0,1),src(1,0)});emit(p,2,{dst(0,3,7),src(0,3),src(0,1)});}
 if(mode==7){emit(p,35,{dst(0,1,2),sw(src(1,0),0)});emit(p,2,{dst(0,3,7),src(0,3),sw(src(0,1),0x55)});}
 if(mode==8)emit(p,65,{dst(0,0)}); // optimizer must reject discard epochs
 if(mode==9)emit(p,1,{dst(0,1)|0x00200000u,src(2,0)}); // partial precision
 if(mode==10)for(unsigned i=1;i<28;++i)if(i!=3)emit(p,1,{dst(0,i),src(1,0)}); // no cache registers
 if(mode==11){emit(p,1,{dst(0,0,3),src(2,0)});emit(p,1,{dst(0,0,12),src(1,0)});emit(p,5,{dst(0,3,7),src(0,3),sw(src(0,0),0x1b)});}
 emit(p,5,{dst(8,0,7)|0x00100000u,src(0,3),src(1,0)});emit(p,1,{dst(8,0,8),src(0,3)});p.push_back(65535);
 std::vector<NorthlightHdrUi::Instruction> ops;unsigned originalCost=0;assert(NorthlightHdrUi::parse(p.data(),p.size(),ops,originalCost));bool used[32]{};for(auto&op:ops)for(unsigned k=1;k<op.count;++k)if(regType(p[op.at+k])==0)used[regIndex(p[op.at+k])]=true;const unsigned endpoint[]={28,29,30,31},varying[]={1,2,3,4};for(unsigned r:endpoint)used[r]=true;
 std::vector<Word> shared;unsigned cost=0,removed=0,textures=0;bool accepted=sharedEndpoints(p.data(),ops,0,count,endpoint,varying,used,shared,cost,removed,textures);if(mode>=8&&mode<=10){assert(!accepted);++negative;continue;}assert(accepted);
 std::vector<Word> head{p[0]},duplicate;for(auto&op:ops)if(op.op==31)head.insert(head.end(),p.begin()+op.at,p.begin()+op.at+op.count);for(unsigned i=1;i<count;++i)emit(head,31,{0x80000005u|(i<<16),dst(1,i+1)});
 for(unsigned subset=0;subset<count;++subset)for(auto&op:ops)if(op.op!=31){std::vector<Word> inst(p.begin()+op.at,p.begin()+op.at+op.count);for(unsigned k=1;k<inst.size();++k){if(regType(inst[k])==8)inst[k]=replaceReg(inst[k],0,endpoint[subset]);else if(subset&&regType(inst[k])==1&&regIndex(inst[k])==0)inst[k]=replaceReg(inst[k],1,varying[subset]);}duplicate.insert(duplicate.end(),inst.begin(),inst.end());}
 auto render=[&](std::vector<Word> body){std::vector<Word> shader=head;shader.insert(shader.end(),body.begin(),body.end());emit(shader,1,{dst(0,27),src(0,28)});for(unsigned i=1;i<count;++i)emit(shader,2,{dst(0,27),src(0,27),src(0,endpoint[i])});emit(shader,5,{dst(8,0),src(0,27),src(2,10)});shader.push_back(65535);IDirect3DPixelShader9*ps=nullptr;ok(d->CreatePixelShader((DWORD*)shader.data(),&ps));ok(d->SetPixelShader(ps));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));ok(d->EndScene());ok(d->GetRenderTargetData(rt,copy));D3DLOCKED_RECT lock;ok(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> bytes(64*64*8);for(unsigned y=0;y<64;++y)memcpy(bytes.data()+y*512,(char*)lock.pBits+y*lock.Pitch,512);ok(copy->UnlockRect());ps->Release();return bytes;};auto a=render(duplicate),b=render(shared);assert(a==b);++tests;printf("SHARING_DIFFERENTIAL mode=%u endpoints=%u removed=%u textures=%u bitexact=1\n",mode,count,removed,textures);
 }
 printf("NATIVE_SHARING_SYNTHETIC_PASS images=%u fallbackControls=%u\n",tests,negative);
}

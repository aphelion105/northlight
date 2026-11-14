#pragma once
#define main compositeFixtureMain
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <chrono>
static IDirect3DPixelShader9* shader(IDirect3DDevice9* d,const std::string& path){auto b=read(path);IDirect3DPixelShader9* p=nullptr;ok(d->CreatePixelShader((const DWORD*)b.data(),&p));return p;}
template<class Pixel>static IDirect3DTexture9* pixels(IDirect3DDevice9* d,unsigned w,unsigned h,Pixel pixel){Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(size_t(w)*h*16)};for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto v=pixel(x,y);std::memcpy(im.pixels.data()+(size_t(y)*w+x)*16,v.data(),16);}return texture(d,im);}

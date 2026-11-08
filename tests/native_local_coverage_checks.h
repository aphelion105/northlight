#pragma once
// The real coverage factory used to hide native material endpoints from local
// lighting. Compare its joint output with an independently corrected native draw.
template<class Submit,class Read,class ReadSurface,class Fill> void nativeLocalCoverage(IDirect3DDevice9* d,
    NorthlightNativeLocalDraw light,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,
    Submit submit,Read read,ReadSurface readSurface,Fill fill){
    using namespace NorthlightEffectCoverage;
    const auto must=[](HRESULT hr){assert(SUCCEEDED(hr));};
    const auto shaderState=[&](IDirect3DPixelShader9* expected){IDirect3DPixelShader9* p=nullptr;IDirect3DVertexShader9* v=nullptr;
        must(d->GetPixelShader(&p));must(d->GetVertexShader(&v));assert(p==expected&&v==vs);p->Release();v->Release();};
    const auto draw=[&](auto call){must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());must(call());must(d->EndScene());shaderState(ps);return read();};
    fill(1);light.gain=1;const auto original=draw(submit);unsigned cases=0,negative=0;
    for(unsigned count=1;count<=2;++count){
        NorthlightNativeLocalLighting renderer(d);auto second=light;second.slot=1;
        const auto corrected=[&](IDirect3DPixelShader9* bound,const NativePixel* owned){return renderer.draw(light,vs,bound,submit,count==2?&second:nullptr,nullptr,nullptr,nullptr,owned);};
        std::vector<float> reference;
        for(unsigned warm=0;warm<3;++warm){++light.rendererFrame;reference=draw([&]{return corrected(ps,nullptr);});}
        if(count==2&&!renderer.multiDraws)continue;
        assert(reference!=original);float difference=0;for(unsigned i=0;i<reference.size();++i)difference=std::max(difference,std::fabs(reference[i]-original[i]));assert(difference>.03f);
        for(auto write:{Write::Alpha,Write::Premultiplied,Write::Replace,Write::Clear,Write::Additive,Write::Modulate}){
            Capture capture;must(d->BeginScene());must(capture.draw(d,Write::Alpha,submit));must(capture.draw(d,Write::Additive,submit));must(d->EndScene());assert(capture.active()&&!capture.nativePixel());
            unsigned called=0;for(unsigned warm=0;warm<3;++warm){++light.rendererFrame;must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x80204060,1,0));must(d->BeginScene());
            must(capture.draw(d,write,[&]{++called;IDirect3DPixelShader9* bound=nullptr;must(d->GetPixelShader(&bound));const auto* owned=capture.nativePixel();
                assert(owned&&owned->source==ps&&owned->matches(bound)&&bound!=ps&&owned->count==(write==Write::Additive||write==Write::Modulate?1u:2u));
                const auto before=renderer.draws;const auto hr=corrected(bound,owned);assert(renderer.draws==before+1);shaderState(bound);bound->Release();return hr;}));
            must(d->EndScene());assert(!capture.nativePixel());shaderState(ps);}assert(called==3);const auto image=read();if(image!=reference){unsigned differing=0,first=0;float worst=0;for(unsigned i=0;i<image.size();++i)if(image[i]!=reference[i]){if(!differing)first=i;++differing;worst=std::max(worst,std::fabs(image[i]-reference[i]));}std::printf("COVERAGE_COLOR_FAIL count=%u write=%u pixels=%u first=%u ref=%g actual=%g maxError=%g\n",count,unsigned(write),differing,first,reference[first],image[first],worst);}assert(image==reference);
            IDirect3DSurface9* scene=nullptr;must(d->GetRenderTarget(0,&scene));auto* packed=capture.texture(d,scene);scene->Release();assert(packed&&capture.hasEmission());
            IDirect3DSurface9* surface=nullptr;must(packed->GetSurfaceLevel(0,&surface));const auto mask=readSurface(surface);surface->Release();
            for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x){const unsigned i=(y*256+x)*4;
                const float alpha=write==Write::Clear?0.f:write==Write::Premultiplied?image[i+3]:1.f;
                assert(std::fabs(mask[i+3]-alpha)<.001f);
                for(unsigned c=0;c<3;++c){const float energy=write==Write::Additive||write==Write::Modulate?image[i+c]:0.f;assert(std::fabs(mask[i+c]-energy)<.001f);}
            }
            // A foreign/mismatched plan must never authorize unwrapping an MRT.
            if(write==Write::Clear){
                for(bool mismatch:{false,true}){NorthlightNativeLocalLighting control(d);++light.rendererFrame;
                    const auto broken=draw([&]{return capture.draw(d,write,[&]{IDirect3DPixelShader9* bound=nullptr;must(d->GetPixelShader(&bound));NativePixel invalid=*capture.nativePixel();invalid.bound=ps;
                        const auto hr=control.draw(light,vs,bound,submit,nullptr,nullptr,nullptr,nullptr,mismatch?&invalid:nullptr);bound->Release();return hr;});});
                    assert(broken==original&&control.fallbacks==1&&!control.draws&&!capture.nativePixel());++negative;
                }
                must(d->BeginScene());assert(capture.draw(d,write,[&]{assert(capture.nativePixel());return E_FAIL;})==E_FAIL);must(d->EndScene());assert(!capture.nativePixel());shaderState(ps);
            }
            IDirect3DSurface9* aux=nullptr;assert(d->GetRenderTarget(1,&aux)==D3DERR_NOTFOUND);assert(d->GetRenderTarget(2,&aux)==D3DERR_NOTFOUND);++cases;
        }
    }
    assert(cases>=6&&negative>=2);std::printf("NATIVE_COVERAGE_PASS cases=%u negativeControls=%u color0=bitexact coverageAndEmission=preserved failureScope=restored mismatch=conservative\n",cases,negative);
}

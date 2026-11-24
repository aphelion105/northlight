#pragma once
#include "point_opaque_face_cache.h"

namespace NorthlightPointShadow {

// The proof and the R32F image must both belong to the slot's token.
class MixedFaceCache {
public:
    struct Image {
        IDirect3DTexture9* texture=nullptr;
        IDirect3DSurface9* surface=nullptr;
        uint64_t token=0;
    };
    struct Match {
        const Image* image=nullptr;
        const OpaqueFaceCache::Work* work=nullptr;
        explicit operator bool()const{return image&&work;}
    };

    MixedFaceCache()=default;
    MixedFaceCache(const MixedFaceCache&)=delete;
    MixedFaceCache& operator=(const MixedFaceCache&)=delete;
    ~MixedFaceCache(){clear();}

    const auto& opaque()const{return opaque_;}
    const auto& remaining()const{return remaining_;}
    bool canStore()const{return !storeRefused_;}
    void refuseStore(){storeRefused_=true;}

    template<class Candidates,class At,class Rows>
    bool split(const Candidates& candidates,const At& at,const Rows& rows)noexcept{
        opaque_.clear();
        remaining_.clear();
        triangles_=0;
        try{
            for(size_t index:candidates){
                const auto& replay=at(index);
                if(OpaqueFaceCache::supportsReplay(replay)&&rows(replay)){
                    if(opaque_.size()==OpaqueFaceCache::PacketLimit)return false;
                    opaque_.push_back(index);
                    triangles_+=replay.count;
                }else remaining_.push_back(index);
            }
        }catch(const std::bad_alloc&){return false;}
        return !opaque_.empty()&&!remaining_.empty();
    }

    template<class At,class Rows>
    Match match(uint64_t token,const OpaqueFaceCache::Key& key,const At& at,const Rows& rows)const{
        if(!token)return {};
        const auto& image=images_[token%images_.size()];
        if(image.token!=token)return {};
        const auto* work=proof_.match(token,key,opaque_,at,rows);
        return work?Match{&image,work}:Match{};
    }

    template<class At,class Rows,class Draw>
    uint64_t store(IDirect3DDevice9* d,UINT size,const OpaqueFaceCache::Key& key,
                   const At& at,const Rows& rows,const Draw& draw){
        if(storeRefused_||opaque_.empty()||remaining_.empty())return 0;
        const auto n=unsigned(opaque_.size());
        const auto token=proof_.record(key,opaque_,at,rows,{n,n,triangles_});
        if(!token)return 0;
        auto& image=images_[token%images_.size()];
        image.token=0;
        if(!image.surface){
            if(FAILED(d->CreateTexture(size,size,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&image.texture,nullptr))||
               !image.texture||FAILED(image.texture->GetSurfaceLevel(0,&image.surface))||!image.surface){
                release(image);
                refuseStore();
                return 0;
            }
        }
        if(!draw(image.surface)){refuseStore();return 0;}
        image.token=token;
        return token;
    }

    void clear(){
        for(auto& image:images_)release(image);
        proof_.clear();
        storeRefused_=false;
        std::vector<size_t>().swap(opaque_);
        std::vector<size_t>().swap(remaining_);
    }

    template<class Visit> void visitTextures(const Visit& visit)const{
        for(const auto& image:images_)if(image.texture)visit(image.texture);
    }

private:
    OpaqueFaceCache proof_;
    std::array<Image,OpaqueFaceCache::FaceLimit> images_{};
    std::vector<size_t> opaque_,remaining_;
    uint64_t triangles_=0;
    bool storeRefused_=false;

    static void release(Image& image){
        if(image.surface)image.surface->Release();
        if(image.texture)image.texture->Release();
        image={};
    }
};

} // namespace NorthlightPointShadow

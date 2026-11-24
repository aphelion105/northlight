#pragma once
#include "hdr_frame.h"
#include "frame_parity_clock.h"
#include "world_lighting_capture.h"
#include <array>
#include <charconv>

// A private diagnostic run owns the folder. Live arms after scene readiness;
// the stand receives those same zero-based Present ordinals from the runner.
class NorthlightFrameParityCapture {
    static constexpr unsigned Count=2, Spacing=8;
    std::array<unsigned,Count> frames_{};
    unsigned index_=0;
    std::string directory_;
    bool lightHistoryAudit_=false;
    bool lightHistoryBetween_=false;
#ifdef NORTHLIGHT_REPLAY_STAND
    unsigned historyPredecessors_=0;
#else
    unsigned firstAuditedFrame_=~0u;

    bool armed()const{
        std::error_code error;
        return std::filesystem::is_regular_file(std::filesystem::u8path(directory_)/"arm",error);
    }
#endif

public:
    NorthlightFrameParityCapture(){
        wchar_t path[1024];
        const DWORD size=GetEnvironmentVariableW(L"NORTHLIGHT_FRAME_PARITY_ROOT",path,std::size(path));
        if(!size||size>=std::size(path))return;
#ifdef NORTHLIGHT_REPLAY_STAND
        char list[32];
        const DWORD length=GetEnvironmentVariableA("NORTHLIGHT_FRAME_PARITY_FRAMES",list,sizeof list);
        if(!length||length>=sizeof list)return;
        const char* next=list;
        for(unsigned i=0;i<Count;++i){
            const auto parsed=std::from_chars(next,list+length,frames_[i]);
            if(parsed.ec!=std::errc{}||(parsed.ptr==list+length)!=(i+1==Count)||
               (parsed.ptr!=list+length&&*parsed.ptr!=','))return;
            next=parsed.ptr+1;
        }
        if(frames_[1]<=frames_[0])return;
#endif
        char audit[2];
        lightHistoryAudit_=GetEnvironmentVariableA("NORTHLIGHT_FRAME_PARITY_LIGHT_HISTORY",audit,sizeof audit)==1&&audit[0]=='1';
        lightHistoryBetween_=lightHistoryAudit_&&NorthlightFrameParityClock::auditLightHistoryInterval();
#ifdef NORTHLIGHT_REPLAY_STAND
        if(lightHistoryAudit_&&GetEnvironmentVariableA("NORTHLIGHT_FRAME_PARITY_HISTORY_PREDECESSORS",audit,sizeof audit)==1&&
           audit[0]>='0'&&audit[0]<='8')historyPredecessors_=unsigned(audit[0]-'0');
#endif
        directory_=std::filesystem::path(path).u8string();
    }

    bool pending()const{return !directory_.empty()&&index_<Count;}
    bool requiresWorldGeometry()const{return lightHistoryAudit_;}
    const std::string& outputDirectory()const{return directory_;}
    bool finalAuditFrame(unsigned frame)const{
        return lightHistoryBetween_&&pending()&&index_+1==Count&&frame==frames_[index_];
    }
    void discardUnfinishedPair(){
#ifndef NORTHLIGHT_REPLAY_STAND
        if(index_!=1)return;
        logf("FRAME_PARITY_DISCARD first=%u",frames_[0]);
        index_=0;
        firstAuditedFrame_=~0u;
#endif
    }
    const std::string* auditDirectory(unsigned frame) const{
        if(!lightHistoryAudit_||!pending())return nullptr;
        if(lightHistoryBetween_&&index_)
            return frame>=frames_[0]&&frame<=frames_[index_]?&directory_:nullptr;
#ifdef NORTHLIGHT_REPLAY_STAND
        const unsigned target=frames_[index_];
        return frame<=target&&target-frame<=historyPredecessors_?&directory_:nullptr;
#else
        if(index_)return frame==frames_[index_]?&directory_:nullptr;
        return armed()?&directory_:nullptr;
#endif
    }
    const std::string* lightHistoryAudit(unsigned frame){
        const auto* directory=auditDirectory(frame);
#ifndef NORTHLIGHT_REPLAY_STAND
        if(directory&&!index_)firstAuditedFrame_=frame;
#endif
        return directory;
    }

    bool select(unsigned frame,bool ready){
#ifndef NORTHLIGHT_REPLAY_STAND
        if(!index_){
            // The arm file can arrive after World rendered this frame.
            if(!ready||(lightHistoryAudit_?firstAuditedFrame_!=frame:!armed()))return false;
            frames_={frame,frame+Spacing};
        }
#else
        (void)ready;
#endif
        if(frame!=frames_[index_])return false;
#ifndef NORTHLIGHT_REPLAY_STAND
        if(!ready){discardUnfinishedPair();return false;}
#endif
        ++index_;
        return true;
    }

    void save(IDirect3DDevice9* device,unsigned frame,IDirect3DSurface9* color,
              IDirect3DTexture9* depth,const D3DSURFACE_DESC& source,bool ready,
              const NorthlightHdr::Frame& hdr,const NorthlightHdr::Settings& settings,IDirect3DSurface9* native){
        if(!ready){logf("FRAME_PARITY present=%u scene=unavailable",frame);return;}
        if(NorthlightFrameParityClock::enabled())
            logf("FRAME_PARITY_CLOCK present=%u hz=%u tick=%u",frame,NorthlightFrameParityClock::Hz,NorthlightFrameParityClock::milliseconds(frame));
        NorthlightLightingCapture::Capture capture(device,directory_,frame,frame,frame,
                                                   source.Width,source.Height,"frame-parity");
        capture.image("hdr-linear",hdr.surface());
        if(native)capture.image("native-color",native);
        if(settings.autoExposure&&hdr.hasExposure())capture.image("hdr-exposure",hdr.exposureSurface());
        logf("HDR parity present=%u white=%.9g uiWhite=%.9g peak=%.9g ev=%.9g auto=%u history=%u bloom=%.9g wideBloom=%u expansion=%.9g",
             frame,settings.paperWhite,settings.uiWhite,settings.maxNits,settings.exposure,unsigned(settings.autoExposure),
             unsigned(hdr.hasExposure()),settings.bloom,unsigned(settings.wideBloom),settings.legacyExpansion);
        const bool saved=capture.finish(capture.presentedFrame(color,depth,source));
        logf("FRAME_PARITY present=%u saved=%u depth_samples=%u depth_quality=%lu",frame,unsigned(saved),
             unsigned(source.MultiSampleType),(unsigned long)source.MultiSampleQuality);
    }
};

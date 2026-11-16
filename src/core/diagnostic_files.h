#pragma once
#include "backend_loader.h"
#include "utf8_path.h"
#include <algorithm>
#include <filesystem>

// Explicit diagnostics only. A session owns its blobs; no GPU objects, pixel
// copies or deduplication cache survive the write. Old unmarked folders stay raw.
namespace NorthlightDiagnosticFiles {
inline constexpr const char* BlobDirectory=".capture-blobs";
inline constexpr const char* ReferenceMagic="NLCREF1\n";
inline bool enable(const std::string& directory)noexcept{
    try{
        std::error_code error;
        std::filesystem::create_directory(std::filesystem::u8path(directory)/BlobDirectory,error);
        if(error)return false;
        const auto path=directory+"/CAPTURE-FORMAT.txt";
        FILE* f=NorthlightFiles::open(path.c_str(),"wb");if(!f)return false;
        const char text[]="Northlight compact capture v1\n"
            "Keep this whole folder, including .capture-blobs, when making a ZIP.\n"
            "Binary files contain NLCREF1 references to unique SHA-256 blobs.\n"
            "Restore ordinary files for existing analysis tools with:\n"
            "python scripts/capture_files.py unpack <session-or-zip> <new-output-folder>\n";
        const bool ok=std::fwrite(text,1,sizeof text-1,f)==sizeof text-1;return std::fclose(f)==0&&ok;
    }catch(...){return false;}
}

// visit supplies borrowed byte spans twice (hash, then write if new), including
// only meaningful image rows, never D3D pitch padding. It must stop on false.
template<class Visit> bool write(const std::string& path,Visit visit)noexcept{
    try{
        const auto destination=std::filesystem::u8path(path);
        const auto blobs=destination.parent_path()/BlobDirectory;
        std::error_code error;
        const bool compact=std::filesystem::is_directory(blobs,error);
        if(error&&error!=std::errc::no_such_file_or_directory)return false;
        auto save=[&](const std::filesystem::path& target){
            FILE* f=NorthlightFiles::open(target.u8string().c_str(),"wb");if(!f)return false;
            const bool ok=visit([&](const void* data,size_t size){return std::fwrite(data,1,size,f)==size;});
            return std::fclose(f)==0&&ok;
        };
        if(!compact)return save(destination);
        NorthlightBackendLoader::Sha256 hash;uint64_t size=0;
        if(!visit([&](const void* data,size_t count){hash.update(data,count);size+=count;return true;}))return false;
        const auto digest=hash.hex();const auto blob=blobs/digest;
        error.clear();const bool exists=std::filesystem::exists(blob,error);if(error)return false;
        if(exists){if(std::filesystem::file_size(blob,error)!=size||error)return false;}
        else{
            const auto temporary=blobs/(digest+".tmp");
            if(!save(temporary)){std::filesystem::remove(temporary,error);return false;}
            std::filesystem::rename(temporary,blob,error);
            if(error){std::filesystem::remove(temporary,error);return false;}
        }
        // Publish the reference only after its complete blob exists. A failed
        // reference write makes the caller's export journal incomplete.
        FILE* f=NorthlightFiles::open(path.c_str(),"wb");if(!f)return false;
        const bool ok=std::fprintf(f,"%s%s %llu\n",ReferenceMagic,digest.c_str(),(unsigned long long)size)>0;
        return std::fclose(f)==0&&ok;
    }catch(...){return false;}
}
inline bool bytes(const std::string& path,const void* data,size_t size)noexcept{
    return write(path,[&](auto put){return put(data,size);});
}
inline bool image(const std::string& path,unsigned width,unsigned height,unsigned format,unsigned stride,
                  const void* pixels,size_t pitch)noexcept{
    if(!width||!height||!stride||uint64_t(width)*height*stride>64u*1024u*1024u||
       (pixels&&pitch<size_t(width)*stride))return false;
    const uint32_t header[]={0x31524746,width,height,format,stride};
    return write(path,[&](auto put){
        if(!put(header,sizeof header))return false;
        if(pixels){for(unsigned y=0;y<height;++y)if(!put(static_cast<const char*>(pixels)+size_t(y)*pitch,size_t(width)*stride))return false;}
        else{const unsigned char zero[4096]={};for(uint64_t left=uint64_t(width)*height*stride;left;){
            const auto count=size_t(std::min<uint64_t>(left,sizeof zero));if(!put(zero,count))return false;left-=count;}}
        return true;
    });
}
}

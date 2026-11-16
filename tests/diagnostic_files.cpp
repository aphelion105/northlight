#include "diagnostic_files.h"
#include <cassert>
#include <fstream>
#include <vector>

int main(int argc,char** argv){
    assert(argc==2);const auto root=std::filesystem::u8path(argv[1])/std::filesystem::u8path(u8"unicode-\u0442\u0435\u0441\u0442");
    std::filesystem::create_directories(root/"raw");std::filesystem::create_directories(root/"compact");
    const unsigned char pixels[]={1,2,3,4,99,99,99,99,5,6,7,8,77,77,77,77};
    const unsigned char packed[]={1,2,3,4,5,6,7,8};
    for(const char* mode:{"raw","compact"}){
        const auto folder=root/mode;if(std::string(mode)=="compact")assert(NorthlightDiagnosticFiles::enable(folder.u8string()));
        const auto path=[&](const char* name){return (folder/name).u8string();};
        assert(NorthlightDiagnosticFiles::image(path("padded.fgr"),1,2,21,4,pixels,8));
        assert(NorthlightDiagnosticFiles::image(path("packed.fgr"),1,2,21,4,packed,4));
        assert(NorthlightDiagnosticFiles::image(path("other-format.fgr"),1,2,22,4,packed,4));
        assert(NorthlightDiagnosticFiles::image(path("zero.fgr"),1,2,21,4,nullptr,0));
        const unsigned char zero[8]={};
        assert(NorthlightDiagnosticFiles::image(path("zero-again.fgr"),1,2,21,4,zero,4));
        assert(NorthlightDiagnosticFiles::bytes(path("shader.bin"),packed,sizeof packed));
        assert(NorthlightDiagnosticFiles::bytes(path("same-constants.f32"),packed,sizeof packed));
        assert(NorthlightDiagnosticFiles::bytes(path("empty.bin"),packed,0));
        assert(NorthlightDiagnosticFiles::bytes(path("empty-again.bin"),packed,0));
        assert(!NorthlightDiagnosticFiles::image(path("bad.fgr"),1,2,21,4,packed,3));
        assert(!NorthlightDiagnosticFiles::image(path("huge.fgr"),16384,16384,21,4,nullptr,0));
        assert(!NorthlightDiagnosticFiles::bytes(path("missing/failed.bin"),packed,sizeof packed));
    }
    const auto blobs=root/"compact"/NorthlightDiagnosticFiles::BlobDirectory;
    assert(std::distance(std::filesystem::directory_iterator(blobs),std::filesystem::directory_iterator{})==5);
    // A failed write cannot publish a reference to a truncated or absent blob.
    const unsigned char value=42;NorthlightBackendLoader::Sha256 hash;hash.update(&value,1);
    const auto digest=hash.hex();const auto bad=blobs/digest;std::ofstream(bad,std::ios::binary).put('a').put('b');
    assert(!NorthlightDiagnosticFiles::bytes((root/"compact"/"corrupt.bin").u8string(),&value,1));
    assert(!std::filesystem::exists(root/"compact"/"corrupt.bin"));std::filesystem::remove(bad);
    std::filesystem::create_directory(blobs/(digest+".tmp"));
    assert(!NorthlightDiagnosticFiles::bytes((root/"compact"/"failed.bin").u8string(),&value,1));
    assert(!std::filesystem::exists(root/"compact"/"failed.bin"));
    std::puts("DIAGNOSTIC_FILES_PASS raw compact pitch format zeros aliases failures");
}

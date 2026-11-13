// Read-only background inspection. No input, window, remote-thread or write APIs.
#include "native_scene_lights.h"
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <string>

int main(int argc,char** argv){
    if(argc!=7){std::cerr<<"Usage: inspect-native-lights PID OUTPUT.jsonl CATALOG X Y Z\n";return 2;}
    const DWORD pid=std::stoul(argv[1]);
    HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process){std::cerr<<"OpenProcess(read only): "<<GetLastError()<<"\n";return 1;}
    struct Close {HANDLE handle;~Close(){CloseHandle(handle);}} close{process};
    std::ofstream out(argv[2]);if(!out)return 1;
    // Freeze each individual read for reproducible diagnosis. These blocks are
    // NOT an atomic game snapshot; the game is never suspended.
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::vector<unsigned char>> blocks;
    std::size_t captured=0;
    const auto read=[&](std::uint32_t address,void* output,std::size_t size){
        const auto key=std::make_pair(address,std::uint32_t(size));const auto cached=blocks.find(key);
        if(cached!=blocks.end()){std::memcpy(output,cached->second.data(),size);return true;}
        if(size>1024*1024||size>64*1024*1024-captured)return false;
        SIZE_T copied=0;const bool ok=ReadProcessMemory(process,reinterpret_cast<const void*>(std::uintptr_t(address)),output,size,&copied)&&copied==size;
        if(ok){const auto* first=static_cast<const unsigned char*>(output);blocks.emplace(key,std::vector<unsigned char>(first,first+size));captured+=size;}
        else out<<"{\"event\":\"read-failed\",\"address\":"<<address<<",\"bytes\":"<<size<<",\"error\":"<<GetLastError()<<"}\n";
        return ok;};
    if(!NorthlightNativeLights::Reader::verify(read)){std::cerr<<"Unsupported executable signatures\n";return 1;}
    out<<"{\"event\":\"scope\",\"frozen_reads\":true,\"atomic_snapshot\":false,\"pid\":"<<pid<<"}\n";
    const float camera[]={std::stof(argv[4]),std::stof(argv[5]),std::stof(argv[6])};
    auto reader=std::make_unique<NorthlightNativeLights::Reader>();
    reader->fixtures(NorthlightNativeFixtures::load(argv[3]));
    for(unsigned sample=0;sample<60;++sample){
        NorthlightNativeLights::Snapshot values;reader->update(read,camera,values);
        out<<"{\"event\":\"sample\",\"sample\":"<<sample<<",\"valid\":"<<values.valid<<",\"count\":"<<values.count
           <<",\"scene\":"<<values.scene<<",\"time\":"<<values.time<<",\"models\":"<<reader->modelCount()
           <<",\"scanned\":"<<reader->scanned<<",\"unsupported\":"<<reader->unsupported<<",\"malformed\":"<<reader->malformed
           <<",\"failures\":"<<reader->failures<<",\"budget_limited\":"<<reader->budgetLimited<<",\"metadata_limited\":"<<reader->metadataLimited<<"}\n";
        for(unsigned i=0;i<values.count;++i){const auto& p=values.lights[i];
            out<<std::setprecision(9)<<"{\"event\":\"source\",\"sample\":"<<sample<<",\"id\":\""<<p.id<<"\",\"flags\":"<<p.flags<<",\"fixture\":"<<p.reserved
               <<",\"position\":["<<p.position[0]<<','<<p.position[1]<<','<<p.position[2]<<"],\"rgb\":["<<p.color[0]<<','<<p.color[1]<<','<<p.color[2]<<"]}\n";}
        Sleep(20);
    }
    using namespace NorthlightNativeLights::SceneDetail;
    std::uint32_t scene=0;std::array<std::uint32_t,6> header{};
    if(!read(ScenePointer,&scene,4)||!read(scene,header.data(),sizeof header))return 1;
    std::set<std::uint32_t> visited;auto address=header[2];unsigned emitters=0;
    while(address&&visited.size()<16384&&visited.insert(address).second){
        std::array<unsigned char,ModelBytes> model{};
        if(!read(address,model.data(),model.size()))break;
        const auto current=address;address=at<std::uint32_t>(model,12);
        const auto shared=at<std::uint32_t>(model,44);std::uint32_t data=0;std::array<unsigned char,304> asset{};
        if(!read(shared+0x150,&data,4)||!read(data,asset.data(),asset.size())||std::memcmp(asset.data(),"MD20",4))continue;
        const auto count=at<std::uint32_t>(asset,264);if(!count||count>64)continue;++emitters;
        std::string name;const auto length=at<std::uint32_t>(asset,8);
        if(length>0&&length<=256){name.resize(length);if(!read(at<std::uint32_t>(asset,12),name.data(),length))name.clear();
            while(!name.empty()&&name.back()==0)name.pop_back();for(char& c:name)if(static_cast<unsigned char>(c)<32)c='?';}
        out<<"{\"event\":\"model\",\"address\":"<<current<<",\"shared\":"<<shared<<",\"data\":"<<data
           <<",\"name\":"<<std::quoted(name)<<",\"flags\":"<<at<std::uint32_t>(model,16)<<",\"parent\":"<<at<std::uint32_t>(model,0x48)
           <<",\"count\":"<<count<<",\"model_scene\":"<<at<std::uint32_t>(model,40)<<",\"position\":["<<at<float>(model,0xe4)<<','<<at<float>(model,0xe8)<<','<<at<float>(model,0xec)<<"]}\n";
    }
    out<<"{\"event\":\"walk\",\"models\":"<<visited.size()<<",\"emitters\":"<<emitters<<",\"stopped_at\":"<<address<<",\"atomic_snapshot\":false}\n";
    std::ofstream memory(std::string(argv[2])+".memory.bin",std::ios::binary);
    memory.write("NLR1",4);const std::uint32_t count=std::uint32_t(blocks.size());memory.write(reinterpret_cast<const char*>(&count),4);
    for(const auto& block:blocks){const std::uint32_t fields[]={block.first.first,block.first.second};memory.write(reinterpret_cast<const char*>(fields),8);memory.write(reinterpret_cast<const char*>(block.second.data()),block.second.size());}
    std::cout<<"Read-only inspection: "<<visited.size()<<" models, "<<emitters<<" emitters; "<<argv[2]<<"\n";
    return out?0:1;
}

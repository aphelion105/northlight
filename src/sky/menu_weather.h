#pragma once
#include <algorithm>
#include <string_view>
#include "weather_state.h"

namespace NorthlightMenu {
inline NorthlightWeather::State backdropWeather(std::string_view name){
    NorthlightWeather::State out;
    for(const std::string_view tag:{"dwarf","gnome","lichking","icecrown"}){
        const auto equal=[](char a,char b){return (a>='A'&&a<='Z'?a+('a'-'A'):a)==b;};
        if(std::search(name.begin(),name.end(),tag.begin(),tag.end(),equal)==name.end())continue;
        out.kind=NorthlightWeather::Kind::Snow;out.intensity=.3f;out.blend=1;break;
    }
    return out;
}
}

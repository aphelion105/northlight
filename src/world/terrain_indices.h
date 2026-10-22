#pragma once
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace NorthlightTerrainIndices {
// Only CPU snapshot storage is compact. Consumers receive uint32 values or a
// typed storage view; the GPU arena still rebases into its original 32-bit IB.
class Indices {
    std::vector<std::uint16_t> small_;
    std::vector<std::uint32_t> large_;
    bool wide_=false;
    void widen(std::size_t capacity){
        std::vector<std::uint32_t> next;next.reserve(std::max(capacity,small_.capacity()));
        next.insert(next.end(),small_.begin(),small_.end());large_.swap(next);
        std::vector<std::uint16_t>().swap(small_);wide_=true;
    }
public:
    Indices()=default;
    Indices(std::initializer_list<std::uint32_t> values){reserve(values.size());for(auto v:values)push_back(v);}
    std::size_t size()const{return wide_?large_.size():small_.size();}
    bool empty()const{return !size();}
    std::size_t allocatedBytes()const{return small_.capacity()*sizeof(std::uint16_t)+large_.capacity()*sizeof(std::uint32_t);}
    std::uint32_t operator[](std::size_t i)const{return wide_?large_[i]:small_[i];}
    void reserve(std::size_t count,std::uint32_t maximum=0){
        if(!wide_&&maximum>UINT16_MAX)widen(count);
        if(wide_)large_.reserve(count);else small_.reserve(count);
    }
    void push_back(std::uint32_t value){
        if(!wide_&&value>UINT16_MAX)widen(size()+1);
        if(wide_)large_.push_back(value);else small_.push_back(std::uint16_t(value));
    }
    void set(std::size_t index,std::uint32_t value){
        if(!wide_&&value>UINT16_MAX)widen(size());
        if(wide_)large_[index]=value;else small_[index]=std::uint16_t(value);
    }
    template<class F> decltype(auto) visit(F f)const{
        if(wide_)return f(large_.data(),large_.size());
        return f(small_.data(),small_.size());
    }
    const void* storageIdentity()const{return visit([](const auto* p,std::size_t){return static_cast<const void*>(p);});}
    struct Iterator {
        const Indices* owner;std::size_t index;
        std::uint32_t operator*()const{return (*owner)[index];}
        Iterator& operator++(){++index;return *this;}
        bool operator!=(const Iterator& b)const{return owner!=b.owner||index!=b.index;}
    };
    Iterator begin()const{return {this,0};}
    Iterator end()const{return {this,size()};}
    template<class Other> bool operator==(const Other& b)const{
        if(size()!=b.size())return false;for(std::size_t i=0;i<size();++i)if((*this)[i]!=b[i])return false;return true;
    }
};
}

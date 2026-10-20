#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

// Portable register differencing for sequential replay draws. No D3D calls,
// heap allocation or floating-point comparison; NaN payloads and signed zero
// are preserved exactly as supplied by the application.
namespace NorthlightReplayConstants {
struct DirtySpan { uint32_t first=0,count=0; };

template<class T,std::size_t Count,std::size_t Width>
class DirtyRegisters {
    static_assert(std::is_trivially_copyable<T>::value,"Register elements must be trivially copyable");
    static_assert(Count>0&&Width>0,"Register bank dimensions must be nonzero");
    static_assert(Count<=UINT32_MAX,"Register count must fit the D3D9 UINT interface");
    static_assert(Width<=std::numeric_limits<std::size_t>::max()/sizeof(T)/Count,"Register bank size overflow");
    static constexpr std::size_t RegisterBytes=sizeof(T)*Width;
    static constexpr std::size_t BankBytes=RegisterBytes*Count;
    // Intentionally not value-initialized. No cached element is read before
    // the initial full copy, and reset needs no redundant large memset.
    std::array<T,Count*Width> values_;
    bool valid_=false;
public:
    // desired points to Count*Width elements. The returned first/count are
    // REGISTERS, not scalar elements. Upload desired+first*Width, count regs.
    // Cache updates immediately: caller must abort/reset this cache if its
    // subsequent backend upload fails. Call reset after any external register
    // write, state-block restore, reset or other operation invalidating the bank.
    DirtySpan update(const T* desired) noexcept {
        if(!valid_){
            std::memcpy(values_.data(),desired,BankBytes);valid_=true;
            return {0,static_cast<uint32_t>(Count)};
        }
        if(std::memcmp(values_.data(),desired,BankBytes)==0)return {};
        std::size_t first=0,last=Count;
        while(first<Count&&std::memcmp(values_.data()+first*Width,desired+first*Width,RegisterBytes)==0)++first;
        while(last>first&&std::memcmp(values_.data()+(last-1)*Width,desired+(last-1)*Width,RegisterBytes)==0)--last;
        std::memcpy(values_.data()+first*Width,desired+first*Width,(last-first)*RegisterBytes);
        return {static_cast<uint32_t>(first),static_cast<uint32_t>(last-first)};
    }
    void reset() noexcept {valid_=false;}
};
} // namespace NorthlightReplayConstants

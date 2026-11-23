#pragma once

#if defined(_WIN32) && defined(__i386__) && !defined(NORTHLIGHT_REPLAY_STAND)
#include <cstdint>
#include <cstring>

namespace NorthlightGx12340 {
struct LiveMemory {
    static std::uintptr_t stackTop(){
        return reinterpret_cast<std::uintptr_t>(reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
    }
    static std::uint32_t word(std::uintptr_t at){
        std::uint32_t value;std::memcpy(&value,reinterpret_cast<const void*>(at),sizeof value);return value;
    }
    static std::uint16_t half(std::uintptr_t at){
        std::uint16_t value;std::memcpy(&value,reinterpret_cast<const void*>(at),sizeof value);return value;
    }
};
}
#endif

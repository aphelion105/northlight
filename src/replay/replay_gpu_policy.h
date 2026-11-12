#pragma once
#include <cstddef>
#include <cstdint>

namespace NorthlightReplayGPU {
inline constexpr bool CreateTimeBudget=true;
inline constexpr double CreateBudgetMs=1.0;
// Shared by production residency and the legacy comparison caches.
inline double& createBudgetMs(){static double value=CreateBudgetMs;return value;}

struct Stats {
    unsigned countPressure=0,bytePressure=0,roomRejected=0,promotionCountOnly=0,promotionCountRejected=0,evictions=0;
    unsigned probationEvictions=0,residentEvictions=0,countOnlyProbationEvictions=0;
    unsigned warmup=0,uploadDeferred=0,admissionRejected=0;
    unsigned scanPasses=0,scanVisits=0,scanMemoHits=0;
    unsigned evictionChecks=0,lruMoves=0;
    unsigned attemptDeferred=0,uploadByteDeferred=0,expiredEntries=0;
    unsigned timeDeferred=0,created=0;double createMs=0;
    size_t expiredBytes=0;
    unsigned identityHits=0,reconstructedHits=0;size_t identityBytes=0;
};
struct Population {size_t entries=0,resident=0,probation=0;};
struct ClearStats {uint64_t calls=0,entries=0,bytes=0;};
struct BatchStats {
    size_t batches=0,batchBytes=0,separateBytes=0,liveBytes=0,compactedBytes=0;
    unsigned batchedUploads=0,separateUploads=0,compactions=0,batchFailures=0;
};
}

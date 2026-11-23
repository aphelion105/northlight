#pragma once

static FILE* logFile;
// Called once, under logLock, before the first open truncates the log.
struct WindowsLogFiles {
    DWORD error=0;
    bool exists(const wchar_t* path){return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;}
    bool move(const wchar_t* from,const wchar_t* to){if(MoveFileExW(from,to,MOVEFILE_REPLACE_EXISTING))return true;error=GetLastError();return false;}
    bool readOnly(const wchar_t* path){const DWORD a=GetFileAttributesW(path);return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_READONLY)&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
    bool makeWritable(const wchar_t* path){const DWORD a=GetFileAttributesW(path);return a!=INVALID_FILE_ATTRIBUTES&&SetFileAttributesW(path,a&~DWORD(FILE_ATTRIBUTE_READONLY))!=FALSE;}
    // First 16 KiB, shared with a live writer; unreadable = not a session.
    bool session(const wchar_t* path){
        HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
        static char head[16384];DWORD got=0;const bool ok=ReadFile(h,head,sizeof(head),&got,nullptr)!=FALSE;if(!ok)error=GetLastError();CloseHandle(h);
        return ok&&NorthlightLogRotation::recordedSession(head,got);}
};
static NorthlightLogRotation::Result logRotation=NorthlightLogRotation::Result::Missing;static DWORD logRotationError=0;
static void rotatePreviousLog(){
    wchar_t current[MAX_PATH],previous[MAX_PATH];
    if(swprintf(current,MAX_PATH,L"%lsnorthlight-renderer.log",rootPath)<0||swprintf(previous,MAX_PATH,L"%lsnorthlight-renderer.prev.log",rootPath)<0){logRotation=NorthlightLogRotation::Result::Failed;return;}
    WindowsLogFiles files;logRotation=NorthlightLogRotation::rotate(files,current,previous);logRotationError=files.error;
}
static SRWLOCK logLock=SRWLOCK_INIT;
static bool logRotated=false;
struct LogCost {
    unsigned long long calls=0,formattedBytes=0,ioErrors=0;
    LONGLONG wallTicks=0,maxTicks=0;
};
static LogCost logCost;
static LONGLONG logFrequency=0,logIntervalStart=0;
// Lines written by the calling thread (RenderProfile: frames that logged are left out of FRAME cost).
static thread_local unsigned long long threadLogLines=0;
static thread_local unsigned logFlushBatchDepth=0;
static thread_local bool logFlushPending=false;
// Frame-boundary diagnostics become visible together, including on exceptions.
// Other threads may still flush this stream while the batch is being assembled.
struct LogFlushBatch {
    LogFlushBatch(){++logFlushBatchDepth;}
    ~LogFlushBatch(){
        if(--logFlushBatchDepth||!logFlushPending)return;
        logFlushPending=false;
        LARGE_INTEGER start={};const bool timed=QueryPerformanceCounter(&start)!=0;
        AcquireSRWLockExclusive(&logLock);
        if(logFile&&fflush(logFile)!=0)++logCost.ioErrors;
        LARGE_INTEGER end={};if(timed&&QueryPerformanceCounter(&end)&&end.QuadPart>=start.QuadPart){
            const auto ticks=end.QuadPart-start.QuadPart;logCost.wallTicks+=ticks;
            if(ticks>logCost.maxTicks)logCost.maxTicks=ticks;
        }
        ReleaseSRWLockExclusive(&logLock);
    }
    LogFlushBatch(const LogFlushBatch&)=delete;
};
static void logf(const char* fmt, ...) {
#ifdef NORTHLIGHT_REPLAY_STAND
    ensureRootPath();
    if(!rootPath[0]){std::fputs("NORTHLIGHT STAND REPLAY: module root unavailable; file logging refused\n",stderr);return;}
#endif
    LARGE_INTEGER start={};const bool timed=QueryPerformanceCounter(&start)!=0;
    // Render and worker threads share the stream. Hold one lock over opening,
    // the entire line, flushing and counters; report after releasing this lock.
    AcquireSRWLockExclusive(&logLock);
    if(!logFrequency){LARGE_INTEGER frequency={};if(QueryPerformanceFrequency(&frequency))logFrequency=frequency.QuadPart;}
    if(!logIntervalStart&&timed)logIntervalStart=start.QuadPart;
    if (!logFile) {
        if(!logRotated){logRotated=true;rotatePreviousLog();}
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%lsnorthlight-renderer.log", rootPath);
        logFile = _wfopen(path, L"w");
    }
    ++logCost.calls;++threadLogLines;
    if(logFile){
        va_list args;va_start(args,fmt);int bytes;
        if(fmt[0]=='%'&&fmt[1]=='s'&&!fmt[2]){
            va_list probe;va_copy(probe,args);const char* line=va_arg(probe,const char*);va_end(probe);
            const size_t length=line?std::strlen(line):size_t(-1);
            if(length<=0x7fffffffU)bytes=std::fputs(line,logFile)==EOF?-1:int(length);
            else bytes=vfprintf(logFile,fmt,args);
        }else{
            char buffer[2048];va_list copy;va_copy(copy,args);
            const int length=vsnprintf(buffer,sizeof buffer,fmt,copy);va_end(copy);
            if(length<0)bytes=-1;
            else if(size_t(length)<sizeof buffer)bytes=fwrite(buffer,1,size_t(length),logFile)==size_t(length)?length:-1;
            else bytes=vfprintf(logFile,fmt,args);
        }
        va_end(args);
        if(bytes>=0)logCost.formattedBytes+=unsigned(bytes);else ++logCost.ioErrors;
        if(fputc('\n',logFile)!=EOF)++logCost.formattedBytes;else ++logCost.ioErrors;
        if(logFlushBatchDepth)logFlushPending=true;
        else if(fflush(logFile)!=0)++logCost.ioErrors;
    }else ++logCost.ioErrors;
    LARGE_INTEGER end={};
    if(timed&&QueryPerformanceCounter(&end)&&end.QuadPart>=start.QuadPart){
        const LONGLONG ticks=end.QuadPart-start.QuadPart;logCost.wallTicks+=ticks;
        if(ticks>logCost.maxTicks)logCost.maxTicks=ticks;
    }
    ReleaseSRWLockExclusive(&logLock);
}
static void reportLogCost(){
    AcquireSRWLockExclusive(&logLock);
    const LogCost sample=logCost;const LONGLONG frequency=logFrequency;
    LARGE_INTEGER now={};const bool timed=QueryPerformanceCounter(&now)!=0;
    const LONGLONG interval=timed&&logIntervalStart&&now.QuadPart>=logIntervalStart?now.QuadPart-logIntervalStart:0;
    logCost={};logIntervalStart=timed?now.QuadPart:0;
    ReleaseSRWLockExclusive(&logLock);
    // Aggregated caller wall time includes lock waits and can overlap between
    // threads. It is not CPU utilization. Bytes exclude text-mode CRLF growth.
    if(sample.calls&&frequency>0){const double ms=1000.0/double(frequency);
        logf("LOGGER intervalMs=%.3f calls=%llu formattedBytes=%llu callerWallMs=%.3f maxCallMs=%.3f ioErrors=%llu",
            interval*ms,sample.calls,sample.formattedBytes,sample.wallTicks*ms,sample.maxTicks*ms,sample.ioErrors);
    }
}

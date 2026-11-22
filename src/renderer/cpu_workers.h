#pragma once
#include <atomic>
#include <cstdint>
#ifdef FNV_VITA_TARGET
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#else
#include <thread>
#include <mutex>
#include <condition_variable>
#endif

// A synchronous, bounded parallel-for. Callbacks only touch CPU-owned data;
// all workers are joined at the barrier before GPU submission or resource edits.
class CpuWorkers {
public:
    using Function = void (*)(void*, unsigned, unsigned);
    CpuWorkers() = default;
    CpuWorkers(const CpuWorkers&) = delete;
    CpuWorkers& operator=(const CpuWorkers&) = delete;
    ~CpuWorkers() { shutdown(); }
    unsigned start(unsigned limit=2) {
        if(m_started)return m_workers;
        m_started=true;m_stop=false;
        for(unsigned i=0;i<2 && i<limit;++i) {
            auto& w=m_worker[i];w.owner=this;w.lane=i+1;
#ifdef FNV_VITA_TARGET
            w.go=sceKernelCreateSema("fnv_render_go",0,0,1,nullptr);
            w.done=sceKernelCreateSema("fnv_render_done",0,0,1,nullptr);
            if(w.go<0 || w.done<0) { release(w);break; }
            int priority=sceKernelGetThreadCurrentPriority();
            w.thread=sceKernelCreateThread("fnv_render_worker",entry,priority,64*1024,0,
                i==0?SCE_KERNEL_CPU_MASK_USER_1:SCE_KERNEL_CPU_MASK_USER_2,nullptr);
            Worker* arg=&w;
            if(w.thread<0 || sceKernelStartThread(w.thread,sizeof(arg),&arg)<0) { release(w);break; }
#else
            w.thread=std::thread([&w]{w.owner->loop(w);});
#endif
            ++m_workers;
        }
#ifdef FNV_VITA_TARGET
        if(m_workers) {
            m_mainThread=sceKernelGetThreadId();m_originalAffinity=sceKernelGetThreadCpuAffinityMask(m_mainThread);
            if(m_originalAffinity>=0)sceKernelChangeThreadCpuAffinityMask(m_mainThread,SCE_KERNEL_CPU_MASK_USER_0);
        }
#endif
        return m_workers;
    }
    void run(unsigned count,Function function,void* context,bool parallel=true) {
        m_next=0;m_count=count;m_function=function;m_context=context;
        for(auto& n:m_counts)n=0;
        if(parallel && m_workers && count>1) {
            for(unsigned i=0;i<m_workers;++i)wake(m_worker[i]);
            consume(0);
            for(unsigned i=0;i<m_workers;++i)wait(m_worker[i]);
        } else consume(0);
    }
    unsigned workers() const { return m_workers; }
    unsigned jobs(unsigned lane) const { return m_counts[lane]; }
    void shutdown() {
        if(!m_started)return;
        m_stop=true;
        for(unsigned i=0;i<m_workers;++i)wake(m_worker[i]);
        for(unsigned i=0;i<m_workers;++i) {
            auto& w=m_worker[i];
#ifdef FNV_VITA_TARGET
            sceKernelWaitThreadEnd(w.thread,nullptr,nullptr);release(w);
#else
            if(w.thread.joinable())w.thread.join();
#endif
        }
        m_workers=0;m_started=false;
#ifdef FNV_VITA_TARGET
        if(m_mainThread>=0 && m_originalAffinity>=0)sceKernelChangeThreadCpuAffinityMask(m_mainThread,m_originalAffinity);
        m_mainThread=m_originalAffinity=-1;
#endif
    }
private:
    struct Worker {
        CpuWorkers* owner=nullptr;unsigned lane=0;
#ifdef FNV_VITA_TARGET
        SceUID thread=-1,go=-1,done=-1;
        std::atomic<bool> published{false},completed{false};
#else
        std::thread thread;std::mutex mutex;std::condition_variable cv;
        bool ready=false,complete=false;
#endif
    } m_worker[2];
    bool m_started=false;std::atomic<bool> m_stop{false};unsigned m_workers=0;
    std::atomic<unsigned> m_next{0};unsigned m_count=0,m_counts[3]{};
    Function m_function=nullptr;void* m_context=nullptr;
#ifdef FNV_VITA_TARGET
    SceUID m_mainThread=-1;int m_originalAffinity=-1;
#endif
    void consume(unsigned lane) {
        for(;;) {
            unsigned index=m_next.fetch_add(1,std::memory_order_relaxed);
            if(index>=m_count)break;
            m_function(m_context,index,lane);++m_counts[lane];
        }
    }
    static void wake(Worker& w) {
#ifdef FNV_VITA_TARGET
        w.completed.store(false,std::memory_order_relaxed);
        w.published.store(true,std::memory_order_release);
        sceKernelSignalSema(w.go,1);
#else
        {std::lock_guard<std::mutex> lock(w.mutex);w.complete=false;w.ready=true;}
        w.cv.notify_one();
#endif
    }
    static void wait(Worker& w) {
#ifdef FNV_VITA_TARGET
        sceKernelWaitSema(w.done,1,nullptr);
        (void)w.completed.load(std::memory_order_acquire);
#else
        std::unique_lock<std::mutex> lock(w.mutex);w.cv.wait(lock,[&w]{return w.complete;});
#endif
    }
    void loop(Worker& w) {
        for(;;) {
#ifdef FNV_VITA_TARGET
            sceKernelWaitSema(w.go,1,nullptr);
            (void)w.published.exchange(false,std::memory_order_acquire);
#else
            {std::unique_lock<std::mutex> lock(w.mutex);w.cv.wait(lock,[&w]{return w.ready;});w.ready=false;}
#endif
            if(m_stop.load())return;
            consume(w.lane);
#ifdef FNV_VITA_TARGET
            w.completed.store(true,std::memory_order_release);
            sceKernelSignalSema(w.done,1);
#else
            {std::lock_guard<std::mutex> lock(w.mutex);w.complete=true;}w.cv.notify_one();
#endif
        }
    }
#ifdef FNV_VITA_TARGET
    static int entry(SceSize,void* arg) {auto& w=**static_cast<Worker**>(arg);w.owner->loop(w);return 0;}
    static void release(Worker& w) {
        if(w.thread>=0)sceKernelDeleteThread(w.thread);
        if(w.go>=0)sceKernelDeleteSema(w.go);
        if(w.done>=0)sceKernelDeleteSema(w.done);
        w.thread=w.go=w.done=-1;
    }
#endif
};

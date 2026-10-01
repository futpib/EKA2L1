#include <common/native_profile.h>
#if defined(__linux__) && defined(__x86_64__) && !defined(__EMSCRIPTEN__)
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ucontext.h>
#include <pthread.h>
#include <time.h>
#include <dlfcn.h>
#include <array>
#include <map>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <cstring>

namespace eka2l1::common::native_profile {
    static std::array<std::uintptr_t, 131072> samples{};
    static volatile sig_atomic_t count = 0, active = 0;
    static timer_t timer;
    static struct sigaction previous;
    static std::chrono::steady_clock::time_point begin;
    static timespec cpu_begin;
    static const char *path;
    static unsigned period_us = 1000;
    static void sample(int, siginfo_t *, void *context) {
        if (active && count < static_cast<sig_atomic_t>(samples.size())) {
            const auto *state = static_cast<ucontext_t *>(context);
            samples[count] = state->uc_mcontext.gregs[REG_RIP];
            count = count + 1;
        }
    }
    void start() {
        path = std::getenv("EKA2L1_NATIVE_SAMPLE_OUTPUT");
        if (!path || !*path) return;
        if (const char *value = std::getenv("EKA2L1_NATIVE_SAMPLE_PERIOD_US")) {
            char *end = nullptr;
            const auto parsed = std::strtoul(value, &end, 10);
            if (!end || *end || parsed < 100 || parsed > 100000)
                throw std::runtime_error("Native sample period must be 100..100000 us");
            period_us = static_cast<unsigned>(parsed);
        }
        struct sigaction action{};
        sigaction(SIGPROF,nullptr,&previous);
        if(previous.sa_handler != SIG_DFL) throw std::runtime_error("SIGPROF already owned");
        action.sa_sigaction=sample; action.sa_flags=SA_SIGINFO|SA_RESTART;
        sigemptyset(&action.sa_mask);
        if(sigaction(SIGPROF,&action,nullptr)) throw std::runtime_error("sigaction failed");
        sigevent event{}; event.sigev_notify=SIGEV_THREAD_ID;event.sigev_signo=SIGPROF;
        event._sigev_un._tid=static_cast<pid_t>(syscall(SYS_gettid));
        if(timer_create(CLOCK_MONOTONIC,&event,&timer)) throw std::runtime_error("timer_create failed");
        count=0;active=1;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_begin);
        begin=std::chrono::steady_clock::now();
        itimerspec spec{};spec.it_value.tv_nsec=spec.it_interval.tv_nsec=period_us*1000;
        if(timer_settime(timer,0,&spec,nullptr))throw std::runtime_error("timer_settime failed");
    }
    void stop() {
        if(!active)return;
        sigset_t blocked,old;sigemptyset(&blocked);sigaddset(&blocked,SIGPROF);
        pthread_sigmask(SIG_BLOCK,&blocked,&old);timer_delete(timer);active=0;
        timespec zero{};while(sigtimedwait(&blocked,nullptr,&zero)>=0){}
        sigaction(SIGPROF,&previous,nullptr);pthread_sigmask(SIG_SETMASK,&old,nullptr);
        timespec cpu_end;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_end);
        const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        const double cpu=cpu_end.tv_sec-cpu_begin.tv_sec+(cpu_end.tv_nsec-cpu_begin.tv_nsec)/1e9;
        std::map<std::uintptr_t,std::size_t> histogram;
        for(int i=0;i<count;++i)++histogram[samples[i]];
        std::ofstream out(path);
        out<<"pc\tcount\tobject_offset\tobject\tdynamic_symbol\n";
        for(const auto &[pc,n]:histogram) {
            Dl_info info{};dladdr(reinterpret_cast<void *>(pc),&info);
            out<<std::hex<<pc<<std::dec<<'\t'<<n<<'\t'<<std::hex
               <<pc-reinterpret_cast<std::uintptr_t>(info.dli_fbase)<<std::dec<<'\t'
               <<(info.dli_fname?info.dli_fname:"<anonymous>")<<'\t'
               <<(info.dli_sname?info.dli_sname:"")<<'\n';
        }
        std::ofstream maps(std::string(path)+".maps");std::ifstream input("/proc/self/maps");maps<<input.rdbuf();
        std::ofstream meta(std::string(path)+".json");
        meta<<"{\"period_us\":"<<period_us<<",\"samples\":"<<count<<",\"capacity\":"<<samples.size()
            <<",\"wall_seconds\":"<<wall<<",\"thread_cpu_seconds\":"<<cpu<<"}\n";
    }
}
#else
namespace eka2l1::common::native_profile { void start() {} void stop() {} }
#endif

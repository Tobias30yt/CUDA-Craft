#include "cudacraft/performance.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
namespace cudacraft {
void Performance::append(std::deque<Sample>& samples,double ms,Clock::time_point now) {
    if(!std::isfinite(ms)||ms<0)return;
    samples.push_back({now,ms});
    while(!samples.empty()&&(now-samples.front().at>std::chrono::seconds(60)||samples.size()>4096))samples.pop_front();
}
void Performance::tick(double ms,Clock::time_point now){append(ticks_,ms,now);++tick_count;if(ms>50)++over_budget;}
void Performance::work(double ms,Clock::time_point now){append(work_,ms,now);}
double Performance::uptime(Clock::time_point now) const {return std::max(0.0,std::chrono::duration<double>(now-started_).count());}
double Performance::tps(double seconds,Clock::time_point now) const {
    const double duration=std::min(seconds,uptime(now));if(duration<=0)return 0;
    const auto count=std::count_if(ticks_.begin(),ticks_.end(),[&](const auto& s){double age=std::chrono::duration<double>(now-s.at).count();return age>=0&&age<seconds;});
    return std::min(20.0,static_cast<double>(count)/duration);
}
Performance::Timing Performance::summarize(const std::deque<Sample>& samples,Clock::time_point now) {
    Timing result;std::vector<double> values;
    for(auto s:samples)if(now-s.at<=std::chrono::seconds(60)){values.push_back(s.ms);result.mean+=s.ms;}
    result.count=values.size();if(values.empty())return result;
    result.mean/=static_cast<double>(values.size());std::sort(values.begin(),values.end());
    result.maximum=values.back();result.p95=values[static_cast<std::size_t>(std::ceil(values.size()*.95))-1];return result;
}
Performance::Timing Performance::ticks(Clock::time_point now) const{return summarize(ticks_,now);}
Performance::Timing Performance::work_times(Clock::time_point now) const{return summarize(work_,now);}
void Performance::sample_process() {
    const auto now=Clock::now();const double seconds=std::chrono::duration<double>(now-process_at_).count();
    if(seconds<1)return;
    receive_rate=static_cast<double>(bytes_received-previous_received_)/seconds;send_rate=static_cast<double>(bytes_sent-previous_sent_)/seconds;
    previous_received_=bytes_received;previous_sent_=bytes_sent;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    process_available=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=0;
    if(process_available){working_set=memory.WorkingSetSize;private_bytes=memory.PrivateUsage;}
    FILETIME created{},exited{},kernel{},user{};
    if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) {
        auto value=[](FILETIME t){return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
        const auto total=value(kernel)+value(user);
        if(process_cpu_){cpu_percent=std::clamp(static_cast<double>(total-process_cpu_)/1e7/seconds/std::max(1u,std::thread::hardware_concurrency())*100,0.0,100.0);cpu_available=true;}
        process_cpu_=total;
    }
#endif
    process_at_=now;
}
std::string Performance::brief() const {
    const auto timing=ticks();std::ostringstream out;out<<std::fixed<<std::setprecision(1)<<"TPS "<<tps(1)<<"/"<<tps(10)<<"/"<<tps(60)
        <<" | sim "<<std::setprecision(2)<<timing.mean<<" ms (p95 "<<timing.p95<<") | RAM ";
    if(process_available)out<<std::setprecision(0)<<static_cast<double>(working_set)/1048576<<" MiB";else out<<"n/a";
    out<<" | CPU ";if(cpu_available)out<<std::setprecision(1)<<cpu_percent<<"%";else out<<"warming up";return out.str();
}
}

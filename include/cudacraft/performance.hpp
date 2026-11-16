#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>

namespace cudacraft {
class Performance {
public:
    using Clock=std::chrono::steady_clock;
    struct Sample { Clock::time_point at; double ms; };
    struct Timing { std::size_t count=0; double mean=0,p95=0,maximum=0; };
    explicit Performance(Clock::time_point start=Clock::now()):started_(start),process_at_(start){}
    void tick(double ms,Clock::time_point now=Clock::now());
    void work(double ms,Clock::time_point now=Clock::now());
    double tps(double seconds,Clock::time_point now=Clock::now()) const;
    Timing ticks(Clock::time_point now=Clock::now()) const;
    Timing work_times(Clock::time_point now=Clock::now()) const;
    double uptime(Clock::time_point now=Clock::now()) const;
    void sample_process();
    std::string brief() const;
    static double elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
    std::uint64_t bytes_received=0,bytes_sent=0,tick_count=0,over_budget=0,dropped_tick_slots=0;
    std::uint64_t chunks_generated=0,chunks_sent=0,cache_hits=0,saves=0;
    std::size_t connections=0,queued_bytes=0,working_set=0,private_bytes=0;
    double cpu_percent=0,receive_rate=0,send_rate=0,last_generation_ms=0,max_generation_ms=0,last_chunk_ms=0,last_save_ms=0;
    bool process_available=false,cpu_available=false;
private:
    Clock::time_point started_,process_at_;
    std::deque<Sample> ticks_,work_;
    std::uint64_t process_cpu_=0,previous_received_=0,previous_sent_=0;
    static void append(std::deque<Sample>& samples,double ms,Clock::time_point now);
    static Timing summarize(const std::deque<Sample>& samples,Clock::time_point now);
};
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <avrt.h>
#else
#include <pthread.h>
#include <pthread/qos.h>
#include <condition_variable>
#include <mutex>
#endif
namespace vban {
class WorkerPriority {
#ifdef _WIN32
    DWORD index_=0;
    HANDLE handle_=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index_);
public:
    bool active() const { return handle_ != nullptr; }
    ~WorkerPriority() { if(handle_) AvRevertMmThreadCharacteristics(handle_); }
#else
    bool active_=pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0)==0;
public:
    bool active() const { return active_; }
#endif
};
class WorkerWait {
#ifdef _WIN32
    HANDLE stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE timer_=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
public:
    WorkerWait() {
        if(!timer_) timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        if(!stop_ || !timer_) {
            if(stop_) CloseHandle(stop_);
            if(timer_) CloseHandle(timer_);
            throw std::runtime_error("Cannot create monitor return worker timer.");
        }
    }
    ~WorkerWait() { CloseHandle(timer_); CloseHandle(stop_); }
    void stop() { SetEvent(stop_); }
    void wait(int64_t nanoseconds) {
        LARGE_INTEGER due{}; due.QuadPart=-(nanoseconds/100);
        if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE))
            throw std::runtime_error("Monitor return scheduling timer failed.");
        const HANDLE handles[]{stop_,timer_};
        if(WaitForMultipleObjects(2,handles,FALSE,INFINITE)==WAIT_FAILED)
            throw std::runtime_error("Monitor return worker wait failed.");
    }
#else
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_=false;
public:
    void stop() { { std::lock_guard lock(mutex_); stopping_=true; } wake_.notify_all(); }
    void wait(int64_t nanoseconds) {
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock,std::chrono::nanoseconds(nanoseconds),[this]{return stopping_;});
    }
#endif
};
}

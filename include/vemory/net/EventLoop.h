#pragma once

#include <sys/epoll.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <utility>

#include <spdlog/spdlog.h>

#include "vemory/util/Timer.h"

inline constexpr int kMaxEvents = 1024;

using IoEvents = uint32_t;
using IoHandler = std::function<void(IoEvents)>;

class EventLoop {
 public:
  EventLoop() : epfd_(::epoll_create1(0)) {
    if (epfd_ == -1) {
      spdlog::error("epoll_create error: {}", errno);
      exit(EXIT_FAILURE);
    }
  }

  ~EventLoop() { close(epfd_); }

  void SetIdleCallback(std::function<void()> cb) { idle_ = std::move(cb); }

  void AddEvent(int fd, IoEvents events, void* ptr) {
    epoll_event ev;
    ev.events = events;
    ev.data.ptr = ptr;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
      spdlog::error("epoll_ctl add error: {}", errno);
    }
  }

  void ModEvent(int fd, IoEvents events, void* ptr) {
    epoll_event ev;
    ev.events = events;
    ev.data.ptr = ptr;
    if (::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
      spdlog::error("epoll_ctl mod error: {}", errno);
    }
  }

  void DelEvent(int fd) {
    if (::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
      spdlog::error("epoll_ctl del error: {}", errno);
    }
  }

  void Run() {
    epoll_event events[kMaxEvents];
    while (true) {
      int nfds = ::epoll_wait(epfd_, events, kMaxEvents,
                              Timer::GetInstance()->WaitTime());
      if (nfds == -1) {
        if (errno == EINTR) {
          continue;
        }
        spdlog::error("epoll_wait error: {}", errno);
        return;
      }

      for (int i = 0; i < nfds; ++i) {
        auto* handler = static_cast<IoHandler*>(events[i].data.ptr);
        (*handler)(events[i].events);
      }
      Timer::GetInstance()->HandleTimeout();
      if (idle_) {
        idle_();
      }
    }
  }

 private:
  int epfd_;
  std::function<void()> idle_;
};

////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Christoph Uhde
/// @author Ewout Prangsma
////////////////////////////////////////////////////////////////////////////////
#pragma once

#ifndef ARANGO_CXX_DRIVER_SERVER
#define ARANGO_CXX_DRIVER_SERVER

#include <thread>
#include <utility>

#include <fuerte/asio_ns.h>

// run / runWithWork / poll for Loop mapping to ioservice
// free function run with threads / with thread group barrier and work

namespace arangodb { namespace fuerte { inline namespace v1 {

namespace impl {
class VpackInit;
}

// need partial rewrite so it can be better integrated in client applications

typedef asio_ns::io_context asio_io_context;
typedef asio_ns::executor_work_guard<asio_ns::io_context::executor_type>
    asio_work_guard;

// GlobalService is intended to be instantiated once for the entire
// lifetime of a program using fuerte.
// It initializes all global state, needed by fuerte.
class GlobalService {
 public:
  // get GlobalService singelton.
  static GlobalService& get() {
    static GlobalService service;
    return service;
  }

 private:
  GlobalService();
  ~GlobalService();

  // Prevent copying
  GlobalService(GlobalService const& other) = delete;
  GlobalService& operator=(GlobalService const& other) = delete;

 private:
  std::unique_ptr<impl::VpackInit> _vpack_init;
};

/// @brief EventLoopService implements single-threaded event loops
/// Idea is to shard connections across io context's to avoid
/// unnecessary synchronization overhead. Please note that on
/// linux epoll has max 64 instances, so there is a limit on the
/// number of io_context instances.
class EventLoopService {
  friend class ConnectionBuilder;

 public:
  // Initialize an EventLoopService with a given number of threads
  //  and a given number of io_context
  EventLoopService(unsigned int threadCount = 1);
  virtual ~EventLoopService();

  // Prevent copying
  EventLoopService(EventLoopService const& other) = delete;
  EventLoopService& operator=(EventLoopService const& other) = delete;

 protected:

  // io_service returns a reference to the boost io_service.
  std::shared_ptr<asio_io_context>& nextIOContext() {
    return _ioContexts[_lastUsed.fetch_add(1, std::memory_order_relaxed) % _ioContexts.size()];
  }

 private:
  GlobalService& global_service_;
  std::atomic<uint32_t> _lastUsed;

  /// io contexts
  std::vector<std::shared_ptr<asio_io_context>> _ioContexts;
  /// Used to keep the io-context alive.
  std::vector<asio_work_guard> _guards;
  /// Threads powering each io_context
  std::vector<std::thread> _threads;
};
}}}  // namespace arangodb::fuerte::v1
#endif

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
////////////////////////////////////////////////////////////////////////////////

#include <memory>

#include <fuerte/FuerteLogger.h>
#include <fuerte/loop.h>
#include <fuerte/types.h>

namespace arangodb { namespace fuerte { inline namespace v1 {

EventLoopService::EventLoopService(unsigned int threadCount) : _lastUsed(0) {
  for (unsigned i = 0; i < threadCount; i++) {
    _ioContexts.emplace_back(new asio_ns::io_context(1));
    _guards.emplace_back(asio_ns::make_work_guard(*_ioContexts.back()));
    _threads.emplace_back([this, i]() { _ioContexts[i]->run(); });
  }
}

EventLoopService::~EventLoopService() {
  for (auto& guard : _guards) {
    guard.reset();  // allow run() to exit
  }
  for (std::thread& thread : _threads) {
    thread.join();
  }
  for (auto& ctx : _ioContexts) {
    ctx->stop();
  }
}
}}}  // namespace arangodb::fuerte::v1

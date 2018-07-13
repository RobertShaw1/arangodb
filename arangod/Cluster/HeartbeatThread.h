////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2018 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_CLUSTER_HEARTBEAT_THREAD_H
#define ARANGOD_CLUSTER_HEARTBEAT_THREAD_H 1

#include "Basics/Thread.h"

#include "Agency/AgencyComm.h"
#include "Basics/ConditionVariable.h"
#include "Basics/Mutex.h"
#include "Cluster/CriticalThread.h"
#include "Cluster/DBServerAgencySync.h"
#include "Logger/Logger.h"

#include <chrono>
#include <velocypack/Slice.h>

namespace arangodb {

struct AgencyVersions {
  uint64_t plan;
  uint64_t current;

  AgencyVersions(uint64_t _plan, uint64_t _current)
      : plan(_plan), current(_plan) {}

  explicit AgencyVersions(const DBServerAgencySyncResult& result)
      : plan(result.planVersion), current(result.currentVersion) {}
};

class AgencyCallbackRegistry;

class HeartbeatThread : public CriticalThread,
                        public std::enable_shared_from_this<HeartbeatThread> {
 public:
  HeartbeatThread(AgencyCallbackRegistry*, std::chrono::microseconds,
                  uint64_t maxFailsBeforeWarning);
  ~HeartbeatThread();

 public:
  //////////////////////////////////////////////////////////////////////////////
  /// @brief initializes the heartbeat
  //////////////////////////////////////////////////////////////////////////////

  bool init();

  //////////////////////////////////////////////////////////////////////////////
  /// @brief whether or not the thread is ready
  //////////////////////////////////////////////////////////////////////////////

  bool isReady() const { return _ready.load(); }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief set the thread status to ready
  //////////////////////////////////////////////////////////////////////////////

  void setReady() { _ready.store(true); }

  void runBackgroundJob();

  void dispatchedJobResult(DBServerAgencySyncResult);

  //////////////////////////////////////////////////////////////////////////////
  /// @brief whether or not the thread has run at least once.
  /// this is used on the coordinator only
  //////////////////////////////////////////////////////////////////////////////

  static bool hasRunOnce() { return HasRunOnce.load(std::memory_order_acquire); }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief break runDBserver out of wait on condition after setting state in
  /// base class
  //////////////////////////////////////////////////////////////////////////////
  virtual void beginShutdown() override;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief add thread name to ongoing list of threads that have crashed
  ///        unexpectedly
  //////////////////////////////////////////////////////////////////////////////

  static void recordThreadDeath(const std::string & threadName);

  //////////////////////////////////////////////////////////////////////////////
  /// @brief post list of deadThreads to current log.  Called regularly, but only
  ///        posts to log roughly every 60 minutes
  //////////////////////////////////////////////////////////////////////////////

  static void logThreadDeaths(bool force=false);

 protected:
  //////////////////////////////////////////////////////////////////////////////
  /// @brief heartbeat main loop
  //////////////////////////////////////////////////////////////////////////////

  void run() override;

 private:
  //////////////////////////////////////////////////////////////////////////////
  /// @brief heartbeat main loop, coordinator version
  //////////////////////////////////////////////////////////////////////////////

  void runCoordinator();

  //////////////////////////////////////////////////////////////////////////////
  /// @brief heartbeat main loop, dbserver version
  //////////////////////////////////////////////////////////////////////////////

  void runDBServer();

  //////////////////////////////////////////////////////////////////////////////
  /// @brief heartbeat main loop, single server version
  //////////////////////////////////////////////////////////////////////////////

  void runSingleServer();

  //////////////////////////////////////////////////////////////////////////////
  /// @brief heartbeat main loop for agent and single db ... provides thread crash reporting
  //////////////////////////////////////////////////////////////////////////////

  void runSimpleServer();

  //////////////////////////////////////////////////////////////////////////////
  /// @brief handles a plan change, coordinator case
  //////////////////////////////////////////////////////////////////////////////

  bool handlePlanChangeCoordinator(uint64_t);

  //////////////////////////////////////////////////////////////////////////////
  /// @brief handles a plan change, DBServer case
  //////////////////////////////////////////////////////////////////////////////

  bool handlePlanChangeDBServer(uint64_t);

  //////////////////////////////////////////////////////////////////////////////
  /// @brief sends the current server's state to the agency
  //////////////////////////////////////////////////////////////////////////////

  bool sendServerState();

  //////////////////////////////////////////////////////////////////////////////
  /// @brief bring the db server in sync with the desired state
  //////////////////////////////////////////////////////////////////////////////

  void syncDBServerStatusQuo(bool asyncPush = false);

  //////////////////////////////////////////////////////////////////////////////
  /// @brief update the local agent pool from the slice
  //////////////////////////////////////////////////////////////////////////////

  void updateAgentPool(arangodb::velocypack::Slice const& agentPool);

  //////////////////////////////////////////////////////////////////////////////
  /// @brief update the server mode from the slice
  //////////////////////////////////////////////////////////////////////////////

  void updateServerMode(arangodb::velocypack::Slice const& readOnlySlice);

 private:
  //////////////////////////////////////////////////////////////////////////////
  /// @brief AgencyCallbackRegistry
  //////////////////////////////////////////////////////////////////////////////

  AgencyCallbackRegistry* _agencyCallbackRegistry;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief status lock
  //////////////////////////////////////////////////////////////////////////////

  std::shared_ptr<arangodb::Mutex> _statusLock;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief AgencyComm instance
  //////////////////////////////////////////////////////////////////////////////

  AgencyComm _agency;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief condition variable for heartbeat
  //////////////////////////////////////////////////////////////////////////////

  arangodb::basics::ConditionVariable _condition;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief this server's id
  //////////////////////////////////////////////////////////////////////////////

  std::string const _myId;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief heartbeat interval
  //////////////////////////////////////////////////////////////////////////////

  std::chrono::microseconds  _interval;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief number of fails in a row before a warning is issued
  //////////////////////////////////////////////////////////////////////////////

  uint64_t _maxFailsBeforeWarning;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief current number of fails in a row
  //////////////////////////////////////////////////////////////////////////////

  uint64_t _numFails;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief last successfully dispatched version
  //////////////////////////////////////////////////////////////////////////////

  uint64_t _lastSuccessfulVersion;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief current plan version
  //////////////////////////////////////////////////////////////////////////////

  uint64_t _currentPlanVersion;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief whether or not the thread is ready
  //////////////////////////////////////////////////////////////////////////////

  std::atomic<bool> _ready;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief whether or not the heartbeat thread has run at least once
  /// this is used on the coordinator only
  //////////////////////////////////////////////////////////////////////////////

  static std::atomic<bool> HasRunOnce;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief keeps track of the currently installed versions
  //////////////////////////////////////////////////////////////////////////////
  AgencyVersions _currentVersions;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief keeps track of the currently desired versions
  //////////////////////////////////////////////////////////////////////////////
  std::shared_ptr<AgencyVersions> _desiredVersions;

  bool _wasNotified;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief number of background jobs that have been posted to the scheduler
  //////////////////////////////////////////////////////////////////////////////

  std::atomic<uint64_t> _backgroundJobsPosted;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief number of background jobs that have been launched by the scheduler
  //////////////////////////////////////////////////////////////////////////////

  std::atomic<uint64_t> _backgroundJobsLaunched;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief flag indicates whether or not a background job is either
  /// scheduled with boost::asio or is already running, this and the
  /// next one about having to start another background job when the
  /// current one is finished are protected by the statusLock.
  //////////////////////////////////////////////////////////////////////////////

  bool _backgroundJobScheduledOrRunning;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief flag indicates whether or not a new background job needs
  /// to be started when the current one has terminated. This and the
  /// previous one are protected by the statusLock.
  //////////////////////////////////////////////////////////////////////////////

  bool _launchAnotherBackgroundJob;

  // when was the javascript sync routine last run?
  double _lastSyncTime;
};
}

#endif

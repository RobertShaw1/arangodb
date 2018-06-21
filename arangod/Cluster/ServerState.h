////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
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

#ifndef ARANGOD_CLUSTER_SERVER_STATE_H
#define ARANGOD_CLUSTER_SERVER_STATE_H 1

#include "Basics/Common.h"
#include "Basics/ReadWriteLock.h"

#include <iosfwd>

namespace arangodb {
class AgencyComm;
class Result;

class ServerState {
 public:
  /// @brief an enum describing the roles a server can have
  enum RoleEnum : int {
    ROLE_UNDEFINED = 0,  // initial value
    ROLE_SINGLE,         // is set when cluster feature is off
    ROLE_PRIMARY,
    ROLE_COORDINATOR,
    ROLE_AGENT
  };

  /// @brief an enum describing the possible states a server can have
  enum StateEnum {
    STATE_UNDEFINED = 0,  // initial value
    STATE_STARTUP,        // used by all roles
    STATE_SERVING,        // used by all roles
    STATE_STOPPING,       // primary only
    STATE_STOPPED,        // primary only
    STATE_SHUTDOWN        // used by all roles
  };

  enum class Mode : uint8_t {
    DEFAULT = 0,
    /// reject all requests
    MAINTENANCE = 1,
    /// status unclear, client must try again
    TRYAGAIN = 2,
    /// redirect to lead server if possible
    REDIRECT = 3,
    INVALID = 255, // this mode is used to indicate shutdown
  };

 public:
  ServerState();

  ~ServerState();

 public:
  /// @brief create the (sole) instance
  static ServerState* instance();

  /// @brief get the string representation of a role
  static std::string roleToString(RoleEnum);

  static std::string roleToShortString(RoleEnum);

  /// @brief get the key for lists of a role in the agency
  static std::string roleToAgencyListKey(RoleEnum);

  /// @brief get the key for a role in the agency
  static std::string roleToAgencyKey(RoleEnum role);

  /// @brief convert a string to a role
  static RoleEnum stringToRole(std::string const&);

  /// @brief get the string representation of a state
  static std::string stateToString(StateEnum);

  /// @brief convert a string representation to a state
  static StateEnum stringToState(std::string const&);

  /// @brief get the string representation of a mode
  static std::string modeToString(Mode);

  /// @brief convert a string representation to a mode
  static Mode stringToMode(std::string const&);

 public:
  /// @brief sets the initialized flag
  void setInitialized() { _initialized = true; }

  /// @brief whether or not the cluster was properly initialized
  bool initialized() const { return _initialized; }

  /// @brief flush the server state (used for testing)
  void flush();

  bool isSingleServer() { return isSingleServer(loadRole()); }

  static bool isSingleServer(ServerState::RoleEnum role) {
    return (role == ServerState::ROLE_SINGLE);
  }

  /// @brief check whether the server is a coordinator
  bool isCoordinator() { return isCoordinator(loadRole()); }

  /// @brief check whether the server is a coordinator
  static bool isCoordinator(ServerState::RoleEnum role) {
    return (role == ServerState::ROLE_COORDINATOR);
  }

  /// @brief check whether the server is a DB server (primary or secondary)
  /// running in cluster mode.
  bool isDBServer() { return isDBServer(loadRole()); }

  /// @brief check whether the server is a DB server (primary or secondary)
  /// running in cluster mode.
  static bool isDBServer(ServerState::RoleEnum role) {
    return (role == ServerState::ROLE_PRIMARY);
  }

  /// @brief whether or not the role is a cluster-related role
  static bool isClusterRole(ServerState::RoleEnum role) {
    return (role == ServerState::ROLE_PRIMARY ||
            role == ServerState::ROLE_COORDINATOR);
  }

  /// @brief whether or not the role is a cluster-related role
  bool isClusterRole() {return (isClusterRole(loadRole()));};

  /// @brief check whether the server is an agent
  bool isAgent() { return isAgent(loadRole()); }

  /// @brief check whether the server is an agent
  static bool isAgent(ServerState::RoleEnum role) {
    return (role == ServerState::ROLE_AGENT);
  }

  /// @brief check whether the server is running in a cluster
  bool isRunningInCluster() { return isClusterRole(loadRole()); }

  /// @brief check whether the server is running in a cluster
  static bool isRunningInCluster(ServerState::RoleEnum role) {
    return isClusterRole(role);
  }

  /// @brief check whether the server is a single or coordinator
  bool isSingleServerOrCoordinator() {
    RoleEnum role = loadRole();
    return isCoordinator(role) || isSingleServer(role);
  }

  /// @brief get the server role
  inline RoleEnum getRole() const { return loadRole(); }

  /// @brief register with agency, create / load server ID
  bool integrateIntoCluster(RoleEnum role, std::string const& myAddr);

  /// @brief unregister this server with the agency
  bool unregister();

  /// @brief set the server role
  void setRole(RoleEnum);
  
  /// @brief atomically load current server mode
  Mode mode() const {
    return _mode.load(std::memory_order_acquire);;
  }
  
  /// @brief sets server mode, returns previously held
  /// value (performs atomic read-modify-write operation)
  Mode setServerMode(Mode mode);
  
  /// @brief checks maintenance mode
  bool isMaintenance() const {
    return mode() == Mode::MAINTENANCE;
  }
  
  /// @brief should not allow DDL operations / transactions
  bool readOnly() const {
    return _readOnly.load(std::memory_order_acquire);
  }

  /// @brief set server read-only
  bool setReadOnly(bool ro) {
    return _readOnly.exchange(ro, std::memory_order_release);
  }

  /// @brief get the server id
  std::string getId() const;

  /// @brief set the server id
  void setId(std::string const&);

  /// @brief get the server address
  std::string getAddress();

  /// @brief set the server address
  void setAddress(std::string const&);

  /// @brief find a host identification string
  void findHost(std::string const& fallback);

  /// @brief get a string to identify the host we are running on
  std::string getHost() {
    return _host;
  }

  /// @brief get the current state
  StateEnum getState();

  /// @brief set the current state
  void setState(StateEnum);

  /// @brief gets the JavaScript startup path
  std::string getJavaScriptPath();

  /// @brief sets the JavaScript startup path
  void setJavaScriptPath(std::string const&);

  bool isFoxxmaster();

  std::string const& getFoxxmaster();

  void setFoxxmaster(std::string const&);

  void setFoxxmasterQueueupdate(bool);

  bool getFoxxmasterQueueupdate();

  std::string getPersistedId();
  bool hasPersistedId();
  bool writePersistedId(std::string const&);
  std::string generatePersistedId(RoleEnum const&);

  /// @brief sets server mode and propagates new mode to agency
  Result propagateClusterReadOnly(bool);

  /// file where the server persists it's UUID
  std::string getUuidFilename();

 private:
  /// @brief atomically fetches the server role
  inline RoleEnum loadRole() const {
    return _role.load(std::memory_order_consume);
  }

  /// @brief validate a state transition for a primary server
  bool checkPrimaryState(StateEnum);

  /// @brief validate a state transition for a coordinator server
  bool checkCoordinatorState(StateEnum);

  /// @brief register at agency
  bool registerAtAgency(AgencyComm&, const RoleEnum&, std::string const&);
  /// @brief register shortname for an id
  bool registerShortName(std::string const& id, const RoleEnum&);
  
private:
  
  /// @brief server role
  std::atomic<RoleEnum> _role;
  
  /// @brief server mode
  std::atomic<ServerState::Mode> _mode;
  
  /// @brief is this server in the read-only mode
  std::atomic<bool> _readOnly;
  
  /// @brief r/w lock for state
  mutable arangodb::basics::ReadWriteLock _lock;

  /// @brief the server's id, can be set just once
  std::string _id;

  /// @brief the JavaScript startup path, can be set just once
  std::string _javaScriptStartupPath;

  /// @brief the server's own address, can be set just once
  std::string _address;

  /// @brief an identification string for the host a server is running on
  std::string _host;

  /// @brief the current state
  StateEnum _state;

  /// @brief whether or not the cluster was initialized
  bool _initialized;

  std::string _foxxmaster;

  bool _foxxmasterQueueupdate;
};
}

std::ostream& operator<<(std::ostream&, arangodb::ServerState::RoleEnum);

#endif

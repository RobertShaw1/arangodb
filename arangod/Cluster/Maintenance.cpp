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
/// @author Kaveh Vahedipour
/// @author Matthew Von-Maszewski
////////////////////////////////////////////////////////////////////////////////

#include "Basics/StringUtils.h"
#include "Basics/VelocyPackHelper.h"
#include "Logger/Logger.h"
#include "Cluster/FollowerInfo.h"
#include "Cluster/Maintenance.h"
#include "Utils/DatabaseGuard.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/Methods/Databases.h"

#include <velocypack/Compare.h>
#include <velocypack/Iterator.h>
#include <velocypack/Slice.h>
#include <velocypack/velocypack-aliases.h>

#include <algorithm>

using namespace arangodb;
using namespace arangodb::basics;
using namespace arangodb::maintenance;
using namespace arangodb::methods;

static std::vector<std::string> cmp {
  "journalSize", "waitForSync", "doCompact", "indexBuckets"};
static std::string const CURRENT_COLLECTIONS("Current/Collections/");
static std::string const CURRENT_DATABASES("Current/Databases/");
static std::string const ERROR_MESSAGE("errorMessage");
static std::string const ERROR_NUM("errorNum");
static std::string const ERROR("error");
static std::string const PLAN_ID("planId");
static std::string const PRIMARY("primary");
static std::string const SERVERS("servers");
static std::string const SELECTIVITY_ESTIMATE("selectivityEstimate");
static std::string const COLLECTIONS("Collections");
static std::string const DB ("/_db/");
static std::string const FOLLOWER_ID("followerId");
static VPackValue const VP_DELETE("delete");
static VPackValue const VP_SET("set");
static std::string const OP("op");
static std::string const UNDERSCORE("_");

std::shared_ptr<VPackBuilder> createProps(VPackSlice const& s) {
  auto builder = std::make_shared<VPackBuilder>();
  TRI_ASSERT(s.isObject());
  { VPackObjectBuilder b(builder.get());
    for (auto const& attr : VPackObjectIterator(s)) {
      std::string const key = attr.key.copyString();
      if (key == ID || key == NAME) {
        continue;
      }
      builder->add(key, attr.value);
    }}
  return builder;
}


std::shared_ptr<VPackBuilder> compareRelevantProps (
  VPackSlice const& first, VPackSlice const& second) {
  auto result = std::make_shared<VPackBuilder>();
  { VPackObjectBuilder b(result.get());
    for (auto property : cmp) {
      auto const& planned = first.get(property);
      if (planned != second.get(property)) { // Register any change
        result->add(property,planned);
      }
    }
  }
  return result;
}


VPackBuilder compareIndexes(
  std::string const& shname, VPackSlice const& plan, VPackSlice const& local,
  std::unordered_set<std::string>& indis) {
  
  VPackBuilder builder;
  { VPackArrayBuilder a(&builder);
    if (plan.isArray()) {
      for (auto const& pindex : VPackArrayIterator(plan)) {

        // Skip unique on _key
        auto const& ptype   = pindex.get(TYPE).copyString();
        if (ptype == PRIMARY || ptype == EDGE) { 
          continue;
        }
        auto const& pfields = pindex.get(FIELDS);
        indis.emplace(shname + "/" + pindex.get(ID).copyString());
      
        bool found = false;
        if (local.isArray()) {
          for (auto const& lindex : VPackArrayIterator(local)) {

            // Skip unique and edge indexes
            auto const& ltype   = lindex.get(TYPE).copyString();
            if (ltype == PRIMARY || ltype == EDGE) { 
              continue;
            }
            auto const& lfields = lindex.get(FIELDS);

            // Already have
            if (VPackNormalizedCompare::equals(pfields, lfields) && ptype == ltype) { 
              found = true;
              break;
            }
          }
        }
        if (!found) {
          builder.add(pindex);
        }
      }
    }
  }
  
  return builder;
}


void handlePlanShard(
  VPackSlice const& db, VPackSlice const& cprops, VPackSlice const& ldb,
  std::string const& dbname, std::string const& colname, std::string const& shname,
  std::string const& serverId, std::string const& leaderId,
  std::unordered_set<std::string>& colis, std::unordered_set<std::string>& indis,
  std::vector<ActionDescription>& actions) {

  bool shouldBeLeading = serverId == leaderId;
  
  // We only care for shards, where we find our own ID
  if (db.copyString() == serverId)  {
    colis.emplace(shname);
    auto props = createProps(cprops); // Only once might need often!

    if (ldb.hasKey(shname)) {   // Have local collection with that name
      auto const lcol = ldb.get(shname);
      bool leading = lcol.get(LEADER).copyString().empty();
      auto const properties = compareRelevantProps(cprops, lcol);

      // If comparison has brought any updates
      if (properties->slice() != VPackSlice::emptyObjectSlice()
          || leading != shouldBeLeading) {
        actions.push_back(
          ActionDescription(
            {{NAME, "UpdateCollection"}, {DATABASE, dbname}, {COLLECTION, shname},
              {LEADER, shouldBeLeading ? std::string() : leaderId},
              {LOCAL_LEADER, lcol.get(LEADER).copyString()}},
            properties));
      }
      
      // Indexes
      if (cprops.hasKey(INDEXES)) {
        auto const& pindexes = cprops.get(INDEXES);
        auto const& lindexes = lcol.get(INDEXES);
        auto difference = compareIndexes(shname, pindexes, lindexes, indis);

        if (difference.slice().isArray()) {
          for (auto const& index : VPackArrayIterator(difference.slice())) {
            actions.push_back(
              ActionDescription({{NAME, "EnsureIndex"}, {COLLECTION, shname},
                {DATABASE, dbname}, {TYPE, index.get(TYPE).copyString()},
                {FIELDS, index.get(FIELDS).toJson()}
                }, std::make_shared<VPackBuilder>(index))
              );
          }
        }
      }
    } else {                   // Create the sucker!
      actions.push_back(
        ActionDescription(
          {{NAME, "CreateCollection"}, {COLLECTION, colname}, {SHARD, shname},
            {DATABASE, dbname}, {LEADER, shouldBeLeading ? std::string() : leaderId}},
          props));
    }
  }
}            


void handleLocalShard(
  std::string const& dbname, std::string const& colname, VPackSlice const& cprops, 
  VPackSlice const& shardMap, std::unordered_set<std::string>& colis,
  std::unordered_set<std::string>& indis, std::string serverId,
  std::vector<ActionDescription>& actions) {
  
  bool drop = false;
  std::unordered_set<std::string>::const_iterator it;

  std::string plannedLeader ;
  if (shardMap.hasKey(colname) && shardMap.get(colname).isArray()) {
    plannedLeader = shardMap.get(colname)[0].copyString();
  }
  bool localLeader = cprops.get(LEADER).copyString().empty();
  if (plannedLeader == UNDERSCORE + serverId && localLeader) {
    actions.push_back(
      ActionDescription (
        {{NAME, "ResignShardLeadership"}, {DATABASE, dbname}, {SHARD, colname}}));
  } else {

    if (colis.empty()) {
      drop = true;
    } else {
      it = std::find(colis.begin(), colis.end(), colname);
      if (it == colis.end()) {
        drop = true;
      }
    }

    if (drop) {
      actions.push_back(
        ActionDescription({{NAME, "DropCollection"},
            {DATABASE, dbname}, {COLLECTION, colname}}));
    } else {
      colis.erase(it);

      // We only drop indexes, when collection is not being dropped already
      if (cprops.hasKey(INDEXES)) {
        if (cprops.get(INDEXES).isArray()) {
          for (auto const& index :
                 VPackArrayIterator(cprops.get(INDEXES))) {
            auto const& type = index.get(TYPE).copyString();
            if (type != PRIMARY && type != EDGE) {
              std::string const id = index.get(ID).copyString();
              if (indis.find(colname + "/" + id) != indis.end()) {
                indis.erase(id);
              } else {
                actions.push_back(
                  ActionDescription(
                    {{NAME, "DropIndex"}, {DATABASE, dbname}, {COLLECTION, colname}, {"index", id}}));
              }
            }
          }
        }
      }
    }
  }
}

/// @brief Get a map shardName -> servers
VPackBuilder getShardMap (VPackSlice const& plan) {
  VPackBuilder shardMap;
  { VPackObjectBuilder o(&shardMap);
    for (auto database : VPackObjectIterator(plan)) {
      for (auto collection : VPackObjectIterator(database.value)) {
        for (auto shard : VPackObjectIterator(collection.value.get(SHARDS))) {
          std::string const shName = shard.key.copyString();
          shardMap.add(shName, shard.value);
        }
      }
    }}
  return shardMap;
}


/// @brief calculate difference between plan and local for for databases
arangodb::Result arangodb::maintenance::diffPlanLocal (
  VPackSlice const& plan, VPackSlice const& local, std::string const& serverId,
  std::vector<ActionDescription>& actions) {

  arangodb::Result result;
  std::unordered_set<std::string> colis; // Intersection collections plan&local
  std::unordered_set<std::string> indis; // Intersection indexes plan&local
  
  // Plan to local mismatch ----------------------------------------------------
  // Create or modify if local databases are affected
  auto pdbs = plan.get("Databases");
  for (auto const& pdb : VPackObjectIterator(pdbs)) {
    auto const& dbname = pdb.key.copyString();
    if (!local.hasKey(dbname)) {
      actions.push_back(
        ActionDescription({{NAME, "CreateDatabase"}, {DATABASE, dbname}}));
    }
  }

  for (auto const& ldb : VPackObjectIterator(local)) {
    auto const& dbname = ldb.key.copyString();
    if (!plan.hasKey(std::vector<std::string> {"Databases", dbname})) {
      actions.push_back(
        ActionDescription({{NAME, "DropDatabase"}, {DATABASE, dbname}}));
    }
  }

  // Create or modify if local collections are affected
  pdbs = plan.get(COLLECTIONS);
  for (auto const& pdb : VPackObjectIterator(pdbs)) {
    auto const& dbname = pdb.key.copyString();
    if (local.hasKey(dbname)) {    // have database in both see to collections
      auto const& ldb = local.get(dbname);
      for (auto const& pcol : VPackObjectIterator(pdb.value)) {
        auto const& cprops = pcol.value;
        for (auto const& shard : VPackObjectIterator(cprops.get(SHARDS))) {
          if (shard.value.isArray()) {
            for (auto const& db : VPackArrayIterator(shard.value)) {
              handlePlanShard(
                db, cprops, ldb, dbname, pcol.key.copyString(),
                shard.key.copyString(), serverId, shard.value[0].copyString(),
                colis, indis, actions);
            }
          }
        }
      }
    } else { // Create new database
      actions.push_back(
        ActionDescription({{NAME, "CreateDatabase"}, {DATABASE, dbname}}));
    }
  }
  
  // Compare local to plan -----------------------------------------------------
  auto const shardMap = getShardMap(pdbs);
  for (auto const& db : VPackObjectIterator(local)) {
    auto const& dbname = db.key.copyString();
    if (pdbs.hasKey(dbname)) {
      for (auto const& col : VPackObjectIterator(db.value)) {
        std::string shName = col.key.copyString();
        if (shName.front() != '_') { // exclude local system collections
          handleLocalShard(dbname, shName, col.value, shardMap.slice(), colis,
                           indis, serverId, actions);
        } 
      }
    } 
  }

  return result;
  
}

/// @brief handle plan for local databases
arangodb::Result arangodb::maintenance::executePlan (
  VPackSlice const& plan, VPackSlice const& local,
  std::string const& serverId, MaintenanceFeature& feature) {

  arangodb::Result result;
  
  // build difference between plan and local
  std::vector<ActionDescription> actions;
  diffPlanLocal(plan, local, serverId, actions);
  
  // enact all
  for (auto const& action : actions) {
    LOG_TOPIC(DEBUG, Logger::MAINTENANCE) << "adding action " << action << " to feature ";
    feature.addAction(std::make_shared<ActionDescription>(action), true);
  }
  
  return result;  
}

/// @brief add new database to current
void addDatabaseToTransactions(
  std::string const& name, Transactions& transactions) {

  // [ {"dbPath":{}}, {"dbPath":{"oldEmpty":true}} ]
  
  std::string dbPath = CURRENT_COLLECTIONS + name;
  VPackBuilder operation; // create database in current
  { VPackObjectBuilder b(&operation);
    operation.add(dbPath, VPackSlice::emptyObjectSlice()); }
  VPackBuilder precondition;
  { VPackObjectBuilder b(&precondition);
    precondition.add(VPackValue(dbPath));
    { VPackObjectBuilder bb(&precondition);
      precondition.add("oldEmpty", VPackValue(true)); }}
  transactions.push_back({operation, precondition});
  
}
  
/// @brief report local to current
arangodb::Result arangodb::maintenance::diffLocalCurrent (
  VPackSlice const& local, VPackSlice const& current,
  std::string const& serverId, Transactions& transactions) {

  arangodb::Result result;
  auto const& cdbs = current;

  // Iterate over local databases
  for (auto const& ldbo : VPackObjectIterator(local)) {
    std::string dbname = ldbo.key.copyString();
    //VPackSlice ldb = ldbo.value;

    // Current has this database
    if (cdbs.hasKey(dbname)) {

    } else {
      // Create new database in current
      addDatabaseToTransactions(dbname, transactions);
    }
  }
  
  return result;
}


/// @brief Phase one: Compare plan and local and create descriptions
arangodb::Result arangodb::maintenance::phaseOne (
  VPackSlice const& plan, VPackSlice const& local, std::string const& serverId,
  MaintenanceFeature& feature, VPackBuilder& report) {

  arangodb::Result result;
  
  report.add(VPackValue("phaseOne"));
  { VPackObjectBuilder por(&report);

    // Execute database changes
    try {
      result = executePlan(plan, local, serverId, feature);
    } catch (std::exception const& e) {
      LOG_TOPIC(ERR, Logger::MAINTENANCE)
        << "Error executing plan: " << e.what()
        << ". " << __FILE__ << ":" << __LINE__;
    }}

  report.add(VPackValue("Plan"));
  { VPackObjectBuilder p(&report);
    report.add("Version", plan.get("Version"));}
  
  return result;
  
}

VPackBuilder removeSelectivityEstimate(VPackSlice const& index) {
  VPackBuilder ret;
  { VPackObjectBuilder o(&ret);
    for (auto const& i : VPackObjectIterator(index)) {
      auto const& key = i.key.copyString();
      if (key != SELECTIVITY_ESTIMATE) {
        ret.add(key, i.value);
      }
    }
  }
  return ret;
}

VPackBuilder assembleLocalCollectioInfo(
  VPackSlice const& info, std::string const& database,
  std::string const& shard, std::string const& ourselves) {

  VPackBuilder ret;

  auto vocbase = Databases::lookup(database);
  if (vocbase == nullptr) {
    std::string errorMsg(
      "Maintenance::assembleLocalCollectionInfo: Failed to lookup database ");
    errorMsg += database;
    LOG_TOPIC(DEBUG, Logger::MAINTENANCE) << errorMsg;
    { VPackObjectBuilder o(&ret); }
    return ret;
  }

  auto collection = vocbase->lookupCollection(shard);
  if (collection == nullptr) {
    std::string errorMsg(
      "Maintenance::assembleLocalCollectionInfo: Failed to lookup collection ");
    errorMsg += shard;
    LOG_TOPIC(DEBUG, Logger::MAINTENANCE) << errorMsg;
    { VPackObjectBuilder o(&ret); }
    return ret;
  }

  { VPackObjectBuilder r(&ret);
    ret.add(ERROR, VPackValue(false));
    ret.add(ERROR_MESSAGE, VPackValue(std::string()));
    ret.add(ERROR_NUM, VPackValue(0));
    ret.add(VPackValue(INDEXES));
    { VPackArrayBuilder ixs(&ret);
      if (info.get(INDEXES).isArray()) {
        for (auto const& index : VPackArrayIterator(info.get(INDEXES))) {
          ret.add(removeSelectivityEstimate(index).slice());
        }}}
    ret.add(VPackValue(SERVERS));
    { VPackArrayBuilder a(&ret);
      ret.add(VPackValue(ourselves));
      for (auto const& server : *(collection->followers()->get())) {
        ret.add(VPackValue(server));
      }}}
  
  return ret;
  
}

bool equivalent(VPackSlice const& local, VPackSlice const& current) {
  for (auto const& i : VPackObjectIterator(local)) {
    if (!VPackNormalizedCompare::equals(i.value,current.get(i.key.copyString()))) {
      return false;
    }
  }
  return true;
}

VPackBuilder assembleLocalDatabaseInfo (std::string const& database) {

  VPackBuilder ret;
  
  auto vocbase = Databases::lookup(database);
  if (vocbase == nullptr) {
    std::string errorMsg(
      "Maintenance::assembleLocalDatabaseInfo: Failed to lookup database ");
    errorMsg += database;
    LOG_TOPIC(DEBUG, Logger::MAINTENANCE) << errorMsg;
    { VPackObjectBuilder o(&ret); }
    return ret;
  }

  { VPackObjectBuilder o(&ret);
    ret.add(ERROR, VPackValue(false));
    ret.add(ERROR_NUM, VPackValue(0));
    ret.add(ERROR_MESSAGE, VPackValue(""));
    ret.add(ID, VPackValue(std::to_string(vocbase->id())));
    ret.add("name", VPackValue(vocbase->name())); }

  return ret;
}


// udateCurrentForCollections
// diff current and local and prepare agency transactions or whatever
// to update current. Will report the errors created locally to the agency
arangodb::Result arangodb::maintenance::reportInCurrent(
  VPackSlice const& plan, VPackSlice const& cur, VPackSlice const& local,
  std::string const& serverId, VPackBuilder& report) {
  arangodb::Result result;


  for (auto const& database : VPackObjectIterator(local)) {
    auto const dbName = database.key.copyString();

    std::vector<std::string> const cdbpath {"Databases", dbName, serverId};

    if (!cur.hasKey(cdbpath)) {
      auto const localDatabaseInfo = assembleLocalDatabaseInfo(dbName);
      if (!localDatabaseInfo.slice().isEmptyObject()) {
        report.add(VPackValue(CURRENT_DATABASES + dbName + "/" + serverId));
        { VPackObjectBuilder o(&report);
          report.add(OP, VP_SET);
          report.add("payload", localDatabaseInfo.slice()); }
      }
    }
    
  
    for (auto const& shard : VPackObjectIterator(database.value)) {

      auto const shName = shard.key.copyString();
      if (shName.at(0) == '_') { // local system collection
        continue;
      }
      auto const shSlice = shard.value;

      auto const colName = shSlice.get(PLAN_ID).copyString();
      VPackBuilder error;

  
      if (shSlice.get(LEADER).copyString().empty()) { // Leader

        auto const localCollectionInfo =
          assembleLocalCollectioInfo(shSlice, dbName, shName, serverId);

        // Collection no longer exists
        if (localCollectionInfo.slice().isEmptyObject()) { 
          continue;
        }
        
        auto cp = std::vector<std::string> {COLLECTIONS, dbName, colName, shName};
        
  
        auto inCurrent = cur.hasKey(cp);
        if (!inCurrent ||
            (inCurrent &&
             !equivalent(localCollectionInfo.slice(), cur.get(cp)))) {

  
        report.add(
          VPackValue(CURRENT_COLLECTIONS + dbName + "/" + colName + "/" +shName));
          { VPackObjectBuilder o(&report); 
            report.add(OP, VP_SET);
            report.add("payload", localCollectionInfo.slice()); }
        }
      } else {
  
        auto servers = std::vector<std::string>
          {COLLECTIONS, dbName, colName, shName, SERVERS};
        if (cur.hasKey(servers)) {
          auto s = cur.get(servers);
          if (s.isArray() && cur.get(servers)[0].copyString() == serverId) {
            // we were previously leader and we are done resigning.
            // update current and let supervision handle the rest
            VPackBuilder ns;
            { VPackArrayBuilder a(&ns);
              bool front = true;
              if (s.isArray()) {
                for (auto const& i : VPackArrayIterator(s)) {
                  ns.add(VPackValue((!front) ? i.copyString() : UNDERSCORE + i.copyString()));
                  front = false;
                }}}
            report.add(
              VPackValue(
                CURRENT_COLLECTIONS + dbName + "/" + colName + "/" + shName
                + "/" + SERVERS));
  
              { VPackObjectBuilder o(&report);
                report.add(OP, VP_SET);
                report.add("payload", ns.slice()); }
            }
          }
      }
    }
  }
  
  auto cdbs = cur.get(COLLECTIONS);
  auto pdbs = plan.get(COLLECTIONS);
  auto shardMap = getShardMap(plan.get(COLLECTIONS));

  // UpdateCurrentForDatabases
  for (auto const& database : VPackObjectIterator(cdbs)) { 
    auto const dbName = database.key.copyString();

     // Database no longer in Plan and local
    if (!local.hasKey(dbName) && !pdbs.hasKey(dbName)) {
      report.add(VPackValue(CURRENT_DATABASES + dbName + "/" + serverId));
      { VPackObjectBuilder o(&report);
        report.add(OP, VP_DELETE); }
      continue;
    }
  
    // UpdateCurrentForCollections (Current/Collections/Collection)
    for (auto const& collection : VPackObjectIterator(database.value)) {
      auto const colName = collection.key.copyString();

      for (auto const& shard : VPackObjectIterator(collection.value)) {
        auto const shName = shard.key.copyString();
        
        // Shard in current and has servers
        if (shard.value.hasKey(SERVERS)) {
          auto servers = shard.value.get(SERVERS);

          if (servers.isArray() && servers.length() > 0    // servers in current
              && servers[0].copyString() == serverId       // we are leading 
              && !local.hasKey(
                std::vector<std::string> {dbName, shName}) // no local collection
              && !shardMap.slice().hasKey(shName)) {               // no such shard in plan
            report.add(
              VPackValue(
                CURRENT_COLLECTIONS + dbName + "/" + colName + "/" + shName));
            { VPackObjectBuilder o(&report);
              report.add(OP, VP_DELETE); }
          }

        }
      }
    }
  }
  
  return result;

}


template<typename T> int indexOf(VPackSlice const& slice, T const& t) {
  size_t counter = 0;
  if (slice.isArray()) {
    for (auto const& entry : VPackArrayIterator(slice)) {
      if (entry.isNumber()) {
        if (entry.getNumber<T>() == t) {
          return counter;
        }
      }
      counter++;
    }
    return -1;
  }
}

template<> int indexOf<std::string> (
  VPackSlice const& slice, std::string const& val) {
  size_t counter = 0;
  if (slice.isArray()) {
    for (auto const& entry : VPackArrayIterator(slice)) {
      if (entry.isString()) {
        if (entry.copyString() == val) {
          return counter;
        }
      }
      counter++;
    }
  }
  return -1;
}

arangodb::Result arangodb::maintenance::syncReplicatedShardsWithLeaders(
  VPackSlice const& plan, VPackSlice const& current, VPackSlice const& local,
  std::string const& serverId, std::vector<ActionDescription>& actions) {

  auto pdbs = plan.get(COLLECTIONS);
  auto cdbs = current.get(COLLECTIONS);
  
  for (auto const& pdb : VPackObjectIterator(pdbs)) {
    auto const& dbname = pdb.key.copyString();
    if (local.hasKey(dbname) && cdbs.hasKey(dbname)) {
      for (auto const& pcol : VPackObjectIterator(pdb.value)) {
        auto const& colname = pcol.key.copyString();
        if (cdbs.get(dbname).hasKey(colname)) {
          for (auto const& pshrd : VPackObjectIterator(pcol.value.get(SHARDS))) {
            auto const& shname = pshrd.key.copyString();

            // shard does not exist locally so nothing we can do at this point
            if (!local.hasKey(std::vector<std::string>{dbname, shname})) {
              continue;
            }

            // current stuff is created by the leader this one here will just
            // bring followers in sync so just continue here
            auto cpath = std::vector<std::string> {dbname, colname, shname};
            if (!cdbs.hasKey(cpath)) {
              continue;
            }

            // Plan's servers
            auto ppath = std::vector<std::string> {dbname, colname, SHARDS, shname};
            if (!pdbs.hasKey(ppath)) {
              LOG_TOPIC(ERR, Logger::MAINTENANCE)
                << "Shard " << shname
                << " does not have servers substructure in 'Plan'";
              continue;
            }
            auto const& pservers = pdbs.get(ppath);
            
            // Current's servers
            cpath.push_back(SERVERS);
            if (!cdbs.hasKey(cpath)) {
              LOG_TOPIC(ERR, Logger::MAINTENANCE)
                << "Shard " << shname
                << " does not have servers substructure in 'Current'";
              continue;
            }
            auto const& cservers = cdbs.get(cpath);

            // we are not planned to be a follower
            if (indexOf(pservers, serverId) <= 0) {
              continue;
            }
            // if we are considered to be in sync there is nothing to do
            if (indexOf(cservers, serverId) > 0) {
              continue;
            }

            auto const leader = pservers[0].copyString();
            actions.push_back(
              ActionDescription(
                {{NAME, "SynchronizeShard"}, {DATABASE, dbname},
                 {COLLECTION, colname}, {SHARD, shname}, {LEADER, leader}}));

          }
        }
      }
    }
  }

  return Result();
  
}



/// @brief Phase two: See, what we can report to the agency
arangodb::Result arangodb::maintenance::phaseTwo (
  VPackSlice const& plan, VPackSlice const& cur, VPackSlice const& local,
  std::string const& serverId, MaintenanceFeature& feature, VPackBuilder& report) {

  arangodb::Result result;

  report.add(VPackValue("phaseTwo"));
  { VPackObjectBuilder p2(&report);

    // Update Current
    try {
      result = reportInCurrent(plan, cur, local, serverId, report);
    } catch (std::exception const& e) {
      LOG_TOPIC(ERR, Logger::MAINTENANCE)
        << "Error reporting in current: " << e.what() << ". "
        << __FILE__ << ":" << __LINE__;
    }

    try {
      std::vector<ActionDescription> actions;
      result = syncReplicatedShardsWithLeaders(plan, cur, local, serverId, actions);

      for (auto const& action : actions) {
        feature.addAction(std::make_shared<ActionDescription>(action), true);
      }
    } catch (std::exception const& e) {
      LOG_TOPIC(ERR, Logger::MAINTENANCE)
        << "Error scheduling shards: " << e.what() << ". "
        << __FILE__ << ":" << __LINE__;
    }}

  report.add(VPackValue("Current"));
  { VPackObjectBuilder p(&report);
    report.add("Version", cur.get("Version"));}
  
  return result;
  
}


arangodb::Result arangodb::maintenance::synchroniseShards (
  VPackSlice const&, VPackSlice const&, VPackSlice const&) {
  arangodb::Result result;
  return result;
}


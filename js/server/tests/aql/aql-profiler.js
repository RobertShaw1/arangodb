/*jshint globalstrict:true, strict:true, esnext: true */
/*global AQL_EXPLAIN */

"use strict";

////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2018 ArangoDB GmbH, Cologne, Germany
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
/// @author Tobias Gödderz
////////////////////////////////////////////////////////////////////////////////


// contains common code for aql-profiler* tests
const profHelper = require("@arangodb/aql-profiler-test-helper");

const db = require('@arangodb').db;
const jsunity = require("jsunity");
const assert = jsunity.jsUnity.assertions;


////////////////////////////////////////////////////////////////////////////////
/// @brief test suite for AQL tracing/profiling
////////////////////////////////////////////////////////////////////////////////


function ahuacatlProfilerTestSuite () {

  // TODO test skipSome as well, all current tests run getSome only

  // import some names from profHelper directly into our namespace:
  const colName = profHelper.colName;
  const defaultBatchSize = profHelper.defaultBatchSize;

  const { CalculationNode, CollectNode, DistributeNode, EnumerateCollectionNode,
    EnumerateListNode, EnumerateViewNode, FilterNode, GatherNode, IndexNode,
    InsertNode, LimitNode, NoResultsNode, RemoteNode, RemoveNode, ReplaceNode,
    ReturnNode, ScatterNode, ShortestPathNode, SingletonNode, SortNode,
    SubqueryNode, TraversalNode, UpdateNode, UpsertNode } = profHelper;

  const { CalculationBlock, CountCollectBlock, DistinctCollectBlock,
    EnumerateCollectionBlock, EnumerateListBlock, FilterBlock,
    HashedCollectBlock, IndexBlock, LimitBlock, NoResultsBlock, RemoteBlock,
    ReturnBlock, ShortestPathBlock, SingletonBlock, SortBlock,
    SortedCollectBlock, SortingGatherBlock, SubqueryBlock, TraversalBlock,
    UnsortingGatherBlock, RemoveBlock, InsertBlock, UpdateBlock, ReplaceBlock,
    UpsertBlock, ScatterBlock, DistributeBlock, IResearchViewUnorderedBlock,
    IResearchViewBlock, IResearchViewOrderedBlock } = profHelper;

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
      db._drop(colName);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test {profile: 0}
////////////////////////////////////////////////////////////////////////////////

    testProfile0Fields : function () {
      const query = 'RETURN 1';
      const profileDefault = db._query(query, {}).getExtra();
      const profile0 = db._query(query, {}, {profile: 0}).getExtra();
      const profileFalse = db._query(query, {}, {profile: false}).getExtra();

      profHelper.assertIsLevel0Profile(profileDefault);
      profHelper.assertIsLevel0Profile(profile0);
      profHelper.assertIsLevel0Profile(profileFalse);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test {profile: 1}
////////////////////////////////////////////////////////////////////////////////

    testProfile1Fields : function () {
      const query = 'RETURN 1';
      const profile1 = db._query(query, {}, {profile: 1}).getExtra();
      const profileTrue = db._query(query, {}, {profile: true}).getExtra();

      profHelper.assertIsLevel1Profile(profile1);
      profHelper.assertIsLevel1Profile(profileTrue);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test {profile: 2}
////////////////////////////////////////////////////////////////////////////////

    testProfile2Fields : function () {
      const query = 'RETURN 1';
      const profile2 = db._query(query, {}, {profile: 2}).getExtra();

      profHelper.assertIsLevel2Profile(profile2);
      profHelper.assertStatsNodesMatchPlanNodes(profile2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief minimal stats test
////////////////////////////////////////////////////////////////////////////////

    testStatsMinimal : function () {
      const query = 'RETURN 1';
      const profile = db._query(query, {}, {profile: 2}).getExtra();

      profHelper.assertIsLevel2Profile(profile);
      profHelper.assertStatsNodesMatchPlanNodes(profile);

      assert.assertEqual(
        [
          { type : SingletonBlock, calls : 1, items : 1 },
          { type : CalculationBlock, calls : 1, items : 1 },
          { type : ReturnBlock, calls : 1, items : 1 }
        ],
        profHelper.getCompactStatsNodes(profile)
      );
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test EnumerateListBlock
////////////////////////////////////////////////////////////////////////////////

    testEnumerateListBlock : function () {
      const query = 'FOR i IN 1..@rows RETURN i';
      const genNodeList = (rows, batches) => [
        { type : SingletonBlock, calls : 1, items : 1 },
        { type : CalculationBlock, calls : 1, items : 1 },
        { type : EnumerateListBlock, calls : batches, items : rows },
        { type : ReturnBlock, calls : batches, items : rows }
      ];
      profHelper.runDefaultChecks({query, genNodeList});
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test CalculationBlock
////////////////////////////////////////////////////////////////////////////////

    testCalculationBlock : function () {
      const query = 'FOR i IN 1..@rows RETURN i*i';
      const genNodeList = (rows, batches) => [
        { type : SingletonBlock, calls : 1, items : 1 },
        { type : CalculationBlock, calls : 1, items : 1 },
        { type : EnumerateListBlock, calls : batches, items : rows },
        { type : CalculationBlock, calls : batches, items : rows },
        { type : ReturnBlock, calls : batches, items : rows }
      ];
      profHelper.runDefaultChecks({query, genNodeList});
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test CountCollectBlock
////////////////////////////////////////////////////////////////////////////////

    testCountCollectBlock : function () {
      const query = 'FOR i IN 1..@rows COLLECT WITH COUNT INTO c RETURN c';
      const genNodeList = (rows, batches) => [
        { type : SingletonBlock, calls : 1, items : 1 },
        { type : CalculationBlock, calls : 1, items : 1 },
        { type : EnumerateListBlock, calls : batches, items : rows },
        { type : CountCollectBlock, calls : 1, items : 1 },
        { type : ReturnBlock, calls : 1, items : 1 }
      ];
      profHelper.runDefaultChecks({query, genNodeList});
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test DistinctCollectBlock
////////////////////////////////////////////////////////////////////////////////

    testDistinctCollectBlock1 : function () {
      const query = 'FOR i IN 1..@rows RETURN DISTINCT i';
      const genNodeList = (rows, batches) => [
        { type : SingletonBlock, calls : 1, items : 1 },
        { type : CalculationBlock, calls : 1, items : 1 },
        { type : EnumerateListBlock, calls : batches, items : rows },
        { type : DistinctCollectBlock, calls : batches, items : rows },
        { type : ReturnBlock, calls : batches, items : rows }
      ];
      profHelper.runDefaultChecks({query, genNodeList});
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test DistinctCollectBlock
////////////////////////////////////////////////////////////////////////////////

    testDistinctCollectBlock2 : function () {
      const query = 'FOR i IN 1..@rows RETURN DISTINCT i%7';
      const genNodeList = (rows, batches) => {
        const resultRows = Math.min(rows, 7);
        const resultBatches = Math.ceil(resultRows / 1000);
        return [
          {type: SingletonBlock, calls: 1, items: 1},
          {type: CalculationBlock, calls: 1, items: 1},
          {type: EnumerateListBlock, calls: batches, items: rows},
          {type: CalculationBlock, calls: batches, items: rows},
          {type: DistinctCollectBlock, calls: resultBatches, items: resultRows},
          {type: ReturnBlock, calls: resultBatches, items: resultRows}
        ];
      };
      profHelper.runDefaultChecks({query, genNodeList});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test FilterBlock
    ////////////////////////////////////////////////////////////////////////////////

    testFilterBlock1: function() {
      const query = 'FOR i IN 1..@rows FILTER true RETURN i';
      const options = {
        optimizer: {
          rules: [
            "-remove-unnecessary-filters",
            "-remove-unnecessary-filters-2",
            "-move-filters-up",
            "-move-filters-up-2",
          ]
        }
      };
      const genNodeList = (rows, batches) => [
        {type: SingletonBlock, calls: 1, items: 1},
        {type: CalculationBlock, calls: 1, items: 1},
        {type: CalculationBlock, calls: 1, items: 1},
        {type: EnumerateListBlock, calls: batches, items: rows},
        {type: FilterBlock, calls: batches, items: rows},
        {type: ReturnBlock, calls: batches, items: rows},
      ];
      profHelper.runDefaultChecks({query, genNodeList, options});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test FilterBlock
    ////////////////////////////////////////////////////////////////////////////////

    testFilterBlock2 : function () {
      const query = 'FOR i IN 1..@rows FILTER i % 13 != 0 RETURN i';
      const genNodeList = (rows, batches) => {
        const rowsAfterFilter = rows - Math.floor(rows / 13);
        const batchesAfterFilter = Math.ceil(rowsAfterFilter / defaultBatchSize);

        return [
          { type : SingletonBlock, calls : 1, items : 1 },
          { type : CalculationBlock, calls : 1, items : 1 },
          { type : EnumerateListBlock, calls : batches, items : rows },
          { type : CalculationBlock, calls : batches, items : rows },
          { type : FilterBlock, calls : batchesAfterFilter, items : rowsAfterFilter },
          { type : ReturnBlock, calls : batchesAfterFilter, items : rowsAfterFilter },
        ];
      };
      profHelper.runDefaultChecks({query, genNodeList});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test FilterBlock
    ////////////////////////////////////////////////////////////////////////////////

    testFilterBlock3 : function () {
      const query = 'FOR i IN 1..@rows FILTER i % 13 == 0 RETURN i';
      const genNodeList = (rows, batches) => {
        const rowsAfterFilter = Math.floor(rows / 13);
        const batchesAfterFilter = Math.max(1, Math.ceil(rowsAfterFilter / defaultBatchSize));

        return [
          { type : SingletonBlock, calls : 1, items : 1 },
          { type : CalculationBlock, calls : 1, items : 1 },
          { type : EnumerateListBlock, calls : batches, items : rows },
          { type : CalculationBlock, calls : batches, items : rows },
          { type : FilterBlock, calls : batchesAfterFilter, items : rowsAfterFilter },
          { type : ReturnBlock, calls : batchesAfterFilter, items : rowsAfterFilter },
        ];
      };
      profHelper.runDefaultChecks({query, genNodeList});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test HashedCollectBlock
    ////////////////////////////////////////////////////////////////////////////////

    testHashedCollectBlock1 : function () {
      const query = 'FOR i IN 1..@rows COLLECT x = i RETURN x';
      const genNodeList = (rows, batches) => {

        return [
          { type: SingletonBlock, calls: 1, items: 1 },
          { type: CalculationBlock, calls: 1, items: 1 },
          { type: EnumerateListBlock, calls: batches, items: rows },
          { type: HashedCollectBlock, calls: 1, items: rows },
          { type: SortBlock, calls: batches, items: rows },
          { type: ReturnBlock, calls: batches, items: rows },
        ];
      };
      profHelper.runDefaultChecks({query, genNodeList});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test HashedCollectBlock
    ////////////////////////////////////////////////////////////////////////////////

    testHashedCollectBlock2 : function () {
      // x is [1,1,1,2,2,2,3,3,3,4,...
      const query = 'FOR i IN 1..@rows COLLECT x = FLOOR((i-1) / 3)+1 RETURN x';
      const genNodeList = (rows, batches) => {
        const rowsAfterCollect = Math.ceil(rows / 3);
        const batchesAfterCollect = Math.ceil(rowsAfterCollect / defaultBatchSize);

        return [
          { type: SingletonBlock, calls: 1, items: 1 },
          { type: CalculationBlock, calls: 1, items: 1 },
          { type: EnumerateListBlock, calls: batches, items: rows },
          { type: CalculationBlock, calls: batches, items: rows },
          { type: HashedCollectBlock, calls: 1, items: rowsAfterCollect },
          { type: SortBlock, calls: batchesAfterCollect, items: rowsAfterCollect },
          { type: ReturnBlock, calls: batchesAfterCollect, items: rowsAfterCollect },
        ];
      };
      profHelper.runDefaultChecks({query, genNodeList});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test HashedCollectBlock
    ////////////////////////////////////////////////////////////////////////////////

    testHashedCollectBlock3 : function () {
      // example:
      // for @rows = 5,  x is [1,2,0,1,2]
      // for @rows = 12, x is [1,2,3,4,5,0,1,2,3,4,5,0]
      const query = 'FOR i IN 1..@rows COLLECT x = i % CEIL(@rows / 2) RETURN x';
      const genNodeList = (rows, batches) => {
        const rowsAfterCollect = Math.ceil(rows / 2);
        const batchesAfterCollect = Math.ceil(rowsAfterCollect / defaultBatchSize);

        return [
          { type: SingletonBlock, calls: 1, items: 1 },
          { type: CalculationBlock, calls: 1, items: 1 },
          { type: EnumerateListBlock, calls: batches, items: rows },
          { type: CalculationBlock, calls: batches, items: rows },
          { type: HashedCollectBlock, calls: 1, items: rowsAfterCollect },
          { type: SortBlock, calls: batchesAfterCollect, items: rowsAfterCollect },
          { type: ReturnBlock, calls: batchesAfterCollect, items: rowsAfterCollect },
        ];
      };
      profHelper.runDefaultChecks({query, genNodeList});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test LimitBlock
    ////////////////////////////////////////////////////////////////////////////////

    testLimitBlock1: function() {
      const query = 'FOR i IN 1..@rows LIMIT 0, @rows RETURN i';
      const genNodeList = (rows, batches) => [
        {type: SingletonBlock, calls: 1, items: 1},
        {type: CalculationBlock, calls: 1, items: 1},
        {type: EnumerateListBlock, calls: batches, items: rows},
        {type: LimitBlock, calls: batches, items: rows},
        {type: ReturnBlock, calls: batches, items: rows},
      ];
      profHelper.runDefaultChecks({query, genNodeList});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test LimitBlock
    ////////////////////////////////////////////////////////////////////////////////

    testLimitBlock2: function() {
      const query = 'FOR i IN 1..@rows LIMIT @limit RETURN i';
      const limit = rows => Math.ceil(3*rows/4);
      const restBatches = rows => Math.max(1, Math.ceil(limit(rows) / defaultBatchSize));

      const genNodeList = (rows) => [
        {type: SingletonBlock, calls: 1, items: 1},
        {type: CalculationBlock, calls: 1, items: 1},
        {type: EnumerateListBlock, calls: restBatches(rows), items: limit(rows)},
        {type: LimitBlock, calls: restBatches(rows), items: limit(rows)},
        {type: ReturnBlock, calls: restBatches(rows), items: limit(rows)},
      ];
      const bind = (rows) => ({
        rows,
        limit: limit(rows)
      });
      profHelper.runDefaultChecks({query, genNodeList, bind});
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief test LimitBlock
    ////////////////////////////////////////////////////////////////////////////////

    testLimitBlock3: function() {
      const query = 'FOR i IN 1..@rows LIMIT @skip, @count RETURN i';
      const skip = rows => Math.floor(rows/4);
      const count = rows => Math.ceil(3*rows/4);
      const countBatches = rows => Math.ceil(count(rows) / defaultBatchSize);

      const genNodeList = (rows) => [
        {type: SingletonBlock, calls: 1, items: 1},
        {type: CalculationBlock, calls: 1, items: 1},
        {type: EnumerateListBlock, calls: countBatches(rows), items: count(rows)},
        {type: LimitBlock, calls: countBatches(rows), items: count(rows)},
        {type: ReturnBlock, calls: countBatches(rows), items: count(rows)},
      ];
      const bind = (rows) => ({
        rows,
        skip: skip(rows),
        count: count(rows),
      });
      profHelper.runDefaultChecks({query, genNodeList, bind});
    },


// TODO Every block must be tested separately. Here follows the list of blocks
// (partly grouped due to the inheritance hierarchy). Intermediate blocks
// like ModificationBlock and BlockWithClients are never instantiated separately
// and therefore don't need to be tested on their own.

// *CalculationBlock
// *CountCollectBlock
// *DistinctCollectBlock
// *EnumerateCollectionBlock
// *EnumerateListBlock
// *FilterBlock
// *HashedCollectBlock
// *IndexBlock
// *LimitBlock
// NoResultsBlock
// RemoteBlock
// ReturnBlock
// ShortestPathBlock
// SingletonBlock
// SortBlock
// SortedCollectBlock
// SortingGatherBlock
// SubqueryBlock
// TraversalBlock
// UnsortingGatherBlock
//
// ModificationBlock
// -> RemoveBlock
// -> InsertBlock
// -> UpdateBlock
// -> ReplaceBlock
// -> UpsertBlock
//
// BlockWithClients
// -> ScatterBlock
// -> DistributeBlock
//
// IResearchViewBlockBase
// -> IResearchViewUnorderedBlock
//    -> IResearchViewBlock
// -> IResearchViewOrderedBlock

  };
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

jsunity.run(ahuacatlProfilerTestSuite);

return jsunity.done();

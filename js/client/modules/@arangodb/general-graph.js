'use strict';

// //////////////////////////////////////////////////////////////////////////////
// / @brief Replication management
// /
// / @file
// /
// / DISCLAIMER
// /
// / Copyright 2012 triagens GmbH, Cologne, Germany
// /
// / Licensed under the Apache License, Version 2.0 (the "License")
// / you may not use this file except in compliance with the License.
// / You may obtain a copy of the License at
// /
// /     http://www.apache.org/licenses/LICENSE-2.0
// /
// / Unless required by applicable law or agreed to in writing, software
// / distributed under the License is distributed on an "AS IS" BASIS,
// / WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// / See the License for the specific language governing permissions and
// / limitations under the License.
// /
// / Copyright holder is triAGENS GmbH, Cologne, Germany
// /
// / @author Jan Steemann
// / @author Copyright 2013, triAGENS GmbH, Cologne, Germany
// //////////////////////////////////////////////////////////////////////////////

const internal = require('internal');
const db = internal.db;
const arangosh = require('@arangodb/arangosh');
const ggc = require('@arangodb/general-graph-common');

const GRAPH_PREFIX = '_api/gharial/';

// remove me later
exports._listObjects = ggc._listObjects;
exports._list = ggc._list;
exports._exists = ggc._exists;

// inherited graph class
let CommonGraph = ggc.__GraphClass;

CommonGraph.prototype.__updateDefinitions = function (edgeDefs, orphans) {
  this.__edgeDefinitions = edgeDefs;
  this.__orphanCollections = orphans;
};

CommonGraph.prototype._extendEdgeDefinitions = function (edgeDefinition) {
  var data = {};
  if (edgeDefinition) {
    data = edgeDefinition;
  }
  var uri = GRAPH_PREFIX + encodeURIComponent(this.__name) + "/edge";
  var requestResult = db._connection.POST(uri, JSON.stringify(data));
  try {
    this.__updateDefinitions(requestResult.graph.edgeDefinitions, requestResult.graph.orphanCollections);
  } catch (ignore) {
  }

  return arangosh.checkRequestResult(requestResult);
};

CommonGraph.prototype._editEdgeDefinitions = function (edgeDefinition) {
  var data = {};
  if (edgeDefinition) {
    data = edgeDefinition;
  }
  var uri = GRAPH_PREFIX + encodeURIComponent(this.__name) + "/edge/" + edgeDefinition.collection;
  var requestResult = db._connection.PUT(uri, JSON.stringify(data));
  try {
    this.__updateDefinitions(requestResult.graph.edgeDefinitions, requestResult.graph.orphanCollections);
  } catch (ignore) {
  }
};

CommonGraph.prototype._addVertexCollection = function (name, createCollection) {
  var data = {};
  if (name) {
    data.collection = name;
  }
  var uri = GRAPH_PREFIX + encodeURIComponent(this.__name) + "/vertex";
  if (createCollection) {
    uri += "?createCollection=true";
  } else {
    uri += "?createCollection=false";
  }
  var requestResult = db._connection.POST(uri, JSON.stringify(data));

  this.__updateDefinitions(requestResult.graph.edgeDefinitions, requestResult.graph.orphanCollections);
};

/*
CommonGraph.prototype._addVertexCollection = function (name) {
  var data = {};
  if (name) {
    data.collection = name;
  }
  var uri = GRAPH_PREFIX + encodeURIComponent(this.__name) + "/vertex";
  print(data);
  print(uri);
  var requestResult = db._connection.DELETE(uri, JSON.stringify(data));
  return arangosh.checkRequestResult(requestResult);
};*/

/*
exports._exists = function (graphName) {
  var uri = GRAPH_PREFIX + encodeURIComponent(graphName);
  var requestResult = db._connection.GET(uri);
  return arangosh.checkRequestResult(requestResult).graph;
};*/

exports._graph = function (graphName) {
  var uri = GRAPH_PREFIX + encodeURIComponent(graphName);
  var requestResult = db._connection.GET(uri);
  return new CommonGraph(arangosh.checkRequestResult(requestResult).graph);
};

exports._create = function (name, edgeDefinitions, orphans, options) {
  var data = {};
  if (name) {
    data.name = name;
  }
  if (edgeDefinitions) {
    data.edgeDefinitions = edgeDefinitions;
  }
  if (orphans) {
    data.orphans = orphans;
  }
  if (options) {
    data.options = options;
  }

  var uri = GRAPH_PREFIX;
  var requestResult = db._connection.POST(uri, JSON.stringify(data));
  return new CommonGraph(arangosh.checkRequestResult(requestResult).graph);
};

exports._drop = function (graphName, dropCollections) {

  var uri = GRAPH_PREFIX + encodeURIComponent(graphName);
  if (dropCollections) {
    uri += "?dropCollections=true";
  }
  var requestResult = db._connection.DELETE(uri);
  return arangosh.checkRequestResult(requestResult).result;
};

// js based helper functions
exports.__GraphClass = CommonGraph;
exports._edgeDefinitions = ggc._edgeDefinitions;
exports._extendEdgeDefinitions = ggc._extendEdgeDefinitions;
exports._relation = ggc._relation;
exports._registerCompatibilityFunctions = ggc._registerCompatibilityFunctions;

/**  
 * Copyright (c) 2009 Carnegie Mellon University. 
 *     All rights reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an "AS
 *  IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied.  See the License for the specific language
 *  governing permissions and limitations under the License.
 *
 * For more about this software visit:
 *
 *      http://www.graphlab.ml.cmu.edu
 *
 */

/**
 * Also contains code that is Copyright 2011 Yahoo! Inc.  All rights
 * reserved.  
 *
 * Contributed under the iCLA for:
 *    Joseph Gonzalez (jegonzal@yahoo-inc.com) 
 *
 */



#ifndef GRAPHLAB_SCOPE_MANAGER_HPP
#define GRAPHLAB_SCOPE_MANAGER_HPP

#include <vector>

#include <graphlab/scope/iscope.hpp>
#include <graphlab/scope/general_scope.hpp>
#include <graphlab/parallel/pthread_tools.hpp>
#include <graphlab/graph/graph.hpp>

#include <graphlab/scope/idiffable.hpp>





#include <graphlab/macros_def.hpp>
namespace graphlab {


  template<typename Graph>
  class scope_manager {

  public:

    typedef Graph graph_type;
    typedef iscope<Graph>                   iscope_type;
    typedef typename Graph::vertex_id_type  vertex_id_type;
    typedef typename Graph::edge_id_type    edge_id_type;
    typedef typename Graph::edge_list_type  edge_list_type;
    typedef typename Graph::vertex_data_type vertex_data_type;
    typedef general_scope<Graph>            general_scope_type;
    typedef typename general_scope_type::cache_map_type cache_map_type;
    typedef typename general_scope_type::cache_entry cache_entry_type;
    typedef boost::is_base_of< idiffable<vertex_data_type>, 
                               vertex_data_type> is_diffable;

  private:
    Graph& graph;
    std::vector<general_scope_type> scopes;
    
    std::vector<rwlock> locks;
    consistency_model::model_enum default_scope;

  public:

    scope_manager(Graph& graph,
                  size_t ncpus,
                  consistency_model::model_enum default_scope_range 
                  = consistency_model::EDGE_CONSISTENCY) :
      graph(graph), scopes(ncpus), locks(graph.num_vertices()),
      default_scope(default_scope_range) {
      if (default_scope == consistency_model::USE_DEFAULT)
        default_scope = consistency_model::EDGE_CONSISTENCY;
    } // end of scope manager


    void set_default_scope(consistency_model::model_enum default_scope_range) {
      default_scope = default_scope_range;
      if (default_scope == consistency_model::USE_DEFAULT) 
        default_scope = consistency_model::EDGE_CONSISTENCY;
    }


    // -----------------ACQUIRE SCOPE-----------------------------
    general_scope_type& get_scope(size_t cpuid,
                                  vertex_id_type v,
                                  consistency_model::model_enum scope = 
                                  consistency_model::USE_DEFAULT) {
      // Verify that the cpuid and vertex id are valid
      ASSERT_LT(cpuid, scopes.size());
      ASSERT_LT(v, locks.size());

      if (scope == consistency_model::USE_DEFAULT) 
        scope = default_scope;      
      switch(scope){
      case consistency_model::VERTEX_CONSISTENCY:
        return get_vertex_scope(cpuid, v);
      case consistency_model::VERTEX_READ_CONSISTENCY:
        return get_vertex_read_scope(cpuid, v);
      case consistency_model::READ_CONSISTENCY:
        return get_read_scope(cpuid, v);
      case consistency_model::EDGE_CONSISTENCY:
        return get_edge_scope(cpuid, v);
      case consistency_model::FULL_CONSISTENCY:
        return get_full_scope(cpuid, v);
      case consistency_model::NULL_CONSISTENCY:
        return get_null_scope(cpuid, v);
      default:
        logstream(LOG_FATAL) << "UNREACHABLE STATE!" << std::endl;
        return get_edge_scope(cpuid,v);
      }
    } // end of get scope
    


    void flush_cache(size_t cpuid) {  flush_cache(cpuid, is_diffable()); }

    //! Nothing to flush if it is not diffable
    void flush_cache(size_t cpuid, const boost::false_type&) { 
      // Check that all the caches are empty (they must be)
      foreach(const general_scope_type& scope, scopes)
        ASSERT_EQ(scope.cache.size(), 0);
    } // end of flush_cache

    void flush_cache(size_t cpuid, const boost::true_type&) {
      cache_map_type& cache = scopes[cpuid].cache;
      typedef typename cache_map_type::value_type cache_pair_type;
      foreach(const cache_pair_type& pair, cache) {
        const vertex_id_type vid = pair.first;
        const cache_entry_type& entry = pair.second;
        locks[vid].writelock();
        graph.vertex_data(vid).apply_diff(entry.current, entry.old);
        locks[vid].unlock();        
      }
      // Empty the cache
      cache.clear();
    } // end of flush cache


    void acquire_writelock(const size_t cpuid, const vertex_id_type vid, 
                           const bool is_center = false) {
      acquire_writelock(cpuid, vid, is_center, is_diffable());
    }

    void acquire_writelock(const size_t cpuid, const vertex_id_type vid,
                           const bool is_center,
                           const boost::false_type&) {
      locks[vid].writelock();
    }

    void acquire_writelock(const size_t cpuid, const vertex_id_type vid,
                           const bool is_center,
                           const boost::true_type&) {
      const size_t MAX_WRITES = 100;
      // First check the cache
      general_scope_type& scope = scopes[cpuid];
      typedef typename cache_map_type::iterator iterator_type;
      iterator_type iter = scope.cache.find(vid);
      const bool is_cached = iter != scope.cache.end();
      if(is_cached) {
        cache_entry_type& cache_entry = iter->second;        
        if(++cache_entry.writes > MAX_WRITES || is_center) {
          locks[vid].writelock();
          vertex_data_type& vdata = graph.vertex_data(vid);
          vdata.apply_diff(cache_entry.current, cache_entry.old);
          if(is_center) { // if it is the center vertex we evict it
                          // from the cache and retain the write lock.
            scope.cache.erase(vid);
            return;
          } else {
            cache_entry.current = vdata;
            locks[vid].unlock();
            cache_entry.old = cache_entry.current;
            cache_entry.writes = cache_entry.reads = 0;
          }
        } 
      } else {       
        locks[vid].readlock();
        // create a cache entry
        cache_entry_type& cache_entry = scope.cache[vid];
        cache_entry.current = graph.vertex_data(vid);
        locks[vid].unlock();
        cache_entry.old = cache_entry.current;
      }
    } // end of acquire write lock



    void acquire_readlock(const size_t cpuid, const vertex_id_type vid) {      
      acquire_readlock(cpuid, vid, is_diffable());
    }

    void acquire_readlock(const size_t cpuid, const vertex_id_type vid,
                          const boost::false_type&) {
      locks[vid].readlock();
    }

    void acquire_readlock(const size_t cpuid, const vertex_id_type vid,
                          const boost::true_type&) {
      const size_t MAX_READS = 100;
      // First check the cache
      general_scope_type& scope = scopes[cpuid];
      typedef typename cache_map_type::iterator iterator_type;
      iterator_type iter = scope.cache.find(vid);
      const bool is_cached = iter != scope.cache.end();
      if(is_cached) {
        cache_entry_type& cache_entry = iter->second;        
        if(++cache_entry.reads > MAX_READS) {        
          locks[vid].readlock();
          const vertex_data_type& vdata = graph.vertex_data(vid);
          cache_entry.current.apply_diff(vdata, cache_entry.old);
          cache_entry.old = vdata;
          locks[vid].unlock(); 
        }
      } else {
        // Try to get the write lock
        locks[vid].readlock();
        // create a cache entry
        cache_entry_type& cache_entry = scope.cache[vid];
        cache_entry.current = graph.vertex_data(vid);
        locks[vid].unlock();
        cache_entry.old = cache_entry.current;        
      }
    } // end of acquire readlock






    
    void release_lock(size_t cpuid, vertex_id_type vid) {
      release_lock(cpuid, vid, is_diffable());
    }

    void release_lock(size_t cpuid, vertex_id_type vid,
                      const boost::false_type&) {
      locks[vid].unlock();
    }

    void release_lock(size_t cpuid, vertex_id_type vid,
                      const boost::true_type&) {
      // First check the cache
      general_scope_type& scope = scopes[cpuid];
      typedef typename general_scope_type::cache_entry cache_entry_type;
      typedef typename general_scope_type::cache_map_type::iterator
        iterator_type;
      iterator_type iter = scope.cache.find(vid);
      const bool is_cached = iter != scope.cache.end();

      if(is_cached) return;
      else locks[vid].unlock();
    }







    
    general_scope_type& get_full_scope(size_t cpuid, vertex_id_type v) {
      // grab the scope
      general_scope_type& scope(scopes[cpuid]);
      
      scope.init(&graph, v, consistency_model::FULL_CONSISTENCY);

      const edge_list_type inedges =  graph.in_edge_ids(v);
      const edge_list_type outedges = graph.out_edge_ids(v);

      size_t inidx = 0;
      size_t outidx = 0;

      bool curlocked = false;
      const vertex_id_type numv = 
        vertex_id_type(graph.num_vertices());
      vertex_id_type curv = scope.vertex();
      vertex_id_type inv  = 
        (inedges.size() > 0) ? graph.source(inedges[0]) : numv;
      vertex_id_type outv  = 
        (outedges.size() > 0) ? graph.target(outedges[0]) : numv;

      // iterate both in order and lock
      // include the current vertex in the iteration
      while (inidx < inedges.size() || outidx < outedges.size()) {
        if (!curlocked && curv < inv  && curv < outv) {
          acquire_writelock(cpuid, curv, true); // locks[curv].writelock();
          curlocked = true;
          curv = numv;
        } else if (inv < outv) {
          acquire_writelock(cpuid, inv); // locks[inv].writelock(); 
          ++inidx;
          inv = (inedges.size() > inidx) ? 
            graph.source(inedges[inidx]) : numv;
        } else if (outv < inv) {
          acquire_writelock(cpuid, outv); // locks[outv].writelock(); 
          ++outidx;
          outv = (outedges.size() > outidx) ? 
            graph.target(outedges[outidx]) : numv;
        } else if (inv == outv) {
          acquire_writelock(cpuid, inv); // locks[inv].writelock();
          ++inidx; ++outidx;
          inv = (inedges.size() > inidx) ? 
            graph.source(inedges[inidx]) : numv;
          outv = (outedges.size() > outidx) ? 
            graph.target(outedges[outidx]) : numv;
        }
      }
      // just in case we never got around to locking it
      if (!curlocked) {
        acquire_writelock(cpuid, curv, true); // locks[curv].writelock();
      }
      return scope;
    } // end of get_full_scope




    general_scope_type& get_edge_scope(size_t cpuid, vertex_id_type v) {
      general_scope_type& scope = scopes[cpuid];
      
      scope.init(&graph, v, consistency_model::EDGE_CONSISTENCY);

      const edge_list_type inedges =  graph.in_edge_ids(v);
      const edge_list_type outedges = graph.out_edge_ids(v);

      size_t inidx = 0;
      size_t outidx = 0;

      bool curlocked = false;
      const vertex_id_type numv = vertex_id_type(graph.num_vertices());
      vertex_id_type curv = scope.vertex();
      vertex_id_type inv  = 
        (inedges.size() > 0) ? graph.source(inedges[0]) : numv;
      vertex_id_type outv  = 
        (outedges.size() > 0) ? graph.target(outedges[0]) : numv;
      // iterate both in order and lock
      // include the current vertex in the iteration
      while (inidx < inedges.size() || outidx < outedges.size()) {
        if (!curlocked && curv < inv  && curv < outv) {
          acquire_writelock(cpuid, curv, true); // locks[curv].writelock();
          curlocked = true;
          curv = numv;
        } else if (inv < outv) {
          acquire_readlock(cpuid, inv); // locks[inv].readlock(); 
          ++inidx;
          inv = (inedges.size() > inidx) ? 
            graph.source(inedges[inidx]) : numv;
        } else if (outv < inv) {
          acquire_readlock(cpuid, outv); // locks[outv].readlock(); 
          ++outidx;
          outv = (outedges.size() > outidx) ? 
            graph.target(outedges[outidx]) : numv;
        } else if (inv == outv){
          acquire_readlock(cpuid, inv); // locks[inv].readlock();
          ++inidx; ++outidx;
          inv = (inedges.size() > inidx) ? 
            graph.source(inedges[inidx]) : numv;
          outv = (outedges.size() > outidx) ? 
            graph.target(outedges[outidx]) : numv;
        }
      }
      // just in case we never got around to locking it
      if (!curlocked) {
        acquire_writelock(cpuid, curv, true); // locks[curv].writelock();
      }
      return scope;
    } // end of get edge scope

    

    general_scope_type& get_single_edge_scope(size_t cpuid,
                                       vertex_id_type center_vid,
                                       edge_id_type eid,
                                       bool writable) {
      general_scope_type& scope = scopes[cpuid];
      const consistency_model::model_enum consistency = writable? 
        consistency_model::SINGLE_EDGE_WRITE_CONSISTENCY :
        consistency_model::SINGLE_EDGE_READ_CONSISTENCY;
      scope.init(&graph, center_vid, consistency);
      scope.edge_id() = eid;
      vertex_id_type source = graph.source(eid);
      vertex_id_type target = graph.target(eid);
      ASSERT_NE(source, target);
      ASSERT_TRUE(source == center_vid || target == center_vid);
      if(source < target) std::swap(source, target);
      if(source != center_vid && writable) {
        acquire_writelock(cpuid, source); // locks[source].writelock(); 
      } else { 
        acquire_readlock(cpuid, source); // locks[source].readlock(); 
      }
      if(target != center_vid && writable) { 
        acquire_writelock(cpuid, target); // locks[target].writelock();
      } else { 
        acquire_readlock(cpuid, target); // locks[target].readlock(); 
      }
      return scope;
    } // end of get single edge scope


    general_scope_type& get_vertex_scope(size_t cpuid, vertex_id_type v) {
      general_scope_type& scope = scopes[cpuid];      
      scope.init(&graph, v, consistency_model::VERTEX_CONSISTENCY);
      const vertex_id_type curv = scope.vertex();
      acquire_writelock(cpuid, curv, true); // locks[curv].writelock();     
      return scope;
    }

    general_scope_type& get_vertex_read_scope(size_t cpuid, vertex_id_type v) {
      general_scope_type& scope = scopes[cpuid];      
      scope.init(&graph, v, consistency_model::READ_CONSISTENCY);
      const vertex_id_type curv = scope.vertex();
      acquire_readlock(cpuid, curv); // locks[curv].readlock();      
      return scope;
    }

    general_scope_type& get_read_scope(size_t cpuid, vertex_id_type v) {
      general_scope_type& scope = scopes[cpuid];
      
      scope.init(&graph, v, consistency_model::READ_CONSISTENCY);

      const edge_list_type inedges =  graph.in_edge_ids(v);
      const edge_list_type outedges = graph.out_edge_ids(v);

      size_t inidx = 0;
      size_t outidx = 0;

      bool curlocked = false;
      const vertex_id_type numv = (vertex_id_type)(graph.num_vertices());
      vertex_id_type curv = scope.vertex();
      vertex_id_type inv  = 
        (inedges.size() > 0) ? graph.source(inedges[0]) : numv;
      vertex_id_type outv  = 
        (outedges.size() > 0) ? graph.target(outedges[0]) : numv;
      // iterate both in order and lock
      // include the current vertex in the iteration
      while (inidx < inedges.size() || outidx < outedges.size()) {
        if (!curlocked && curv < inv  && curv < outv) {
          acquire_readlock(cpuid, curv); // locks[curv].readlock();
          curlocked = true;
          curv = numv;
        } else if (inv < outv) {
          acquire_readlock(cpuid, inv); // locks[inv].readlock(); 
          ++inidx;
          inv = (inedges.size() > inidx) ? 
            graph.source(inedges[inidx]) : numv;
        } else if (outv < inv) {
          acquire_readlock(cpuid, outv); // locks[outv].readlock(); 
          ++outidx;
          outv= (outedges.size() > outidx) ? 
            graph.target(outedges[outidx]) : numv;
        } else if (inv == outv) {
          acquire_readlock(cpuid, inv); // locks[inv].readlock();
          ++inidx; ++outidx;
          inv = (inedges.size() > inidx) ? 
            graph.source(inedges[inidx]) : numv;
          outv = (outedges.size() > outidx) ? 
            graph.target(outedges[outidx]) : numv;
        }
      }
      // just in case we never got around to locking it
      if (!curlocked) {
        acquire_readlock(cpuid, curv); // locks[curv].readlock();
      }
      return scope;
    }

    
    general_scope_type& get_null_scope(size_t cpuid, vertex_id_type v) {
      general_scope_type& scope = scopes[cpuid];      
      scope.init(&graph, v, consistency_model::NULL_CONSISTENCY);
      return scope;
    }



    // -----------------RELEASE SCOPE-----------------------------
    void release_scope(size_t cpuid, general_scope_type& scope) {
      switch(scope.consistency()) {
      case consistency_model::VERTEX_CONSISTENCY:
      case consistency_model::VERTEX_READ_CONSISTENCY:
        release_vertex_scope(cpuid, scope);
        break;
      case consistency_model::EDGE_CONSISTENCY:
      case consistency_model::FULL_CONSISTENCY:
      case consistency_model::READ_CONSISTENCY:
        release_full_edge_scope(cpuid, scope);
        break;
      case consistency_model::NULL_CONSISTENCY:
        release_null_scope(cpuid, scope);
        break;
      case consistency_model::SINGLE_EDGE_READ_CONSISTENCY:
      case consistency_model::SINGLE_EDGE_WRITE_CONSISTENCY:
        release_single_edge_scope(cpuid, scope);
        break;
      default:
        ASSERT_TRUE(false);
      }

      // \todo: shrink the cache as needed
    } // end of release scope



    void release_single_edge_scope(size_t cpuid, 
                                   general_scope_type& scope) {
      const edge_id_type eid = scope.edge_id();
      vertex_id_type source = graph.source(eid);
      vertex_id_type target = graph.target(eid);
      ASSERT_NE(source, target);
      if(source > target) std::swap(source, target);
      release_lock(cpuid, source);
      release_lock(cpuid, target);
    } // end of release single edge scope


    void release_full_edge_scope(size_t cpuid,
                                 general_scope_type& scope) {
      const vertex_id_type v = scope.vertex();
      const edge_list_type inedges =  graph.in_edge_ids(v);
      const edge_list_type outedges = graph.out_edge_ids(v);
      size_t inidx = inedges.size() - 1;
      size_t outidx = outedges.size() - 1;

      bool curvunlocked = false;

      vertex_id_type curv = scope.vertex();
      vertex_id_type inv  = (inedges.size() > inidx) ?
        graph.source(inedges[inidx]) : vertex_id_type(-1);
      vertex_id_type outv  = (outedges.size() > outidx) ?
        graph.target(outedges[outidx]) : vertex_id_type(-1);
      // iterate both in order and lock
      // include the current vertex in the iteration
      while (inidx < inedges.size() || outidx < outedges.size()) {
        if (!curvunlocked && (curv > inv || inv == vertex_id_type(-1)) &&
            (curv > outv || outv == vertex_id_type(-1))) {
          release_lock(cpuid, curv); // locks[curv].unlock();
          curvunlocked = true;
        } else if ((inv+1) > (outv+1)) {
          release_lock(cpuid, inv); // locks[inv].unlock(); 
          --inidx;
          inv  = (inedges.size() > inidx) ?
            graph.source(inedges[inidx]) : vertex_id_type(-1);
        } else if ((outv+1) > (inv+1)) {
          release_lock(cpuid, outv); // locks[outv].unlock(); 
          --outidx;
          outv  = (outedges.size() > outidx) ?
            graph.target(outedges[outidx]) : vertex_id_type(-1);
        } else if (inv == outv){
          release_lock(cpuid, inv); // locks[inv].unlock();
          --inidx; --outidx;
          inv  = (inedges.size() > inidx) ?
            graph.source(inedges[inidx]) : vertex_id_type(-1);
          outv  = (outedges.size() > outidx) ?
            graph.target(outedges[outidx]) : vertex_id_type(-1);
        }
      }

      if (!curvunlocked) {
        release_lock(cpuid, curv); // locks[curv].unlock();
      }
    }

    void release_vertex_scope(size_t cpuid, 
                              general_scope_type& scope) {
      vertex_id_type curv = scope.vertex();
      release_lock(cpuid, curv); // locks[curv].unlock();
    }

    void release_null_scope(size_t cpuid, 
                            general_scope_type& scope) { }

    size_t num_vertices() const { return graph.num_vertices(); }

    Graph& get_graph() { return graph; }
    
  };
}; // end of graphlab namespace
#include <graphlab/macros_undef.hpp>



#endif

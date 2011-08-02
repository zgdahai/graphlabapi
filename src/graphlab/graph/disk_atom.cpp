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



#include <sstream>
#include <map>
#include <graphlab/serialization/serialization_includes.hpp>
#include <graphlab/graph/graph.hpp>
#include <graphlab/parallel/pthread_tools.hpp>
#include <graphlab/logger/logger.hpp>
#include <graphlab/graph/disk_atom.hpp>
#include <kchashdb.h>


namespace graphlab {

  disk_atom::disk_atom(std::string filename, 
                       uint16_t atomid):atomid(atomid),filename(filename) {
    db.tune_options(storage_type::TLINEAR);
    db.tune_buckets(1000);
    db.tune_page(32768);
#if __LP64__
    db.tune_map(256 * 1024 * 1024); // 256MB
#endif
    cache_invalid = true;
    ASSERT_TRUE(db.open(filename));
    // get the pointers to the linked list of vertices
    if (db.get("head_vid", 7, (char*)&head_vid, sizeof(head_vid)) == -1) head_vid = (uint64_t)(-1);
    if (db.get("tail_vid", 7, (char*)&tail_vid, sizeof(tail_vid)) == -1) tail_vid = (uint64_t)(-1);
    if (db.get("numv", 4, (char*)&numv.value, sizeof(numv.value)) == -1) numv = 0;
    if (db.get("nume", 4, (char*)&nume.value, sizeof(nume.value)) == -1) nume = 0;
    if (db.get("numlocalv", 9, (char*)&numlocalv.value, sizeof(numlocalv.value)) == -1) numlocalv = 0;
    if (db.get("numlocale", 9, (char*)&numlocale.value, sizeof(numlocale.value)) == -1) numlocale = 0;
  }

  void disk_atom::precache() {
    storage_type::Cursor* cur = db.cursor();
    cur->jump();
    std::string key, val;
    while (cur->get(&key, &val, true)) {
      cache.set(key, val);
    }
    delete cur;
    cache_invalid = false;
  }

  disk_atom::~disk_atom() {
    synchronize();
    db.close();
  }

  void disk_atom::synchronize() {
    // update the linked list
    db.set("head_vid", 7, (char*)&head_vid, sizeof(head_vid));
    db.set("tail_vid", 7, (char*)&tail_vid, sizeof(tail_vid));
    db.set("numv", 4, (char*)&numv.value, sizeof(numv.value));
    db.set("nume", 4, (char*)&nume.value, sizeof(nume.value));
    db.set("numlocalv", 9, (char*)&numlocalv.value, sizeof(numlocalv.value));
    db.set("numlocale", 9, (char*)&numlocale.value, sizeof(numlocale.value));
    db.synchronize();
  }

  void disk_atom::add_vertex(disk_atom::vertex_id_type vid, uint16_t owner) {
    if (!add_vertex_skip(vid, owner)) {
      std::stringstream strm;
      oarchive oarc(strm);    
      oarc << owner;
      strm.flush();
      db.set("v"+id_to_str(vid), strm.str());
      cache_invalid = true;
    }
  }


  bool disk_atom::add_vertex_skip(disk_atom::vertex_id_type vid, uint16_t owner) {
    std::stringstream strm;
    oarchive oarc(strm);    
    oarc << owner;
    strm.flush();
    if (db.add("v"+id_to_str(vid), strm.str())) {
      // first entry in linked list
      mut.lock();
      if (head_vid == (uint64_t)(-1)) {
        head_vid = vid;
        tail_vid = vid;
        mut.unlock();
      }
      else {
        // update linked list
        std::string tail_next_key = "ll" + id_to_str(tail_vid);
        tail_vid = vid;
        mut.unlock();
        db.set(tail_next_key.c_str(), tail_next_key.length(), 
               (char*)&vid, sizeof(vid));
        cache_invalid = true;
      }
      numv.inc();
      if (owner == atomid) numlocalv.inc();
      return true;
    }
    return false;
  }


  void disk_atom::add_edge(disk_atom::vertex_id_type src, disk_atom::vertex_id_type target) {
    if (!add_edge_skip(src, target)) {
      db.set("e"+id_to_str(src)+"_"+id_to_str(target), std::string(""));
      cache_invalid = true;
    }
  }


  bool disk_atom::add_edge_skip(disk_atom::vertex_id_type src, disk_atom::vertex_id_type target) {
    if (db.add("e"+id_to_str(src)+"_"+id_to_str(target), std::string(""))) {
      // increment the number of edges
      nume.inc();
      // append to the adjacency entries
      std::string oadj_key = "o"+id_to_str(src);
      uint64_t target64 = (uint64_t)target;
      db.append(oadj_key.c_str(), oadj_key.length(), (char*)&target64, sizeof(target64));
    
      std::string iadj_key = "i"+id_to_str(target);
      uint64_t src64 = (uint64_t)src;
      db.append(iadj_key.c_str(), iadj_key.length(), (char*)&src64, sizeof(src64));
      return true;
    }
    return false;
  }

  std::vector<disk_atom::vertex_id_type> disk_atom::enumerate_vertices() {
    std::vector<disk_atom::vertex_id_type> ret;
    if (head_vid == (uint64_t)(-1)) return ret;
    else {
      uint64_t curvid = head_vid;
      while(1) {
        ret.push_back((disk_atom::vertex_id_type)(curvid));
        std::string next_key = "ll" + id_to_str(curvid);
        if (cache_invalid || 
            cache.get(next_key.c_str(), next_key.length(), (char*)&curvid, sizeof(curvid)) == -1) {
          if (db.get(next_key.c_str(), next_key.length(), (char*)&curvid, sizeof(curvid)) == -1) {
            break;
          }
        }
      }
    }
    return ret;
  }


  bool disk_atom::get_vertex(disk_atom::vertex_id_type vid, uint16_t &owner) {
    std::string val;
    std::string key = "v"+id_to_str(vid);
    if (cache_invalid || cache.get(key, &val) == false) {
      if (db.get("v"+id_to_str(vid), &val) == false) return false;
    }
  
    boost::iostreams::stream<boost::iostreams::array_source> 
      istrm(val.c_str(), val.length());   
    iarchive iarc(istrm);
    iarc >> owner;
    return true;
  }


  std::map<uint16_t, uint32_t> disk_atom::enumerate_adjacent_atoms() {
    std::map<uint16_t, uint32_t> ret;
    if (head_vid == (uint64_t)(-1)) return ret;
    else {
      uint64_t curvid = head_vid;
      while(1) {
        uint16_t owner;
        get_vertex((disk_atom::vertex_id_type)curvid, owner);
        if (owner != atomid) ret[owner]++;
        std::string next_key = "ll" + id_to_str(curvid);
        if (db.get(next_key.c_str(), next_key.length(), (char*)&curvid, sizeof(curvid)) == -1) {
          break;
        }
      }
    }
    return ret;
  }

  disk_atom::vertex_color_type disk_atom::max_color() {
    disk_atom::vertex_color_type mcolor = 0;
    if (head_vid == (uint64_t)(-1)) return mcolor;
    else {
      uint64_t curvid = head_vid;
      while(1) {

        disk_atom::vertex_color_type c = get_color(curvid);
        if (c != disk_atom::vertex_color_type(-1)) {
          mcolor = std::max(mcolor, c);
        }
        std::string next_key = "ll" + id_to_str(curvid);
        if (db.get(next_key.c_str(), next_key.length(), (char*)&curvid, sizeof(curvid)) == -1) {
          break;
        }
      }
    }
    return mcolor;
  }


  std::vector<disk_atom::vertex_id_type> disk_atom::get_in_vertices(disk_atom::vertex_id_type vid) {
    std::vector<disk_atom::vertex_id_type> ret;
    std::string val;
    std::string key = "i"+id_to_str(vid);
    if ((cache_invalid == false && cache.get(key, &val)) || db.get(key, &val)) {
      const uint64_t* v = reinterpret_cast<const uint64_t*>(val.c_str());
      ASSERT_TRUE(val.length() % 8 == 0);
      size_t numel = val.length() / 8;
      ret.resize(numel);
      for (size_t i = 0;i < numel; ++i) ret[i] = v[i];
    }
    return ret;    
  }
   
   

  std::vector<disk_atom::vertex_id_type> disk_atom::get_out_vertices(disk_atom::vertex_id_type vid) {
    std::vector<disk_atom::vertex_id_type> ret;
    std::string val;
    std::string key = "o"+id_to_str(vid);
    if ((cache_invalid == false && cache.get(key, &val)) || db.get(key, &val)) {
      const uint64_t* v = reinterpret_cast<const uint64_t*>(val.c_str());
      size_t numel = val.length() / 8;
      ASSERT_TRUE(val.length() % 8 == 0);
      ret.resize(numel);
      for (size_t i = 0;i < numel; ++i) ret[i] = v[i];
    }
    return ret;    
  }



  disk_atom::vertex_color_type 
  disk_atom::get_color(disk_atom::vertex_id_type vid) {
    std::string key = "c" + id_to_str(vid);
    disk_atom::vertex_color_type  ret;
    if (cache_invalid == false && 
        cache.get(key.c_str(), key.length(), 
                  (char*)&ret, sizeof(ret)) != -1) return ret;

    if (db.get(key.c_str(), key.length(), (char*)&ret, sizeof(ret)) == -1) 
      ret = disk_atom::vertex_color_type(-1);
    return ret;
  }


  void disk_atom::set_color(disk_atom::vertex_id_type vid, 
                            disk_atom::vertex_color_type color) {
    std::string key = "c" + id_to_str(vid);
    db.set(key.c_str(), key.length(), (char*)&color, sizeof(color));
    cache_invalid = true;
  }


  uint16_t disk_atom::get_owner(disk_atom::vertex_id_type vid) {
    std::string key = "h" + id_to_str(vid);
    uint16_t ret;
    if (cache_invalid == false && 
        cache.get(key.c_str(), key.length(), (char*)&ret, sizeof(ret)) != -1) return ret;
  
    if (db.get(key.c_str(), key.length(), (char*)&ret, sizeof(ret)) == -1) ret = (uint16_t)(-1);
    return ret;
  }


  void disk_atom::set_owner(disk_atom::vertex_id_type vid, uint16_t owner) {
    std::string key = "h" + id_to_str(vid);
    db.set(key.c_str(), key.length(), (char*)&owner, sizeof(owner));
    cache_invalid = true;
  }


  void disk_atom::clear() {
    head_vid = (uint64_t)(-1);   
    tail_vid = (uint64_t)(-1);
    numv.value = 0;
    nume.value = 0;
    numlocalv.value = 0;
    numlocale.value = 0;
    db.clear();
  }


} // namespace graphlab


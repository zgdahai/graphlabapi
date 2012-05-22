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


#ifndef GRAPHLAB_FIFO_SCHEDULER_HPP
#define GRAPHLAB_FIFO_SCHEDULER_HPP

#include <algorithm>
#include <queue>


#include <graphlab/parallel/pthread_tools.hpp>
#include <graphlab/parallel/atomic.hpp>

#include <graphlab/util/random.hpp>
#include <graphlab/scheduler/ischeduler.hpp>
#include <graphlab/scheduler/terminator/iterminator.hpp>
#include <graphlab/scheduler/vertex_map.hpp>

#include <graphlab/scheduler/terminator/critical_termination.hpp>
#include <graphlab/options/options_map.hpp>


#include <graphlab/macros_def.hpp>
namespace graphlab {

  /**
   * \ingroup group_schedulers 
   *
   * This class defines a multiple queue approximate fifo scheduler.
   * Each processor has its own in_queue which it puts new tasks in
   * and out_queue which it pulls tasks from.  Once a processors
   * in_queue gets too large, the entire queue is placed at the end of
   * the shared master queue.  Once a processors out queue is empty it
   * grabs the next out_queue from the master.
   */
  template<typename Graph, typename Message>
  class fifo_scheduler : public ischeduler<Graph, Message> {
  
  public:

    typedef ischeduler<Graph, Message> base;
    typedef typename base::graph_type graph_type;
    typedef typename base::vertex_id_type vertex_id_type;
    typedef typename base::message_type message_type;

    typedef std::deque<vertex_id_type> queue_type;

  private:

    vertex_map<message_type> messages;
    std::vector<queue_type> queues;
    std::vector<spinlock>   locks;
    size_t multi;
    std::vector<size_t>     current_queue;

    // Terminator
    critical_termination term;
 


  public:

    fifo_scheduler(const graph_type& graph, 
                   size_t ncpus,
                   const options_map& opts) :
      messages(graph.num_vertices()), multi(0),
      current_queue(ncpus), term(ncpus) {     
      opts.get_option("multi", multi);
      const size_t nqueues = std::max(multi*ncpus, size_t(1));
      if(multi > 0) {
        logstream(LOG_INFO) << "Using " << multi 
                            << " queues per thread." << std::endl;
      }
      queues.resize(nqueues);
      locks.resize(nqueues);
    }

    void start() { term.reset(); }
   


    void schedule(const vertex_id_type vid, 
                  const message_type& msg) {      
      if (messages.add(vid, msg)) {
        /* "Randomize" the task queue task is put in. Note that we do
           not care if this counter is corrupted in race conditions
           Find first queue that is not locked and put task there (or
           after iteration limit) Choose two random queues and use the
           one which has smaller size */
        // M.D. Mitzenmacher The Power of Two Choices in Randomized
        // Load Balancing (1991)
        // http://www.eecs.harvard.edu/~michaelm/postscripts/mythesis.
        size_t idx = 0;
        if(queues.size() > 1) {
          const uint32_t prod = 
            random::fast_uniform(uint32_t(0), 
                                 uint32_t(queues.size() * queues.size() - 1));
          const uint32_t r1 = prod / queues.size();
          const uint32_t r2 = prod % queues.size();
          idx = (queues[r1].size() < queues[r2].size()) ? r1 : r2;  
        }
        // if(multi == 0) 
        // else term.new_job(idx / multi);
        locks[idx].lock(); queues[idx].push_back(vid); locks[idx].unlock();
        term.new_job();
      }
    } // end of schedule

    void schedule_all(const message_type& msg,
                      const std::string& order) {
      if(order == "shuffle") {
        // add vertices randomly
        std::vector<vertex_id_type> permutation = 
          random::permutation<vertex_id_type>(messages.size());       
        foreach(vertex_id_type vid, permutation) {
          if(messages.add(vid,msg)) {
            const size_t idx = vid % queues.size();
            locks[idx].lock(); queues[idx].push_back(vid); locks[idx].unlock();
            term.new_job();
          }
        }
      } else {
        // Add vertices sequentially
        for (vertex_id_type vid = 0; vid < messages.size(); ++vid) {
          if(messages.add(vid,msg)) {
            term.new_job();
            const size_t idx = vid % queues.size();
            locks[idx].lock(); queues[idx].push_back(vid); locks[idx].unlock();
          }
        }
      }
    } // end of schedule_all

    void completed(const size_t cpuid,
                   const vertex_id_type vid,
                   const message_type& msg) { term.completed_job(); }


    /** Get the next element in the queue */
    sched_status::status_enum get_next(const size_t cpuid,
                                       vertex_id_type& ret_vid,
                                       message_type& ret_msg) {
      /* Check all of my queues for a task */
      for(size_t i = 0; i < multi; ++i) {
        const size_t idx = (++current_queue[cpuid] % multi) + cpuid * multi;
        locks[idx].lock();
        if(!queues[idx].empty()) {
          ret_vid = queues[idx].front();
          queues[idx].pop_front();
          locks[idx].unlock();
          const bool get_success = messages.test_and_get(ret_vid, ret_msg);
          ASSERT_TRUE(get_success);
          return sched_status::NEW_TASK;          
        }
        locks[idx].unlock();
      }
      /* Check all the queues */
      for(size_t i = 0; i < queues.size(); ++i) {
        const size_t idx = ++current_queue[cpuid] % queues.size();
        if(!queues[idx].empty()) { // quick pretest
          locks[idx].lock();
          if(!queues[idx].empty()) {
            ret_vid = queues[idx].front();
            queues[idx].pop_front();
            locks[idx].unlock();
            const bool get_success = messages.test_and_get(ret_vid, ret_msg);
            ASSERT_TRUE(get_success);
            return sched_status::NEW_TASK;          
          }
          locks[idx].unlock();
        }
      }
      return sched_status::EMPTY;     
    } // end of get_next_task

    iterminator& terminator() { return term; }

    size_t num_joins() const {
      return messages.num_joins();
    }

    static void print_options_help(std::ostream& out) { 
      out << "\t mult=3: number of queues per thread." << std::endl;
    }


  }; // end of fifo scheduler 


} // end of namespace graphlab
#include <graphlab/macros_undef.hpp>

#endif


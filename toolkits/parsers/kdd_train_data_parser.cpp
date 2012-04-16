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


#include <cstdio>
#include <map>
#include <iostream>
#include "graphlab.hpp"
#include "../shared/io.hpp"
#include "../shared/types.hpp"
using namespace graphlab;
using namespace std;

int nodes = 2421057;
int num_edges = 39752419;
int split_time = 1321891200; //time to split test data
int ratingA = 19349608;
int ratingB = 15561329;
int split_day_of_year = 310;

bool debug = false;
std::string datafile;
unsigned long long total_lines = 0;
bool gzip = false;

int get_day(time_t pt);

struct vertex_data {
  string filename;
  vertex_data() { }
  vertex_data(std::string _filename) : filename(_filename) { }
}; // end of vertex_data

struct edge_data {
};

struct vertex_data2 {
  double A_ii;
  double value;
  vertex_data2(): A_ii(1) { } 
  void add_self_edge(double value) { A_ii = value; }
  void set_val(double value, int field_type) { 
  }  
  double get_output(int field_type){ return -1; }
}; // end of vertex_data


struct edge_data2 {
  int rating;
  int time;
  edge_data2(int _rating, int _time) : rating(_rating),time(_time) { }
};

typedef graphlab::graph<vertex_data, edge_data> graph_type;
typedef graphlab::graph<vertex_data2, edge_data2> graph_type2;
/***
* Line format is:
1::gift card
2::
3::
4::might have art deco roots but the exaggerated basket weave pattern */
struct stringzipparser_update :
   public graphlab::iupdate_functor<graph_type, stringzipparser_update>{
   void operator()(icontext_type& context) {

typedef graphlab::graph<vertex_data2, edge_data2>::edge_list_type edge_list;
    
   vertex_data& vdata = context.vertex_data();
   gzip_in_file fin((vdata.filename), gzip);
   gzip_out_file fout((vdata.filename + ".out"), gzip);
   gzip_out_file fout_validation((vdata.filename + ".oute"), gzip);
    
    MM_typecode out_typecode;
    mm_clear_typecode(&out_typecode);
    mm_set_integer(&out_typecode); 
    mm_set_sparse(&out_typecode); 
    mm_set_matrix(&out_typecode);
    mm_write_cpp_banner(fout.get_sp(), out_typecode);
    mm_write_cpp_banner(fout_validation.get_sp(), out_typecode);
    char linebuf[24000];
    char saveptr[1024];
    int added = 0;
    int last_from = -1, last_to = -1, last_rating = 0, last_time = 0;
    int ignore_last_to = -1, ignore_last_from = -1;
    int positive_examples = 0, negative_examples = 0;
    int maxdayofyear = 0; 
    int mindayofyear = 10000000;
    graph_type2 out_graph;
    out_graph.resize(2*nodes);
    graph_type2 out_graph_validation;

  
 
    while(true){
      fin.get_sp().getline(linebuf, 24000);
      if (fin.get_sp().eof())
        break;

      char *pch = strtok_r(linebuf," \r\n\t",(char**)&saveptr);
      if (!pch){
        logstream(LOG_ERROR) << "Error when parsing file: " << vdata.filename << ":" << total_lines << "[" << linebuf << "]" << std::endl;
        return;
       }
      int from = atoi(pch);
      if (from <= 0){
         logstream(LOG_ERROR) << "Error when parsing file: " << vdata.filename << ":" << total_lines << " document ID is zero or less: " << from << std::endl;
         return;
      }
      pch = strtok_r(NULL," \r\n\t",(char**)&saveptr);
      if (!pch){
        logstream(LOG_ERROR) << "Error when parsing file: " << vdata.filename << ":" << total_lines << "[" << linebuf << "]" << std::endl;
        return;
       }
      int to = atoi(pch);
      if (to <= 0){
         logstream(LOG_ERROR) << "Error when parsing file: " << vdata.filename << ":" << total_lines << " document ID is zero or less: " << from << std::endl;
         return;
      }
      pch = strtok_r(NULL," \r\n\t",(char**)&saveptr);
      if (!pch){
        logstream(LOG_ERROR) << "Error when parsing file: " << vdata.filename << ":" << total_lines << "[" << linebuf << "]" << std::endl;
        return;
       }
      int rating = atoi(pch);
      if (rating != -1 && rating != 1){
         logstream(LOG_ERROR) << "Error when parsing file: " << vdata.filename << ":" << total_lines << " invalid rating, not -1 or 1 " << from << std::endl;
         return;
      }
      pch = strtok_r(NULL," \r\n\t",(char**)&saveptr);
      if (!pch){
        logstream(LOG_ERROR) << "Error when parsing file: " << vdata.filename << ":" << total_lines << "[" << linebuf << "]" << std::endl;
        return;
       }
      int time = atoi(pch);
      if (time <= 0){
         logstream(LOG_ERROR) << "Error when parsing file: " << vdata.filename << ":" << total_lines << " invalid time " << from << std::endl;
         return;
      }

      total_lines++;
      if (debug && (total_lines % 50000 == 0))
        logstream(LOG_INFO) << "Parsed line: " << total_lines << " selected lines: " << added << std::endl;

      //duplicate entry in different time, nothing to do. 
      if (last_from == from && last_to == to && last_rating == rating){

      }
      //item with opposite ratings, ignore
      else if (last_from == from && last_to == to && last_rating != rating){
         ignore_last_from = from;
         ignore_last_to = to;
      }
      //a different rating encountered
      else if ((last_from > -1) && (last_from != from || last_to != to)){
         if (last_from == ignore_last_from && last_to == ignore_last_to){
         }
         else { 
            int dayofyear = get_day(last_time);
            if (dayofyear < mindayofyear)
                mindayofyear = dayofyear;
            if (dayofyear > maxdayofyear)
                maxdayofyear = dayofyear;
            edge_data2 edge(last_rating, dayofyear);
            if (dayofyear >= split_day_of_year)
              out_graph_validation.add_edge(last_from - 1, last_to+nodes-1, edge);
            else out_graph.add_edge(last_from - 1, last_to+nodes-1, edge);
            added++;
            if (last_rating == -1)
	            negative_examples++;
            else positive_examples++;
                       //cout<<"day of year: " << get_day(last_time) << endl;
         }
      }
   
      last_from = from;
      last_to = to;
      last_rating = rating;
      last_time = time;
    } 

   logstream(LOG_INFO) <<"Finished parsing total of " << total_lines << " lines in file " << vdata.filename << endl;
   logstream(LOG_INFO) <<"Min day of year " << mindayofyear << " maxday " << maxdayofyear << endl;
    
   int dayofyear = get_day(last_time);
   if (dayofyear < mindayofyear)
                mindayofyear = dayofyear;
   if (dayofyear > maxdayofyear)
                maxdayofyear = dayofyear;
   edge_data2 last_edge(last_rating, dayofyear);
   if (dayofyear >= split_day_of_year)
      out_graph_validation.add_edge(last_from - 1, last_to+nodes-1, last_edge);
   else out_graph.add_edge(last_from - 1, last_to+nodes-1, last_edge);

   out_graph.finalize();
   out_graph_validation.finalize();

    int total_edges = 0;
    mm_write_cpp_mtx_crd_size(fout.get_sp(), nodes, nodes, out_graph.num_edges());

    for (int j=0; j< nodes; j++){
       edge_list out_edges = out_graph.out_edges(j);
       for (uint k=0; k < out_edges.size(); k++){
          total_edges++;
          fout.get_sp() << (out_edges[k].source() +1) << " " <<
							    (out_edges[k].target() + 1 - nodes) << " " <<
                  out_graph.edge_data(out_edges[k]).rating << " " <<
                  out_graph.edge_data(out_edges[k]).time << std::endl;
				          
       }
    }

    logstream(LOG_INFO)<<"Finished exporting a total of " << total_edges << " ratings " << std::endl << " Positive ratings: " << positive_examples << " Negative ratings: " << negative_examples << std::endl;
    ASSERT_EQ(out_graph.num_edges(), total_edges);
    int total_edges_validation = 0;
    mm_write_cpp_mtx_crd_size(fout_validation.get_sp(), nodes, nodes, out_graph_validation.num_edges());
    for (int j=0; j< nodes; j++){
       edge_list out_edges = out_graph_validation.out_edges(j);
       for (uint k=0; k < out_edges.size(); k++){
          total_edges_validation++;
          fout_validation.get_sp() << (out_edges[k].source() +1) << " " <<
							    (out_edges[k].target() + 1 - nodes) << " " <<
                  out_graph_validation.edge_data(out_edges[k]).rating << " " <<
                  out_graph_validation.edge_data(out_edges[k]).time << std::endl;
				          
       }
    }

    logstream(LOG_INFO)<<"Finished exporting a total of " << total_edges_validation << " ratings to validation graph. " << endl;
    ASSERT_EQ(total_edges_validation + total_edges, num_edges);
    ASSERT_EQ(out_graph_validation.num_edges(), total_edges_validation);

  }


};



int main(int argc,  char *argv[]) {
  
  global_logger().set_log_level(LOG_INFO);
  global_logger().set_log_to_console(true);

  graphlab::command_line_options clopts("GraphLab Parsers Library");
  clopts.attach_option("data", &datafile, datafile,
                       "training data input file");
  clopts.add_positional("data");
  clopts.attach_option("debug", &debug, debug, "Display debug output.");
  clopts.attach_option("gzip", &gzip, gzip, "Gzipped input file?");
  clopts.attach_option("split_day_of_year", &split_day_of_year, split_day_of_year, "split training set to validation set, for days >= split_day_of_year");

  // Parse the command line arguments
  if(!clopts.parse(argc, argv)) {
    std::cout << "Invalid arguments!" << std::endl;
    return EXIT_FAILURE;
  }

  logstream(LOG_WARNING)
    << "Eigen detected. (This is actually good news!)" << std::endl;
  logstream(LOG_INFO) 
    << "GraphLab parsers library code by Danny Bickson, CMU" 
    << std::endl 
    << "Send comments and bug reports to danny.bickson@gmail.com" 
    << std::endl 
    << "Currently implemented parsers are: Call data records, document tokens "<< std::endl;

  // Create a core
  graphlab::core<graph_type, stringzipparser_update> core;
  core.set_options(clopts); // Set the engine options
  core.set_scope_type("none");

  vertex_data data(datafile);
  core.graph().add_vertex(0, data);
  core.schedule_all(stringzipparser_update());
  double runtime= core.start();
 
  std::cout << "Finished in " << runtime << std::endl;
}




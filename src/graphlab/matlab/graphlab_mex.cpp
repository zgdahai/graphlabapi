#include <mex.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <graphlab.hpp>
#include <cstdlib>
#include "graphlab_mex_parse.hpp"
#include "graphlab_mex_output.hpp"
#include "rtwtypes.h"
#include "updates_types.h"
#include "mx_emx_converters.hpp"
#include "updates_initialize.h"
#include "gl_emx_graphtypes.hpp"
#include "update_function_generator.hpp"


/**
 * Frees the graph datastructures.
 */
void cleanup_graph(emx_graph &graph) {
  // free the graph
  for (size_t i = 0;i < graph.num_vertices(); ++i) {
    freeemx(graph.vertex_data(i));
  }
  for (size_t i = 0;i < graph.num_edges(); ++i) {
    freeemx(graph.edge_data(i));
  }
  graph.clear();
}


/**
 * Parses the options structure
 */
std::vector<parsed_initial_schedule>
parse_emx_schedule(const emxArray_graphlab_initial_schedule &sched) {
  std::vector<parsed_initial_schedule> out;
  size_t numentries = 1;
  for (int i = 0;i < sched.numDimensions; ++i) numentries *= sched.size[i];
  
  for (size_t i = 0;i < numentries; ++i) {

    parsed_initial_schedule parse;
    parse.update_function =
                emxArray_char_T_to_string(sched.data[i].update_function);
    if (parse.update_function.length() == 0) {
      mexWarnMsgTxt("schedule entry with no update function.");
      continue;
    }
    // parse the vertices and priorities
    
    parse.vertices = emxArray_to_vector<uint32_t>(sched.data[i].vertices);
    parse.priorities = emxArray_to_vector<double>(sched.data[i].priorities);
    if (parse.vertices.size() != parse.priorities.size()) {
      mexWarnMsgTxt("#vertices do not match #priorities");
    }
    out.push_back(parse);
  }
  return out;
}


/**
 * Tries to call the binary side of the mex
 * returns false on failure
 */
bool issue_binary_call(std::string binarystage,
                       graphlab_options_struct &optionsstruct,
                       std::string tempfilename) {
  // compile the command...
  std::stringstream command;
  command << "./" << binarystage;

  command << " --engine=async";
  // ncpus
  size_t ncpus = optionsstruct.ncpus;
  if (ncpus < 1) {
    std::cout << "Invalid value for ncpus. Setting to 1." << std::endl;
    ncpus = 1;
  }
  else if (ncpus > 65536){
    std::cout << "Do you really want to start > 65536 threads?" << std::endl;
    std::cout << "Maxing out to 65536." << std::endl;
    ncpus = 65536;
  }
  command << " --ncpus=" << ncpus;
  if (optionsstruct.scheduler == NULL) {
    mexWarnMsgTxt("Scheduler parameter not optional.");
    return false;
  }

  command << " --scheduler=" <<
                          emxArray_char_T_to_string(*optionsstruct.scheduler);

  if (optionsstruct.scope == NULL) {
    mexWarnMsgTxt("Scope parameter missing. Defaulting to \'edge\'.");
    command << "--scope=edge";
  }
  else {
    command << " --scope=" <<
                          emxArray_char_T_to_string(*optionsstruct.scope);
  }

  // tempfilename
  command << " --graphfile=" << tempfilename;

  std::string commandstr = command.str();
  mexPrintf("Issuing command: %s\n", commandstr.c_str());
  
  int ret = system(commandstr.c_str());
  if (ret != 0) {
    mexWarnMsgTxt("Failed to execute command!");
    std::cerr << "Error number: " << errno << std::endl;
    std::cerr << "Return value : " << WEXITSTATUS(ret) << std::endl;
    return false;
  }
  return true;
}

/**
 * [newvdata, newadjmat, newedata] = graphlab_mex(vertexdata, adj_mat, edgedata, options, strict)
 *
 * vertexdata: cell array of vertex data
 * adj_mat: (sparse) adjacency matrix where adj_mat[i][j] is an edge from vertex
 *          i to vertex j and the data on the edge is edgedata(adjmat[i][j])
 * edgedata: cell array of edge data
 * options: options and schedule struct
 * strict: numeric 0/1 . Strictness of typechecking
 *  Returns new graph  data on exit
 *
 *
 *  optionsstruct:
 * -- scheduler: Scheduler string
 * -- scope: Scope Type
 * -- ncpus: number of cpus to use
 * -- initial_schedule: and array of structs describing the schedule
  */
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
  // basic data type checks
  // we must output to something
  if (nlhs < 3) {
    mexWarnMsgTxt("Too few output arguments.");
    return;
  }
  else if (nlhs > 3) {
    mexWarnMsgTxt("Too many output arguments.");
    return;
  }
  // there must be exactly 4 arguments
  if (nrhs != 5) {
    mexWarnMsgTxt("Erronous function call");
    mexWarnMsgTxt("Usage: graphlab_mex(vertexdata, adj_mat, edgedata, schedule, strict)");
    return;
  }

  // fill the parameters structure
  mex_parameters param;
  param.vdata = prhs[0];
  param.adjmat = prhs[1];
  param.edata = prhs[2];
  param.options = prhs[3];
  param.strict = prhs[4];

    // basic type check of the parameters
  if (basic_typecheck(param) == false) {
    mexWarnMsgTxt("Basic typechecks failed.");
    return;
  }
  bool strict = mxGetScalar(param.strict) != 0;

  // read the options information
  graphlab_options_struct optionsstruct;
  memset(&optionsstruct, 0, sizeof(graphlab_options_struct));
  mxarray2emx(param.options, optionsstruct);

  // construct the graph
  emx_graph graph;
  bool ret = construct_graph(graph, param.vdata, param.adjmat, param.edata);
  if (ret == false) {
    if (strict != 0) {
      mexWarnMsgTxt("Type conversion errors. Strict-mode is set. Terminating.");
      cleanup_graph(graph);
      return;
    }
    else {
      mexWarnMsgTxt("Type conversion errors. Strict-mode is not set. Continuing.");
    }
  }
  graph.finalize();
  graph.compute_coloring();

  // create a temporary file to store the serialization
  char* tmpfilename = tmpnam(NULL);
  if (tmpfilename == NULL) {
    mexWarnMsgTxt("Cannot create temporary file! Terminating.");
    cleanup_graph(graph);
    return;
  }
  mexPrintf("Serializing to: %s\n", tmpfilename);
  std::ofstream fout(tmpfilename, std::ios_base::binary);
  if (!fout.good()) {
    mexWarnMsgTxt("Unable to open temporary file! Terminating.");
    cleanup_graph(graph);
    return;
  }
  graphlab::oarchive oarc(fout);
  std::vector<parsed_initial_schedule> schedule = 
                    parse_emx_schedule(*optionsstruct.initial_schedule);
  
  oarc << graph;
  oarc << schedule;
  fout.close();
  cleanup_graph(graph);

  issue_binary_call("binary_stage", optionsstruct, tmpfilename);
  return;
}



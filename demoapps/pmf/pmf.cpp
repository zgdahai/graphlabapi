// written by Danny Bickson, CMU 
// #define NDEBUG
#include <fstream>
#include <cmath>
#include <cstdio>

#include "graphlab.hpp"
#include "itppvecutils.hpp"
#include "pmf.h"
//include "vecutils.hpp"
#include "prob.hpp"
//#include "graphlab/util/timer.hpp"

#include <graphlab/macros_def.hpp>
/**

   Probabalistic matrix/tensor factorization written Danny Bickson, CMU


*/

using namespace graphlab;
bool BPTF = true; //is this a BPTF algo?
bool tensor = true;
bool debug = false;
bool ZERO = false; //support edges with zero weight
runmodes options;
timer gt;
using namespace itpp;
using namespace std;
double scaling = 1;
double truncating =0;
std::vector<edge_id_t> * edges;
std::string infile;
bool loadfactors = false;
bool savefactors = true;
bool loadgraph = false;
bool savegraph = false;
bool stats = false;
bool regnormal = false; //regular normalization
extern bool finish; //defined in convergence.hpp
int iiter = 1;//count number of time zero node run

/* Variables for PMF */
int M,N,K,L;//training size
int Le = 0; //validation size
int Lt = 0;//test size
double pU = 10; //regularization for matrix
double pT = 1;
double pV = 10;
double muT = 1;
vec vones; 
mat eDT; 
mat dp;

/* variables for BPTF */
double nuAlpha = 1;
double Walpha = 1;
double mu0 = 0;
double mu0T = 1;
double nu0 = D;
double alpha = 0;
double beta = 1;
double beta0 = 1; //TODO
mat W0;
mat W0T;
double iWalpha;
mat iW0;
mat iW0T;
mat A_U, A_V, A_T;
vec mu_U, mu_V, mu_T;

double counter[20];

vertex_data * times = NULL;

typedef graphlab::graph<vertex_data, multiple_edges> graph_type;
typedef graphlab::types<graph_type> gl_types;
gl_types::iengine * engine;
graph_type* g;
graph_type validation_graph;
graph_type test_graph;
gl_types::thread_shared_data sdm;

#ifndef _min
#define _min(a,b) (a>b)?b:a
#endif


const size_t RMSE = 0;


void init_self_pot(){
  //assert(BPTF);

  W0 = eye(D);
  W0T = eye(D);
  iWalpha = 1.0/Walpha;
  iW0 = inv(W0);
  iW0T = inv(W0T);
  nu0 = D;

  A_U = eye(D);
  A_V = eye(D);
  A_T = eye(D);

  mu_U = zeros(D); mu_V = zeros(D); mu_T = zeros(D);
  printf("nuAlpha=%g, Walpha=%g, mu=%g, muT=%g, nu=%g, "
         "beta=%g, W=%g, WT=%g BURN_IN=%d\n", nuAlpha, Walpha, mu0, 
         mu0T, nu0, beta0, W0(1,1), W0T(1,1), BURN_IN);


  //test_randn(); 
  //test_wishrnd();
  //test_wishrnd2(); 
  //test_chi2rnd();
  //test_wishrnd3();
  //test_mvnrndex();
}

 
/**
 * pmf APPLICATION CLASS
 */
    
/** CONSTRUCTOR **/
void init_pmf() {
  if (BPTF)
    pT=10;
  eDT = itpp::eye(D)*pT;
  vones = itpp::ones(D);
}
    
void load_pmf_graph(const char* filename, graph_type * g, testtype flag,gl_types::core & glcore);    
void calc_T(int id);    
double calc_obj(double res);
void last_iter();
void export_kdd_format(graph_type * _g, bool dosave);
void calc_stats(testtype type);

void sample_alpha(double res2){
  
  if (debug)
    printf("res is %g\n", res2); 
  
  double res = res2;
  assert(BPTF);
  if (nuAlpha > 0){
    double nuAlpha_ =nuAlpha+ L;
    mat iWalpha_(1,1);
    iWalpha_.set(0,0, iWalpha + res);
    mat iiWalpha_ = zeros(1,1);
    iiWalpha_ = inv(iWalpha_);
    alpha = wishrnd(iiWalpha_, nuAlpha_).get(0,0);
    assert(alpha != 0);

    if (debug)
      cout<<"Sampling from alpha" <<nuAlpha_<<" "<<iWalpha<<" "<< iiWalpha_<<" "<<alpha<<endl;
    printf("sampled alpha is %g\n", alpha); 
  }
}


mat calc_MMT(int start_pos, int end_pos, vec &Umean){

  int batchSize = 1000;
  mat U = zeros(batchSize,D);
  mat MMT = zeros(D,D);
  int cnt = 0;
  timer t;

  for (int i=start_pos; i< end_pos; i++){
    if ((i-start_pos) % batchSize == 0){
      U=zeros(batchSize, D);
      cnt = 1;
    }

    const vertex_data * data= &g->vertex_data(i);
     
    vec mean = data->pvec;
    Umean += mean;
    //Q.set_col(k, ret);
    t.start(); 
    for (int s=0; s<D; s++)
      U(i%batchSize,s)=mean(s);
    if (debug && (i==start_pos || i == end_pos-1))
      std::cout<<" clmn "<<i<< " vec: " << mean <<std::endl;

    if ((cnt  == batchSize) || (cnt < batchSize && i == end_pos-1)){
      MMT = MMT+transpose(U)*U;
    }
    counter[8] += t.current_time();
    cnt++;
  }
  Umean /= (end_pos-start_pos);
  if (debug)
    cout<<"mean: "<<Umean<<endl;

  assert(MMT.rows() == D && MMT.cols() == D);
  assert(Umean.size() == D);
  return MMT;
}



// sample from movie nodes
void sample_U(){
  assert(BPTF);

  vec Umean;
  mat UUT = calc_MMT(0,M,Umean);
  
  double beta0_ = beta0 + M;
  vec mu0_ = (beta0*mu0 + M*Umean)/beta0_;
  double nu0_ = nu0 +M;
  vec dMu = mu0 - Umean;
  if (debug)
    cout<<"dMu:"<<dMu<<"beta0: "<<beta0<<" beta0_ "<<beta0_<<" nu0_ " <<nu0_<<" mu0_ " << mu0_<<endl;
  mat UmeanT = M*(itpp::outer_product(Umean, Umean));
  assert(UmeanT.rows() == D && UmeanT.cols() == D);
  mat dMuT = (beta0*M/beta0_)*(itpp::outer_product(dMu, dMu));
  mat iW0_ = iW0 + UUT - UmeanT + dMuT;
  mat W0_; 
  bool ret =inv(iW0_, W0_);
  assert(ret);
  mat tmp = (W0_+transpose(W0_))*0.5;
  if (debug)
    cout<<iW0<<UUT<<UmeanT<<dMuT<<W0_<<tmp<<nu0_<<endl;
  A_U = wishrnd(tmp, nu0_);
  mat tmp2;  
  ret =  inv(beta0_ * A_U, tmp2);
  assert(ret);
  mu_U = mvnrndex(mu0_, tmp2, D);
  if (debug)
    cout<<"Sampling from U" <<A_U<<" "<<mu_U<<" "<<Umean<<" "<<W0_<<tmp<<endl;
}

// sample from user nodes
void sample_V(){

  assert(BPTF);
  vec Vmean;
  mat VVT = calc_MMT(M, M+N, Vmean);   

  double beta0_ = beta0 + N;
  vec mu0_ = (beta0*mu0 + N*Vmean)/beta0_;
  double nu0_ = nu0 +N;
  vec dMu = mu0 - Vmean;
  if (debug)
    cout<<"dMu:"<<dMu<<"beta0: "<<beta0<<" beta0_ "<<beta0_<<" nu0_ " <<nu0_<<endl;
  mat VmeanT = N*(itpp::outer_product(Vmean, Vmean));
  assert(VmeanT.rows() == D && VmeanT.cols() == D);
  mat dMuT =  (beta0*N/beta0_)*itpp::outer_product(dMu, dMu);
  mat iW0_ = iW0 + VVT - VmeanT + dMuT;
  mat W0_;
  bool ret = inv(iW0_, W0_);
  assert(ret);
  mat tmp = (W0_+transpose(W0_))*0.5;
  if (debug)
    cout<<"iW0: "<<iW0<<" VVT: "<<VVT<<" VmeanT: "<<VmeanT<<" dMuT: " <<dMuT<<"W0_"<< W0_<<" tmp: " << tmp<<" nu0_: "<<nu0_<<endl;
  A_V = wishrnd(tmp, nu0_);
  mat tmp2; 
  ret = inv(beta0_*A_V, tmp2);
  assert(ret);
  mu_V = mvnrndex(mu0_, tmp2, D);
  if (debug)
    cout<<"Sampling from V: A_V" <<A_V<<" mu_V: "<<mu_V<<" Vmean: "<<Vmean<<" W0_: "<<W0_<<" tmp: "<<tmp<<endl;
}


mat calc_DT(){

  assert(tensor);

  mat T = zeros(D, K);
  for (int i=0; i<K; i++){
    T.set_col(i,times[i].pvec);
  }
  
  mat diff = zeros(D,K-1);
  for (int i=0; i<K-1; i++){
    diff.set_col(i , T.get_col(i) - T.get_col(i+1));
  }
  if (debug)
    cout<<"T:"<<T<<" diff: " << diff<<endl;
  
  return diff;

}

// sample from time nodes
void sample_T(){
  assert(BPTF);
  assert(tensor);

  double beta0_ = beta0 + 1;
  vec pvec = times[0].pvec; 
  vec mu0_ = (pvec + beta0*mu0T)/beta0_;
  double nu0_ = nu0 +K;
  //vec dMu = mu0 - Umean;
  if (debug){
    cout<<"beta0_ " << beta0_ << " beta0: " << beta0 << " nu0_ " << nu0_ << endl;
  } 

  mat dT = calc_DT();
  vec dTe = pvec - mu0T;
  mat iW0_ = iW0T + dT*transpose(dT) + (beta0/beta0_)*(itpp::outer_product(dTe,dTe));
  
  mat W0_;
  bool ret =inv(iW0_, W0_);
  assert(ret);
  mat tmp = W0_+transpose(W0_)*0.5;
  A_T = wishrnd(tmp, nu0_);

  mat tmp2 ;
  ret = inv(beta0_*A_T, tmp2);
  assert(ret);
  mu_T = mvnrndex(mu0_, tmp2, D);
  if (debug)
    cout<<"Sampling from T: A_T" <<A_T<<" mu_V: "<<mu_T<<" W0_: "<<W0_<<" tmp: "<<tmp<<endl;
   
}

// update function for time nodes
void time_node_update_function(gl_types::iscope &scope, gl_types::icallback &scheduler, gl_types::ishared_data* shared_data) {

  assert(tensor);

  int id = scope.vertex() - M-N;
  assert(id >=0 && id < K);	
  if (debug && (id == 0 || id == K-1)){
    printf("entering time node  %d \n", id);   
  } 
  if (K > 1)
    calc_T(id); 
  else last_iter();
}

//calculate RMSE
double calc_rmse(graph_type * _g, bool test, double & res){

  if (test && Le == 0)
    return NAN;
      
  res = 0;
  double RMSE = 0;
  int e = 0;
  for (int i=M; i< M+N; i++){ //TODO: optimize to start from N?
    vertex_data * data = &g->vertex_data(i);
    foreach(edge_id_t iedgeid, _g->in_edge_ids(i)) {
            
      multiple_edges & edges = _g->edge_data(iedgeid);
      vertex_data * pdata = &g->vertex_data(_g->source(iedgeid)); 
      for (int j=0; j< (int)edges.medges.size(); j++){       
    
        edge_data & edge = edges.medges[j];
        if (!ZERO)
          assert(edge.weight != 0);
        double sum = 0; 
        double add = rmse(data->pvec, pdata->pvec, tensor? (&times[(int)edge.time].pvec):NULL, D, edge.weight, sum);
        if (!ZERO)
          assert(sum != 0);         
        if (BPTF && iiter > BURN_IN)
          edge.avgprd += sum;

        if (debug && (i== M || i == M+N-1) && (e == 0 || e == (test?Le:L)))
          cout<<"RMSE:"<<i <<"u1"<< data->pvec << " v1 "<< pdata->pvec<<endl; 
        //assert(add<25 && add>= 0);
          
        if (BPTF && iiter > BURN_IN){
          add = powf((edge.avgprd / (iiter - BURN_IN)) - edge.weight, 2);
        }
            
        RMSE+= add;
        e++;
      }
    }
  }
  res = RMSE;
  assert(e == (test?Le:L));
  return sqrt(RMSE/(double)e);

}
double calc_rmse_q(double & res){

  timer t; t.start();
  res = 0;
  double RMSE = 0;
  for (int i=M; i< M+N; i++){ 
    const vertex_data * data = &g->vertex_data(i);
    RMSE+= data->rmse;
  }
  res = RMSE;
  counter[2] += t.current_time();
  return sqrt(RMSE/(double)L);

}


inline void parse_edge(const edge_data& edge, const vertex_data & pdata, mat & Q, vec & vals, int i){
      
  if (!ZERO)  
    assert(edge.weight != 0);

  if (tensor){
    dot2(pdata.pvec,  times[(int)edge.time].pvec, Q, i, D);  
  }
  else {
    for (int j=0; j<D; j++)
      Q.set(j,i, pdata.pvec[j]); 
  }
 
  vals[i] = edge.weight;
}
 
  
int count_edges(edge_list es){
  
  if (options != BPTF_TENSOR_MULT && options != ALS_TENSOR_MULT)
    return es.size();

  int cnt = 0; 
  for (int j=0; j< (int)es.size(); j++){
    cnt += g->edge_data(es[j]).medges.size();
  }
  return cnt;
}


/***
 * UPDATE FUNCTION
 */
void user_movie_nodes_update_function(gl_types::iscope &scope, 
                                      gl_types::icallback &scheduler,
                                      gl_types::ishared_data* shared_data) {
    

  //bool debug = false;
  /* GET current vertex data */

  vertex_data& vdata = scope.vertex_data();
 
  
  /* CALCULATE new value */
  if (debug&& (scope.vertex() == 0 || ((int)scope.vertex() == M-1) || ((int)scope.vertex() == M) || ((int)scope.vertex() == M+N-1) || ((int)scope.vertex() == 93712))){
    printf("entering %s node  %u \n", (((int)scope.vertex() < M) ? "movie":"user"), (int)scope.vertex());   
    debug_print_vec((((int)scope.vertex() < M) ? "V " : "U") , vdata.pvec, D);
  }

  assert((int)scope.vertex() < M+N);

  vdata.rmse = 0;

 
  //int i=0; 
  // update neighbors 
  edge_list outs = scope.out_edge_ids();
  edge_list ins = scope.in_edge_ids();
  int numedges = vdata.num_edges;
  if (numedges == 0){
    return;
  }

  //assert(outs.size() == 0 || ins.size() == 0);   
  assert(numedges > 0);

  timer t;
  mat Q(D,numedges);
  vec vals(numedges);

  int i=0;

  t.start(); 
  if ((int)scope.vertex() < M){
    
    //USER NODES
    foreach(graphlab::edge_id_t oedgeid, outs) {

      multiple_edges &medges =scope.edge_data(oedgeid);
      const vertex_data  & pdata = scope.const_neighbor_vertex_data(scope.target(oedgeid)); 
	   
      for (int j=0; j< (int)medges.medges.size(); j++){
        edge_data& edge = medges.medges[j];
        parse_edge(edge, pdata, Q, vals, i); 
     
        if (debug && ((int)scope.vertex() == 0 || (int)scope.vertex() == M-1) && (i==0 || i == numedges-1))
          std::cout<<"set col: "<<i<<" " <<Q.get_col(i)<<" " <<std::endl;
        i++;
      }   
           
            
    }
  }

  else {


    //MOVIE NODES
    foreach(edge_id_t iedgeid, ins) {

      const vertex_data & pdata = scope.const_neighbor_vertex_data(scope.source(iedgeid)); 
      multiple_edges & medges =scope.edge_data(iedgeid);
      for (int j=0; j< (int)medges.medges.size(); j++){
        edge_data& edge = medges.medges[j];
        parse_edge(edge, pdata, Q, vals, i); 
     
        if (debug && (((int)scope.vertex() == M) || ((int)scope.vertex() == M+N-1)) && (i==0 || i == numedges-1))
          std::cout<<"set col: "<<i<<" " <<Q.get_col(i)<<" " <<std::endl;

        i++;
        double sum;     
        double trmse = rmse(vdata.pvec, pdata.pvec, tensor?(&times[(int)edge.time].pvec):NULL, D, edge.weight, sum);
        //assert(sum != 0);
        if (BPTF && iiter > BURN_IN){
          edge.avgprd += sum;        
          trmse = pow((edge.avgprd / (iiter - BURN_IN)) - edge.weight, 2);
        }
        vdata.rmse += trmse; 
 
     
      }
      
    }

  }
  assert(i == numedges);
  counter[0] += t.current_time();

  vec result;
      
  if (!BPTF){
    //COMPUTE LEAST SQUARES (ALTERNATING LEAST SQUARES)
    t.start();
    double regularization = LAMBDA;
    if (!regnormal)
      regularization*= Q.cols();

    bool ret = itpp::ls_solve(Q*itpp::transpose(Q)+eDT*regularization, Q*vals, result);
    //assert(result.size() == D);     
    assert(ret);
    counter[3] += t.current_time();
  }
  else {
    //COMPUTE LEAST SQUARES (BPTF)
    assert(Q.rows() == D);
    t.start();
    mat iAi_;
    bool ret =inv(((int)scope.vertex() < M? A_U : A_V) + alpha *  Q*itpp::transpose(Q), iAi_);
    assert(ret);
    t.start();
    vec mui_ =  iAi_*((((int)scope.vertex() < M)? (A_U*mu_U) : (A_V*mu_V)) + alpha * Q * vals);
    counter[10]+= t.current_time();
       
    t.start();
    result = mvnrndex(mui_, iAi_, D); 
    assert(result.size() == D);
    counter[9] += t.current_time();
  }

  if (debug && (((int)scope.vertex()  == 0) || ((int)scope.vertex() == M-1) || ((int)scope.vertex() == M) || ((int)scope.vertex() == M+N-1))){
    std::cout <<(BPTF?"BPTF":"ALS")<<" Q: " << Q << std::endl<<" result: " << result << " edges: " << numedges << std::endl;
  }
      
  //store new result
  vdata.pvec =  result;

  //calc post round tasks
  if (!tensor && (int)scope.vertex() == M+N-1)
    last_iter();

}

void last_iter(){
  printf("Entering last iter with %d\n", iiter);

  double res,res2;
  double rmse = calc_rmse_q(res);
  //rmse=0;
  printf("%g) Iter %s %d  Obj=%g, TRAIN RMSE=%0.4f TEST RMSE=%0.4f.\n", gt.current_time(), BPTF?"BPTF":"ALS", iiter,calc_obj(res),  rmse, calc_rmse(&validation_graph, true, res2));
  iiter++;
  if (iiter == BURN_IN && BPTF){
    printf("Finished burn-in period. starting to aggregate samples\n");
  }
         
  if (iiter == MAX_ITER){
    //engine->stop();
  }
  if (BPTF){
    timer t;
    t.start();
    if (iiter > BURN_IN)
      sample_alpha(res);
    sample_U();
    sample_V();
    if (tensor) 
      sample_T();
    counter[1] += t.current_time();
    if (infile == "kddcup" || infile == "kddcup2")
      export_kdd_format(&test_graph, false);
  }
}


void calc_T(int i){

  assert(tensor);
  //bool debug = false;

  //for (int i=0; i< K; i++){
  assert(i >=0 && i < K);
  if (ZERO && edges[i].size() == 0)
    return;

  assert(edges[i].size() > 0);

  if (debug && (i==0 || i == K-1))
    printf("node %d with Q size: %d %d\n", i, (int)edges[i].size(), (int)D);

  int k=0;
  int batchSize = 100;
  mat Q(batchSize, D); 
  vec vals(batchSize);
  mat QQ = zeros(D,D);
  vec RQ = zeros(D);
  int cnt =0;

  timer t; t.start();
  foreach (edge_id_t edge, edges[i]){
    if (k % batchSize == 0){
      Q.zeros();
      vals.zeros();
      cnt = 1;
    }

    //find the right edge which matches the current time
    multiple_edges * medges= &g->edge_data(edge);
    edge_data data;
    for (int j=0; j< (int)medges->medges.size(); j++){
      data = medges->medges[j];
      if (data.time == i)
        break;
    }

    assert(data.time == i);

    assert((int)g->target(edge)>= M);
    assert((int)g->source(edge)< M);
    vertex_data * v1 = &g->vertex_data(g->target(edge));
    vertex_data * v2 = &g->vertex_data(g->source(edge));
    vec ret = elem_mult(v1->pvec, v2->pvec);
     
    //Q.set_col(k, ret);
    for (int s=0; s<D; s++)
      Q(k%batchSize,s)=ret(s);
    if (debug && (i==0 || i == K-1) && (k == 0 || k == (int)edges[i].size() - 1))
      std::cout<<" clmn "<<k<< " vec: " << ret<<std::endl;

    vals[k%batchSize] = data.weight;
    //assert(data->weight >=1 && data->weight <= 5);
    k++;

    if ((cnt  == batchSize) || (cnt < batchSize && k == (int)edges[i].size()-1)){
      QQ += transpose(Q)*Q;
      RQ += transpose(Q)*vals;
      assert(QQ.rows() == D && QQ.cols() == D);
    }
    cnt++;
  }
  counter[5] += t.current_time();


  if (debug && (i == 0 ||i== K-1 )){
    std::cout<<"QQ:"<<QQ<<std::endl;
    std::cout<<"RQ:"<<RQ<<std::endl;
  }

  assert(RQ.size() == D);
  assert((unsigned int)k == edges[i].size());
  vec sol;    
  vec out;

  t.start();
  if (i == 0){
    vec t1 = times[1].pvec; 
    if (!BPTF){
      QQ = QQ+ 2*eDT;
      bool ret = itpp::ls_solve(QQ, pT*(t1 + vones*muT)+ RQ, out);
      assert(ret);
    }
    else {
      mat A_k = 2*A_T+alpha*QQ;
      mat iAk_;
      bool ret = inv(A_k, iAk_);
      assert(ret);
      vec muk_ = iAk_*(A_T*(t1 + mu_T)+alpha*RQ); //TODO
      out = mvnrndex(muk_, iAk_, D);
    }
  }
  else if (i == K-1){
    vec tk_2 = times[K-2].pvec; 
    if (!BPTF){
      QQ = QQ + eDT;
      bool ret = itpp::ls_solve(QQ, pT*tk_2 + RQ, out);
      assert(ret); 
    }
    else {
      mat A_k = A_T+alpha*QQ;
      mat iAk_; 
      bool ret = inv(A_k, iAk_);
      assert(ret);
      vec muk_ = iAk_*(A_T*tk_2+alpha*RQ); //TODO
      out = mvnrndex(muk_, iAk_, D);
    }
  }
  else {
    vec tsum; 
    vec t1 = times[i-1].pvec; 
    vec t2 = times[i+1].pvec;
    tsum = t1+t2;
    if (!BPTF){
      QQ = QQ + 2*eDT;
      bool ret = itpp::ls_solve(QQ, pT*tsum + RQ, out);
      assert(ret);
    }
    else {
      mat A_k = 2*A_T+alpha*QQ;
      mat iAk_;
      bool ret = inv(A_k, iAk_);
      assert(ret);
      vec muk_ = iAk_*(A_T* tsum +alpha*RQ); //TODO
      out = mvnrndex(muk_, iAk_, D);
    }
  }

  times[i].pvec = out;
  if (debug && (i == 0|| i == K-1)) 
    std::cout<<times[i].pvec<<std::endl;
  assert(QQ.rows() == D && QQ.cols() == D);
  counter[6] += t.current_time();
  //}

  if (i == K-1){
    last_iter();
  }
          


}

// CALCULATE OBJECTIVE VALUE
double calc_obj(double res){
   
  double sumU = 0, sumV = 0, sumT = 0;
  timer t;
  t.start(); 
  for (int i=0; i< M; i++){
    const vertex_data * data = &g->vertex_data(i);
    sumU += sum_sqr(data->pvec);
  } 

  for (int i=M; i< M+N; i++){
    const vertex_data * data = &g->vertex_data(i);
    sumV += sum_sqr(data->pvec);
  } 


  mat T;
  if (tensor){
    T = zeros(D,K);
    for (int i=0; i<K; i++){
      vec tmp = times[i].pvec;
      sumT += pow(norm(tmp - vones, 2),2);
      T.set_col(i, tmp);
    }
    sumT *= pT;
  }
  counter[7]+= t.current_time();
  
  double obj = (res + pU*sumU + pV*sumV + sumT + (tensor?trace(T*dp*T.transpose()):0)) / 2.0;
  return obj;
}


//SAVE FACTORS TO FILE
void export_uvt_to_file(){
  //saving output to file 
  mat U = zeros(M,D);
  mat V = zeros(N,D);
  mat T = zeros(K,D);
  for (int i=0; i< M+N; i++){ 
    vertex_data & data = g->vertex_data(i);
    if (i < M)
      memcpy(U._data() + i*D, data.pvec._data(), D*sizeof(double));
    else
      memcpy(V._data() + (i-M)*D, data.pvec._data(), D*sizeof(double));
  }

  if (tensor){ 
    for (int i=0; i<K; i++){
      memcpy(T._data() + i*D, times[i].pvec._data(), D*sizeof(double));
    }
  } 

  char dfile[256] = {0};
  sprintf(dfile,"%s%d.out",infile.c_str(), D);
  it_file output(dfile);
  output << Name("U") << U;
  output << Name("V") << V;
  if (tensor){
    output << Name("T") << T;
  }
  output.close();
}

//LOAD FACTORS FROM FILE
void import_uvt_from_file(){

  mat U,V,T;
  char dfile[256] = {0};
  sprintf(dfile,"%s%d.out",infile.c_str(), D);
  printf("Loading factors U,V,T from file\n");
  it_file input(dfile);
  input >> Name("U") >> U;
  input >> Name("V") >> V;
  if (tensor){
    input >> Name("T") >> T;
  }
  input.close();
  //saving output to file 
  for (int i=0; i< M+N; i++){ 
    vertex_data & data = g->vertex_data(i);
    if (i < M)
      memcpy( data.pvec._data(),U._data() + i*D, D*sizeof(double));
    else
      memcpy(data.pvec._data(),V._data() + (i-M)*D,  D*sizeof(double));
  }

  if (tensor){ 
    for (int i=0; i<K; i++){
      memcpy(times[i].pvec._data(), T._data() + i*D, D*sizeof(double));
    }
  } 
 
}


 
/** 
 * ==== SETUP AND START
 */
void start(int argc, char ** argv) {
      
  command_line_options clopts;
  //clopts.scheduler_type = "round_robin";
  clopts.attach_option("debug", &debug, debug, "Display debug output. (optional)");
  clopts.attach_option("max_iter", &MAX_ITER, MAX_ITER, "maximum allowed iterations (optional).");
  clopts.attach_option("burn_in", &BURN_IN, BURN_IN, "burn-in period");
  clopts.attach_option("float", &FLOAT, FLOAT, "is data in float format?");
  clopts.attach_option("D", &D, D, "dmension of weight vector");
  clopts.attach_option("lambda", &LAMBDA, LAMBDA, "regularization weight");  
  clopts.attach_option("zero", &ZERO, ZERO, "support zero edges");  
  clopts.attach_option("scaling", &scaling, scaling, "time scaling factor (optional)");  
  clopts.attach_option("truncating", &truncating, truncating, "time truncation factor (optional)");  
  clopts.attach_option("savegraph", &savegraph, savegraph, "save graphs to file");  
  clopts.attach_option("loadgraph", &loadgraph, loadgraph, "load graphs to file");  
  clopts.attach_option("savefactors", &savefactors, savefactors, "save factors to file");  
  clopts.attach_option("loadfactors", &loadfactors, loadfactors, "load factors from file");  
  clopts.attach_option("stats", &stats, stats, "compute graph statistics");  
  clopts.attach_option("alpha", &alpha, alpha, "BPTF alpha (noise parameter)");  
  clopts.attach_option("regnormal", &regnormal, regnormal, "regular normalization? ");  
 
  gl_types::core glcore;
  assert(clopts.parse(argc-2, argv+2));

  //read the training data
  printf("loading data file %s\n", infile.c_str());
  if (!loadgraph){
    g=&glcore.graph();
    load_pmf_graph(infile.c_str(), g, TRAINING, glcore);

    //read the vlidation data (optional)
    printf("loading data file %s\n", (infile+"e").c_str());
    load_pmf_graph((infile+"e").c_str(),&validation_graph, VALIDATION, glcore);

    //read the test data (optional)
    printf("loading data file %s\n", (infile+"t").c_str());
    load_pmf_graph((infile+"t").c_str(),&test_graph, TEST, glcore);


    if (savegraph){
      printf("Saving .graph files\n");
      char filename[256];
      sprintf(filename, "%s%d.graph", infile.c_str(), D);
      std::ofstream fout(filename, std::fstream::binary);
      graphlab::oarchive oarc(fout);
      oarc << M << N << K << L << Le << Lt << D;
      oarc << *g << validation_graph << test_graph;
      printf("Done!\n");
      fout.close();
      exit(0);
    }

  } else {
    char filename[256];
    sprintf(filename, "%s%d.graph", infile.c_str(), D);
    std::ifstream fin(filename, std::fstream::binary);
    graphlab::iarchive iarc(fin);
    iarc >> M >> N >> K >> L >> Le >> Lt >> D;
    printf("Loading graph from file\n");
    iarc >> glcore.graph() >> validation_graph >> test_graph;
    g=&glcore.graph();
    printf("Matrix size is: %dx %dx %d D=%d\n", M, N, K, D);   
    printf("Creating %d edges...\n", L);
  }
  

  if (loadfactors){
    import_uvt_from_file();
  }


  if (stats){
    calc_stats(TRAINING);
    calc_stats(VALIDATION);
    calc_stats(TEST);
    exit(0);
  }

  printf("setting regularization weight to %g\n", LAMBDA);
  pU=pV=LAMBDA;
  glcore.set_engine_options(clopts); 

  if (tensor)
    dp = GenDiffMat(K)*pT;
  if (debug)
    std::cout<<dp<<std::endl;

  std::vector<vertex_id_t> um;
  for (int i=0; i< M+N; i++)
    um.push_back(i);
 
  // add update function for user and movie nodes (tensor dims 1+2) 
  glcore.add_tasks(um, user_movie_nodes_update_function, 1);
  
  std::vector<vertex_id_t> tv;
  if (tensor){
    for (int i=M+N; i< M+N+K; i++)
      tv.push_back(i);
    // add update function for time nodes (tensor dim 3)
    glcore.add_tasks(tv, time_node_update_function, 1);
  }

  
  printf("%s for %s (%d, %d, %d):%d.  D=%d\n", BPTF?"BPTF":"PTF_ALS", tensor?"tensor":"matrix", M, N, K, L, D);
  
  if (!BPTF)
    printf("pU=%g, pV=%g, pT=%g, muT=%g, D=%d\n", pU, pV, pT, muT,D);  

  //if (BPTF)
  init_self_pot(); //init anyway
  init_pmf();

  double res, res2;
  double rmse =  calc_rmse(g, false, res);
  printf("complete. Obj=%g, TRAIN RMSE=%0.4f TEST RMSE=%0.4f.\n", calc_obj(res), rmse, calc_rmse(&validation_graph, true, res2));


  if (BPTF){
    if (alpha == 0)
      sample_alpha(L);
    sample_U();
    sample_V();
    if (tensor) 
      sample_T();
  }

  /// Timing
  gt.start();
  g->finalize();  

  /**** START GRAPHLAB *****/
  glcore.start();

  // calculate final RMSE
  rmse =  calc_rmse_q(res);
  printf("Final result. Obj=%g, TRAIN RMSE= %0.4f TEST RMSE= %0.4f.\n", calc_obj(res),  rmse, calc_rmse(&validation_graph, true, res2));
  
  if (infile == "kddcup" || infile == "kddcup2")
    export_kdd_format(&test_graph, true);

  /**** POST-PROCESSING *****/
  double runtime = gt.current_time();
  printf("Finished in %lf \n", runtime);
      
  //timing counters
  for (int i=0; i<11; i++){
    if (counter[i] > 0)
      printf("Counters are: %d) %s, %g\n",i, countername[i], counter[i]); 
  }

  export_uvt_to_file();
}


/**
 * read edges from file, with support with multiple edges between the same pair of nodes (in different times)
 */
template<typename edgedata>
int read_mult_edges(FILE * f, int nodes, graph_type * g, bool symmetry = false){
     
  //typedef typename graph::edge_data_type edge_data;
  bool * flags = NULL;
  if (options == BPTF_TENSOR_MULT || options == ALS_TENSOR_MULT){
    flags = new bool[nodes];
    memset(flags, 0, sizeof(bool)*nodes);
  }
 
  unsigned int e;
  int rc = fread(&e,1,4,f);
  assert(rc == 4);
  printf("Creating %d edges...\n", e);
  assert(e>0);
  int total = 0;
  edgedata* ed = new edgedata[200000];
  int edgecount_in_file = e;
  while(true){
    //memset(ed, 0, 200000*sizeof(edge_float));
    rc = (int)fread(ed, sizeof(edgedata), _min(200000, edgecount_in_file - total), f);
    total += rc;

    for (int i=0; i<rc; i++){
      multiple_edges edges;
      edge_data edge;
      if (!ZERO)
        assert(ed[i].weight != 0); // && ed[i].weight <= 5);
      assert((int)ed[i].from >= 1 && (int)ed[i].from <= nodes);
      assert((int)ed[i].to >= 1 && (int)ed[i].to <= nodes);
      assert((int)ed[i].to != (int)ed[i].from);
      edge.weight = (double)ed[i].weight;
      edge.time = (int)((ed[i].time -1-truncating)/scaling);
 
      std::pair<bool, edge_id_t> ret;
      if (options != BPTF_TENSOR_MULT && options != ALS_TENSOR_MULT){//no support for specific edge returning on different times
        ret.first = false;
      }
      else if (flags[(int)ed[i].from-1] == true && flags[(int)ed[i].to-1] == true){
        ret = g->find((int)ed[i].from-1, (int)ed[i].to-1);
      }
      else ret.first = false;

      if (ret.first == false){
        edges.medges.push_back(edge); 
        g->add_edge((int)ed[i].from-1, (int)ed[i].to-1, edges); // Matlab export has ids starting from 1, ours start from 0
        if (options == BPTF_TENSOR_MULT || options == ALS_TENSOR_MULT){
          flags[(int)ed[i].from-1] = true;
          flags[(int)ed[i].to-1] = true;
        }
      }
      else {
        g->edge_data(ret.second).medges.push_back(edge);
      }
    } 
    printf(".");
    fflush(0);
    if (rc == 0 || total >= edgecount_in_file)
      break;
  }
  assert(total == (int)e);
  delete [] ed; ed = NULL;
  if (flags != NULL)
    delete[] flags;
  return e;
}


/* function that reads the tensor from file */
void load_pmf_graph(const char* filename, graph_type * g, testtype data_type,gl_types::core & glcore) {

  printf("Loading %s %s\n", filename, testtypename[data_type]);
  FILE * f = fopen(filename, "r");
  if (data_type!=TRAINING && f == NULL){//skip optional files, if the file is missing
    printf("skipping file\n");
    return;
  }

  assert(f!= NULL);

  fread(&M,1,4,f);//movies
  fread(&N,1,4,f);//users/
  fread(&K,1,4,f);//time
  assert(K>=1);
  assert(M>=1 && N>=1); 

  K=ceil((K-truncating)/scaling);

  //if (data_type==TRAINING)
  printf("Matrix size is: %d %d %d\n", M, N, K);
 
  vertex_data vdata;

  // add M movie nodes (tensor dim 1)
  for (int i=0; i<M; i++){
    
    vdata.pvec = debug ? (itpp::ones(D)*0.1) : (itpp::randu(D)*0.1);
    g->add_vertex(vdata);
    if (debug && (i<= 5 || i == M-1))
      debug_print_vec("U: ", vdata.pvec, D);
  }
  
  // add N user node (tensor dim 2) 
  for (int i=0; i<N; i++){
    vdata.pvec = debug ? (itpp::ones(D)*0.1) : (itpp::randu(D)*0.1);
    g->add_vertex(vdata);
    if (debug && (i<=5 || i==N-1))
      debug_print_vec("V: ", vdata.pvec, D);
  }
  
  if (data_type==TRAINING && tensor){
    //init times
    times = new vertex_data[K];
    vec tones = itpp::ones(D)*(K==1?1:0.1);
    //add T time node (tensor dim 3)
    for (int i=0; i<K; i++){
      times[i].pvec =tones;
      g->add_vertex(times[i]);
      if (debug && (i <= 5 || i == K-1))
        debug_print_vec("T: ", times[i].pvec, D);
    }
  }
  
  // read tensor non zero edges from file
  int val = 0; 
  if (!FLOAT) 
    val = read_mult_edges<edge_double>(f, M+N, g);
  else val = read_mult_edges<edge_float>(f,M+N, g);

  switch(data_type){
  case TRAINING: L= val; break;
  case VALIDATION: Le = val; break; 
  case TEST: Lt = val; break;
  }  

  if (data_type==TRAINING && tensor && K>1) 
    edges = new std::vector<edge_id_t>[K]();

        

  //verify edges
  for (int i=M; i < M+N; i++){
    foreach(graphlab::edge_id_t eid, g->in_edge_ids(i)){          
      multiple_edges & tedges= g->edge_data(eid);
      int from = g->source(eid);
      int to = g->target(eid);
      assert(from < M);
      assert(to >= M && to < M+N);

      for (int j=0; j< (int)tedges.medges.size(); j++){
        edge_data & data= tedges.medges[j];
	if (!ZERO)
          assert(data.weight != 0);  
        assert(data.time < K);
  
        if (K > 1 && data_type==TRAINING && tensor)
          edges[(int)data.time].push_back(eid);
      }
    }
  }
  
  //verify that the correct number of edges where added into the graph 
  if (data_type == TRAINING){
    for (int i=0; i<M+N; i++){
      vertex_data &vdata = g->vertex_data(i);
      if (i < M)
        vdata.num_edges = count_edges(g->out_edge_ids(i));
      else
        vdata.num_edges = count_edges(g->in_edge_ids(i));
    }
  }
 
  if (data_type==TRAINING && tensor && K>1){
    int cnt = 0;
    for (int i=0; i<K; i++){
      cnt+= edges[i].size();
    }
    assert(cnt == L);
  }

  fclose(f);
}


//main function 
int main(int argc,  char *argv[]) {

  global_logger().set_log_level(LOG_INFO);
  global_logger().set_log_to_console(true);

  infile = argv[1];

  if (infile == "" || argc <= 2) {
    std::cout << "PMF <input file> <run mode [0-4]>\n";
    return 0;
  }
  
  // select tun mode
  options = (runmodes)atoi(argv[2]);
  printf("Setting run mode %s\n", runmodesname[options]);
  switch(options){
  case ALS_MATRIX:
    // iterative matrix factorization
    tensor = false; BPTF = false;
    break;
  case BPTF_TENSOR:
    // MCMC tensor factorization
    tensor = true; BPTF = true;
    break;
    //MCMC matrix factorization
  case BPTF_MATRIX:
    tensor = false; BPTF = true;
    break;
    // tensor factorization , allow for multiple edges between user and movie in different times
  case BPTF_TENSOR_MULT:
    tensor = true; BPTF = true;
    break;
    // tensor factorization
  case ALS_TENSOR_MULT:
    tensor = true; BPTF = false;
    break;
  default:
    assert(0);
  }

  logger(LOG_INFO, "%s starting\n",runmodesname[options]);
  start(argc, argv);
}

// CALC SOME STATISTICS ABOUT MATRIX / TENSOR 
void calc_stats(testtype type){
  graph_type * gr = NULL;
  switch(type){
  case TRAINING: gr = g; break;
  case VALIDATION: gr = &validation_graph; break;
  case TEST: gr = &test_graph; break;
  }

  if (gr->num_vertices() == 0){
    printf("%s is missing, skipping data\n", testtypename[type]);
    return;
  } 

  if (tensor && type == TRAINING){
    int firsttimeused=-1;
    int lasttimeused=-1;
    for (int i=0; i<K; i++){
      if (edges[i].size() > 0)
        firsttimeused = i;
    }
    for (int i=K-1; i>=0; i--){
      if (edges[i].size() > 0)
        lasttimeused = i;

    }
    printf("Out of total %d time components, first used is %d, last used is %d\n", K, firsttimeused, lasttimeused);
  }	

  double avgval=-1, minval=1e100, maxval=-1e100;
  double avgtime=-1, mintime=1e100, maxtime=-1e100;
  double minV=1e100, maxV=-1e100, minU=1e100, maxU=-1e100;
  int moviewithoutedges = 0;
  int userwithoutedges = 0;
  int numedges = 0;
  for (int i=M; i< M+N; i++){ 
    const vertex_data * data = &gr->vertex_data(i);
    if (itpp::min(data->pvec) < minU)
      minU = itpp::min(data->pvec);
    if (itpp::max(data->pvec) > maxU)
      maxU = itpp::max(data->pvec);
    if (gr->in_edge_ids(i).size() == 0)
      moviewithoutedges++;
    foreach(edge_id_t iedgeid, gr->in_edge_ids(i)) {
            
      multiple_edges & edges = gr->edge_data(iedgeid);
      vertex_data * pdata = &gr->vertex_data(gr->source(iedgeid)); 
      for (int j=0; j< (int)edges.medges.size(); j++){     
        numedges++;
        edge_data & data = edges.medges[j];
        avgval += data.weight;
        avgtime += data.time;
        if (data.weight<minval)
          minval=data.weight;
        if (data.time <mintime)
          mintime = data.time;
        if (data.weight>maxval)
          maxval=data.weight;
        if (data.time > maxtime)
          maxtime =data.time;
	        
      }  
	
    }
  }
  for (int i=0; i< M; i++){ 
    const vertex_data * data = &gr->vertex_data(i);
    if (itpp::min(data->pvec) < minV)
      minV = itpp::min(data->pvec);
    if (itpp::max(data->pvec) > maxV)
      maxV = itpp::max(data->pvec);
	 
    if (gr->out_edge_ids(i).size() == 0)
      userwithoutedges++;
  }
 
  avgval /= numedges;
  avgtime /= numedges; 
  printf("%s Avg matrix value %g min val %g max value %g\n", testtypename[type],avgval, minval, maxval);
  printf("%s Avg time value %g min val %g max value %g\n", testtypename[type], avgtime, mintime, maxtime);
  printf("%s User without edges: %d movie without edges: %d\n", testtypename[type], userwithoutedges, moviewithoutedges);
  printf("%s Min V: %g Max V: %g Min U: %g, Max U: %g \n", testtypename[type], minV, maxV, minU, maxU);

  switch(type){
  case TRAINING: assert(numedges==L); break;
  case VALIDATION: assert(numedges==Le); break;
  case TEST: assert(numedges==Lt); break;
  }
}

//
//The input prediction file should contain 6005940 lines, corresponding
//to the 6005940 user-item pairs in the test set.
//Each line contains a predicted score (a real number between 0 and 100).
//The generated output file can be submitted to the KDD-Cup'11 evaluation
//system.


void export_kdd_format(graph_type * _g, bool dosave) {

  bool debugkdd = true;
  assert(_g != NULL);
  if (!dosave)
    assert(BPTF);	

  FILE * outFp = NULL;
  if (dosave){
    printf("Exporting KDD cup test graph: %s\n", (infile+"t.kdd.out").c_str());
    outFp = fopen((infile+"t.kdd.out").c_str(), "w");
    assert(outFp);
  }
  const int ExpectedTestSize = 6005940;

  int lineNum = 0;
  double prediction;
  double sumPreds=0;


  for (int i=0; i< M; i++){ //TODO: optimize to start from N?
    vertex_data & data = g->vertex_data(i);
    foreach(edge_id_t iedgeid, _g->out_edge_ids(i)) {
            
      multiple_edges & edges = _g->edge_data(iedgeid);
      vertex_data & pdata = g->vertex_data(_g->target(iedgeid)); 
      for (int j=0; j< (int)edges.medges.size(); j++){       
        assert(j==0); 
        edge_data & edge = edges.medges[j];
        if (!ZERO)
          assert(edge.weight != 0);

        prediction = 0;
        rmse(data.pvec, pdata.pvec, tensor? (&times[(int)edge.time].pvec):NULL, D, edge.weight, prediction);
        if (BPTF && iiter > BURN_IN)
          edge.avgprd += prediction;

        if (debugkdd && (i== M || i == M+N-1))
          cout<<lineNum<<") prediction:"<<prediction<<endl; 
        //assert(add<25 && add>= 0);
          
        if (BPTF && iiter > BURN_IN){
          //add = powf((edge.avgprd / (iiter - BURN_IN)) - edge.weight, 2);
          prediction = (edge.avgprd / (iiter - BURN_IN));
        }
           
        if (prediction<0)
          prediction=0;
        else if (prediction>100)
          prediction=100; 
        unsigned char roundScore = (unsigned char)(2.55*prediction + 0.5); 
        if (dosave)
          fwrite(&roundScore,1,1,outFp);
        sumPreds += prediction;

        lineNum++; 
      }
    }
  }

  assert(lineNum==ExpectedTestSize);  
  if (dosave){
    fclose(outFp);
    fprintf(stderr, "**Completed successfully (mean prediction: %lf)**\n",sumPreds/ExpectedTestSize);
  }
}













#include <graphlab/macros_undef.hpp>

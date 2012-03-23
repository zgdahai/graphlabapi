package org.graphlab.toolkits.matrix;

import org.graphlab.data.Vertex;

import cern.colt.matrix.tdouble.DoubleMatrix1D;

/**
 * Vertex that holds a vector.
 * 
 * In the context of a recommender system, a vertex is either a user
 * or an object of interest, and each vertex contains a vector of latent
 * factors.
 * 
 * @author Jiunn Haur Lim <jiunnhal@cmu.edu>
 */
public class VectorVertex extends Vertex {

  /**
   * Application vertex ID.
   * In the context of a recommender system, this will be the ID
   * of the user and the ID of the object.
   */
  private int mId;
  
  /** Data for this vertex */
  private DoubleMatrix1D mVector;
  
  public VectorVertex(){}
  
  /**
   * Constructs a new vector vertex with the given ID
   * @param id
   *          Used to identify this vertex within the application.
   */
  public VectorVertex(int id){
    mId = id;
  }
  
  @Override
  public int id(){
    return mId;
  }
  
  /**
   * Sets the data for this vertex.
   * @param vector
   */
  public void setVector(DoubleMatrix1D vector){
    mVector = vector;
  }
  
  public DoubleMatrix1D vector(){
    return mVector;
  }
  
  @Override
  public String toString(){
    return "id: " + mId + " value: " + mVector;
  }

  @Override
  public void setId(int id) {
    mId = id;    
  }
  
}

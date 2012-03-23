package org.graphlab.toolkits.matrix;

import java.io.FileReader;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

import org.graphlab.data.Vertex;
import org.jgrapht.WeightedGraph;
import org.jgrapht.graph.DefaultWeightedEdge;

import cern.colt.matrix.io.MatrixSize;
import cern.colt.matrix.io.MatrixVectorReader;

/**
 * Loads a matrix from a MatrixMarket file and populates the corresponding graph.
 * @author Jiunn Haur Lim
 */
public class MatrixLoader {

  /**
   * Constructs graph from an MM file (assuming general coordinate format with real values.)
   * @param graph       the graph to construct
   * @param filename    name of file containing the matrix
   * @throws IOException
   *            if file could not be read
   * @throws IllegalAccessException 
   *            if vertices could not be instantiated
   * @throws InstantiationException 
   *            if vertices could not be instantiated
   */
  public static <V extends Vertex> void
    loadGraph(
        WeightedGraph<V, DefaultWeightedEdge> graph,
        Class<V> vertexClass,
        String filename)
  throws IOException, InstantiationException, IllegalAccessException {
    
    if (null == graph || null == vertexClass || null == filename)
      throw new NullPointerException("graph, vertexClass, and filename cannot be null.");
    
    // read matrix metadata
    MatrixVectorReader reader = new MatrixVectorReader(new FileReader(filename));
    MatrixSize size = reader.readMatrixSize(reader.readMatrixInfo());
    
    // I really need this - hashcode trick doesn't work
    Map<Integer, V> vertices = new HashMap<Integer, V>();
    
    // iterate through file entries and construct graph
    int[] row = new int[1];
    int[] col = new int[1];
    double[] data = new double[1];
    for (int i=0; i<size.numEntries(); i++){
      
      reader.readCoordinate(row, col, data);
      
      V source = vertices.get(row[0]);
      if (null == source){
        source = vertexClass.newInstance();
        source.setId(row[0]);
        graph.addVertex(source);
        vertices.put(row[0], source);
      }
      
      V target = vertices.get(size.numRows()+col[0]);
      if (null == target){
         target = vertexClass.newInstance();
         target.setId(size.numRows()+col[0]);
         graph.addVertex(target);
         vertices.put(size.numRows()+col[0], target);
      }
      
      DefaultWeightedEdge edge = graph.addEdge(source, target);
      graph.setEdgeWeight(edge, data[0]);
      
    }
    
    // add the remaining vertices
    for (int i=0; i<size.numRows() + size.numColumns(); i++){
      V vertex = vertexClass.newInstance();
      vertex.setId(i);
      graph.addVertex(vertex);  // does nothing if already exists
    }
    
  }
  
}

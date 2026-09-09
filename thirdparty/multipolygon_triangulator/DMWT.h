#ifndef _DMWT_H_
#define _DMWT_H_

#include "Configure.h"
#include "DelaunayFaces.h"
#include "EdgeInfo.h"
#include "Point3.h"
#include "TriangleInfo.h"
#include "Vector3.h"
#include <cmath>
#include <fstream>
#include <stdio.h>
#include <string>
#include <vector>

#define MWT_RIGHT 1 //[a,b]
#define MWT_LEFT 0  //[-infinity,a)&(b,+infinity]
#define triangle(t, i) tris[t * 3 + i]
#define mwt_pt(v) Point3(points[v * 3], points[v * 3 + 1], points[v * 3 + 2])
#define leftEdgeInd(ei, i) edgeInfoList[ei]->leftEdgeInd[i]
#define leftTris(ei, i) edgeInfoList[ei]->leftTris[i]
#define rightEdgeInd(ei, i) edgeInfoList[ei]->rightEdgeInd[i]
#define rightTris(ei, i) edgeInfoList[ei]->rightTris[i]

namespace mwt {

class DMWT {
public:
  DMWT();
  DMWT(int ptn, double* pts, double* deGenPts, bool isdegen);
  DMWT(int ptn, double* pts, double* deGenPts, float* norms, bool isdegen);
  ~DMWT();
  void preprocess();

  // ENG-82 follow-up: sub-stage probes for preprocess() timing. The cassie
  // harness in cassie_triangulator.cpp calls these in place of preprocess()
  // when CASSIE_TRIANGULATOR_PROFILE is set, then reads last_delaunay_us to
  // separate Delaunay3D cost from the rest of genTriCandidates. The probes
  // forward to the protected implementations; no behavioural change.
  void genTriCandidatesProbe() { genTriCandidates(); }
  void buildListProbe()        { buildList(); }
  uint64_t last_delaunay_us = 0;

  // Cross-stage Delaunay reuse: MingCurve already ran the same Delaunay3D
  // on the same post-EP points. When set, genTriCandidates copies these
  // and skips its own delaunay.compute() call. The pointer is non-owning;
  // caller keeps the data alive until after preprocess() returns.
  void setPresuppliedTris(const int* p_tris, int p_count) {
    presupplied_tris_   = p_tris;
    presupplied_tris_n_ = p_count;
  }

  bool start();
  void setWeights(float wtri, float wedge, float wbitri, float wtribd, float wwst);

  bool reLoadNormalFile(const char* normalfile);
  void statistics();
  void setRound(int r);
  float optimalCost;
  void setDot(bool isdot1);
  void clearTiling();
  void setPointLimit(int limit);

  //------------- for cycle breaking project --------------//
  void getResult(double** outFaces, int* outNum, double** outPoints, float** outNorms, int* outPn, bool dosmooth,
                 int subd, int laps);
  void getResult(int** outFaces, int* outNum, double** outPoints, float** outNorms, int* outPn);

  // Fill flat std::vector arrays — no Eigen dependency.
  // verts: numofpoints * 3 doubles (x,y,z interleaved)
  // faces: numoftilingtris * 3 ints (vertex indices, three per triangle)
  void getResultVectors(std::vector<double>& verts, std::vector<int>& faces) const;

  bool EXPSTOP;

protected:
  //-------------variables--------------//
  char* filename;
  cassie::DelaunayFaces delaunay;
  int round;
  int startEdge;
  bool withNormal;
  bool useBiTri;
  bool hasIntersect; // Intersect
  bool hasIntersect2;
  char dot;
  int DMWT_LIMIT;

  int numofpoints;
  int numoftris;
  int numofedges;
  int numofnormals;
  int numoftilingtris;
  int* tris;
  // True when genTriCandidates allocated tris itself (3-point fast path,
  // presupplied copy, 2D fallback); false when tris aliases the
  // delaunay member's trifacelist, which delaunay frees.
  bool owns_tris;
  double* points;
  double* deGenPoints;
  float* normals;
  EdgeInfo** edgeInfoList;
  TriangleInfo** triangleInfoList;
  int* tiling;

  float weightTri;
  float weightEdge;
  float weightBiTri;
  float weightTriBd;
  bool useWorstDihedral;

  int** ehash;
  int** ehashLeft;
  int** ehashRight;

  int intsTriInd[2];

  // Cross-stage Delaunay reuse — see setPresuppliedTris().
  const int* presupplied_tris_   = nullptr;
  int        presupplied_tris_n_ = 0;

  //-------------functions--------------//
  int scanTrianglesOnce();
  char getSide(int v1, int v2, int v3);

  float measureEdge(int v1, int v2);
  float measureTri(int v1, int v2, int v3);
  float measureBiTri(int v1, int v2, int p, int q);
  float measureTriBd(int v1, int v2, int v3, int ni);
  float costTri(float measure);
  float costEdge(float measure);
  float costBiTri(float measure);
  float costTriBd(float measure);

  bool tileSegment(int eind, char side, int ti, float& thiscost, int& thistile);
  void buildTiling(int eind, char side, int ti);
  void buildList();
  void genTriCandidates();
  char getSide(int i);

  void initBasics();
  bool triShareEdge(int trii, int trij);

  // ------------------- for cycle project -----------------//
  bool isDeGen; // degenerated cases: plane
  void saveTilingObj(char* tilefile, const double* finalPoints);
  void saveMeshObj(char* tilefile, int nT, const double* mesh);

  //-------------evaluations--------------//
  float timeReadIn;
  float timePreprocess;
  float timeMWT;
  float timeTotal;
  float timeDelaunay;
  float getSize();

  // Not used for now
  int numofcurves;
  bool isOpen;
  int capacity;
  int maxFacePerEdge;
};

}  // namespace mwt

#endif

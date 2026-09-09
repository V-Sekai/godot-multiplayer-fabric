#include "DMWT.h"

#include "delaunay_geogram.h"

#include <chrono>
#include <cmath>
#include <iostream>

// #include "SmoothPatch.h"

using namespace std;

namespace mwt {

DMWT::DMWT() {
  EXPSTOP          = false;
  hasIntersect     = false;
  hasIntersect2    = false;
  useWorstDihedral = false;
  withNormal       = false;
  useBiTri         = true;
  dot              = 2;
  timePreprocess   = 0.0;
  timeMWT          = 0.0;
  timeDelaunay     = 0.0;
  round            = 0;
  numoftilingtris  = 0;
}

DMWT::DMWT(int ptn, double* pts, double* deGenPts, bool isdegen) {

  initBasics();

  isDeGen     = isdegen;
  filename    = (char*)"DMWT_NULL.curve";
  numofpoints = ptn;
  numoftris   = 0;
  points      = new double[3 * numofpoints];
  tiling      = new int[numofpoints - 2];
  // read point set data
  for (int i = 0; i < numofpoints; i++) {
    points[i * 3]     = pts[i * 3];
    points[i * 3 + 1] = pts[i * 3 + 1];
    points[i * 3 + 2] = pts[i * 3 + 2];
  }

  if (isDeGen) {
    deGenPoints = new double[3 * (size_t)(unsigned)numofpoints];
    for (int i = 0; i < numofpoints; i++) {
      deGenPoints[i * 3]     = deGenPts[i * 3];
      deGenPoints[i * 3 + 1] = deGenPts[i * 3 + 1];
      deGenPoints[i * 3 + 2] = deGenPts[i * 3 + 2];
    }
  }
}

DMWT::DMWT(int ptn, double* pts, double* deGenPts, float* norms, bool isdegen) {

  initBasics();

  isDeGen = isdegen;
  /// init points///
  filename    = (char*)"DMWT_NULL.curve";
  numofpoints = ptn;
  numoftris   = 0;
  points      = new double[3 * numofpoints];
  tiling      = new int[numofpoints - 2];
  // read point set data
  for (int i = 0; i < numofpoints; i++) {
    points[i * 3]     = pts[i * 3];
    points[i * 3 + 1] = pts[i * 3 + 1];
    points[i * 3 + 2] = pts[i * 3 + 2];
  }

  if (isDeGen) {
    deGenPoints = new double[3 * (size_t)(unsigned)numofpoints];
    for (int i = 0; i < numofpoints; i++) {
      deGenPoints[i * 3]     = deGenPts[i * 3];
      deGenPoints[i * 3 + 1] = deGenPts[i * 3 + 1];
      deGenPoints[i * 3 + 2] = deGenPts[i * 3 + 2];
    }
  }

  /// init normals///
  withNormal = true;
  normals    = new float[3 * numofpoints];
  for (int i = 0; i < numofpoints; i++) {
    normals[i * 3]     = norms[i * 3];
    normals[i * 3 + 1] = norms[i * 3 + 1];
    normals[i * 3 + 2] = norms[i * 3 + 2];
  }
}

DMWT::~DMWT() {
  // `points` is caller-owned. `tris` ownership depends on which
  // genTriCandidates path ran: the 3-point fast path, the presupplied
  // copy, and the 2D-fallback all allocate fresh int[] that DMWT owns
  // (owns_tris == true); the 3D path aliases delaunay.trifacelist,
  // which the `delaunay` member frees itself.
  // delete [] filename;
  if (!EXPSTOP) {
    delete[] tiling;
    // Both DMWT constructors allocate `points = new double[3*numofpoints]` and
    // copy the caller's data in, so DMWT owns this buffer (the old "caller-owned"
    // comment was wrong) and must free it.
    delete[] points;
    if (withNormal)
      delete[] normals;
    // buildList() allocates every edgeInfoList[i]/triangleInfoList[i] element
    // unconditionally (preprocess() always calls it, degenerate curves
    // included), so the elements must be freed unconditionally too. Guarding
    // these loops on !isDeGen leaked one EdgeInfo/TriangleInfo per edge/tri for
    // every degenerate-curve triangulation. The pointers are null-initialized
    // in initBasics, so delete on an unpopulated slot is a no-op.
    for (int i = 0; i < numofedges; i++)
      delete edgeInfoList[i];
    for (int i = 0; i < numoftris; i++)
      delete triangleInfoList[i];
  }
  // The element loops above free the EdgeInfo/TriangleInfo objects but
  // not the pointer arrays themselves. All of these are null-initialized
  // in initBasics, so delete[] is safe on every early-exit path.
  delete[] edgeInfoList;
  delete[] triangleInfoList;
  delete[] deGenPoints;
  if (owns_tris)
    delete[] tris;
}

void DMWT::initBasics() {

  EXPSTOP          = false;
  hasIntersect     = false;
  hasIntersect2    = false;
  DMWT_LIMIT       = 100000;
  withNormal       = false;
  useBiTri         = true;
  dot              = 2;
  useWorstDihedral = false;
  timePreprocess   = 0.0;
  timeMWT          = 0.0;
  timeDelaunay     = 0.0;
  round            = 0;
  numoftilingtris  = 0;
  isDeGen          = false;
  owns_tris        = false;
  tris             = nullptr;
  deGenPoints      = nullptr;
  edgeInfoList     = nullptr;
  triangleInfoList = nullptr;
}
//==================================Weight Functions============================//

void DMWT::setWeights(float wtri, float wedge, float wbitri, float wtribd, float wwst) {
  weightTri   = wtri;
  weightEdge  = wedge;
  weightBiTri = wbitri;
  weightTriBd = wtribd;
  if ((weightBiTri == 0.0f) && (weightTriBd == 0.0f) && (wwst == 0.0f))
    useBiTri = false;
  if (wwst != 0.0f)
    useWorstDihedral = true;
}

float DMWT::costTri(float measure) {
  return weightTri * measure;
}
float DMWT::costEdge(float measure) {
  return weightEdge * measure;
}
float DMWT::costBiTri(float measure) {
  return weightBiTri * measure;
}
float DMWT::costTriBd(float measure) {
  return weightTriBd * measure;
}

float DMWT::measureEdge(int v1, int v2) {
  Point3 p1 = mwt_pt(v1);
  Point3 p2 = mwt_pt(v2);
  return (float)(p2 - p1).length();
}
float DMWT::measureTri(int v1, int v2, int v3) {
  Point3 p1 = mwt_pt(v1);
  Point3 p2 = mwt_pt(v2);
  Point3 p3 = mwt_pt(v3);
  return (float)((p2 - p1) ^ (p3 - p2)).length() / 2;
}
float DMWT::measureBiTri(int v1, int v2, int p, int q) {
  Point3 p1  = mwt_pt(v1);
  Point3 p2  = mwt_pt(v2);
  Point3 pp  = mwt_pt(p);
  Point3 pq  = mwt_pt(q);
  Vector3 n1 = (p2 - pp) ^ (p1 - p2);
  n1.normalize();
  Vector3 n2 = (p2 - p1) ^ (pq - p2);
  n2.normalize();
  float cosvalue = (float)(n1 * n2);
  cosvalue       = cosvalue < 1.0 ? cosvalue : 1.0f - FLT_EPSILON;
  cosvalue       = cosvalue > -1.0 ? cosvalue : -1.0f + FLT_EPSILON;
  return acos(cosvalue);
}
float DMWT::measureTriBd(int v1, int v2, int v3, int ni) {
  if (!withNormal)
    return 0.0;
  Point3 p1  = mwt_pt(v1);
  Point3 p2  = mwt_pt(v2);
  Point3 p3  = mwt_pt(v3);
  Vector3 n  = Vector3(normals[ni * 3], normals[ni * 3 + 1], normals[ni * 3 + 2]);
  Vector3 nt = (p2 - p1) ^ (p3 - p2);
  nt.normalize();
  float cosvalue = (float)(nt * n);
  cosvalue       = cosvalue < 1.0 ? cosvalue : 1.0f - FLT_EPSILON;
  cosvalue       = cosvalue > -1.0 ? cosvalue : -1.0f + FLT_EPSILON;
  return acos(cosvalue);
}

//==================================Tiling Functions============================//

void DMWT::preprocess() {
  genTriCandidates();
  buildList();
}

void DMWT::clearTiling() {
  for (int i = 0; i < numoftris; i++) {
    triangleInfoList[i]->optCost[0] = FLT_MIN;
    triangleInfoList[i]->optCost[1] = FLT_MIN;
    triangleInfoList[i]->optCost[2] = FLT_MIN;
  }
  hasIntersect    = false;
  hasIntersect2   = false; // intersect
  useBiTri        = true;
  dot             = 1;
  timeMWT         = 0.0;
  timeTotal       = 0.0;
  timeDelaunay    = 0.0;
  numoftilingtris = 0;
}

bool DMWT::start() {
  round++;
  numoftilingtris = 0;
  if (startEdge < 0) {
    // No candidate triangles were produced (empty Delaunay result or
    // boundary with no valid edge between vertex 0 and vertex 1).
    cout << "NOTICE: No solution!" << endl;
    return false;
  }
  int optTile;
  tileSegment(startEdge, 0, -1, optimalCost, optTile);
  buildTiling(startEdge, 0, optTile);

  if (numoftilingtris != numofpoints - 2) {
    cout << "NOTICE: No solution!" << endl;
    return false;
  }
  return true;
}

void DMWT::buildTiling(int eind, char side, int ti) {
  EdgeInfo* einfo;
  TriangleInfo* tinfo;
  int tind, ei;
  int* tlist;
  char newside;
  einfo = edgeInfoList[eind];
  // 1. hit boudary, return assert(ti>=0 <=?)
  if (ti == -1)
    return;
  if (side == MWT_RIGHT) {
    tlist = einfo->rightTris;
    ei    = einfo->rightEdgeInd[ti];
  } else {
    tlist = einfo->leftTris;
    ei    = einfo->leftEdgeInd[ti];
  }
  tind  = tlist[ti];
  tinfo = triangleInfoList[tind];
  // tiling.push_back(tind);
  tiling[numoftilingtris] = tind;
  numoftilingtris++;
  for (int ej = 0; ej < 3; ej++) {
    if (ej == ei)
      continue;
    if (tinfo->optCost[ej] == FLT_MIN) {
      cout << "Error in building Tiling! Tri:" << tind << " Edgei:" << ej << endl;
      return;
    }
    newside = ej == 2 ? 1 : 0;
    buildTiling(tinfo->edgeIndex[ej], 1 - newside, tinfo->optTile[ej]);
  }
}

//==================================List Related Functions============================//

char DMWT::getSide(int v1, int v2, int v3) {
  return (v3 < v2 && v3 > v1);
}
char DMWT::getSide(int i) {
  if (i == 2)
    return 1;
  return 0;
}

/// return # of edges
int DMWT::scanTrianglesOnce() {
  int edgenum = 0;
  char side;
  int min, max, sum = 0, mid, v, v1, v2;
  for (int t = 0; t < numoftris; t++) {
    // sort vertices
    min = INT_MAX;
    max = -1;
    sum = 0;
    for (int i = 0; i < 3; i++) {
      v   = triangle(t, i);
      min = min < v ? min : v;
      max = max > v ? max : v;
      sum += v;
    }
    mid            = sum - min - max;
    triangle(t, 0) = min;
    triangle(t, 1) = mid;
    triangle(t, 2) = max;

    for (int i = 0; i < 3; i++) {
      if (i == 2) {
        v1 = triangle(t, 0);
        v2 = triangle(t, 2);
      } else {
        v1 = triangle(t, i);
        v2 = triangle(t, i + 1);
      }
      side = getSide(v1, v2, sum - v1 - v2);
      if (ehash[v1][v2] == -1) {
        ehash[v1][v2] = edgenum;
        edgenum++;
      }
      if (side == 0)
        ehashLeft[v1][v2] += 1;
      else
        ehashRight[v1][v2] += 1;
    }
  }
  return edgenum;
}

void DMWT::buildList() {
  // initialize ehash
  ehash      = new int*[numofpoints];
  ehashLeft  = new int*[numofpoints];
  ehashRight = new int*[numofpoints];
  for (int i = 0; i < numofpoints; i++) {
    ehash[i]      = new int[numofpoints];
    ehashLeft[i]  = new int[numofpoints];
    ehashRight[i] = new int[numofpoints];
    for (int j = 0; j < numofpoints; j++) {
      ehash[i][j]      = -1;
      ehashLeft[i][j]  = 0;
      ehashRight[i][j] = 0;
    }
  }
  triangleInfoList = new TriangleInfo*[numoftris];
  for (int i = 0; i < numoftris; i++) {
    triangleInfoList[i] = new TriangleInfo();
  }
  // scan triangle list once to assign index of edges
  numofedges = scanTrianglesOnce();
  // create edgeInfoList & triangleInfoList
  startEdge    = ehash[0][1];
  edgeInfoList = new EdgeInfo*[numofedges];
  for (int i = 0; i < numofedges; i++) {
    edgeInfoList[i] = new EdgeInfo();
  }

  int v1, v2, ei = -1, left, right;
  char newside;
  // scan all triangles, setup triangleInfoList and most of edgeInfoList except BiTri information
  // initialize all edgeInfoList and set its left/rightsize, left/rightEdgeInd and left/rightTris
  for (int t = 0; t < numoftris; t++) {
    for (int i = 0; i < 3; i++) {
      if (i == 2) {
        v1 = triangle(t, 0);
        v2 = triangle(t, 2);
      } else {
        v1 = triangle(t, i);
        v2 = triangle(t, i + 1);
      }
      ei = ehash[v1][v2];
      // after process one edge, set ehashLeft[v1][v2]&ehashRight[v1][v2] to
      // the next slot for inserting a triangle
      if (edgeInfoList[ei]->leftsize == -1) {
        left                           = ehashLeft[v1][v2];
        right                          = ehashRight[v1][v2];
        edgeInfoList[ei]->v1           = v1;
        edgeInfoList[ei]->v2           = v2;
        edgeInfoList[ei]->leftsize     = left;
        edgeInfoList[ei]->rightsize    = right;
        edgeInfoList[ei]->leftEdgeInd  = new char[left];
        edgeInfoList[ei]->leftTris     = new int[left];
        edgeInfoList[ei]->rightEdgeInd = new char[right];
        edgeInfoList[ei]->rightTris    = new int[right];
        ehashLeft[v1][v2]              = 0;
        ehashRight[v1][v2]             = 0;
      }
      left    = ehashLeft[v1][v2];
      right   = ehashRight[v1][v2];
      newside = i == 2 ? 1 : 0;
      if (newside == 0) {
        leftTris(ei, left)               = t;
        leftEdgeInd(ei, left)            = i;
        triangleInfoList[t]->triIndex[i] = left;
        ehashLeft[v1][v2]++;
      } else {
        rightTris(ei, right)             = t;
        rightEdgeInd(ei, right)          = i;
        triangleInfoList[t]->triIndex[i] = right;
        ehashRight[v1][v2]++;
      }
      triangleInfoList[t]->edgeIndex[i] = ei;
    }
  }

  for (int i = 0; i < numofpoints; i++) {
    delete[] ehash[i];
    delete[] ehashLeft[i];
    delete[] ehashRight[i];
  }
  delete[] ehashLeft;
  delete[] ehashRight;
  delete[] ehash;
}

//==================================Delaunay Functions==========================//

void DMWT::genTriCandidates() {
  // Generates candidate triangles as the unique triangular facets of
  // the 3D Delaunay tetrahedralization of `points`. Backed by Geogram
  // since the AGPL TetGen swap; the rest of DMWT consumes `tris` as a
  // flat int[numoftris*3] of vertex indices and does not care about
  // facet orientation, so the wrapper emits sorted-ascending triples.
  //
  // 3-point fast path: a triangle's own face is its only candidate.
  // The 3D Delaunay needs >= 4 points so it would otherwise return
  // empty, leaving the downstream tile_segment recursion with no
  // candidate -- the V-Sekai DMWT (which this one replaced) had the
  // same special case. cassie-triangulation upstream skipped DMWT
  // entirely for nB=3 inside Triangulate(); we need this here because
  // PolygonTriangulationGodot.create() calls DMWT directly.
  if (numofpoints == 3) {
    numoftris = 1;
    tris      = new int[3];
    owns_tris = true;
    tris[0]   = 0;
    tris[1]   = 1;
    tris[2]   = 2;
    return;
  }

  // Cross-stage Delaunay reuse: caller already ran Delaunay3D on the same
  // post-EP point set (typically MingCurve). Copy the trifacelist and skip
  // our own delaunay.compute() call. The DMWT pipeline owns tris[] and frees
  // it in the destructor, so allocate here even though we got the data
  // pre-computed.
  if (presupplied_tris_ != nullptr && presupplied_tris_n_ > 0) {
    last_delaunay_us = 0;
    numoftris        = presupplied_tris_n_;
    tris             = new int[numoftris * 3];
    owns_tris        = true;
    std::copy(presupplied_tris_,
              presupplied_tris_ + numoftris * 3,
              tris);
    return;
  }

  // Coplanarity test: scalar triple product of the first three edge
  // vectors. If the input is (near-)planar, Geogram's 3D Delaunay
  // produces zero tets and DMWT's downstream tile_segment recursion
  // walks invalid indices. Fall back to 2D Delaunay in that case.
  // The V-Sekai DMWT this replaced had the same branch; cassie-
  // triangulation's pipeline avoided it by perturbing planar inputs
  // through MingCurve, but PolygonTriangulationGodot.create() calls
  // DMWT directly without MingCurve.
  bool coplanar = false;
  if (numofpoints >= 4) {
    const double *p0 = &points[0];
    const double *p1 = &points[3];
    const double *p2 = &points[6];
    const double *p3 = &points[9];
    const double e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
    const double e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
    const double e3[3] = { p3[0]-p0[0], p3[1]-p0[1], p3[2]-p0[2] };
    const double cx = e2[1]*e3[2] - e2[2]*e3[1];
    const double cy = e2[2]*e3[0] - e2[0]*e3[2];
    const double cz = e2[0]*e3[1] - e2[1]*e3[0];
    const double volume6 = e1[0]*cx + e1[1]*cy + e1[2]*cz;
    coplanar = std::abs(volume6) < 1e-10;
  }

  // Always try the 3D Delaunay first (via DelaunayFaces, backed by Godot's
  // Delaunay3D with NO_SNAP|NO_FILTER). For MingCurve-perturbed near-flat
  // inputs this produces all tetrahedral faces as candidates — a richer set
  // than any 2D projection, and required for non-convex polygons.
  {
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = delaunay.compute(points, numofpoints);
    const auto t1 = std::chrono::steady_clock::now();
    last_delaunay_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    if (ok) {
      numoftris = delaunay.numberoftrifaces;
      tris      = delaunay.trifacelist; // delaunay-owned; freed by the member
      owns_tris = false;
      return;
    }
  }

  // 3D produced nothing (truly flat — tets cannot be formed from coplanar
  // points). Fall back to 2D Delaunay projected onto the dominant plane.
  if (coplanar) {
    // Project onto the two axes with the largest coordinate ranges.
    // CASSIE inputs are in the XZ plane (y=0); projecting to XY collapses
    // all points onto the x-axis when y=0. We find the flattest axis
    // (smallest range) and project onto the other two instead.
    double lo[3] = {points[0], points[1], points[2]};
    double hi[3] = {points[0], points[1], points[2]};
    for (int i = 1; i < numofpoints; ++i) {
      for (int a = 0; a < 3; ++a) {
        if (points[i*3+a] < lo[a]) lo[a] = points[i*3+a];
        if (points[i*3+a] > hi[a]) hi[a] = points[i*3+a];
      }
    }
    double span[3] = { hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2] };
    int flat = 0;
    if (span[1] < span[flat]) flat = 1;
    if (span[2] < span[flat]) flat = 2;
    int ax0 = (flat + 1) % 3;
    int ax1 = (flat + 2) % 3;

    std::vector<double> xy;
    xy.reserve(std::size_t(numofpoints) * 2u);
    for (int i = 0; i < numofpoints; ++i) {
      xy.push_back(points[i * 3 + ax0]);
      xy.push_back(points[i * 3 + ax1]);
    }
    int *face_idx = nullptr;
    int face_count = 0;
    if (cassie::delaunay_triangulate_2d_raw(xy.data(), numofpoints, &face_idx, &face_count) &&
            face_count > 0) {
      numoftris = face_count;
      tris      = face_idx; // delaunay_triangulate_2d_raw transfers ownership
      owns_tris = true;
    } else {
      numoftris = 0;
      tris      = nullptr;
    }
    return;
  }

  numoftris = 0;
  tris      = nullptr;
}
//==================================IO Read Functions============================//

//==================================IO Write Functions============================//
void DMWT::setPointLimit(int limit) {
  DMWT_LIMIT = limit;
  if (numofpoints > DMWT_LIMIT) {
    EXPSTOP = true;
  }
}
void DMWT::setRound(int r) {
  round = r;
}

//==================================Evaluation Functions============================//
// intersect
bool DMWT::triShareEdge(int trii, int trij) {
  int num = 0;
  int ipind[3];
  int jpind[3];
  for (int k = 0; k < 3; k++) {
    ipind[k] = tris[trii * 3 + k];
    jpind[k] = tris[trij * 3 + k];
  }
  for (int ii = 0; ii < 3; ii++) {
    for (int jj = 0; jj < 3; jj++) {
      if (ipind[ii] == jpind[jj])
        num++;
    }
  }
  if (num == 2)
    return true;
  return false;
}

void DMWT::saveTilingObj(char* tilefile, const double* finalPoints) {
  int n = numoftilingtris;
  std::ofstream writer(tilefile, std::ofstream::out);
  if (!writer.good())
    exit(1);
  writer << "# OBJ File Generated by MMWT\n";
  writer << "# Vertices: " << numofpoints << "\n";
  writer << "# Faces: " << n << "\n";
  // write vertices
  for (int i = 0; i < numofpoints; i++) {
    writer << "v " << finalPoints[i * 3] << " " << finalPoints[i * 3 + 1] << " " << finalPoints[i * 3 + 2] << "\n";
  }
  // write faces
  int t = 0;
  for (int i = 0; i < n; i++) {
    t = tiling[i];
    writer << "f " << triangle(t, 0) + 1 << " " << triangle(t, 1) + 1 << " " << triangle(t, 2) + 1 << "\n";
  }
  writer.close();
}
void DMWT::saveMeshObj(char* tilefile, int nT, const double* mesh) {
  std::ofstream writer(tilefile, std::ofstream::out);
  if (!writer.good())
    exit(1);
  writer << "# OBJ File Generated by MMWT\n";
  writer << "# Vertices: " << nT * 3 << "\n";
  writer << "# Faces: " << nT << "\n";
  // write vertices
  for (int i = 0; i < nT; i++) {
    for (int j = 0; j < 3; j++) {
      writer << "v " << mesh[i * 9 + j * 3] << " " << mesh[i * 9 + j * 3 + 1] << " " << mesh[i * 9 + j * 3 + 2] << "\n";
    }
  }
  // write faces
  for (int i = 0; i < nT; i++) {
    writer << "f " << i * 3 + 1 << " " << i * 3 + 2 << " " << i * 3 + 3 << "\n";
  }
  writer.close();
}

//================ for cycle breaking project=====================
void DMWT::getResult(double** outFaces, int* outNum, double** outPoints, float** outNorms, int* outPn, bool dosmooth,
                     int subd, int laps) {
  double* finalPoints;
  if (isDeGen) {
    finalPoints = deGenPoints;
  } else {
    finalPoints = points;
  }

  double* outTris;
  // 	if (dosmooth){
  //
  // 		SP::Mesh mesh,outputMesh;
  // 		mesh.allocateVertices(numofpoints);
  // 		mesh.allocateFaces(numoftilingtris);
  //
  // 		float *pPositions(mesh.getPositions()),
  // 			  *pNormals(mesh.getNormals());
  //
  // 		for(int i=0;i<numofpoints;++i){
  // 			pPositions[3*i] = (float)finalPoints[3*i];
  // 			pPositions[3*i+1] = (float)finalPoints[3*i+1];
  // 			pPositions[3*i+2] = (float)finalPoints[3*i+2];
  // 			pNormals[3*i] = (float)normals[3*i];
  // 			pNormals[3*i+1] = (float)normals[3*i+1];
  // 			pNormals[3*i+2] = (float)normals[3*i+2];
  // 		}
  //
  // 		int *pFaceIndices=mesh.getFaceIndices();
  // 		int nTempIndex(0);
  // 		for(int i=0; i<numoftilingtris; i++){
  // 			int triId = tiling[i];
  // 			for (int j=0;j<3;j++){
  // 				pFaceIndices[nTempIndex] = tris[triId*3+j];
  // 				++nTempIndex;
  // 			}
  // 		}
  //
  // 		SP::SmoothPatchSettings settings;
  // 		settings.mbConstrainNormals=true;
  // 		settings.mbRemesh=true;
  // 		settings.mfTension=0.0;
  // 		settings.mnNumSubdivisions=subd;
  // 		settings.mnNumLaplacianSmooths=laps;
  //
  // 		SP::SmoothPatchBuilder smoothPatchBuilder;
  //
  // 		smoothPatchBuilder.buildSmoothPatch(settings,mesh,outputMesh);
  //
  // 		*outNum = outputMesh.getFaceCount();
  // 		outTris=	new double[(*outNum)*9];
  // 		int *pInd = outputMesh.getFaceIndices();
  // 		float *pPoints=outputMesh.getPositions();
  // 		for(int i=0; i<(*outNum); i++){
  // 			for (int j=0;j<3;j++){
  // 				int pointID = pInd[i*3+j];
  // 				for(int k=0;k<3;k++){
  // 					outTris[i*9+j*3+k]=(double)pPoints[pointID*3+k];
  // 				}
  // 			}
  // 		}
  //
  // 		*outFaces =outTris;
  //
  // #if (SAVEOBJ==1)
  // 		saveMeshObj("smth.obj",outputMesh.getFaceCount(),outTris);
  // #endif
  //
  // 	} else
  {
    // return triangle point coordinates to Yixin
    *outNum = numoftilingtris;
    outTris = new double[numoftilingtris * 9];
    for (int i = 0; i < numoftilingtris; i++) {
      int triId = tiling[i];
      for (int j = 0; j < 3; j++) {
        int pointID = tris[triId * 3 + j];
        for (int k = 0; k < 3; k++) {
          outTris[i * 9 + j * 3 + k] = finalPoints[pointID * 3 + k];
        }
      }
    }
    *outFaces = outTris;

#if (SAVEOBJ == 1)
    saveMeshObj("nosmth.obj", numoftilingtris, outTris);
#endif
  }

  *outPn        = numofpoints;
  double* outPs = new double[numofpoints * 3];
  for (int i = 0; i < numofpoints * 3; i++) {
    outPs[i] = points[i];
  }
  *outPoints = outPs;

  if (withNormal) {
    float* outNs = new float[numofpoints * 3];
    for (int i = 0; i < numofpoints * 3; i++) {
      outNs[i] = normals[i];
    }
    *outNorms = outNs;
  }
}

void DMWT::getResult(int** outFaces, int* outNum, double** outPoints, float** outNorms, int* outPn) {
  [[maybe_unused]] double* finalPoints;
  if (isDeGen) {
    finalPoints = deGenPoints;
  } else {
    finalPoints = points;
  }

  int* outTris;
  int triId;

  // return triangle point coordinates to Yixin
  *outNum = numoftilingtris;
  outTris = new int[numoftilingtris * 3];
  for (int i = 0; i < numoftilingtris; i++) {
    triId = tiling[i];
    for (int j = 0; j < 3; j++) {
      outTris[i * 3 + j] = tris[triId * 3 + j];
    }
  }
  *outFaces = outTris;

#if (SAVEOBJ == 1)
  saveMeshObj("nosmth.obj", numoftilingtris, outTris);
#endif

  *outPn        = numofpoints;
  double* outPs = new double[numofpoints * 3];
  for (int i = 0; i < numofpoints * 3; i++) {
    outPs[i] = points[i];
  }
  *outPoints = outPs;

  if (withNormal) {
    float* outNs = new float[numofpoints * 3];
    for (int i = 0; i < numofpoints * 3; i++) {
      outNs[i] = normals[i];
    }
    *outNorms = outNs;
  }
}

void DMWT::getResultVectors(std::vector<double>& verts, std::vector<int>& faces) const {
  verts.resize(numofpoints * 3);
  for (int i = 0; i < numofpoints * 3; ++i)
    verts[i] = points[i];

  faces.resize(numoftilingtris * 3);
  for (int i = 0; i < numoftilingtris; ++i)
    for (int j = 0; j < 3; ++j)
      faces[i * 3 + j] = tris[tiling[i] * 3 + j];
}

}  // namespace mwt

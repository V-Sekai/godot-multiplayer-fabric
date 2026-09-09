#ifndef _MINGCURVE_H_
#define _MINGCURVE_H_

#include "Point3.h"
#include "Vector3.h"
#include "Configure.h"
#include "float.h"
#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include <stdio.h>

using namespace std;

namespace mwt {

class MingCurve {
public:
	MingCurve(const char* file, int limit, bool hasNorm);
	MingCurve(const double* inCurve,const int inNum, int limit, bool hasNorm);
	MingCurve(const double* inCurve,const float* inNrom, const int inNum, int limit, bool hasNorm);
	~MingCurve();

	int getNumOfPoints();
	double* getPoints();
	double* getDeGenPoints();
	char* getFilename();
	float* getNormal();

	//----------------Edge protection----------------//
	bool edgeProtect(bool isdmwt);
	void saveCurve(const char* curvefilein, double* pts, int num);

	// ENG-82: sub-stage probes for edgeProtect timing. Exposed so the
	// CASSIE_TRIANGULATOR_PROFILE harness in cassie_triangulator.cpp can
	// reproduce edgeProtect()'s body with per-stage timing. No behavioural
	// change — these forward to the existing private implementations.
	bool passTetGenProbe()        { return passTetGen(); }
	bool isProtectedProbe()       { return isProtected(); }
	void protectCornerProbe()     { protectCorner(); }
	void insertMidPointsProbe()   { insertMidPointsTetgen(); }
	bool getCurveAfterEPProbe()   { return getCurveAfterEP(); }
	void perturbPtsProbe(double ptb) { perturbPts(ptb); }

	// dmwt_prep follow-up: post-EP, MingCurve's cached Delaunay trifacelist
	// (indices into tempC[0..org_n-1]) is still valid for the post-EP point
	// set when no Steiner insertion happened — the points are the same up to
	// the newCurve permutation captured during getCurveAfterEP. This writes
	// a remapped trifacelist (post-EP indices) so DMWT::genTriCandidates can
	// skip its own Delaunay3D call. Returns false when the cache is stale,
	// Steiner points were inserted, or any reason DMWT must compute fresh.
	bool getRemappedDelaunayFaces(std::vector<int>& out) const;

	//-------------evaluations--------------//
	float timeReadIn;
	float timeEdgeProtect;
	void statistics();

	// ------------------- for cycle project -----------------//
	bool isDeGen; // degenerated cases: plane
	bool badInput;

private:
	char* filename;
	int numofpoints;
	double* points;
	double* DeGenPoints;
	float* normals;
	int PT_LIMIT;
	bool EXPSTOP;
	bool withNorm;
	//----------------for edge protection------------//
	bool readOrgCurveFile(const char* file);
	bool loadOrgCurve(const double* inCurve,const int inNum);
	void readOrgNormFile();
	void loadOrgNorm(const float* inNorm);
	int org_n;
	int n_before;
	int n_after;
	float n_ratio;
	std::vector<Point3> tempC;
	std::vector<Point3> tempOrgC;
	std::vector<std::vector<int>> tempAdj;
	std::vector<Vector3> tempNorm;
	std::vector<std::vector<int>> tempAdjNorm;
	void protectCorner();
	double getAngle(int p1, int p2, int p3);
	double getPt2LineDist(int p1, int p2, int p3);
	void splitEdge(int p1, int p2ind, const Point3 & newP);
	void insertMidPointsTetgen();
	std::vector<std::vector<int>> badEdge;
	std::vector<int> newEdge;
	std::vector<int> newAdj;
	std::vector<int> newNorm;
	std::vector<char> newClip;
	bool isProtected();
	// ENG-82: cache passTetGen's Delaunay output for isProtected to reuse.
	// passTetGen and isProtected both compute the same 3D Delaunay on the
	// same tempC[] point set in well-behaved input; the per-call ~7 ms cost
	// was duplicated. Single-shot cache (consumed by the next isProtected
	// call, then cleared) so insertMidPointsTetgen's while(!isProtected())
	// loop — which mutates tempC via splitEdge — still recomputes correctly.
	std::vector<int> _cached_trifacelist;
	bool _facelist_cached = false;
	// Cache stamp: snapshot of org_n at the time the cache was populated.
	// splitEdge bumps org_n; comparing against current org_n detects Steiner
	// insertion (cache is invalid for the post-EP point set in that case).
	int _facelist_org_n = 0;
	// Permutation captured by getCurveAfterEP: _ep_permutation[i] is the
	// tempC index that became post-EP index i. Used to remap _cached_trifacelist
	// from tempC indices to post-EP indices for DMWT.
	std::vector<int> _ep_permutation;

	// ------------------- for cycle project -----------------//
	bool isDeGenCase();
	void perturbPts(double ptb);
	//bool isDeGen; // degenerated cases: plane
	void splitEdge(int p1, int p2ind, const Point3 &newP, const Point3 &newOrgP);
	
	bool getCurveAfterEP();
	bool sameOrientation(const vector<int> & newCurve);
	bool passTetGen();

	std::vector<double> radius;
	std::vector<double> orgradius;
	std::vector<std::vector<char>> cliped;
	int perturbNum;

	//not used for now
	int numofcurves;
	int numofnormals;
	bool isOpen;
	int capacity;
};

}  // namespace mwt

#endif
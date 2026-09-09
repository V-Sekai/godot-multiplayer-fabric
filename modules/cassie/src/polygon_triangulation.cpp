/**************************************************************************/
/*  polygon_triangulation.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "polygon_triangulation.h"

#include "DMWT.h"

#include "core/object/class_db.h"

#include <vector>
PolygonTriangulation::PolygonTriangulation() :
		optimalCost(0.0f), EXPSTOP(false), dmwt(nullptr) {
}

PolygonTriangulation::~PolygonTriangulation() {
	if (dmwt) {
		delete dmwt;
		dmwt = nullptr;
	}
}

Ref<PolygonTriangulation> PolygonTriangulation::_create_with_degenerates(int ptn, double *pts, double *deGenPts, bool isdegen) {
	Ref<PolygonTriangulation> wrapper;
	wrapper.instantiate();
	wrapper->dmwt = new mwt::DMWT(ptn, pts, deGenPts, isdegen);
	return wrapper;
}

Ref<PolygonTriangulation> PolygonTriangulation::_create_with_normals(int ptn, double *pts, double *deGenPts, float *norms, bool isdegen) {
	Ref<PolygonTriangulation> wrapper;
	wrapper.instantiate();
	wrapper->dmwt = new mwt::DMWT(ptn, pts, deGenPts, norms, isdegen);
	return wrapper;
}

void PolygonTriangulation::preprocess() {
	if (dmwt) {
		dmwt->preprocess();
	}
}

bool PolygonTriangulation::start() {
	if (!dmwt) {
		return false;
	}
	const bool ok = dmwt->start();
	optimalCost = dmwt->optimalCost;
	EXPSTOP = dmwt->EXPSTOP;
	return ok;
}

void PolygonTriangulation::set_weights(float wtri, float wedge, float wbitri, float wtribd, float wwst) {
	if (dmwt) {
		dmwt->setWeights(wtri, wedge, wbitri, wtribd, wwst);
	}
}

void PolygonTriangulation::statistics() {
	if (dmwt) {
		dmwt->statistics();
	}
}

void PolygonTriangulation::set_round(int r) {
	if (dmwt) {
		dmwt->setRound(r);
	}
}

void PolygonTriangulation::set_dot(bool isdot1) {
	if (dmwt) {
		dmwt->setDot(isdot1);
	}
}

void PolygonTriangulation::clear_tiling() {
	if (dmwt) {
		dmwt->clearTiling();
	}
}

void PolygonTriangulation::set_point_limit(int limit) {
	if (dmwt) {
		dmwt->setPointLimit(limit);
	}
}

void PolygonTriangulation::get_result(double **outFaces, int *outNum, double **outPoints,
		float **outNorms, int *outPn, bool dosmooth, int subd, int laps) {
	if (dmwt) {
		dmwt->getResult(outFaces, outNum, outPoints, outNorms, outPn, dosmooth, subd, laps);
	}
}

void PolygonTriangulation::get_indexed_result(Vector<int> &out_face_indices,
		int &out_tri_count, int &out_point_count) const {
	out_tri_count = 0;
	out_point_count = 0;
	out_face_indices.clear();
	if (!dmwt) {
		return;
	}
	std::vector<double> verts;
	std::vector<int> faces;
	dmwt->getResultVectors(verts, faces);
	out_point_count = int(verts.size()) / 3;
	out_tri_count = int(faces.size()) / 3;
	out_face_indices.resize(int(faces.size()));
	for (int i = 0; i < int(faces.size()); ++i) {
		out_face_indices.write[i] = faces[i];
	}
}

void PolygonTriangulation::_bind_methods() {
	ClassDB::bind_method(D_METHOD("preprocess"), &PolygonTriangulation::preprocess);
	ClassDB::bind_method(D_METHOD("start"), &PolygonTriangulation::start);
	ClassDB::bind_method(D_METHOD("set_weights", "wtri", "wedge", "wbitri", "wtribd", "wwst"), &PolygonTriangulation::set_weights);
	ClassDB::bind_method(D_METHOD("statistics"), &PolygonTriangulation::statistics);
	ClassDB::bind_method(D_METHOD("set_round", "r"), &PolygonTriangulation::set_round);
	ClassDB::bind_method(D_METHOD("set_dot", "isdot1"), &PolygonTriangulation::set_dot);
	ClassDB::bind_method(D_METHOD("clear_tiling"), &PolygonTriangulation::clear_tiling);
	ClassDB::bind_method(D_METHOD("set_point_limit", "limit"), &PolygonTriangulation::set_point_limit);
}

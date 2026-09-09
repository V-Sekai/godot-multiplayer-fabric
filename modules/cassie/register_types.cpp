/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "cassie_beautifier.h"
#include "cassie_beautifier_params.h"
#include "cassie_path_3d.h"
#include "cassie_sketcher.h"
#include "cassie_stroke_packet.h"
#include "cassie_surface.h"
#include "cassie_triangulator.h"
#include "constraints/cassie_constraint.h"
#include "constraints/cassie_intersection_constraint.h"
#include "constraints/cassie_mirror_plane_constraint.h"
#include "constraints/cassie_surface_constraint.h"
#include "intrinsic_triangulation.h"
#include "polygon_triangulation.h"
#include "polygon_triangulation_godot.h"
#include "sketch/cassie_curvenet.h"
#include "sketch/cassie_curvenet_extractor.h"
#include "sketch/cassie_curvenet_knot.h"
#include "sketch/cassie_edge_collider.h"
#include "sketch/cassie_final_stroke.h"
#include "sketch/cassie_input_stroke.h"
#include "sketch/cassie_profile_mover.h"
#include "sketch/cassie_sketch_graph.h"
#include "sketch/cassie_surface_manager.h"
#include "sketch/cassie_surface_patch.h"
#include "solver/cassie_constraint_solver.h"

#include "core/object/class_db.h"

void initialize_cassie_module(ModuleInitializationLevel p_level) {
	// Register at SCENE level only. The init function is called once per
	// level — if we registered at every level the EDITOR-level pass would
	// re-register every class under API_EDITOR, blocking GDScript .new() at
	// game-runtime (the symptom seen with ENG-48's curvenet_manipulators.gd
	// when it tried CassieSurfacePatch.new() from a non-editor demo run).
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ClassDB::register_class<PolygonTriangulation>();
	ClassDB::register_class<PolygonTriangulationGodot>();
	ClassDB::register_class<CassiePath3D>();
	ClassDB::register_class<IntrinsicTriangulation>();
	ClassDB::register_class<CassieSurface>();
	ClassDB::register_class<CassieTriangulator>();
	ClassDB::register_class<CassieInputStroke>();
	ClassDB::register_class<CassieConstraint>();
	ClassDB::register_class<CassieIntersectionConstraint>();
	ClassDB::register_class<CassieMirrorPlaneConstraint>();
	ClassDB::register_class<CassieSurfaceConstraint>();
	ClassDB::register_class<CassieFinalStroke>();
	ClassDB::register_class<CassieSurfacePatch>();
	ClassDB::register_class<CassieCurvenetKnot>();
	ClassDB::register_class<CassieCurvenet>();
	ClassDB::register_class<CassieCurvenetExtractor>();
	ClassDB::register_class<CassieProfileMover>();
	ClassDB::register_class<CassieSketchGraphNode>();
	ClassDB::register_class<CassieSketchGraphEdge>();
	ClassDB::register_class<CassieSketchGraph>();
	ClassDB::register_class<CassieSurfaceManager>();
	ClassDB::register_class<CassieEdgeCollider>();
	ClassDB::register_class<CassieSolverParams>();
	ClassDB::register_class<CassieConstraintSolver>();
	ClassDB::register_class<CassieBeautifierParams>();
	ClassDB::register_class<CassieSketchContext>();
	ClassDB::register_class<CassieBeautifier>();
	ClassDB::register_class<CassieStrokePacket>();
	ClassDB::register_class<CassieSketcher>();
}

void uninitialize_cassie_module(ModuleInitializationLevel p_level) {
}

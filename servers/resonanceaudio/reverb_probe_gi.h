/**************************************************************************/
/*  reverb_probe_gi.h                                                     */
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

#pragma once

#include "scene/3d/node_3d.h"
#include "servers/resonanceaudio/reverb_bake_data.h"

class RenderingDevice;
class ResonanceAudioMaterialMap;

class ReverbProbeGI : public Node3D {
	GDCLASS(ReverbProbeGI, Node3D);

public:
	enum WallMaterial {
		MATERIAL_TRANSPARENT = 0,
		MATERIAL_ACOUSTIC_CEILING_TILES,
		MATERIAL_BRICK_BARE,
		MATERIAL_BRICK_PAINTED,
		MATERIAL_CONCRETE_BLOCK_COARSE,
		MATERIAL_CONCRETE_BLOCK_PAINTED,
		MATERIAL_CURTAIN_HEAVY,
		MATERIAL_FIBER_GLASS_INSULATION,
		MATERIAL_GLASS_THIN,
		MATERIAL_GLASS_THICK,
		MATERIAL_GRASS,
		MATERIAL_LINOLEUM_ON_CONCRETE,
		MATERIAL_MARBLE,
		MATERIAL_METAL,
		MATERIAL_PARQUET_ON_CONCRETE,
		MATERIAL_PLASTER_ROUGH,
		MATERIAL_PLASTER_SMOOTH,
		MATERIAL_PLYWOOD_PANEL,
		MATERIAL_POLISHED_CONCRETE_OR_TILE,
		MATERIAL_SHEETROCK,
		MATERIAL_WATER_OR_ICE_SURFACE,
		MATERIAL_WOOD_CEILING,
		MATERIAL_WOOD_PANEL,
		MATERIAL_UNIFORM,
		MATERIAL_MAX,
	};

	enum BakeError {
		BAKE_ERROR_OK,
		BAKE_ERROR_NO_MESHES,
		BAKE_ERROR_NO_PROBES,
	};

private:
	Ref<ReverbBakeData> bake_data;
	Ref<ResonanceAudioMaterialMap> material_map;
	WallMaterial wall_material = MATERIAL_PLASTER_SMOOTH;

	// Cached GPU resources — persist across bakes.
	RenderingDevice *_gpu_rd = nullptr;
	RID _gpu_shader;
	RID _gpu_pipeline;
	void _ensure_gpu_resources();
	void _free_gpu_resources();

	void _collect_mesh_triangles(Node *p_node, Vector<Vector3> &r_vertices, Vector<int> &r_indices, Vector<int> &r_tri_materials, AABB &r_bounds);
	bool _bake_gpu(const PackedVector3Array &p_probes, const Vector<Vector3> &p_vertices, const Vector<int> &p_indices, const Vector<int> &p_tri_materials, const AABB &p_bounds, int p_ray_count, int p_max_bounces, PackedFloat32Array &r_rt60, PackedFloat32Array &r_gains);
	void _update_reverb_from_listener();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_bake_data(const Ref<ReverbBakeData> &p_data);
	Ref<ReverbBakeData> get_bake_data() const { return bake_data; }

	void set_wall_material(WallMaterial p_material);
	WallMaterial get_wall_material() const { return wall_material; }

	void set_material_map(const Ref<ResonanceAudioMaterialMap> &p_map);
	Ref<ResonanceAudioMaterialMap> get_material_map() const;

	BakeError bake(Node *p_from_node, const PackedVector3Array &p_probe_positions, int p_ray_count = 50000, int p_max_bounces = 100);

	ReverbProbeGI() {}
};

VARIANT_ENUM_CAST(ReverbProbeGI::WallMaterial)
VARIANT_ENUM_CAST(ReverbProbeGI::BakeError)

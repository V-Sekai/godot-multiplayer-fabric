/**************************************************************************/
/*  resonance_audio_material_map.h                                        */
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

#include "core/io/resource.h"
#include "servers/resonanceaudio/reverb_probe_gi.h"

class ResonanceAudioMaterialMap : public Resource {
	GDCLASS(ResonanceAudioMaterialMap, Resource);

	// Maps visual material resource path → acoustic WallMaterial enum.
	Dictionary material_mappings;
	ReverbProbeGI::WallMaterial default_material = ReverbProbeGI::MATERIAL_PLASTER_SMOOTH;

protected:
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	static void _bind_methods();

public:
	void set_material_mapping(const String &p_material_path, ReverbProbeGI::WallMaterial p_acoustic);
	ReverbProbeGI::WallMaterial get_material_mapping(const String &p_material_path) const;
	bool has_material_mapping(const String &p_material_path) const;
	void remove_material_mapping(const String &p_material_path);

	void set_default_material(ReverbProbeGI::WallMaterial p_material);
	ReverbProbeGI::WallMaterial get_default_material() const { return default_material; }

	void set_material_mappings(const Dictionary &p_mappings);
	Dictionary get_material_mappings() const { return material_mappings; }

	// Scan a scene tree and populate mappings for all visual materials found.
	// Materials not already in the map get assigned the default.
	void scan_scene(Node *p_root);
	void clear_mappings();
	void _scan_from_editor();

	ResonanceAudioMaterialMap() {}
};

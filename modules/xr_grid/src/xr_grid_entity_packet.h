/**************************************************************************/
/*  xr_grid_entity_packet.h                                               */
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

#include "core/math/quaternion.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class XRGridEntityPacket : public RefCounted {
	GDCLASS(XRGridEntityPacket, RefCounted);

protected:
	static void _bind_methods();

public:
	XRGridEntityPacket() = default;

	static constexpr int PACKET_SIZE = 100;
	static constexpr int PAYLOAD_OFFSET = 58; // userdata starts here (42 bytes)
	static constexpr int64_t PLAYER_ENTITY_BASE = 2'000'000;
	static constexpr int64_t STROKE_ENTITY_BASE = 1'000'000;
	static constexpr int64_t MAX_PLAYER_ID = 700'000'000;

	// Encode the entity envelope into a fixed-size byte buffer ready for
	// fabric broadcast. Returns a 100-byte PackedByteArray.
	static PackedByteArray encode(
			int64_t p_global_id,
			const Vector3 &p_pos,
			const Vector3 &p_vel,
			const Quaternion &p_quat,
			int p_entity_class,
			int p_owner_id,
			int p_hlc_frame,
			int p_hlc_counter,
			int p_sub_index,
			const PackedByteArray &p_payload = PackedByteArray());

	// Decode a packet. Returns:
	//   {global_id, position, velocity, rotation, entity_class, owner_id,
	//    sub_index, hlc_frame, hlc_counter, payload}
	// Position is absolute int64 micrometers (no origin shift); payload is the
	// 42-byte userdata tail.
	// Returns an empty Dictionary on size mismatch.
	static Dictionary decode(const PackedByteArray &p_data);
};

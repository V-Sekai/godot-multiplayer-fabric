/**************************************************************************/
/*  fabric_zone_journal.h                                                 */
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

#include "fabric_zone_types.h"

#include "core/string/ustring.h"

#include <thirdparty/sqlite/sqlite3.h>

// Discrete-mutation journal backed by SQLite.
//
// Only events that change durable state are written:
//   spawn            — entity slot allocated and initialized
//   despawn          — entity slot freed
//   payload_update   — payload words changed (e.g. CMD_INSTANCE_ASSET)
//   snapshot         — full slot-array dump; caps replay length
//
// Physics-step position updates are NOT written — they are deterministic
// from the simulation and too frequent to journal economically.
//
// Crash recovery:
//   open() → replay() → zone runs normally with journal enabled
//
// WAL mode (PRAGMA journal_mode=WAL) is used when available; requires that
// SQLITE_OMIT_WAL is absent from the build (removed from modules/sqlite/SCsub).

class FabricZoneJournal {
public:
	FabricZoneJournal() = default;
	~FabricZoneJournal();

	// Open or create the journal at p_db_path. Returns false on error.
	bool open(const String &p_db_path);
	void close();
	bool is_open() const { return _db != nullptr; }

	// Discrete mutation writers — no-op when not open.
	void journal_spawn(int p_slot_idx, const FabricEntity &p_entity);
	void journal_despawn(int p_slot_idx, int p_global_id);
	// Records changed payload words (asset instance, player state update).
	void journal_payload_update(int p_slot_idx, const FabricEntity &p_entity);
	// Periodic full snapshot — resets the replay start point.
	void journal_snapshot(int p_capacity, const EntitySlot *p_slots);

	// Replay journal into p_slots.  p_slots must already be zero-initialised
	// with capacity p_capacity.  Sets r_entity_count to the number of active
	// slots after replay.  Returns true if any data was replayed.
	bool replay(int p_capacity, EntitySlot *p_slots, int &r_entity_count);

private:
	sqlite3 *_db = nullptr;

	void _exec(const char *p_sql);
	void _create_schema();
	int64_t _latest_snapshot_seq();

	static void _pack_entity(const FabricEntity &p_e, uint8_t *p_out);
	static void _unpack_entity(const uint8_t *p_in, FabricEntity &r_e);
};

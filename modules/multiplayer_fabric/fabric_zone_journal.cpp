/**************************************************************************/
/*  fabric_zone_journal.cpp                                               */
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

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 K. S. Ernest (iFire) Lee
#include "fabric_zone_journal.h"

#include "core/os/time.h"

// ── FabricEntity binary layout (little-endian) ────────────────────────────
// 9 × float32 (cx cy cz vx vy vz ax ay az) = 36 bytes
// 1 × int32   (global_id)                   =  4 bytes
// 14 × uint32  (payload)                    = 56 bytes
// Total: 96 bytes
static constexpr int ENTITY_BYTES = 96;

void FabricZoneJournal::_pack_entity(const FabricEntity &p_e, uint8_t *p_out) {
	float tmp[9] = {
		(float)p_e.cx, (float)p_e.cy, (float)p_e.cz,
		(float)p_e.vx, (float)p_e.vy, (float)p_e.vz,
		(float)p_e.ax, (float)p_e.ay, (float)p_e.az
	};
	memcpy(p_out, tmp, 36);
	int32_t gid = p_e.global_id;
	memcpy(p_out + 36, &gid, 4);
	memcpy(p_out + 40, p_e.payload, 56);
}

void FabricZoneJournal::_unpack_entity(const uint8_t *p_in, FabricEntity &r_e) {
	float tmp[9];
	memcpy(tmp, p_in, 36);
	r_e.cx = tmp[0];
	r_e.cy = tmp[1];
	r_e.cz = tmp[2];
	r_e.vx = tmp[3];
	r_e.vy = tmp[4];
	r_e.vz = tmp[5];
	r_e.ax = tmp[6];
	r_e.ay = tmp[7];
	r_e.az = tmp[8];
	int32_t gid;
	memcpy(&gid, p_in + 36, 4);
	r_e.global_id = gid;
	memcpy(r_e.payload, p_in + 40, 56);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

FabricZoneJournal::~FabricZoneJournal() {
	close();
}

bool FabricZoneJournal::open(const String &p_db_path) {
	close();
	CharString cs = p_db_path.utf8();
	int rc = sqlite3_open(cs.get_data(), &_db);
	if (rc != SQLITE_OK) {
		print_error(vformat("FabricZoneJournal: sqlite3_open(%s) failed: %s",
				p_db_path, sqlite3_errmsg(_db)));
		sqlite3_close(_db);
		_db = nullptr;
		return false;
	}
	// WAL mode for better crash-consistency; falls back to DELETE journal if
	// compiled without WAL (SQLITE_OMIT_WAL), which still gives correct recovery.
	_exec("PRAGMA journal_mode=WAL");
	_exec("PRAGMA synchronous=NORMAL");
	_create_schema();
	return true;
}

void FabricZoneJournal::close() {
	if (_db) {
		sqlite3_close(_db);
		_db = nullptr;
	}
}

void FabricZoneJournal::_exec(const char *p_sql) {
	char *errmsg = nullptr;
	int rc = sqlite3_exec(_db, p_sql, nullptr, nullptr, &errmsg);
	if (rc != SQLITE_OK) {
		print_error(vformat("FabricZoneJournal SQL error: %s", errmsg));
		sqlite3_free(errmsg);
	}
}

void FabricZoneJournal::_create_schema() {
	_exec(R"sql(
		CREATE TABLE IF NOT EXISTS entity_mutations (
			seq       INTEGER PRIMARY KEY AUTOINCREMENT,
			op        TEXT    NOT NULL,
			slot_idx  INTEGER NOT NULL,
			global_id INTEGER NOT NULL,
			entity    BLOB,
			ts        INTEGER NOT NULL
		);
		CREATE TABLE IF NOT EXISTS entity_snapshots (
			id               INTEGER PRIMARY KEY AUTOINCREMENT,
			last_mutation_seq INTEGER NOT NULL,
			slot_data        BLOB    NOT NULL,
			slot_count       INTEGER NOT NULL,
			ts               INTEGER NOT NULL
		);
	)sql");
}

// ── Helpers ───────────────────────────────────────────────────────────────

int64_t FabricZoneJournal::_latest_snapshot_seq() {
	const char *sql = "SELECT last_mutation_seq FROM entity_snapshots "
					  "ORDER BY id DESC LIMIT 1";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return -1;
	}
	int64_t seq = -1;
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		seq = sqlite3_column_int64(stmt, 0);
	}
	sqlite3_finalize(stmt);
	return seq;
}

static int64_t _now_us() {
	return (int64_t)Time::get_singleton()->get_ticks_usec();
}

// ── Mutation writers ──────────────────────────────────────────────────────

void FabricZoneJournal::journal_spawn(int p_slot_idx, const FabricEntity &p_entity) {
	if (!_db) {
		return;
	}
	uint8_t buf[ENTITY_BYTES];
	_pack_entity(p_entity, buf);
	const char *sql = "INSERT INTO entity_mutations(op,slot_idx,global_id,entity,ts) "
					  "VALUES('spawn',?,?,?,?)";
	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr);
	sqlite3_bind_int(stmt, 1, p_slot_idx);
	sqlite3_bind_int(stmt, 2, p_entity.global_id);
	sqlite3_bind_blob(stmt, 3, buf, ENTITY_BYTES, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 4, _now_us());
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
}

void FabricZoneJournal::journal_despawn(int p_slot_idx, int p_global_id) {
	if (!_db) {
		return;
	}
	const char *sql = "INSERT INTO entity_mutations(op,slot_idx,global_id,entity,ts) "
					  "VALUES('despawn',?,?,NULL,?)";
	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr);
	sqlite3_bind_int(stmt, 1, p_slot_idx);
	sqlite3_bind_int(stmt, 2, p_global_id);
	sqlite3_bind_int64(stmt, 3, _now_us());
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
}

void FabricZoneJournal::journal_payload_update(int p_slot_idx, const FabricEntity &p_entity) {
	if (!_db) {
		return;
	}
	uint8_t buf[ENTITY_BYTES];
	_pack_entity(p_entity, buf);
	const char *sql = "INSERT INTO entity_mutations(op,slot_idx,global_id,entity,ts) "
					  "VALUES('payload_update',?,?,?,?)";
	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr);
	sqlite3_bind_int(stmt, 1, p_slot_idx);
	sqlite3_bind_int(stmt, 2, p_entity.global_id);
	sqlite3_bind_blob(stmt, 3, buf, ENTITY_BYTES, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 4, _now_us());
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
}

void FabricZoneJournal::journal_snapshot(int p_capacity, const EntitySlot *p_slots) {
	if (!_db) {
		return;
	}
	// Serialize active slots: [slot_idx u32][entity ENTITY_BYTES] per active entry.
	// Inactive slots are skipped — replay allocates only active ones.
	static constexpr int SLOT_RECORD = 4 + ENTITY_BYTES; // 100 bytes
	Vector<uint8_t> buf;
	int active_count = 0;
	for (int i = 0; i < p_capacity; i++) {
		if (p_slots[i].active) {
			active_count++;
		}
	}
	buf.resize(active_count * SLOT_RECORD);
	uint8_t *w = buf.ptrw();
	int written = 0;
	for (int i = 0; i < p_capacity; i++) {
		if (!p_slots[i].active) {
			continue;
		}
		uint32_t idx = (uint32_t)i;
		memcpy(w + written * SLOT_RECORD, &idx, 4);
		_pack_entity(p_slots[i].entity, w + written * SLOT_RECORD + 4);
		written++;
	}

	// Get last mutation seq to anchor the snapshot.
	int64_t last_seq = 0;
	{
		const char *seq_sql = "SELECT MAX(seq) FROM entity_mutations";
		sqlite3_stmt *st = nullptr;
		sqlite3_prepare_v2(_db, seq_sql, -1, &st, nullptr);
		if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) {
			last_seq = sqlite3_column_int64(st, 0);
		}
		sqlite3_finalize(st);
	}

	const char *sql = "INSERT INTO entity_snapshots(last_mutation_seq,slot_data,slot_count,ts) "
					  "VALUES(?,?,?,?)";
	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr);
	sqlite3_bind_int64(stmt, 1, last_seq);
	sqlite3_bind_blob(stmt, 2, buf.ptr(), buf.size(), SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, active_count);
	sqlite3_bind_int64(stmt, 4, _now_us());
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	// Prune mutations covered by this snapshot — they are no longer needed.
	if (last_seq > 0) {
		const char *prune_sql = "DELETE FROM entity_mutations WHERE seq <= ?";
		sqlite3_stmt *prune = nullptr;
		sqlite3_prepare_v2(_db, prune_sql, -1, &prune, nullptr);
		sqlite3_bind_int64(prune, 1, last_seq);
		sqlite3_step(prune);
		sqlite3_finalize(prune);
	}
}

// ── Replay ────────────────────────────────────────────────────────────────

bool FabricZoneJournal::replay(int p_capacity, EntitySlot *p_slots, int &r_entity_count) {
	if (!_db) {
		return false;
	}
	r_entity_count = 0;
	bool replayed_anything = false;

	// Step 1: restore from the most recent snapshot (if any).
	int64_t after_seq = -1;
	{
		const char *sql = "SELECT last_mutation_seq, slot_data, slot_count FROM entity_snapshots "
						  "ORDER BY id DESC LIMIT 1";
		sqlite3_stmt *stmt = nullptr;
		if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK &&
				sqlite3_step(stmt) == SQLITE_ROW) {
			after_seq = sqlite3_column_int64(stmt, 0);
			int slot_count = sqlite3_column_int(stmt, 2);
			const uint8_t *blob = (const uint8_t *)sqlite3_column_blob(stmt, 1);
			int blob_bytes = sqlite3_column_bytes(stmt, 1);
			static constexpr int SLOT_RECORD = 4 + ENTITY_BYTES;
			int expected = slot_count * SLOT_RECORD;
			if (blob && blob_bytes == expected) {
				for (int i = 0; i < slot_count; i++) {
					const uint8_t *rec = blob + i * SLOT_RECORD;
					uint32_t slot_idx;
					memcpy(&slot_idx, rec, 4);
					if ((int)slot_idx >= p_capacity) {
						continue;
					}
					p_slots[slot_idx] = EntitySlot();
					p_slots[slot_idx].active = true;
					_unpack_entity(rec + 4, p_slots[slot_idx].entity);
					r_entity_count++;
				}
				replayed_anything = (slot_count > 0);
			}
		}
		sqlite3_finalize(stmt);
	}

	// Step 2: replay mutations after the snapshot.
	{
		const char *sql = "SELECT op, slot_idx, global_id, entity FROM entity_mutations "
						  "WHERE seq > ? ORDER BY seq ASC";
		sqlite3_stmt *stmt = nullptr;
		if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
			return replayed_anything;
		}
		sqlite3_bind_int64(stmt, 1, after_seq);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *op = (const char *)sqlite3_column_text(stmt, 0);
			int slot_idx = sqlite3_column_int(stmt, 1);
			int global_id = sqlite3_column_int(stmt, 2);
			if (slot_idx < 0 || slot_idx >= p_capacity || !op) {
				continue;
			}
			if (strcmp(op, "spawn") == 0 || strcmp(op, "payload_update") == 0) {
				const uint8_t *blob = (const uint8_t *)sqlite3_column_blob(stmt, 3);
				int blob_bytes = sqlite3_column_bytes(stmt, 3);
				if (blob && blob_bytes == ENTITY_BYTES) {
					// Only occupy a slot if it is currently free — static world
					// entities are re-initialised before replay and must not be
					// overwritten by a mismatched journal entry.
					if (!p_slots[slot_idx].active) {
						p_slots[slot_idx] = EntitySlot();
						p_slots[slot_idx].active = true;
						_unpack_entity(blob, p_slots[slot_idx].entity);
						r_entity_count++;
						replayed_anything = true;
					} else if (p_slots[slot_idx].entity.global_id == global_id) {
						// Same entity already in slot (e.g. from prior snapshot) — update payload.
						_unpack_entity(blob, p_slots[slot_idx].entity);
						replayed_anything = true;
					}
				}
			} else if (strcmp(op, "despawn") == 0) {
				if (p_slots[slot_idx].active && p_slots[slot_idx].entity.global_id == global_id) {
					p_slots[slot_idx].active = false;
					r_entity_count--;
					replayed_anything = true;
				}
			}
		}
		sqlite3_finalize(stmt);
	}

	if (r_entity_count < 0) {
		r_entity_count = 0;
	}
	return replayed_anything;
}

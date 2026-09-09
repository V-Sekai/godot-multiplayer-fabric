/**************************************************************************/
/*  otel_wal.cpp                                                          */
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

#include "otel_wal.h"

#include "../sqlite/src/godot_sqlite.h"

#include "core/os/os.h"

static const char *SCHEMA_SQL =
		"CREATE TABLE IF NOT EXISTS wal("
		"  id      TEXT NOT NULL PRIMARY KEY,"
		"  signal  TEXT NOT NULL,"
		"  payload TEXT NOT NULL,"
		"  ts      INTEGER NOT NULL"
		");"
		"PRAGMA journal_mode=WAL;";

bool OTelWAL::open(const String &p_path) {
	_db.instantiate();
	if (!_db->open(p_path)) {
		_db.unref();
		return false;
	}
	Ref<SQLiteQuery> q = _db->create_query(SCHEMA_SQL);
	if (!q.is_valid()) {
		_db->close();
		_db.unref();
		return false;
	}
	q->execute(Array());
	_open = true;
	return true;
}

void OTelWAL::close() {
	if (_open && _db.is_valid()) {
		_db->close();
		_db.unref();
		_open = false;
	}
}

bool OTelWAL::write(const String &p_signal, const String &p_id, const String &p_payload) {
	if (!_open) {
		return false;
	}
	Ref<SQLiteQuery> q = _db->create_query(
			"INSERT OR IGNORE INTO wal (id, signal, payload, ts) VALUES (?, ?, ?, ?);");
	if (!q.is_valid()) {
		return false;
	}
	Array args;
	args.push_back(p_id);
	args.push_back(p_signal);
	args.push_back(p_payload);
	args.push_back((int64_t)OS::get_singleton()->get_unix_time());
	q->execute(args);
	return true;
}

Vector<OTelWAL::Row> OTelWAL::read_all() {
	Vector<Row> result;
	if (!_open) {
		return result;
	}
	Ref<SQLiteQuery> q = _db->create_query(
			"SELECT id, signal, payload FROM wal ORDER BY ts ASC;");
	if (!q.is_valid()) {
		return result;
	}
	Variant raw = q->execute(Array());
	if (raw.get_type() != Variant::ARRAY) {
		return result;
	}
	Array rows = raw;
	for (int i = 0; i < rows.size(); i++) {
		Array row = rows[i];
		if (row.size() < 3) {
			continue;
		}
		Row r;
		r.id = row[0].operator String();
		r.signal = row[1].operator String();
		r.payload = row[2].operator String();
		if (!r.id.is_empty()) {
			result.push_back(r);
		}
	}
	return result;
}

bool OTelWAL::remove(const String &p_id) {
	if (!_open) {
		return false;
	}
	Ref<SQLiteQuery> q = _db->create_query("DELETE FROM wal WHERE id = ?;");
	if (!q.is_valid()) {
		return false;
	}
	Array args;
	args.push_back(p_id);
	q->execute(args);
	return true;
}

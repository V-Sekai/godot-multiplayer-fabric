extends SceneTree
# Two clients, one server, and the question of whether it knows which is which.
#
# Usage:
#   godot --headless --script modules/http3/demo/wt_roster_test.gd
#   python roster_client.py --port 54371 --clients 2      # in fabric-wt-harness
#
# `wt_server_demo.gd` beside this file echoes, which proves a session carries bytes. It cannot
# show what this does, because everything it exercises works with one client: `WebTransportPeer`
# kept its clients in a single bool, so `peer_connected` was never emitted at all and
# `disconnect_peer` closed the whole server. Neither is reachable until a second session is in
# the room.
#
# So this asserts the three things two sessions make visible:
#
#   1. two joins arrive, with different peer ids
#   2. dropping one is announced for that id and no other
#   3. the one that was not dropped is still connected afterwards
#
# The client half is deliberately not Godot. See `fabric-wt-harness`: two ends of one
# implementation agree with each other about anything both get wrong.

const PATH = "/wt"
var PORT: int = int(OS.get_environment("ZONE_PORT")) if OS.get_environment("ZONE_PORT") != "" else 54371

# How long to wait for the second client before giving up. The harness staggers its connections
# by a second, so this is generous rather than tight — a timeout here should mean nothing
# arrived, not that the test was impatient.
const WAIT_FOR_CLIENTS_MS = 30000
# After dropping one peer, how long to keep watching. Long enough for a wrong implementation to
# take the other peer down with it, which is the failure being looked for.
const WATCH_AFTER_DROP_MS = 4000

var peer: WebTransportPeer
var joined: Array[int] = []
var left: Array[int] = []
var dropped_id := 0
var dropped_at := 0
var failures := 0
var t0 := 0


static func _fmt_validity(unix_time: int) -> String:
	var dt = Time.get_datetime_dict_from_unix_time(unix_time)
	return "%04d%02d%02d%02d%02d%02d" % [
		dt["year"], dt["month"], dt["day"], dt["hour"], dt["minute"], dt["second"]
	]


func check(cond: bool, what: String) -> void:
	if cond:
		print("  ok    %s" % what)
	else:
		failures += 1
		printerr("  FAIL  %s" % what)


func _init() -> void:
	var crypto = Crypto.new()
	var key = crypto.generate_ecdsa()
	if not key:
		printerr("generate_ecdsa failed")
		quit(1)
		return
	var now = int(Time.get_unix_time_from_system())
	var san = PackedStringArray(["DNS:localhost", "IP:127.0.0.1", "IP:::1"])
	var cert = crypto.generate_self_signed_certificate_san(
		key, "CN=godot-webtransport", _fmt_validity(now), _fmt_validity(now + 13 * 86400), san)
	if not cert:
		printerr("generate_self_signed_certificate_san failed")
		quit(1)
		return

	peer = WebTransportPeer.new()
	# Connect before serving. A client fast enough to arrive between `create_server` and the
	# connect would be announced to nobody, and the test would blame the roster.
	peer.peer_connected.connect(_on_peer_connected)
	peer.peer_disconnected.connect(_on_peer_disconnected)

	var err = peer.create_server(PORT, PATH, cert, key)
	if err != OK:
		printerr("create_server failed: ", err)
		quit(1)
		return

	t0 = Time.get_ticks_msec()
	print("roster test listening on %d%s — connect two clients" % [PORT, PATH])


func _on_peer_connected(id: int) -> void:
	joined.append(id)
	print("  join  peer %d (now %d connected)" % [id, joined.size() - left.size()])


func _on_peer_disconnected(id: int) -> void:
	left.append(id)
	print("  left  peer %d" % id)


func _process(_delta: float) -> bool:
	if not peer:
		return true
	peer.poll()

	var now := Time.get_ticks_msec()

	if dropped_id == 0:
		if joined.size() < 2:
			if now - t0 > WAIT_FOR_CLIENTS_MS:
				printerr("only %d client(s) arrived in %d ms" % [joined.size(), WAIT_FOR_CLIENTS_MS])
				return _finish(false)
			return false

		print("two clients are connected; dropping the first")
		check(joined[0] != joined[1], "the two peers have different ids (%d, %d)"
			% [joined[0], joined[1]])
		check(peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED,
			"the server reports connected with two sessions")

		dropped_id = joined[0]
		dropped_at = now
		peer.disconnect_peer(dropped_id)
		return false

	if now - dropped_at < WATCH_AFTER_DROP_MS:
		return false

	# The one that was dropped is gone, and only that one.
	check(left.has(dropped_id), "peer %d was announced gone" % dropped_id)
	check(not left.has(joined[1]), "peer %d was NOT announced gone" % joined[1])
	check(left.size() == 1, "exactly one disconnection was announced (got %d)" % left.size())
	# The whole point: dropping a client must not stop the server. Before this, `disconnect_peer`
	# called `close()`, and the survivor went with it.
	check(peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED,
		"the server is still serving the peer that stayed")
	return _finish(failures == 0)


func _finish(ok: bool) -> bool:
	if ok:
		print("roster test: every check passed")
	else:
		printerr("roster test: %d failed" % maxi(failures, 1))
	peer.close()
	quit(0 if ok else 1)
	return true

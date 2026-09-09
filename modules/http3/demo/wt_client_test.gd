extends SceneTree
# Demo: WebTransportPeer (standard MultiplayerPeer API) echo round-trip.
# Connects to a webtransportd echo server and verifies datagram round-trip.
#
# Usage:
#   Start echo server:
#     webtransportd --server --cert=auto --port=54370 --exec=<path>/webtransportd/examples/echo
#
#   Run this script:
#     godot --headless --script modules/http3/demo/wt_client_test.gd

const HOST = "127.0.0.1"
const PORT = 54370
const PATH = "/wt"
const MSG = "Hello Godot WebTransport"
const TIMEOUT_MS = 8000

var peer: WebTransportPeer
var sent := false
var t0 := 0

func _init():
	peer = WebTransportPeer.new()
	var err = peer.create_client(HOST, PORT, PATH)
	if err != OK:
		printerr("create_client failed: ", err)
		quit(1)
	else:
		print("connecting to ", HOST, ":", PORT, PATH, " ...")
		t0 = Time.get_ticks_msec()

func _process(_delta: float) -> bool:
	if not peer:
		return false
	peer.poll()
	var state = peer.get_connection_status()

	if Time.get_ticks_msec() - t0 > TIMEOUT_MS:
		printerr("TIMEOUT — connection state: ", state)
		peer.close()
		quit(1)
		return false

	if state == MultiplayerPeer.CONNECTION_CONNECTED and not sent:
		sent = true
		peer.put_packet(MSG.to_utf8_buffer())
		print("sent: ", MSG)

	if state == MultiplayerPeer.CONNECTION_CONNECTED and sent:
		while peer.get_available_packet_count() > 0:
			var echo = peer.get_packet().get_string_from_utf8()
			if echo == MSG:
				print("PASS: echo matched (", JSON.stringify(echo), ")")
				peer.close()
				quit(0)
			else:
				printerr("FAIL: got ", JSON.stringify(echo))
				peer.close()
				quit(1)
			return false

	if state == MultiplayerPeer.CONNECTION_DISCONNECTED and sent:
		printerr("FAIL: disconnected before echo received")
		quit(1)
	return false

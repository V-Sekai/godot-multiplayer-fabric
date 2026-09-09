extends SceneTree
# Demo: WebTransportPeer server — caller builds cert, serves echo + browser test page.
#
# Usage:
#   godot --headless --script modules/http3/demo/wt_server_demo.gd
#
# The server:
#   1. Builds a fresh P-256 ECDSA cert (13-day validity) in GDScript.
#   2. Starts a WebTransport server on PORT with the caller-provided cert+key.
#   3. Prints the base64 SHA-256 cert hash for browser serverCertificateHashes.
#   4. Serves a browser test HTML page over plain HTTP on HTTP_PORT.
#   5. Echoes incoming datagrams and streams back to the sender.

const PATH = "/wt"

# Allow Uro zone supervisor to override port via env var.
var PORT: int = int(OS.get_environment("ZONE_PORT")) if OS.get_environment("ZONE_PORT") != "" else 54370
var HTTP_PORT: int = PORT + 1

var peer: WebTransportPeer
var tcp_server: TCPServer
var http_client: StreamPeerTCP
var html_page: String

static func _fmt_validity(unix_time: int) -> String:
	# Returns a YYYYMMDDHHmmss string from a Unix timestamp (UTC).
	var dt = Time.get_datetime_dict_from_unix_time(unix_time)
	return "%04d%02d%02d%02d%02d%02d" % [
		dt["year"], dt["month"], dt["day"],
		dt["hour"], dt["minute"], dt["second"]
	]

func _init():
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
	var err = peer.create_server(PORT, PATH, cert, key)
	if err != OK:
		printerr("create_server failed: ", err)
		quit(1)
		return

	# Compute SHA-256 fingerprint of the cert DER for browser serverCertificateHashes.
	var ctx = HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	ctx.update(cert.get_der())
	var hash = Marshalls.raw_to_base64(ctx.finish())

	# Emit ready beacon as JSON so Uro zone supervisor can parse it from stdout.
	print(JSON.stringify({"event": "ready", "port": PORT, "cert_hash": hash}))

	html_page = _make_browser_test_page(hash)

	tcp_server = TCPServer.new()
	var herr = tcp_server.listen(HTTP_PORT)
	if herr != OK:
		printerr("HTTP server listen failed: ", herr)
	else:
		print("Browser test page at: http://127.0.0.1:", HTTP_PORT, "/")

func _process(_delta: float) -> bool:
	if not peer:
		return false

	peer.poll()
	_handle_http()

	# Echo all incoming packets back.
	while peer.get_available_packet_count() > 0:
		var pkt = peer.get_packet()
		peer.put_packet(pkt)

	return false

func _handle_http():
	if not tcp_server or not tcp_server.is_listening():
		return
	if tcp_server.is_connection_available():
		http_client = tcp_server.take_connection()
	if not http_client or http_client.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		return
	var available = http_client.get_available_bytes()
	if available > 0:
		http_client.get_data(available)  # discard request
		var body = html_page.to_utf8_buffer()
		var header = ("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n" % body.size())
		http_client.put_data(header.to_utf8_buffer())
		http_client.put_data(body)
		http_client = null

func _make_browser_test_page(p_hash: String) -> String:
	return """<!DOCTYPE html>
<html><head><title>Godot WebTransport Test</title></head><body>
<pre id="log"></pre>
<script>
const PORT = %d, PATH = "%s", HASH = "%s", MSG = "Hello Godot WebTransport";
const log = s => { document.getElementById("log").textContent += s + "\\n"; };
(async () => {
  const hashBytes = Uint8Array.from(atob(HASH), c => c.charCodeAt(0));
  let wt;
  try {
    wt = new WebTransport("https://127.0.0.1:" + PORT + PATH, {
      serverCertificateHashes: [{ algorithm: "sha-256", value: hashBytes }]
    });
    await wt.ready;
    log("session ready");
  } catch(e) { log("FAIL connect: " + e); document.title = "FAIL"; return; }
  try {
    // Send on a browser-initiated bidi stream.
    const stream = await wt.createBidirectionalStream();
    const writer = stream.writable.getWriter();
    await writer.write(new TextEncoder().encode(MSG));
    await writer.close();
    log("sent: " + MSG);
    // Server echoes on a NEW server-initiated stream; listen for it.
    const incomingReader = wt.incomingBidirectionalStreams.getReader();
    const { value: echoStream } = await incomingReader.read();
    const reader = echoStream.readable.getReader();
    let chunks = [];
    while (true) { const {value, done} = await reader.read(); if (done) break; if (value) chunks.push(value); }
    if (chunks.length === 0) { log("FAIL stream: no echo data"); document.title = "FAIL"; return; }
    const echo = new TextDecoder().decode(chunks.reduce((a,b) => { const c = new Uint8Array(a.length+b.length); c.set(a); c.set(b,a.length); return c; }));
    if (echo === MSG) { log("PASS: " + JSON.stringify(echo)); document.title = "PASS"; }
    else { log("FAIL: got " + JSON.stringify(echo)); document.title = "FAIL"; }
  } catch(e) { log("FAIL stream: " + e); document.title = "FAIL"; }
})();
</script></body></html>""" % [PORT, PATH, p_hash]

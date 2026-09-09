/**
 * WebTransport browser-native glue for Godot's quic module.
 *
 * On the web platform, picoquic is NOT compiled. Instead, the browser's
 * built-in WebTransport API handles the QUIC + HTTP/3 + WT session stack.
 * This JS library is registered via env.AddJSLibraries in SCsub and
 * exposes C-callable functions that the web backend invokes.
 */

const GodotWebTransport = {
	$GodotWebTransport__deps: [],
	$GodotWebTransport: {
		_sessions: {},
		_nextId: 1,
	},

	/**
	 * Open a WebTransport session to the given URL.
	 * Returns a session ID (> 0) on success, 0 on failure.
	 */
	godot_WTServerSession__proxy: 'sync',
	godot_wt_connect__sig: 'iii',
	godot_wt_connect: function (p_url_ptr, p_url_len) {
		const url = UTF8ToString(p_url_ptr, p_url_len);
		try {
			const wt = new WebTransport(url);
			const id = GodotWebTransport._nextId++;
			GodotWebTransport._sessions[id] = {
				wt: wt,
				datagrams_in: [],
				streams_in: [],
				connected: false,
				closed: false,
			};
			wt.ready.then(() => {
				const sess = GodotWebTransport._sessions[id];
				if (sess) {
					sess.connected = true;
				}
			});
			wt.closed.then(() => {
				const sess = GodotWebTransport._sessions[id];
				if (sess) {
					sess.closed = true;
				}
			});
			// Start reading datagrams.
			wt.datagrams.readable
				.getReader()
				.read()
				.then(function readDatagram(result) {
					if (result.done) {
						return;
					}
					const sess = GodotWebTransport._sessions[id];
					if (sess) {
						sess.datagrams_in.push(new Uint8Array(result.value));
					}
					wt.datagrams.readable
						.getReader()
						.read()
						.then(readDatagram);
				});
			return id;
		} catch (e) {
			// eslint-disable-next-line no-console
			console.error('godot_wt_connect failed:', e);
			return 0;
		}
	},

	/**
	 * Check if the session is connected (ready promise resolved).
	 */
	godot_wt_is_connected__sig: 'ii',
	godot_wt_is_connected: function (p_id) {
		const sess = GodotWebTransport._sessions[p_id];
		return sess && sess.connected ? 1 : 0;
	},

	/**
	 * Check if the session is closed.
	 */
	godot_wt_is_closed__sig: 'ii',
	godot_wt_is_closed: function (p_id) {
		const sess = GodotWebTransport._sessions[p_id];
		return !sess || sess.closed ? 1 : 0;
	},

	/**
	 * Send an unreliable datagram.
	 */
	godot_wt_send_datagram__sig: 'viii',
	godot_wt_send_datagram: function (p_id, p_data_ptr, p_data_len) {
		const sess = GodotWebTransport._sessions[p_id];
		if (!sess || !sess.connected) {
			return;
		}
		const data = HEAPU8.slice(p_data_ptr, p_data_ptr + p_data_len);
		const writer = sess.wt.datagrams.writable.getWriter();
		writer.write(data);
		writer.releaseLock();
	},

	/**
	 * Receive a pending datagram. Returns the length written to the buffer,
	 * or 0 if no datagram is available.
	 */
	godot_wt_recv_datagram__sig: 'iiii',
	godot_wt_recv_datagram: function (p_id, p_buf_ptr, p_buf_max) {
		const sess = GodotWebTransport._sessions[p_id];
		if (!sess || sess.datagrams_in.length === 0) {
			return 0;
		}
		const dg = sess.datagrams_in.shift();
		const len = Math.min(dg.length, p_buf_max);
		HEAPU8.set(dg.subarray(0, len), p_buf_ptr);
		return len;
	},

	/**
	 * Close the session.
	 */
	godot_wt_close__sig: 'vi',
	godot_wt_close: function (p_id) {
		const sess = GodotWebTransport._sessions[p_id];
		if (sess) {
			try {
				sess.wt.close();
			} catch (e) {
				/* ignore */
			}
			delete GodotWebTransport._sessions[p_id];
		}
	},
};

autoAddDeps(GodotWebTransport, '$GodotWebTransport');
mergeInto(LibraryManager.library, GodotWebTransport);

"""Run Lua on a norns through matron's REPL websocket, the one maiden uses.

norns runs matron under ws-wrapper, which bridges matron's stdin/stdout to a
websocket (nng bus protocol, text frames) on port 5555. Every connected client
sees all of matron's output, so each command here tags its result with an id.
Python standard library only.

    with Norns("192.168.1.20") as n:
        n.run("crow.output[1].volts = 3")
        print(n.query("norns.crow.connected()"))
"""
import base64
import hashlib
import os
import re
import socket
import struct
import threading
import time

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
SUBPROTOCOL = "bus.sp.nanomsg.org"


class NornsError(Exception):
    pass


class NornsUnavailable(NornsError):
    pass


def lua_string(s):
    """s as a one-line Lua string literal."""
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif o < 32 or o == 127:
            out.append(f"\\{o:03d}")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def resolve(host, port, timeout):
    """getaddrinfo with a timeout: an unresolvable name (norns.local in a
    container without mDNS) can otherwise block for 15s or more."""
    result = []

    def lookup():
        try:
            result.append(socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)[0][4][0])
        except OSError as e:
            result.append(e)

    t = threading.Thread(target=lookup, daemon=True)
    t.start()
    t.join(timeout)
    if not result:
        raise NornsUnavailable(f"can't resolve {host} within {timeout}s")
    if isinstance(result[0], Exception):
        raise NornsUnavailable(f"can't resolve {host}: {result[0]}")
    return result[0]


class WebSocket:
    """Minimal RFC 6455 client: text frames, ping/pong, close."""

    def __init__(self, host, port, path="/", subprotocol=SUBPROTOCOL, timeout=2.0):
        try:
            self.sock = socket.create_connection((resolve(host, port, timeout), port), timeout=timeout)
        except OSError as e:
            raise NornsUnavailable(f"can't connect to {host}:{port}: {e}") from e
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((
            f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
            f"Sec-WebSocket-Protocol: {subprotocol}\r\n\r\n").encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise NornsUnavailable(f"{host}:{port} closed the connection during the handshake")
            resp += chunk
        head, self.buf = resp.split(b"\r\n\r\n", 1)
        lines = head.decode("latin-1").split("\r\n")
        if " 101 " not in lines[0] + " ":
            raise NornsUnavailable(f"websocket handshake refused: {lines[0]}")
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        headers = {k.strip().lower(): v.strip() for k, _, v in (l.partition(":") for l in lines[1:])}
        if headers.get("sec-websocket-accept") != accept:
            raise NornsUnavailable("websocket handshake: bad Sec-WebSocket-Accept")
        self.sock.settimeout(None)
        self.send_lock = threading.Lock()

    def _send_frame(self, opcode, payload):
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            header = struct.pack("!BB", 0x80 | opcode, 0x80 | n)
        elif n < 65536:
            header = struct.pack("!BBH", 0x80 | opcode, 0x80 | 126, n)
        else:
            header = struct.pack("!BBQ", 0x80 | opcode, 0x80 | 127, n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        with self.send_lock:
            self.sock.sendall(header + mask + masked)

    def send_text(self, text):
        self._send_frame(0x1, text.encode())

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("websocket closed")
            self.buf += chunk
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def recv_text(self):
        """Next complete text message; None when the connection closes."""
        parts = []
        while True:
            b0, b1 = self._read(2)
            fin, opcode = b0 & 0x80, b0 & 0x0F
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack("!H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack("!Q", self._read(8))[0]
            mask = self._read(4) if b1 & 0x80 else None
            payload = self._read(n)
            if mask:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
            if opcode == 0x8:
                return None
            if opcode == 0x9:
                self._send_frame(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            parts.append(payload)
            if fin:
                return b"".join(parts).decode("utf-8", "replace")

    def close(self):
        try:
            self._send_frame(0x8, b"")
        except OSError:
            pass
        self.sock.close()


class Norns:
    RESULT = re.compile(r"^@@bbt (\d+) (ok|err) ?(.*)$")

    def __init__(self, host=None, port=5555, timeout=2.0):
        self.host = host or os.environ.get("NORNS_HOST", "norns.local")
        self.ws = WebSocket(self.host, port, timeout=timeout)
        self.lines = []          # every line matron has printed since we connected
        self.cond = threading.Condition()
        self.partial = ""
        self.closed = False
        self.next_id = 1
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        try:
            while True:
                text = self.ws.recv_text()
                if text is None:
                    break
                with self.cond:
                    self.partial += text
                    *done, self.partial = self.partial.split("\n")
                    self.lines += done
                    self.cond.notify_all()
        except (OSError, ConnectionError):
            pass
        with self.cond:
            self.closed = True
            self.cond.notify_all()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        self.ws.close()

    def mark(self):
        """A position in the output, for output_since()."""
        with self.cond:
            return len(self.lines)

    def output_since(self, mark):
        with self.cond:
            return list(self.lines[mark:])

    def wait_for(self, pattern, since=0, timeout=5.0):
        """Wait for a line matching regex `pattern` after `since`; returns the match."""
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        with self.cond:
            pos = since
            while True:
                for line in self.lines[pos:]:
                    m = rx.search(line)
                    if m:
                        return m
                pos = len(self.lines)
                left = deadline - time.monotonic()
                if left <= 0 or self.closed:
                    raise NornsError(f"no output matching {pattern!r} within {timeout}s")
                self.cond.wait(left)

    def send(self, line):
        """Send one raw REPL line, as typed into maiden."""
        self.ws.send_text(line + "\n")

    def run(self, code, timeout=5.0):
        """Run Lua on norns. Returns the first result as a string; raises NornsError on error.
        Output printed meanwhile is in output_since()."""
        rid = self.next_id
        self.next_id += 1
        since = self.mark()
        self.send(
            "do local f, e = load(" + lua_string(code) + ', "=bbtest") local ok, r '
            "if f then ok, r = pcall(f) else ok, r = false, e end "
            f'print("@@bbt {rid} " .. (ok and "ok" or "err") .. " " .. (tostring(r):gsub("\\n", " | "))) end')
        m = self.wait_for(rf"^@@bbt {rid} (ok|err) ?(.*)$", since, timeout)
        if m.group(1) == "err":
            raise NornsError(f"norns lua error: {m.group(2)}")
        return m.group(2)

    def query(self, expr, timeout=5.0):
        """Evaluate a Lua expression on norns: number, bool, None (nil) or string."""
        out = self.run("return " + expr, timeout)
        if out == "nil":
            return None
        if out in ("true", "false"):
            return out == "true"
        try:
            return int(out) if re.fullmatch(r"-?\d+", out) else float(out)
        except ValueError:
            return out

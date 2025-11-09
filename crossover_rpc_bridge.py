# crossover_rpc_bridge.py
import socket
import struct
import json
import threading
import time
import sys
from pypresence import Presence, exceptions as pres_exceptions

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 50050

class Bridge:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((SERVER_HOST, SERVER_PORT))
        self.sock.listen(1)
        self.rpc = None
        self.client_id = None
        self.lock = threading.Lock()
        print(f"[bridge] listening on {SERVER_HOST}:{SERVER_PORT}")

    def start(self):
        while True:
            conn, addr = self.sock.accept()
            print(f"[bridge] connection from {addr}")
            t = threading.Thread(target=self.handle_conn, args=(conn,), daemon=True)
            t.start()

    def handle_conn(self, conn):
        try:
            while True:
                hdr = self.recvall(conn, 4)
                if not hdr:
                    break
                (length,) = struct.unpack("<I", hdr)
                data = self.recvall(conn, length)
                if not data:
                    break
                try:
                    obj = json.loads(data.decode('utf-8'))
                except Exception as e:
                    print("[bridge] JSON parse error:", e)
                    continue
                self.dispatch(obj)
        except Exception as e:
            print("[bridge] connection error:", e)
        finally:
            conn.close()

    def recvall(self, conn, n):
        buf = b""
        while len(buf) < n:
            chunk = conn.recv(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    def dispatch(self, obj):
        typ = obj.get("type")
        if typ == "initialize":
            cid = obj.get("client_id")
            print(f"[bridge] initialize for client_id={cid}")
            self.setup_rpc(cid)
        elif typ == "update":
            print(f"[bridge] update payload: {obj}")
            self.update_presence(obj)
        elif typ == "shutdown":
            print("[bridge] shutdown received")
            self.shutdown_rpc()
        else:
            print("[bridge] unknown message type:", typ)

    def setup_rpc(self, client_id):
        with self.lock:
            if self.rpc:
                if self.client_id == client_id:
                    return
                else:
                    try:
                        self.rpc.clear()  # best-effort
                    except:
                        pass
                    self.rpc = None
            self.client_id = client_id
            try:
                rpc = Presence(client_id)
                rpc.connect()  # uses Discord IPC
                self.rpc = rpc
                print(f"[bridge] connected RPC for {client_id}")
            except Exception as e:
                print(f"[bridge] failed to connect RPC for {client_id}: {e}")
                self.rpc = None

    def update_presence(self, payload):
        with self.lock:
            if not self.rpc:
                print("[bridge] no RPC connection; cannot update presence")
                return
            # map fields from our proxy JSON to pypresence.update kwargs
            kwargs = {}
            if "details" in payload:
                kwargs["details"] = payload.get("details") or None
            if "state" in payload:
                kwargs["state"] = payload.get("state") or None
            if "start" in payload and payload.get("start"):
                kwargs["start"] = int(payload.get("start"))
            if "end" in payload and payload.get("end"):
                kwargs["end"] = int(payload.get("end"))
            # images
            if "large" in payload and payload.get("large"):
                kwargs["large_image"] = payload.get("large")
            if "small" in payload and payload.get("small"):
                kwargs["small_image"] = payload.get("small")
            try:
                self.rpc.update(**kwargs)
                print("[bridge] presence updated:", kwargs)
            except pres_exceptions.InvalidID:
                print("[bridge] invalid client id for RPC")
            except Exception as e:
                print("[bridge] failed to update presence:", e)

    def shutdown_rpc(self):
        with self.lock:
            if self.rpc:
                try:
                    self.rpc.clear()
                    self.rpc.close()
                except:
                    pass
            self.rpc = None
            self.client_id = None

if __name__ == "__main__":
    # Ensure pypresence is installed: pip install pypresence
    print("[bridge] starting")
    b = Bridge()
    try:
        b.start()
    except KeyboardInterrupt:
        print("[bridge] exiting")
        sys.exit(0)
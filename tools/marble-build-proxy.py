#!/usr/bin/env python3
"""Relay the host's loopback build proxy solely to the private Linux VM.

No request contents are logged. Creating the stop file ends the relay.
"""
import argparse
from pathlib import Path
import select
import socket
import socketserver

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bind', required=True)
p.add_argument('--guest', required=True)
p.add_argument('--port', type=int, default=11452)
p.add_argument('--upstream-port', type=int, default=11451)
p.add_argument('--stop-file', type=Path, required=True)
a = p.parse_args()


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        if self.client_address[0] != a.guest:
            return
        with socket.create_connection(('127.0.0.1', a.upstream_port), timeout=15) as upstream:
            upstream.settimeout(None)
            pair = [self.request, upstream]
            while not a.stop_file.exists():
                ready, _, _ = select.select(pair, [], [], 2)
                for src in ready:
                    data = src.recv(65536)
                    if not data:
                        return
                    (upstream if src is self.request else self.request).sendall(data)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


with Server((a.bind, a.port), Handler) as server:
    server.timeout = 1
    print('Private build proxy relay ready', flush=True)
    while not a.stop_file.exists():
        server.handle_request()

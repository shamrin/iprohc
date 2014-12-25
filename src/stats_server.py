"""Send (momentary) stats to InfluxDB

Usage:
    python stats_server.py

On another console:
    while true; do sudo kill -USR1 $(cat /var/run/iprohc_server.pid); sleep 1; done

To view InfluxDB data, go to its admin interface (on :8083), and query `rohc` DB
with `select * from client.<address>`. E.g.: `select * from "client.172-30-1-175"`
"""

import sys
import os
import socket
import json
import time

import requests

from cffi import FFI
ffi = FFI()

# load `struct udp_stats` definition
ffi.cdef(open(os.path.dirname(os.path.abspath(__file__)) +
              '/server/stats.h').read())

UDP_IP = "127.0.0.1"
UDP_PORT = 32032

struct = ffi.new("struct udp_stats *")
sock = socket.socket(socket.AF_INET, # Internet
                     socket.SOCK_DGRAM) # UDP
sock.bind((UDP_IP, UDP_PORT))

COLUMNS = '''decomp_failed
             decomp_total
             comp_failed
             comp_total
             head_comp_size
             head_uncomp_size
             total_comp_size
             total_uncomp_size
             unpack_failed
             total_received'''.split()

prev_stats = None

while True:
    nbytes, addr = sock.recvfrom_into(ffi.buffer(struct))
    if nbytes == 0:
        continue

#    stats_packing = [struct.stats_packing[i]
#                     for i in range(1, struct.n_stats_packing)]

    stats = [getattr(struct, col) for col in COLUMNS]

    if prev_stats is None:
        print 'initial stats received (not sent):', dict(zip(COLUMNS, stats))
    else:
        point = [now - prev for prev, now in zip(prev_stats, stats)]

        ip = socket.inet_ntoa(ffi.buffer(ffi.addressof(struct.client_addr)))
        data = [dict(name = "client.%s" % ip.replace('.', '-'),
                     columns = COLUMNS, points = [point])]

        data_json = json.dumps(data)
        print 'sending', data_json,

        r = requests.post("http://localhost:8086/db/rohc/series?u=root&p=root",
                          data=data_json)
        print '=>', r.status_code, r.text

    prev_stats = stats

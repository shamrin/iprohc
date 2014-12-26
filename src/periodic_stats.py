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
from datetime import datetime

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
prev_ts = datetime.now()

while True:
    nbytes, addr = sock.recvfrom_into(ffi.buffer(struct))
    if nbytes == 0:
        continue

#    stats_packing = [struct.stats_packing[i]
#                     for i in range(1, struct.n_stats_packing)]

    stats = [getattr(struct, col) for col in COLUMNS]
    cur_ts = datetime.now()

    if prev_stats is None:
        print ''
        # print 'initial stats received (not sent):', dict(zip(COLUMNS, stats))
    else:
        
        points = [(now - prev) for prev, now in zip(prev_stats, stats)]
        ip = socket.inet_ntoa(ffi.buffer(ffi.addressof(struct.client_addr)))

        print "\n",
        print '-'*13,
        print datetime.now().strftime('%H:%M:%S'),
        print '-'*15
        i = 0
        delta_ts = (cur_ts - prev_ts).total_seconds() 
        for col in COLUMNS:
            print "%s:" % col,
            print " "*(20 - len(col)),
            #print if col.endswith('') then "%.2f" % points[i]/delta_ts else points[i]
            print "%.2f" % (points[i]/delta_ts)
            i += 1

    prev_stats = stats
    prev_ts = cur_ts

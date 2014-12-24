import sys
import os
import socket

import json

from cffi import FFI
ffi = FFI()
ffi.cdef(open(os.path.dirname(os.path.abspath(__file__)) +
              '/server/stats.h').read())

UDP_IP = "127.0.0.1"
UDP_PORT = 32032

stats = ffi.new("struct udp_stats *")
sock = socket.socket(socket.AF_INET, # Internet
                     socket.SOCK_DGRAM) # UDP
sock.bind((UDP_IP, UDP_PORT))

while True:
    data, addr = sock.recvfrom_into(ffi.buffer(stats)) # buffer size is 1024 bytes
    stats_packing = [stats.stats_packing[i] for i in range(1, stats.n_stats_packing)]
    print json.dumps(dict(
        decomp_failed = stats.decomp_failed,
        decomp_total = stats.decomp_total,

        comp_failed = stats.comp_failed,
        comp_total = stats.comp_total,

        head_comp_size = stats.head_comp_size,
        head_uncomp_size = stats.head_uncomp_size,

        total_comp_size = stats.total_comp_size,
        total_uncomp_size = stats.total_uncomp_size,

        unpack_failed = stats.unpack_failed,
        total_received = stats.total_received,

        stats_packing = stats_packing,
        dst_addr = socket.inet_ntoa(ffi.buffer(ffi.addressof(stats.dst_addr))),
        src_addr = socket.inet_ntoa(ffi.buffer(ffi.addressof(stats.src_addr))),
#        n_stats_packing = stats.n_stats_packing,
    ), indent=2)

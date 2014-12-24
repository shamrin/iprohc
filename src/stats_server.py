import sys
from cffi import FFI
ffi = FFI()
ffi.cdef("""
struct in_addr {
   uint32_t       s_addr;     /* address in network byte order */
};

struct udp_stats {
  /* fields from struct statitics */
  int decomp_failed;
  int decomp_total;

  int comp_failed;
  int comp_total;

  int head_comp_size;
  int head_uncomp_size;

  int total_comp_size;
  int total_uncomp_size;

  int unpack_failed;
  int total_received;

  int n_stats_packing;
  int stats_packing[10];

  struct in_addr dst_addr;
  struct in_addr src_addr;
};
""")

import socket
import json

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

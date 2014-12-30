"""Send (momentary) stats to InfluxDB

Usage:
    python stats_server.py

To view InfluxDB data, go to its admin interface (on :8083), and query `rohc` DB
with `select * from client.<address>`. E.g.: `select * from "client.172-30-1-175"`
"""

import sys
import os
import socket
import json
import time
import warnings
from datetime import datetime

import requests

from cffi import FFI

USE_INFLUX = True # send stats to influx server at INFLUX_URL
SHOW_STATS = True # dump stats to screen
INFLUX_URL = "http://localhost:8086/db/rohc/series?u=root&p=root"
UDP_IP = "127.0.0.1"
UDP_PORT = 32032
HTTP_OK = 200

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

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

ffi = FFI()
# load `struct udp_stats` definition
ffi.cdef(open(os.path.dirname(os.path.abspath(__file__)) +
              '/server/stats.h').read())
struct = ffi.new("struct udp_stats *")


def dump_to_screen(points, ip):
  print "\n",
  print '-'*13,
  print datetime.now().strftime('%H:%M:%S'),
  print '-'*15
  i = 0
  for col in COLUMNS:
    print "%s:" % col,
    print " "*(20 - len(col)),
    print "%.2f" % (points[i]/delta_ts)
    i += 1


def save_to_influx(points, ip):
  data = [dict(name = "client.%s" % ip.replace('.', '-'),
               columns = COLUMNS, points = [points])]
  r = requests.post(INFLUX_URL, json.dumps(data))
  if r.status_code != HTTP_OK:
    warings.warn(r.status_code, r.text)


prev_stats = None
prev_ts = datetime.now()

while True:
  nbytes, addr = sock.recvfrom_into(ffi.buffer(struct))
  if nbytes == 0:
    continue

  stats = [getattr(struct, col) for col in COLUMNS]
  cur_ts = datetime.now()
  delta_ts = (cur_ts - prev_ts).total_seconds() 

  if prev_stats is not None:
    points = [(now - prev)/delta_ts for prev, now in zip(prev_stats, stats)]
    ip = socket.inet_ntoa(ffi.buffer(ffi.addressof(struct.client_addr)))

    if SHOW_STATS:
      dump_to_screen(points, ip)

    if USE_INFLUX:
      save_to_influx(points, ip)

  prev_stats = stats
  prev_ts = cur_ts

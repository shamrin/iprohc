#!/bin/bash

#set -e

# interface on top of which tunnel is running
IFACE=em0

# use first argument as a sleep delay or default value of 1.5 seconds
SLEEP_TIME=${1:-1.5}

PIDFILE=/var/run/iprohc_server.pid
if [ -f $PIDFILE ]; then
  echo "iprohc server is running, restarting..."
  sudo kill $(cat $PIDFILE)
  sleep 2
fi

sudo $(dirname $0)/server/iprohc_server -b $IFACE > /dev/null &
sleep 2

PID=$(cat $PIDFILE)

if [ -z "$PID" ]; then
  echo "empty pidfile"
  exit 2
fi

(
  while sleep $SLEEP_TIME; do
    sudo kill -USR1 $PID
  done
) &

echo "stats:"
python $(dirname $0)/periodic_stats.py

# wait for subshells
wait
echo "done."

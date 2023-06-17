#!/bin/sh

exec /app/bin/multiserver -h "$HOST" -p "$PORT" -d /app/data

#!/usr/bin/env bash
#
# Generates a large, realistic log file for performance testing.
#
# Usage:
#   ./generate-large-log.sh [size_mb] [output_file]
#
# Arguments:
#   size_mb      Target file size in megabytes (default: 200)
#   output_file  Output file path (default: large-test.log)
#
# Examples:
#   ./generate-large-log.sh              # 200MB -> large-test.log
#   ./generate-large-log.sh 500          # 500MB -> large-test.log
#   ./generate-large-log.sh 50 my.log    # 50MB  -> my.log

set -euo pipefail

TARGET_MB="${1:-200}"
OUTPUT="${2:-large-test.log}"

echo "Generating ~${TARGET_MB}MB log file -> ${OUTPUT}" >&2

python3 -c "
import random, sys, io

target_bytes = int(sys.argv[1]) * 1024 * 1024
output_path = sys.argv[2]

random.seed(42)

SERVICES = ['api-gateway', 'user-service', 'order-service', 'payment-service', 'inventory-service', 'auth-service', 'notification-service', 'search-service']
METHODS = ['GET', 'GET', 'GET', 'POST', 'PUT', 'DELETE']
PATHS = ['/api/users', '/api/orders', '/api/products', '/api/payments', '/api/inventory', '/api/auth/token', '/api/search', '/api/notifications', '/api/health', '/api/users/profile', '/api/orders/recent', '/api/products/featured']
LEVELS_NORMAL = ['INFO'] * 5 + ['DEBUG'] * 2 + ['WARN', 'ERROR', 'TRACE']
LEVELS_BURST_CDF = [(30, 'ERROR'), (50, 'WARN'), (80, 'INFO'), (95, 'DEBUG'), (100, 'TRACE')]

ERROR_MSGS = [
    'Connection pool exhausted, waiting for available connection',
    'Request timeout after 30000ms',
    'Circuit breaker open for downstream service',
    'Database query exceeded slow query threshold',
    'Failed to serialize response payload',
    'Authentication token expired',
    'Rate limit exceeded for client',
    'DNS resolution failed for upstream host',
    'Out of memory: cannot allocate 64MB buffer',
    'Deadlock detected in transaction',
]
WARN_MSGS = [
    'Response time above p95 threshold',
    'Connection pool utilization above 80%',
    'Retry attempt for downstream call',
    'Cache miss rate above threshold',
    'Memory usage above 80% of limit',
    'Deprecated API version used by client',
    'GC pause exceeded 100ms',
    'Slow query detected',
]

STACK_TRACE = '''com.example.app.exception.ServiceException: Operation failed
\tat com.example.app.service.OrderService.processOrder(OrderService.java:142)
\tat com.example.app.service.OrderService.createOrder(OrderService.java:89)
\tat com.example.app.api.OrderController.create(OrderController.java:56)
\tat org.springframework.web.servlet.FrameworkServlet.service(FrameworkServlet.java:897)
\tat javax.servlet.http.HttpServlet.service(HttpServlet.java:750)
Caused by: java.sql.SQLException: Connection is not available, request timed out after 30000ms
\tat com.zaxxer.hikari.pool.HikariPool.createTimeoutException(HikariPool.java:669)
\tat com.zaxxer.hikari.pool.HikariPool.getConnection(HikariPool.java:183)
\t... 12 more'''

FORMATS = ['standard', 'standard', 'standard', 'json', 'kv']

# Start time: 2024-01-15 00:00:00 UTC
base_epoch = 1705276800
current_epoch = base_epoch
burst_interval = 600
burst_duration = 60

written = 0
line_count = 0
last_report_mb = 0

# Buffered writing for performance
buf = io.StringIO()
buf_size = 0
FLUSH_THRESHOLD = 1024 * 1024  # flush every ~1MB

f = open(output_path, 'w')

def format_timestamp(epoch, ms):
    # Avoid datetime overhead: compute directly
    s = epoch
    days = s // 86400
    rem = s % 86400
    h = rem // 3600
    rem %= 3600
    m = rem // 60
    sec = rem % 60
    # Days since 1970-01-01 to Y-M-D
    # Simple algorithm for dates in 2024 range
    y = 1970
    while True:
        leap = (y % 4 == 0 and (y % 100 != 0 or y % 400 == 0))
        ydays = 366 if leap else 365
        if days < ydays:
            break
        days -= ydays
        y += 1
    leap = (y % 4 == 0 and (y % 100 != 0 or y % 400 == 0))
    month_days = [31, 29 if leap else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    mo = 0
    while days >= month_days[mo]:
        days -= month_days[mo]
        mo += 1
    return f'{y:04d}-{mo+1:02d}-{days+1:02d}T{h:02d}:{m:02d}:{sec:02d}.{ms:03d}Z'

while written < target_bytes:
    current_epoch += random.randint(20, 800)
    ms = random.randint(0, 999)
    timestamp = format_timestamp(current_epoch, ms)

    time_in_cycle = (current_epoch - base_epoch) % burst_interval
    in_burst = time_in_cycle >= (burst_interval - burst_duration)

    if in_burst:
        r = random.randint(0, 99)
        for threshold, lv in LEVELS_BURST_CDF:
            if r < threshold:
                level = lv
                break
    else:
        level = random.choice(LEVELS_NORMAL)

    service = random.choice(SERVICES)
    method = random.choice(METHODS)
    path = random.choice(PATHS)
    duration = random.randint(1, 500)
    status = 200
    trace_id = f'{random.getrandbits(64):016x}'

    if level == 'ERROR':
        status = 500 + random.randint(0, 4)
        duration = random.randint(500, 30000)
    elif level == 'WARN' and random.randint(0, 2) == 0:
        status = 429

    fmt = random.choice(FORMATS)
    level_lower = level.lower()

    if fmt == 'standard':
        if level == 'ERROR':
            msg = random.choice(ERROR_MSGS)
        elif level == 'WARN':
            msg = random.choice(WARN_MSGS)
        else:
            msg = f'request completed method={method} path={path} status={status} duration={duration}ms'
        line = f'{timestamp} [{level}] [{service}] {msg} trace_id={trace_id}'
        if level == 'ERROR' and random.randint(0, 4) == 0:
            line = line + '\n' + STACK_TRACE
    elif fmt == 'json':
        line = f'{{\"timestamp\":\"{timestamp}\",\"level\":\"{level_lower}\",\"service\":\"{service}\",\"msg\":\"request completed\",\"method\":\"{method}\",\"path\":\"{path}\",\"status\":{status},\"duration_ms\":{duration},\"trace_id\":\"{trace_id}\"}}'
    else:
        line = f'ts={timestamp} level={level_lower} service={service} msg=\"request completed\" method={method} path={path} status={status} duration={duration}ms trace_id={trace_id}'

    buf.write(line)
    buf.write('\n')
    line_len = len(line) + 1
    written += line_len
    buf_size += line_len
    line_count += 1

    if buf_size >= FLUSH_THRESHOLD:
        f.write(buf.getvalue())
        buf = io.StringIO()
        buf_size = 0

        mb_written = written // (1024 * 1024)
        if mb_written >= last_report_mb + 10:
            print(f'  {mb_written}MB / {target_bytes // (1024*1024)}MB written ({line_count} lines)...', file=sys.stderr)
            last_report_mb = mb_written

# Flush remaining
if buf_size > 0:
    f.write(buf.getvalue())
f.close()

import os
final_mb = os.path.getsize(output_path) // (1024 * 1024)
print(f'Done! Wrote {line_count} lines, {final_mb}MB -> {output_path}', file=sys.stderr)
" "$TARGET_MB" "$OUTPUT"

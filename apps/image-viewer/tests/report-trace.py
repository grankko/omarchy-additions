#!/usr/bin/env python3
"""Summarize request-to-swap samples without image names or paths."""

import argparse
import collections
import math
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=argparse.FileType("r"))
    args = parser.parse_args()
    requests = {}
    kinds = {}
    samples = collections.defaultdict(list)
    max_bytes = collections.defaultdict(int)
    for line in args.trace:
        _, marker, payload = line.partition("viewer-trace ")
        fields = (marker + payload).split()
        if len(fields) != 5:
            continue
        timestamp, event, request, value = fields[1:]
        if event == "request":
            requests[request] = int(timestamp)
        elif event in {"cache-hit", "cache-miss"}:
            kinds[request] = event
        elif event == "frame-submitted" and request in requests:
            elapsed_ms = (int(timestamp) - requests.pop(request)) / 1_000_000
            samples[kinds.pop(request, "unknown")].append(elapsed_ms)
        elif event in {"cache-bytes", "frame-installed", "decode-complete"}:
            max_bytes[event] = max(max_bytes[event], int(value))

    if not samples:
        parser.error("no request-to-submission pairs")
    for kind, values in sorted(samples.items()):
        values.sort()
        p95 = values[math.ceil(len(values) * 0.95) - 1]
        print(f"{kind}: n={len(values)}, median={statistics.median(values):.2f} ms, p95={p95:.2f} ms")
    for kind, value in sorted(max_bytes.items()):
        print(f"{kind} max: {value} bytes")


if __name__ == "__main__":
    main()

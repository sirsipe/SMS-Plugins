#!/usr/bin/env python3
"""Compare two full-engine benchmark binaries using alternating run order."""
import argparse
import csv
import io
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', required=True, type=Path)
    parser.add_argument('--after', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cpu', type=int, help='Linux CPU affinity; omit for no pinning')
    parser.add_argument('--iterations', type=int, default=200)
    parser.add_argument('--warmups', type=int, default=16)
    parser.add_argument('--rounds', type=int, default=3)
    args = parser.parse_args()
    if args.rounds < 1 or args.iterations < 1 or args.warmups < 0:
        parser.error('rounds/iterations must be positive and warmups nonnegative')
    if args.cpu is not None:
        if not hasattr(os, 'sched_setaffinity'):
            parser.error('--cpu requires Linux sched_setaffinity')
        os.sched_setaffinity(0, {args.cpu})
    binaries = {'before': args.before.resolve(), 'after': args.after.resolve()}
    with args.output.open('w', newline='', encoding='utf-8') as output:
        writer = None
        for round_number in range(args.rounds):
            for label in (('before', 'after') if round_number % 2 == 0 else ('after', 'before')):
                command = [str(binaries[label]), '--label', f'{label}-r{round_number}',
                           '--iterations', str(args.iterations), '--warmups', str(args.warmups)]
                if label == 'after':
                    command.append('--expect-cap96')
                result = subprocess.run(command, text=True, capture_output=True, check=True)
                reader = csv.DictReader(io.StringIO(result.stdout))
                if writer is None:
                    writer = csv.DictWriter(output, fieldnames=reader.fieldnames, lineterminator="\n")
                    writer.writeheader()
                elif reader.fieldnames != writer.fieldnames:
                    raise RuntimeError('benchmark binaries have different CSV schemas')
                rows = list(reader)
                if not rows:
                    raise RuntimeError('benchmark binary returned no scenarios')
                writer.writerows(rows)
                output.flush()
                print(f'{label}-r{round_number}: {len(rows)} scenarios complete', flush=True)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Детерминированный генератор большого VCD для стенда производительности.

Состав: 70 % однобитных сигналов (половина - клоки разных периодов, половина - случайные
переключения), 20 % шин шириной 8-64, 10 % real (затухающие синусы с шумом). Иерархия:
top, до 30 модулей первого уровня, в каждом до 30 модулей второго уровня; сигналы лежат
во втором уровне. Изменения пишутся, пока размер файла меньше --size-mib.
Только стандартная библиотека.
"""

import argparse
import heapq
import math
import random
import sys

FANOUT = 30
TIMESCALE_PS = 1


def identifier(n):
    chars = []
    while True:
        chars.append(chr(33 + n % 94))
        n //= 94
        if n == 0:
            return "".join(chars)
        n -= 1


def build_signals(count, rng):
    """Список (kind, width, param) в порядке id; kind: clock, bit, bus, real."""
    signals = []
    bits = 0
    for i in range(count):
        slot = i % 10
        if slot < 7:
            if bits % 2 == 0:
                signals.append(("clock", 1, 500 * 2 ** rng.randint(0, 7)))
            else:
                signals.append(("bit", 1, rng.randint(2000, 400000)))
            bits += 1
        elif slot < 9:
            width = rng.choice([8, 12, 16, 24, 32, 48, 64])
            signals.append(("bus", width, rng.randint(1000, 200000)))
        else:
            signals.append(("real", 64, rng.randint(5000, 50000)))
    return signals


def scope_layout(count):
    fan = max(1, min(FANOUT, math.ceil(math.sqrt(count / 4))))
    leaves = fan * fan
    return fan, [i * leaves // count for i in range(count)]


def write_header(out, signals, ids):
    count = len(signals)
    fan, leaf_of = scope_layout(count)
    out("$date deterministic $end\n$version gen_vcd.py $end\n$timescale 1ps $end\n")
    out("$scope module top $end\n")
    current = None
    for i, (kind, width, _) in enumerate(signals):
        leaf = leaf_of[i]
        if leaf != current:
            if current is not None:
                out("$upscope $end\n$upscope $end\n" if (current // fan) != (leaf // fan) else "$upscope $end\n")
                if (current // fan) != (leaf // fan):
                    out(f"$scope module m{leaf // fan} $end\n")
            else:
                out(f"$scope module m{leaf // fan} $end\n")
            out(f"$scope module u{leaf % fan} $end\n")
            current = leaf
        if kind == "real":
            out(f"$var real 64 {ids[i]} r{i} $end\n")
        elif width == 1:
            out(f"$var wire 1 {ids[i]} s{i} $end\n")
        else:
            out(f"$var wire {width} {ids[i]} b{i} [{width - 1}:0] $end\n")
    out("$upscope $end\n$upscope $end\n$upscope $end\n$enddefinitions $end\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True)
    ap.add_argument("--signals", type=int, default=10000)
    ap.add_argument("--size-mib", type=float, default=600)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    if args.signals < 1:
        ap.error("--signals must be positive")

    rng = random.Random(args.seed)
    signals = build_signals(args.signals, rng)
    ids = [identifier(i) for i in range(len(signals))]
    target = int(args.size_mib * 1024 * 1024)

    with open(args.out, "w", buffering=1 << 20, newline="\n") as f:
        size = 0

        def out(s):
            nonlocal size
            f.write(s)
            size += len(s)

        write_header(out, signals, ids)
        out("#0\n$dumpvars\n")
        heap = []
        real_state = {}
        rand_bits = rng.getrandbits
        for i, (kind, width, param) in enumerate(signals):
            if kind == "real":
                real_state[i] = [rng.uniform(0.5, 5.0), rng.uniform(20, 200) * param, 0.0]
                out(f"r0 {ids[i]}\n")
            elif width == 1:
                out(f"0{ids[i]}\n")
            else:
                out(f"b0 {ids[i]}\n")
            first = param if kind == "clock" else rng.randint(1, param)
            heap.append((first, i))
        out("$end\n")
        heapq.heapify(heap)

        clock_level = {}
        last_t = 0
        buf = []
        pending = 0
        uniform = rng.random
        randint = rng.randint
        while size + pending < target:
            t, i = heap[0]
            kind, width, param = signals[i]
            if t != last_t:
                line = f"#{t}\n"
                last_t = t
            else:
                line = ""
            ident = ids[i]
            if kind == "clock":
                level = clock_level.get(i, 0) ^ 1
                clock_level[i] = level
                line += f"{level}{ident}\n"
                nxt = t + param
            elif kind == "bit":
                r = uniform()
                v = "x" if r < 0.01 else ("z" if r < 0.02 else str(randint(0, 1)))
                line += f"{v}{ident}\n"
                nxt = t + 1 + int(rng.expovariate(1.0 / param))
            elif kind == "bus":
                if uniform() < 0.01:
                    digits = "".join(rng.choice("01xz") for _ in range(width))
                else:
                    digits = format(rand_bits(width), "b")
                line += f"b{digits} {ident}\n"
                nxt = t + 1 + int(rng.expovariate(1.0 / param))
            else:
                amp, tau, phase = real_state[i]
                if t > 6 * tau:
                    real_state[i] = [rng.uniform(0.5, 5.0), tau, t]
                    amp, phase = real_state[i][0], t
                value = amp * math.exp(-(t - phase) / tau) * math.sin(2 * math.pi * (t - phase) / (param * 40)) \
                    + rng.gauss(0, 0.01)
                line += f"r{value:.6g} {ident}\n"
                nxt = t + param
            heapq.heapreplace(heap, (nxt, i))
            buf.append(line)
            pending += len(line)
            if len(buf) >= 20000:
                out("".join(buf))
                buf = []
                pending = 0
        out("".join(buf))
        out(f"#{last_t + 1}\n")


if __name__ == "__main__":
    sys.exit(main())

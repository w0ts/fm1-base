#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The trial's 64-bit multiplies clear of loads (ir_pass.py's second step, on every unit of the device build).

felucca-bsp's docs/CPU.md: on the FM-1, a 32x32->64-bit multiply (`r9_r8 = r3 * r15 (s)`) with a load in flight
beside it now and then leaves a register a bit off. tools/build.py's check (mul64_near_loads) lists the functions
where a load is one or two instructions before a 64-bit multiply, or after it before its product is used. CPU.md's
cure is a helper of its own (noinline), which cost fm1-radio half its decoder's speed. This pass gives each 64-bit
multiply the same clearance in line instead: the multiply becomes a small piece of inline assembly,

    nop                      two instructions with no load before the multiply
    nop
    rH_rL = rA * rB (s)      the multiply (or (u)), and a multiply-accumulate where C adds two products
    rH_rL += rC * rD (s)     (or rH_rL += rA * rB onto a 64-bit sum C adds the product to)
    rH_rL >>>= K            C's shift when one follows (>>>= arithmetic, >>= logical)
    rH = rH                  a word of the product read at once (the check counts a word, not the pair, as a use)

so nothing the compiler schedules can come between. The arithmetic is C's own, exactly: the same multiply, the
same shift; a 64-bit operand is taken apart as the compiler takes it (its low words' 64-bit product plus the two
cross products' low words, 32 bits up), so the firmware computes what it computed before, bit for bit.

Left as they were: a multiply by a power of two (a shift), one whose product is only truncated to 32 bits (the
compiler makes it a 32-bit multiply), and constant products. OPT_MUL64=0 turns the pass off (the build as before).

Works on the IR clang emits with -disable-llvm-optzns (tools/build.py: before the optimiser): each product a
numbered value with one use, its operands sext / zext of i32 or constants or i64 values."""
import re

NUM = r"%[-\w.]+"
EXT = re.compile(r"^\s*(" + NUM + r") = (sext|zext) i32 (\S+) to i64(?:, !.*)?$")
MUL = re.compile(r"^(\s*)(" + NUM + r") = mul (?:nuw )?(?:nsw )?i64 (\S+), (\S+?)(?:, !.*)?$")
ADD = re.compile(r"^(\s*)(" + NUM + r") = add (?:nuw )?(?:nsw )?i64 (\S+), (\S+?)(?:, !.*)?$")
SHR = re.compile(r"^(\s*)(" + NUM + r") = (ashr|lshr)(?: exact)? i64 (\S+), (\d+)(?:, !.*)?$")
TRUNC = re.compile(r"^\s*" + NUM + r" = trunc i64 (\S+) to i32")
CONST = re.compile(r"-?\d+")
DEAD = "add i64 0, 0"           # (a numbered value kept defined: the numbering stays whole)


def as_i32(v):
    """an integer constant as IR prints an i32 (signed)"""
    n = int(v) & 0xFFFFFFFF
    return str(n - (1 << 32) if n >= 1 << 31 else n)


class Fn:
    """one function's body: its lines and, per value, its defining line and how many times it is used"""

    def __init__(self, lines):
        self.lines = lines
        self.ext = {}
        self.defs = {}
        self.uses = {}
        for i, ln in enumerate(lines):
            d = re.match(r"^\s*(" + NUM + r") = ", ln)
            if d:
                self.defs[d.group(1)] = i
            e = EXT.match(ln)
            if e:
                self.ext[e.group(1)] = (e.group(2), e.group(3))
            rhs = ln[d.end():] if d else ln
            for v in re.findall(NUM + r"(?![-\w.])", rhs):
                self.uses[v] = self.uses.get(v, 0) + 1

    def user(self, v):
        """the one line using v, or None"""
        if self.uses.get(v) != 1:
            return None
        pat = re.compile(re.escape(v) + r"(?![-\w.])")
        for i, ln in enumerate(self.lines):
            d = re.match(r"^\s*" + NUM + r" = ", ln)
            if i != self.defs.get(v) and pat.search(ln[d.end():] if d else ln):
                return i
        return None


def form(fn, v):
    """an operand's form: ('s', i32) a sign-extended word or a constant that fits one, ('u', i32) likewise
    zero-extended, ('p2',) a power of two, ('c', v) another constant, ('w', v) a 64-bit value"""
    if v in fn.ext:
        kind, src = fn.ext[v]
        return ("s" if kind == "sext" else "u", src)
    if CONST.fullmatch(v):
        n = int(v)
        if n > 0 and n & (n - 1) == 0:
            return ("p2",)
        return ("su" if 0 <= n < 1 << 31 else "s" if -(1 << 31) <= n < 0 else "u" if n < 1 << 32 else "c",
                as_i32(v) if -(1 << 31) <= n < 1 << 32 else v)
    return ("w", v)


def sign(fa, fb):
    """'s' or 'u' when a single 32x32->64 multiply makes the product, else None"""
    ka, kb = fa[0], fb[0]
    for k in ("s", "u"):
        if ka in (k, "su") and kb in (k, "su") and not (ka == kb == "su"):
            return k
    return None


class Pass:
    def __init__(self, model=False):
        self.n = 0
        self.kinds = {}
        self.model = model

    def plain(self, ind, dst, prods, acc, shift):
        """what the asm computes, as plain IR (model: the host's check of what this pass takes apart and puts
        together: mul64proof.py, the host build's samples the same with it)"""
        self.n += 1
        out, terms = [], []
        for j, (s, a, b) in enumerate(prods):
            ext = "sext" if s == "s" else "zext"
            xa, xb, m = self.tmp(f"ma{j}"), self.tmp(f"mb{j}"), self.tmp(f"mm{j}")
            out += [f"{ind}{xa} = {ext} i32 {a} to i64", f"{ind}{xb} = {ext} i32 {b} to i64",
                    f"{ind}{m} = mul i64 {xa}, {xb}"]
            terms.append(m)
        total = acc if acc is not None else terms.pop(0)
        for j, t in enumerate(terms):
            nxt = self.tmp(f"ms{j}")
            out.append(f"{ind}{nxt} = add i64 {total}, {t}")
            total = nxt
        out.append(f"{ind}{dst} = {shift[0]} i64 {total}, {shift[1]}" if shift else f"{ind}{dst} = add i64 {total}, 0")
        return "\n".join(out)

    def tmp(self, what):
        return f"%m64.{self.n}.{what}"

    def count(self, k):
        self.kinds[k] = self.kinds.get(k, 0) + 1

    def asm(self, ind, dst, prods, acc, shift):
        """the call that makes dst: prods [(sign, a, b)], acc an i64 added first (or None), shift (op, K) or None"""
        if self.model:
            return self.plain(ind, dst, prods, acc, shift)
        body, cons, args, k = ["nop", "nop"], ["=&r" if len(prods) > 1 else "=r"], [], 1
        if acc is not None:
            cons, args = ["=r"], []
        for j, (s, a, b) in enumerate(prods):
            op = "+=" if j or acc is not None else "="
            body.append(f"$0 {op} ${k} * ${k + 1} ({s})")
            cons += ["r", "r"]
            args += [f"i32 {a}", f"i32 {b}"]
            k += 2
        if acc is not None:
            cons.append("0")
            args.append(f"i64 {acc}")
        if shift:
            body.append(f"$0 {'>>>=' if shift[0] == 'ashr' else '>>='} {shift[1]}")
        body.append("$0.h = $0.h")                  # (a word of the product read: the multiply has finished)
        text = "\\0A".join(body)                     # (IR's newline in a string)
        return f'{ind}{dst} = call i64 asm "{text}", "{",".join(cons)}"({", ".join(args)})'

    def wide(self, fn, i, ind, dst, fa, fb):
        """dst = a * b with a 64-bit operand: the low words' 64-bit product (unsigned) in line, then the cross
        products' low words added 32 bits up, as the compiler lowers a 64-bit multiply"""
        self.n += 1
        out = []

        def parts(f, tag):
            if f[0] == "w":
                lo, hi, sh = self.tmp(tag + "lo"), self.tmp(tag + "hi"), self.tmp(tag + "sh")
                out.extend([f"{ind}{lo} = trunc i64 {f[1]} to i32", f"{ind}{sh} = lshr i64 {f[1]}, 32",
                            f"{ind}{hi} = trunc i64 {sh} to i32"])
                return lo, hi
            if f[0] in ("s", "u", "su"):
                hi = "0"
                if f[0] == "s":
                    hi = self.tmp(tag + "hi")
                    out.append(f"{ind}{hi} = ashr i32 {f[1]}, 31")
                return f[1], hi
            n = int(f[1]) & ((1 << 64) - 1)          # (a constant over 32 bits)
            return as_i32(n & 0xFFFFFFFF), as_i32(n >> 32)
        alo, ahi = parts(fa, "a")
        blo, bhi = parts(fb, "b")
        p, x1, x2, x, xz, xs = (self.tmp(t) for t in ("p", "x1", "x2", "x", "xz", "xs"))
        out.append(self.asm(ind, p, [("u", alo, blo)], None, None))
        out += [f"{ind}{x1} = mul i32 {alo}, {bhi}", f"{ind}{x2} = mul i32 {ahi}, {blo}",
                f"{ind}{x} = add i32 {x1}, {x2}", f"{ind}{xz} = zext i32 {x} to i64",
                f"{ind}{xs} = shl i64 {xz}, 32", f"{ind}{dst} = add i64 {p}, {xs}"]
        self.count("64-bit operand")
        return out

    def function(self, lines):
        fn = Fn(lines)
        new = list(lines)
        done = set()
        for i, ln in enumerate(lines):
            m = MUL.match(ln)
            if not m or i in done:
                continue
            ind, dst, a, b = m.groups()
            fa, fb = form(fn, a), form(fn, b)
            if "p2" in (fa[0], fb[0]) or (CONST.fullmatch(a) and CONST.fullmatch(b)):
                self.count("left: a shift or a constant")
                continue
            u = fn.user(dst)
            if u is not None and TRUNC.match(lines[u]):
                self.count("left: truncated to 32 bits")
                continue
            s = sign(fa, fb)
            if s is None:
                new[i] = "\n".join(self.wide(fn, i, ind, dst, fa, fb))
                continue
            prods, acc, last, top = [(s, fa[1], fb[1])], None, i, dst
            if u is not None and (ma := ADD.match(lines[u])):    # a product added: a multiply-accumulate
                other = ma.group(4) if ma.group(3) == dst else ma.group(3)
                oi = fn.defs.get(other)
                mo = MUL.match(lines[oi]) if oi is not None and oi not in done and oi != i else None
                fo = (form(fn, mo.group(3)), form(fn, mo.group(4))) if mo else None
                if mo and fn.user(other) == u and sign(*fo) == s and "p2" not in (fo[0][0], fo[1][0]):
                    prods.append((s, fo[0][1], fo[1][1]))
                    new[oi] = f"{ind}{other} = {DEAD}"
                    done.add(oi)
                elif not CONST.fullmatch(other) and other != dst:
                    acc = other
                else:
                    ma = None
                if ma:
                    new[i] = f"{ind}{dst} = {DEAD}"
                    last, top = u, ma.group(2)
                    u = fn.user(top)
            shift = None
            if u is not None and (ms := SHR.match(lines[u])) and ms.group(4) == top and 0 < int(ms.group(5)) < 64:
                shift = (ms.group(3), ms.group(5))
                if last == i:
                    new[i] = f"{ind}{dst} = {DEAD}"
                else:
                    new[last] = f"{ind}{top} = {DEAD}"
                last, top = u, ms.group(2)
            new[last] = self.asm(ind, top, prods, acc, shift)
            done.add(last)
            self.count(("multiply-accumulate" if len(prods) > 1 else "accumulate" if acc else "multiply")
                       + (" and shift" if shift else ""))
        return new


def run(ir, model=False):
    """the IR with every 64-bit multiply in line and clear of loads; and what was done, as a line. model: the
    pieces of assembly as plain IR instead (for a host build: the same values, checked by running it)"""
    p, out, body = Pass(model), [], None
    for ln in ir.split("\n"):
        if body is not None:
            body.append(ln)
            if ln == "}":
                out += p.function(body)
                body = None
            continue
        out.append(ln)
        if ln.startswith("define ") and ln.rstrip().endswith("{"):
            body = []
    text = "\n".join(out)
    return text, ", ".join(f"{v} {k}" for k, v in sorted(p.kinds.items(), key=lambda kv: -kv[1]))

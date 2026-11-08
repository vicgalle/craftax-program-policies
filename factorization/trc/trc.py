#!/usr/bin/env python3
"""trc: front end of TR, the rule language of the decompiled policy.

A TR program is an ordered list of rules, run every decision from the top; the
first rule that emits an action wins (a teleo-reactive program in Nilsson's
sense). Rule bodies are a small C-like language:

    const NAME = expr;                       compile-time constant (int or float)
    const int NAME[] = {1, 2, 3};            constant table (int or float)
    ctx { int fire, prep; float x; }         per-decision variables, zeroed each decision
    def int name(int p, out int q) { ... }   procedure (no recursion); an out parameter refers to
                                             the caller's variable, passed as &v (or a caller's out parameter)
    def void init() / observe(int changed) / int filter(int action, out int skill) / commit(int action)
                                             entry points the interpreter calls at reset, after each
                                             observation, on every chosen action and at commit
    rule name [module] { ... }               rule, in priority order

    statements: declarations (int/float, local arrays int v[N] = {...}), assignments (= += -= ... ++ --), if/else,
                for (C form), break, continue, `try E as S;` (emit E as skill S when
                E >= 0), `emit E as S;` (always emit), `pass;` (leave the rule),
                `return E;` (in defs), expression statements, blocks
    expressions: C operators and precedence, int/float with C's conversions,
                 a.<field> (agent memory, writable), o.<field> (observation),
                 cfg.<field> (configuration), ctx.<name>, calls of defs and of the
                 interpreter's natives, &local (an int out-argument), int(e), float(e);
                 a.<record> = a.<record> copies a record, clear(a.<path>) zeroes one

The interpreter (I) supplies the natives, the machine constants and the record
layouts (schema.json); this front end knows nothing about any particular program.
It lowers a program to a flat, typed node image and writes it as a C++ header
used by both execution modes of the interpreter: dynamic interpretation (the
image is opaque runtime data) and static specialisation (the image is a
constexpr template argument).

    trc.py PROGRAM.tr --interp DIR -o OUT.hpp [--image OUT.trb] [--listing OUT.txt]
"""
import argparse, json, os, re, struct, sys

# --------------------------------------------------------------------------- ops
OPS = """
KI KF LOC LDM TAB CALLN CALLD LOCREF
NEG_I NEG_F NOT_I NOT_F BNOT I2F F2I TRUTH_F
ADD_I SUB_I MUL_I DIV_I MOD_I SHL SHR BAND BOR BXOR
EQ_I NE_I LT_I LE_I GT_I GE_I
ADD_F SUB_F MUL_F DIV_F EQ_F NE_F LT_F LE_F GT_F GE_F
LAND LOR COND ASG_L ASG_M
SEQ IF FOR BRK CONT TRY EMIT PASS RET EXPR NOP
LDR ASG_R LDX ASG_X TABP ADDR COPY ZERO
""".split()
OP = {name: i for i, name in enumerate(OPS)}
VOID, INT, FLOAT = 0, 1, 2
TYNAME = {VOID: "void", INT: "int", FLOAT: "float"}
# memory widths (low 4 bits of w); bitfields add shift << 4 and width << 8
W = {"i32": 1, "f32": 2, "u8": 3, "i8": 4, "u16": 5, "i16": 6, "bit": 7}
SCALAR_SIZE = {"i32": 4, "f32": 4, "u8": 1, "i8": 1, "u16": 2, "i16": 2}
ROOTS = {"a": (0, "FPAgent", True), "o": (1, "FPObservation", False), "cfg": (2, "FPConfig", False)}
MODULES = ["core", "nav", "supply", "combat", "build"]
# Entry points the interpreter calls besides the rules: name -> (return type, parameters)
HOOKS = {"init": (VOID, []), "observe": (VOID, [(INT, False)]), "filter": (INT, [(INT, False), (INT, True)]),
         "commit": (VOID, [(INT, False)])}


class TRError(Exception):
    pass


# ------------------------------------------------------------------------- lexer
TOKEN = re.compile(r"""
 (?P<ws>\s+) | (?P<lc>//[^\n]*) | (?P<bc>/\*.*?\*/) |
 (?P<float>(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?[fF]?|\d+[eE][+-]?\d+[fF]?|\d+[fF]) |
 (?P<int>0[xX][0-9a-fA-F]+|\d+) |
 (?P<id>[A-Za-z_]\w*) |
 (?P<op><<=|>>=|\+\+|--|->|&&|\|\||<<|>>|<=|>=|==|!=|\+=|-=|\*=|/=|%=|&=|\|=|\^=|[-+*/%<>=!~&|^?:;,.(){}\[\]])
""", re.S | re.X)
KEYWORDS = {"const", "ctx", "def", "rule", "int", "float", "void", "if", "else", "for", "while", "break",
            "continue", "return", "try", "emit", "as", "pass"}


class Tok:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind, text, line):
        self.kind, self.text, self.line = kind, text, line

    def __repr__(self):
        return f"{self.text!r}@{self.line}"


def lex(src):
    out, pos, line = [], 0, 1
    while pos < len(src):
        m = TOKEN.match(src, pos)
        if not m:
            raise TRError(f"line {line}: unexpected character {src[pos]!r}")
        kind, text = m.lastgroup, m.group(0)
        if kind == "id" and text in KEYWORDS:
            kind = "kw"
        if kind not in ("ws", "lc", "bc"):
            out.append(Tok(kind, text, line))
        line += text.count("\n")
        pos = m.end()
    out.append(Tok("eof", "", line))
    return out


# ------------------------------------------------------------------------ parser
class Parser:
    def __init__(self, toks):
        self.t, self.i = toks, 0

    def peek(self, k=0):
        return self.t[self.i + k]

    def next(self):
        tok = self.t[self.i]
        self.i += 1
        return tok

    def at(self, text):
        return self.peek().text == text and self.peek().kind in ("op", "kw")

    def eat(self, text):
        if self.at(text):
            return self.next()
        return None

    def expect(self, text):
        tok = self.next()
        if tok.text != text:
            raise TRError(f"line {tok.line}: expected {text!r}, found {tok.text!r}")
        return tok

    def ident(self):
        tok = self.next()
        if tok.kind != "id":
            raise TRError(f"line {tok.line}: expected a name, found {tok.text!r}")
        return tok.text

    # program
    def program(self):
        decls = []
        while self.peek().kind != "eof":
            tok = self.peek()
            if self.eat("const") and (self.at("int") or self.at("float")):
                ty = self.type_name()
                name = self.ident()
                self.expect("[")
                self.expect("]")
                self.expect("=")
                decls.append(("table", name, ty, self.init_list(), tok.line))
                self.expect(";")
            elif tok.text == "const":
                name = self.ident()
                self.expect("=")
                decls.append(("const", name, self.expr(), tok.line))
                self.expect(";")
            elif self.eat("ctx"):
                self.expect("{")
                fields = []
                while not self.eat("}"):
                    ty = self.type_name()
                    while True:
                        fields.append((ty, self.ident()))
                        if not self.eat(","):
                            break
                    self.expect(";")
                decls.append(("ctx", fields, tok.line))
            elif self.eat("def"):
                ret = self.type_name(allow_void=True)
                name = self.ident()
                self.expect("(")
                params = []
                if not self.at(")"):
                    while True:
                        out = self.peek().kind == "id" and self.peek().text == "out"
                        if out:
                            self.next()
                        ty = self.type_name()
                        params.append((ty, self.ident(), out))
                        if not self.eat(","):
                            break
                self.expect(")")
                decls.append(("def", name, ret, params, self.block(), tok.line))
            elif self.eat("rule"):
                name = self.ident()
                module = "core"
                if self.eat("["):
                    module = self.ident()
                    self.expect("]")
                decls.append(("rule", name, module, self.block(), tok.line))
            else:
                raise TRError(f"line {tok.line}: expected const, ctx, def or rule, found {tok.text!r}")
        return decls

    def type_name(self, allow_void=False):
        tok = self.next()
        if tok.text in ("int", "float") or (allow_void and tok.text == "void"):
            return {"int": INT, "float": FLOAT, "void": VOID}[tok.text]
        raise TRError(f"line {tok.line}: expected a type, found {tok.text!r}")

    # statements
    def block(self):
        line = self.expect("{").line
        stmts = []
        while not self.eat("}"):
            stmts.append(self.stmt())
        return ("block", stmts, line)

    def stmt(self):
        tok = self.peek()
        line = tok.line
        if self.at("{"):
            return self.block()
        if self.at("int") or self.at("float"):
            s = self.decl()
            self.expect(";")
            return s
        if self.eat("if"):
            self.expect("(")
            c = self.expr()
            self.expect(")")
            then = self.stmt()
            other = self.stmt() if self.eat("else") else None
            return ("if", c, then, other, line)
        if self.eat("for"):
            self.expect("(")
            init = None
            if not self.at(";"):
                init = self.decl() if (self.at("int") or self.at("float")) else ("expr", self.expr(), line)
            self.expect(";")
            cond = None if self.at(";") else self.expr()
            self.expect(";")
            step = None if self.at(")") else ("expr", self.expr(), line)
            self.expect(")")
            return ("for", init, cond, step, self.stmt(), line)
        if self.eat("while"):
            self.expect("(")
            c = self.expr()
            self.expect(")")
            return ("for", None, c, None, self.stmt(), line)
        if self.eat("break"):
            self.expect(";")
            return ("break", line)
        if self.eat("continue"):
            self.expect(";")
            return ("continue", line)
        if self.eat("pass"):
            self.expect(";")
            return ("pass", line)
        if self.eat("return"):
            e = None if self.at(";") else self.expr()
            self.expect(";")
            return ("return", e, line)
        if self.eat("try") or self.eat("emit"):
            e = self.expr()
            self.expect("as")
            sk = self.expr()
            self.expect(";")
            return (tok.text, e, sk, line)
        if self.eat(";"):
            return ("block", [], line)
        e = self.expr()
        self.expect(";")
        return ("expr", e, line)

    def decl(self):
        line = self.peek().line
        ty = self.type_name()
        items = []
        while True:
            name = self.ident()
            size = None
            if self.eat("["):
                size = self.expr()
                self.expect("]")
            if size is not None:
                init = self.init_list() if self.eat("=") else None
            else:
                init = self.assign() if self.eat("=") else None
            items.append((name, init, size))
            if not self.eat(","):
                break
        return ("decl", ty, items, line)

    def init_list(self):
        self.expect("{")
        items = []
        while not self.at("}"):
            items.append(self.assign())
            if not self.eat(","):
                break
        self.expect("}")
        return items

    # expressions (C precedence)
    def expr(self):
        e = self.assign()
        while self.at(","):
            raise TRError(f"line {self.peek().line}: the comma operator is not supported")
        return e

    ASSIGN_OPS = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}

    def assign(self):
        left = self.ternary()
        if self.peek().kind == "op" and self.peek().text in self.ASSIGN_OPS:
            op = self.next()
            return ("assign", op.text, left, self.assign(), op.line)
        return left

    def ternary(self):
        c = self.binary(0)
        if self.at("?"):
            line = self.next().line
            a = self.assign()
            self.expect(":")
            b = self.assign()
            return ("cond", c, a, b, line)
        return c

    LEVELS = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!="], ["<", "<=", ">", ">="], ["<<", ">>"],
              ["+", "-"], ["*", "/", "%"]]

    def binary(self, level):
        if level == len(self.LEVELS):
            return self.unary()
        left = self.binary(level + 1)
        while self.peek().kind == "op" and self.peek().text in self.LEVELS[level]:
            op = self.next()
            left = ("bin", op.text, left, self.binary(level + 1), op.line)
        return left

    def unary(self):
        tok = self.peek()
        if tok.kind == "op" and tok.text in ("!", "-", "+", "~"):
            self.next()
            return ("un", tok.text, self.unary(), tok.line)
        if tok.kind == "op" and tok.text in ("++", "--"):
            self.next()
            return ("incdec", tok.text, True, self.unary(), tok.line)
        if tok.kind == "op" and tok.text == "&":
            self.next()
            return ("addr", self.unary(), tok.line)
        if tok.kind == "op" and tok.text == "(" and self.peek(1).text in ("int", "float") and self.peek(2).text == ")":
            self.next()
            ty = self.type_name()
            self.expect(")")
            return ("cast", ty, self.unary(), tok.line)
        return self.postfix()

    def postfix(self):
        e = self.primary()
        while True:
            tok = self.peek()
            if self.eat("."):
                e = ("member", e, self.ident(), tok.line)
            elif self.eat("["):
                idx = self.expr()
                self.expect("]")
                e = ("index", e, idx, tok.line)
            elif self.at("(") and e[0] == "name":
                self.next()
                args = []
                if not self.at(")"):
                    while True:
                        args.append(self.assign())
                        if not self.eat(","):
                            break
                self.expect(")")
                e = ("call", e[1], args, tok.line)
            elif tok.kind == "op" and tok.text in ("++", "--"):
                self.next()
                e = ("incdec", tok.text, False, e, tok.line)
            else:
                return e

    def primary(self):
        tok = self.next()
        if tok.kind == "int":
            return ("num", int(tok.text, 0), INT, tok.line)
        if tok.kind == "float":
            text = tok.text
            if not text.lower().endswith("f"):
                raise TRError(f"line {tok.line}: float literal {text} needs an f suffix (TR has no double)")
            return ("num", f32(float(text[:-1])), FLOAT, tok.line)
        if tok.kind == "id":
            return ("name", tok.text, tok.line)
        if tok.kind == "kw" and tok.text in ("int", "float") and self.at("("):
            self.next()
            e = self.expr()
            self.expect(")")
            return ("cast", INT if tok.text == "int" else FLOAT, e, tok.line)
        if tok.kind == "kw" and tok.text == "ctx":
            return ("name", "ctx", tok.line)
        if tok.text == "(":
            e = self.expr()
            self.expect(")")
            return e
        raise TRError(f"line {tok.line}: unexpected {tok.text!r}")


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def f32bits(x):
    return struct.unpack("<i", struct.pack("<f", x))[0]


# ----------------------------------------------------------------------- lowering
class Interp:
    """What the interpreter provides: record layouts, natives, machine constants, tables."""

    def __init__(self, d):
        self.schema = json.load(open(os.path.join(d, "schema.json")))
        spec = json.load(open(os.path.join(d, "natives.json")))
        self.natives = {n["name"]: dict(n, id=i) for i, n in enumerate(spec["natives"])}
        self.consts = spec["consts"]
        self.tables = {t["name"]: dict(t, id=i) for i, t in enumerate(spec["tables"])}
        self.queries = spec.get("queries", [])


class Lowerer:
    def __init__(self, interp):
        self.I = interp
        self.nodes, self.extra = [], []
        self.consts = {}           # name -> (type, value)
        self.ctx = {}              # name -> (slot, type)
        self.tables = {}           # program tables: name -> dict(off, ty, size)
        self.hooks = {h: -1 for h in HOOKS}
        self.defs = {}             # name -> info
        self.rules = []
        self.scopes = []
        self.next_slot = 0
        self.max_slot = 0
        self.in_def = None
        self.loop_depth = 0

    # node helpers
    def node(self, op, ty=VOID, w=0, x=0, y=0, z=0):
        self.nodes.append((OP[op], ty, w, x, y, z))
        return len(self.nodes) - 1

    def extra_list(self, items):
        start = len(self.extra)
        self.extra.extend(items)
        return start

    def err(self, line, msg):
        raise TRError(f"line {line}: {msg}")

    # constants
    def const_value(self, e):
        """Fold a constant expression; returns (type, value) or None."""
        k = e[0]
        if k == "num":
            return (e[2], e[1])
        if k == "name":
            if self.scopes and self.lookup(e[1]):
                return None
            if e[1] in self.consts:
                return self.consts[e[1]]
            if e[1] in self.I.consts:
                return (INT, self.I.consts[e[1]])
            return None
        if k == "un":
            v = self.const_value(e[2])
            if v is None:
                return None
            t, x = v
            if e[1] == "-":
                return (t, -x if t == INT else f32(-x))
            if e[1] == "+":
                return v
            if e[1] == "!":
                return (INT, int(not x))
            if e[1] == "~" and t == INT:
                return (INT, ~x)
            return None
        if k == "cast":
            v = self.const_value(e[2])
            if v is None:
                return None
            return (INT, int(v[1])) if e[1] == INT else (FLOAT, f32(float(v[1])))
        if k == "bin":
            a, b = self.const_value(e[2]), self.const_value(e[3])
            if a is None or b is None:
                return None
            if a[0] == INT and b[0] == INT:
                x, y = a[1], b[1]
                op = e[1]
                if op in ("/", "%") and y == 0:
                    return None
                r = {"+": lambda: x + y, "-": lambda: x - y, "*": lambda: x * y,
                     "/": lambda: int(x / y) if (x < 0) != (y < 0) else x // y,
                     "%": lambda: x - y * (int(x / y) if (x < 0) != (y < 0) else x // y),
                     "<<": lambda: x << y, ">>": lambda: x >> y, "&": lambda: x & y, "|": lambda: x | y,
                     "^": lambda: x ^ y, "==": lambda: int(x == y), "!=": lambda: int(x != y),
                     "<": lambda: int(x < y), "<=": lambda: int(x <= y), ">": lambda: int(x > y),
                     ">=": lambda: int(x >= y), "&&": lambda: int(bool(x) and bool(y)),
                     "||": lambda: int(bool(x) or bool(y))}[op]()
                if not -2**31 <= r < 2**31:
                    return None
                return (INT, r)
            return None   # float folding is left to the target arithmetic
        return None

    # scopes and slots
    def push(self):
        self.scopes.append({})

    def pop(self):
        self.scopes.pop()

    def declare(self, name, ty, line, kind="var", size=1):
        """kind: var (a value), ref (an out parameter: holds the slot it refers to), array (size slots)."""
        if name in self.scopes[-1]:
            self.err(line, f"{name} is already declared in this scope")
        slot = self.next_slot
        self.next_slot += size
        self.max_slot = max(self.max_slot, self.next_slot)
        self.def_top = max(getattr(self, "def_top", 0), self.next_slot)
        self.scopes[-1][name] = dict(slot=slot, ty=ty, kind=kind, size=size)
        return slot

    def lookup(self, name):
        for sc in reversed(self.scopes):
            if name in sc:
                return sc[name]
        return None

    # conversions
    def convert(self, n, ty_from, ty_to, line):
        if ty_from == ty_to:
            return n
        if ty_from == VOID or ty_to == VOID:
            self.err(line, "a void value is used")
        return self.node("I2F" if ty_to == FLOAT else "F2I", ty_to, x=n)

    def truth(self, n, ty, line):
        if ty == INT:
            return n
        if ty == FLOAT:
            return self.node("TRUTH_F", INT, x=n)
        self.err(line, "a void value is used as a condition")

    # memory paths
    def elem_size(self, typ):
        return SCALAR_SIZE[typ] if typ in SCALAR_SIZE else self.I.schema[typ]["size"]

    def mem_path(self, e, aggregate=False, lower=True):
        """Resolve a.x[i].y... to (root, writable, off, [(index_node, stride)], width, line) or None.
        With aggregate=True a path naming a record or an array is allowed (its byte size is returned)."""
        chain = []
        cur = e
        while cur[0] in ("member", "index"):
            chain.append(cur)
            cur = cur[1]
        if cur[0] != "name" or cur[1] not in ROOTS:
            return None
        root, struct_name, writable = ROOTS[cur[1]]
        chain.reverse()
        typ, dims, off, idx, bits = struct_name, [], 0, [], None
        for c in chain:
            line = c[3]
            if c[0] == "member":
                if dims:
                    self.err(line, f"index the array before .{c[2]}")
                rec = self.I.schema.get(typ)
                if rec is None:
                    self.err(line, f".{c[2]} on a non-record value")
                fld = rec["fields"].get(c[2])
                if fld is None:
                    self.err(line, f"{typ} has no field {c[2]}")
                off += fld["off"]
                typ, dims = fld["type"], list(fld["dims"])
                bits = fld.get("bits")
            else:
                if not dims:
                    self.err(line, "indexing a value that is not an array")
                elem = 1
                for d in dims[1:]:
                    elem *= d
                elem *= SCALAR_SIZE[typ] if typ in SCALAR_SIZE else self.I.schema[typ]["size"]
                cv = self.const_value(c[2])
                if cv is not None and cv[0] == INT:
                    if not 0 <= cv[1] < dims[0]:
                        self.err(line, f"constant index {cv[1]} is outside [0, {dims[0]})")
                    off += cv[1] * elem
                elif not lower:
                    idx.append((None, elem, c[2]))
                else:
                    n, t = self.rvalue(c[2])
                    if t != INT:
                        self.err(line, "array index is not an int")
                    idx.append((n, elem, c[2]))
                dims = dims[1:]
        line = e[-1]
        if dims or typ not in SCALAR_SIZE:
            if not aggregate:
                self.err(line, "this path names an array or a record, not a scalar")
            size = self.elem_size(typ)
            for d in dims:
                size *= d
            return dict(root=root, writable=writable, off=off, idx=idx, size=size, rtype=(typ, tuple(dims)), line=line)
        if aggregate:
            return dict(root=root, writable=writable, off=off, idx=idx, size=SCALAR_SIZE[typ],
                        rtype=(typ, ()), line=line, bits=bits)
        if typ in ("u32", "u64"):
            self.err(line, f"{typ} fields are not supported by TR")
        if bits:
            width = W["bit"] | (bits[0] << 4) | (bits[1] << 8)
        else:
            width = W[typ]
        ty = FLOAT if typ == "f32" else INT
        return dict(root=root, writable=writable, off=off, idx=idx, width=width, ty=ty, line=line)

    def mem_extra(self, p, value=None):
        items = [len(p["idx"])]
        for n, stride, _ in p["idx"]:
            items += [n, stride]
        if value is not None:
            items.append(value)
        return self.extra_list(items)

    def is_aggregate(self, e):
        if e[0] not in ("member", "index"):
            return False
        base = e
        while base[0] in ("member", "index"):
            base = base[1]
        if base[0] != "name" or base[1] not in ROOTS:
            return False
        p = self.mem_path(e, aggregate=True, lower=False)
        return p is not None and "width" not in p and (p["rtype"][1] or p["rtype"][0] not in SCALAR_SIZE)

    def addr_node(self, p):
        return self.node("ADDR", VOID, x=p["root"], y=p["off"], z=self.mem_extra(p))

    def pure(self, e):
        """No calls, assignments or increments (safe to evaluate twice)."""
        k = e[0]
        if k in ("call", "assign", "incdec"):
            return False
        return all(self.pure(c) for c in e[1:] if isinstance(c, tuple))

    # expressions
    def rvalue(self, e):
        k, line = e[0], e[-1]
        cv = self.const_value(e)
        if cv is not None:
            t, v = cv
            return (self.node("KI", INT, x=v), INT) if t == INT else (self.node("KF", FLOAT, x=f32bits(v)), FLOAT)
        if k == "name":
            name = e[1]
            loc = self.lookup(name)
            if loc:
                if loc["kind"] == "var":
                    return self.node("LOC", loc["ty"], x=loc["slot"]), loc["ty"]
                if loc["kind"] == "ref":
                    return self.node("LDR", loc["ty"], x=loc["slot"]), loc["ty"]
                self.err(line, f"{name} is an array; index it")
            if name in self.tables:
                self.err(line, f"{name} is a table; index it")
            self.err(line, f"unknown name {name}")
        if k == "member":
            if e[1][0] == "name" and e[1][1] == "ctx":
                if e[2] not in self.ctx:
                    self.err(line, f"ctx has no variable {e[2]}")
                slot, ty = self.ctx[e[2]]
                return self.node("LOC", ty, x=slot), ty
            p = self.mem_path(e)
            if p is None:
                self.err(line, "member access on something that is not a, o, cfg or ctx")
            return self.node("LDM", p["ty"], w=p["width"], x=p["root"], y=p["off"], z=self.mem_extra(p)), p["ty"]
        if k == "index" and e[1][0] == "name" and self.lookup(e[1][1]):
            loc = self.lookup(e[1][1])
            if loc["kind"] != "array":
                self.err(line, f"{e[1][1]} is not an array")
            cv = self.const_value(e[2])
            if cv is not None and cv[0] == INT:
                if not 0 <= cv[1] < loc["size"]:
                    self.err(line, f"index {cv[1]} outside {e[1][1]}[{loc['size']}]")
                return self.node("LOC", loc["ty"], x=loc["slot"] + cv[1]), loc["ty"]
            n, ty = self.rvalue(e[2])
            if ty != INT:
                self.err(line, "array index is not an int")
            return self.node("LDX", loc["ty"], w=loc["size"], x=loc["slot"], y=n), loc["ty"]
        if k == "index" and e[1][0] == "name" and e[1][1] in self.tables:
            tb = self.tables[e[1][1]]
            n, ty = self.rvalue(e[2])
            if ty != INT:
                self.err(line, "table index is not an int")
            return self.node("TABP", tb["ty"], w=tb["size"], x=tb["off"], y=n), tb["ty"]
        if k == "index":
            if e[1][0] == "name" and e[1][1] in self.I.tables:
                t = self.I.tables[e[1][1]]
                n, ty = self.rvalue(e[2])
                if ty != INT:
                    self.err(line, "table index is not an int")
                return self.node("TAB", INT, x=t["id"], y=n), INT
            p = self.mem_path(e)
            if p is None:
                self.err(line, "indexing something that is not a table or a memory array")
            return self.node("LDM", p["ty"], w=p["width"], x=p["root"], y=p["off"], z=self.mem_extra(p)), p["ty"]
        if k == "cast":
            n, ty = self.rvalue(e[2])
            return self.convert(n, ty, e[1], line), e[1]
        if k == "un":
            n, ty = self.rvalue(e[2])
            if e[1] == "+":
                return n, ty
            if e[1] == "-":
                return self.node("NEG_F" if ty == FLOAT else "NEG_I", ty, x=n), ty
            if e[1] == "!":
                return self.node("NOT_F" if ty == FLOAT else "NOT_I", INT, x=n), INT
            if e[1] == "~":
                if ty != INT:
                    self.err(line, "~ needs an int")
                return self.node("BNOT", INT, x=n), INT
        if k == "bin":
            return self.binary(e)
        if k == "cond":
            c, ct = self.rvalue(e[1])
            c = self.truth(c, ct, line)
            a, at = self.rvalue(e[2])
            b, bt = self.rvalue(e[3])
            ty = FLOAT if FLOAT in (at, bt) else INT
            a, b = self.convert(a, at, ty, line), self.convert(b, bt, ty, line)
            return self.node("COND", ty, x=c, y=a, z=b), ty
        if k == "call":
            return self.call(e)
        if k == "assign":
            return self.assign(e)
        if k == "incdec":
            return self.incdec(e)
        if k == "addr":
            self.err(line, "& is only allowed on an int local passed as a native's out-argument")
        self.err(line, f"unsupported expression {k}")

    def binary(self, e):
        op, line = e[1], e[-1]
        if op in ("&&", "||"):
            a, at = self.rvalue(e[2])
            b, bt = self.rvalue(e[3])
            return self.node("LAND" if op == "&&" else "LOR", INT, x=self.truth(a, at, line),
                             y=self.truth(b, bt, line)), INT
        a, at = self.rvalue(e[2])
        b, bt = self.rvalue(e[3])
        if VOID in (at, bt):
            self.err(line, "a void value is used")
        if op in ("%", "<<", ">>", "&", "|", "^"):
            if at != INT or bt != INT:
                self.err(line, f"{op} needs int operands")
            name = {"%": "MOD_I", "<<": "SHL", ">>": "SHR", "&": "BAND", "|": "BOR", "^": "BXOR"}[op]
            return self.node(name, INT, x=a, y=b), INT
        ty = FLOAT if FLOAT in (at, bt) else INT
        a, b = self.convert(a, at, ty, line), self.convert(b, bt, ty, line)
        base = {"+": "ADD", "-": "SUB", "*": "MUL", "/": "DIV", "==": "EQ", "!=": "NE", "<": "LT", "<=": "LE",
                ">": "GT", ">=": "GE"}[op]
        rty = INT if base in ("EQ", "NE", "LT", "LE", "GT", "GE") else ty
        return self.node(base + ("_F" if ty == FLOAT else "_I"), rty, x=a, y=b), rty

    def out_arg(self, arg, ty, fname, line):
        """An out argument: &var (its slot) or a caller's own out parameter (the slot it refers to)."""
        inner = arg[1] if arg[0] == "addr" else arg
        if inner[0] == "name":
            loc = self.lookup(inner[1])
            if loc and loc["ty"] == ty:
                if loc["kind"] == "var" and arg[0] == "addr":
                    return self.node("LOCREF", INT, x=loc["slot"])
                if loc["kind"] == "ref":
                    return self.node("LOC", INT, x=loc["slot"])
        if arg[0] == "addr" and inner[0] == "index" and inner[1][0] == "name":
            loc = self.lookup(inner[1][1])
            cv = self.const_value(inner[2])
            if loc and loc["kind"] == "array" and loc["ty"] == ty and cv is not None and 0 <= cv[1] < loc["size"]:
                return self.node("LOCREF", INT, x=loc["slot"] + cv[1])
        self.err(line, f"out argument of {fname} must be &v for a {TYNAME[ty]} variable v, or an out parameter")

    def call(self, e):
        name, args, line = e[1], e[2], e[3]
        if name == "clear" and name not in self.defs:
            if len(args) != 1:
                self.err(line, "clear takes one memory path")
            p = self.mem_path(args[0], aggregate=True)
            if p is None or not p["writable"]:
                self.err(line, "clear needs a path in agent memory (a.*)")
            if p.get("bits"):
                self.err(line, "clear of a bitfield")
            return self.node("ZERO", VOID, x=self.addr_node(p), z=p["size"]), VOID
        if name in self.defs:
            d = self.defs[name]
            if self.in_def is not None and self.in_def == name:
                self.err(line, "recursion is not supported")
            if len(args) != len(d["params"]):
                self.err(line, f"{name} takes {len(d['params'])} arguments")
            nodes = []
            for (pty, _, out), arg in zip(d["params"], args):
                if out:
                    nodes.append(self.out_arg(arg, pty, name, line))
                    continue
                n, ty = self.rvalue(arg)
                nodes.append(self.convert(n, ty, pty, line))
            self.calls_from.setdefault(self.current, set()).add(name)
            return self.node("CALLD", d["ret"], w=len(nodes), x=d["id"], z=self.extra_list(nodes)), d["ret"]
        nat = self.I.natives.get(name)
        if nat is None:
            self.err(line, f"unknown function {name}")
        if len(args) != len(nat["params"]):
            self.err(line, f"{name} takes {len(nat['params'])} arguments, got {len(args)}")
        nodes = []
        for kind, arg in zip(nat["params"], args):
            if kind == "out":
                nodes.append(self.out_arg(arg, INT, name, line))
            else:
                n, ty = self.rvalue(arg)
                nodes.append(self.convert(n, ty, FLOAT if kind == "f" else INT, line))
        ret = {"void": VOID, "int": INT, "float": FLOAT}[nat["ret"]]
        return self.node("CALLN", ret, w=len(nodes), x=nat["id"], z=self.extra_list(nodes)), ret

    def store(self, target, value, vty, line):
        """Assignment node of target = value (value converted to the target's type)."""
        if target[0] == "name":
            loc = self.lookup(target[1])
            if not loc or loc["kind"] == "array":
                self.err(line, f"cannot assign to {target[1]}")
            v = self.convert(value, vty, loc["ty"], line)
            return self.node("ASG_L" if loc["kind"] == "var" else "ASG_R", loc["ty"], x=loc["slot"], y=v), loc["ty"]
        if target[0] == "index" and target[1][0] == "name" and self.lookup(target[1][1]):
            loc = self.lookup(target[1][1])
            if loc["kind"] != "array":
                self.err(line, f"{target[1][1]} is not an array")
            v = self.convert(value, vty, loc["ty"], line)
            cv = self.const_value(target[2])
            if cv is not None and cv[0] == INT:
                if not 0 <= cv[1] < loc["size"]:
                    self.err(line, f"index {cv[1]} outside {target[1][1]}[{loc['size']}]")
                return self.node("ASG_L", loc["ty"], x=loc["slot"] + cv[1], y=v), loc["ty"]
            n, ty = self.rvalue(target[2])
            return self.node("ASG_X", loc["ty"], w=loc["size"], x=loc["slot"], y=n, z=v), loc["ty"]
        if target[0] == "member" and target[1][0] == "name" and target[1][1] == "ctx":
            slot, ty = self.ctx[target[2]]
            return self.node("ASG_L", ty, x=slot, y=self.convert(value, vty, ty, line)), ty
        p = self.mem_path(target)
        if p is None:
            self.err(line, "assignment to something that is not a local, ctx or a.* field")
        if not p["writable"]:
            self.err(line, "only agent memory (a.*) is writable")
        v = self.convert(value, vty, p["ty"], line)
        return self.node("ASG_M", p["ty"], w=p["width"], x=p["root"], y=p["off"], z=self.mem_extra(p, v)), p["ty"]

    def assign(self, e):
        op, target, value, line = e[1], e[2], e[3], e[4]
        if op == "=" and self.is_aggregate(target):
            dst = self.mem_path(target, aggregate=True)
            src = self.mem_path(value, aggregate=True) if value[0] in ("member", "index") else None
            if not dst["writable"]:
                self.err(line, "only agent memory (a.*) is writable")
            if src is None or src["rtype"] != dst["rtype"]:
                self.err(line, "a record or array is assigned only from another of the same type")
            return self.node("COPY", VOID, x=self.addr_node(dst), y=self.addr_node(src), z=dst["size"]), VOID
        if op == "=":
            v, vt = self.rvalue(value)
            return self.store(target, v, vt, line)
        if not self.pure(target):
            self.err(line, "compound assignment needs a side-effect-free target")
        return self.rvalue(("assign", "=", target, ("bin", op[:-1], target, value, line), line))

    def incdec(self, e):
        op, prefix, target, line = e[1], e[2], e[3], e[4]
        if not self.pure(target):
            self.err(line, "++/-- needs a side-effect-free target")
        n, ty = self.rvalue(("assign", "=", target, ("bin", op[0], target, ("num", 1, INT, line), line), line))
        if prefix or self.discarded:
            return n, ty
        if ty != INT or (target[0] == "member" and self.mem_path(target)["width"] != W["i32"]):
            self.err(line, "a post-increment's value is only supported on int variables")
        return self.node("SUB_I" if op == "++" else "ADD_I", INT, x=n, y=self.node("KI", INT, x=1)), INT

    # statements
    def stmt(self, s):
        k = s[0]
        line = s[-1]
        if k == "block":
            self.push()
            items = [self.stmt(c) for c in s[1]]
            self.pop()
            items = [i for i in items if i is not None]
            return self.node("SEQ", w=len(items), z=self.extra_list(items))
        if k == "decl":
            items = []
            for name, init, size in s[2]:
                if size is not None:
                    cv = self.const_value(size)
                    if cv is None or cv[0] != INT or cv[1] <= 0:
                        self.err(line, f"the size of {name} is not a positive constant")
                    inits = init or []
                    if len(inits) > cv[1]:
                        self.err(line, f"too many initialisers for {name}")
                    vals = [self.rvalue(x) for x in inits]
                    slot = self.declare(name, s[1], line, kind="array", size=cv[1])
                    for k in range(cv[1]):
                        v, vt = vals[k] if k < len(vals) else (self.node("KI", INT, x=0), INT)
                        items.append(self.node("EXPR", x=self.node("ASG_L", s[1], x=slot + k,
                                                                   y=self.convert(v, vt, s[1], line))))
                    continue
                if init is None:
                    v, vt = self.node("KI", INT, x=0), INT
                else:
                    v, vt = self.rvalue(init)
                slot = self.declare(name, s[1], line)
                items.append(self.node("EXPR", x=self.node("ASG_L", s[1], x=slot, y=self.convert(v, vt, s[1], line))))
            return items[0] if len(items) == 1 else self.node("SEQ", w=len(items), z=self.extra_list(items))
        if k == "expr":
            self.discarded = True
            n, _ = self.rvalue(s[1])
            self.discarded = False
            return self.node("EXPR", x=n)
        if k == "if":
            c, ct = self.rvalue(s[1])
            c = self.truth(c, ct, line)
            then = self.stmt(s[2])
            other = self.stmt(s[3]) if s[3] is not None else -1
            return self.node("IF", x=c, y=then, z=other)
        if k == "for":
            self.push()
            init = self.stmt(s[1]) if s[1] is not None else -1
            cond = -1
            if s[2] is not None:
                c, ct = self.rvalue(s[2])
                cond = self.truth(c, ct, line)
            step = self.stmt(s[3]) if s[3] is not None else -1
            self.loop_depth += 1
            body = self.stmt(s[4])
            self.loop_depth -= 1
            self.pop()
            return self.node("FOR", x=init, y=cond, z=self.extra_list([step, body]))
        if k in ("break", "continue"):
            if not self.loop_depth:
                self.err(line, f"{k} outside a loop")
            return self.node("BRK" if k == "break" else "CONT")
        if k == "pass":
            if self.in_def is not None:
                self.err(line, "pass is only allowed in rules")
            return self.node("PASS")
        if k in ("try", "emit"):
            if self.in_def is not None:
                self.err(line, f"{k} is only allowed in rules")
            a, at = self.rvalue(s[1])
            b, bt = self.rvalue(s[2])
            if at != INT or bt != INT:
                self.err(line, f"{k} needs an int action and an int skill")
            return self.node("TRY" if k == "try" else "EMIT", x=a, y=b)
        if k == "return":
            if self.in_def is None:
                self.err(line, "return is only allowed in defs (use pass in rules)")
            d = self.defs[self.in_def]
            if s[1] is None:
                if d["ret"] != VOID:
                    self.err(line, "return needs a value")
                return self.node("RET", x=-1)
            n, ty = self.rvalue(s[1])
            return self.node("RET", x=self.convert(n, ty, d["ret"], line))
        self.err(line, f"unsupported statement {k}")

    # program
    def program(self, decls):
        self.discarded = False
        self.calls_from = {}
        # constants may be declared in any order and in several files; a repeated
        # declaration is accepted when it has the same value
        pending = [d for d in decls if d[0] == "const"]
        while pending:
            left = []
            for d in pending:
                v = self.const_value(d[2])
                if v is None:
                    left.append(d)
                    continue
                if d[1] in self.I.consts:
                    raise TRError(f"line {d[3]}: {d[1]} is a machine constant")
                if d[1] in self.consts and self.consts[d[1]] != v:
                    raise TRError(f"line {d[3]}: {d[1]} is redefined with another value")
                self.consts[d[1]] = v
            if len(left) == len(pending):
                raise TRError(f"line {left[0][3]}: {left[0][1]} is not a constant expression")
            pending = left
        for d in decls:
            if d[0] == "const":
                continue
            elif d[0] == "table":
                vals = []
                for x in d[3]:
                    v = self.const_value(x)
                    if v is None:
                        raise TRError(f"line {d[4]}: {d[1]} has a non-constant entry")
                    vals.append(f32bits(float(v[1])) if d[2] == FLOAT else int(v[1]))
                if d[1] in self.tables or d[1] in self.consts:
                    raise TRError(f"line {d[4]}: {d[1]} is already defined")
                self.tables[d[1]] = dict(off=self.extra_list(vals), ty=d[2], size=len(vals))
            elif d[0] == "ctx":
                for ty, name in d[1]:
                    if name in self.ctx:
                        raise TRError(f"line {d[2]}: ctx.{name} is declared twice")
                    self.ctx[name] = (self.next_slot, ty)
                    self.next_slot += 1
        self.nctx = self.next_slot
        self.max_slot = self.next_slot
        # defs are declared first so that rules and other defs can call them
        for d in decls:
            if d[0] == "def":
                if d[1] in self.defs or d[1] in self.I.natives:
                    raise TRError(f"line {d[5]}: {d[1]} is already defined")
                self.defs[d[1]] = dict(id=len(self.defs), ret=d[2], params=d[3], body=d[4], line=d[5])
        # rules share one locals area (they never run nested)
        rule_base = self.nctx
        for d in decls:
            if d[0] == "rule":
                if d[2] not in MODULES:
                    raise TRError(f"line {d[4]}: unknown module {d[2]} (one of {', '.join(MODULES)})")
                self.current = ("rule", d[1])
                self.next_slot = rule_base
                self.push()
                root = self.stmt(d[3])
                self.pop()
                self.rules.append(dict(name=d[1], module=MODULES.index(d[2]), root=root))
        # Def frames are allocated statically along the call graph (there is no
        # recursion): a def's frame starts above the frames of all its callers,
        # so defs that are never active together share slots. Callers are
        # lowered first, so their frame sizes are known.
        self.def_list = sorted(self.defs.values(), key=lambda x: x["id"])
        callees = {name: self.ast_calls(d["body"]) & set(self.defs) for name, d in self.defs.items()}
        order = self.topo_order(callees)
        rules_end = self.max_slot
        base = {name: rules_end for name in self.defs}
        for name in order:
            d = self.defs[name]
            self.current = ("def", d["id"])
            self.in_def = name
            self.next_slot = base[name]
            self.def_top = base[name]
            self.push()
            d["slot0"] = self.next_slot
            for ty, pname, out in d["params"]:
                self.declare(pname, ty, d["line"], kind="ref" if out else "var")
            d["root"] = self.stmt(d["body"])
            self.pop()
            self.in_def = None
            top = self.max_def_slot(d)
            for c in callees[name]:
                base[c] = max(base[c], top)
        self.check_recursion()
        self.query_ids = []
        for q in self.I.queries:
            d = self.defs.get(q["name"])
            if d is not None and (d["ret"] != INT or d["params"]):
                raise TRError(f"line {d['line']}: query {q['name']} must be `def int {q['name']}()`")
            self.query_ids.append(d["id"] if d is not None else -1)
        for name, (ret, params) in HOOKS.items():
            if name in self.defs:
                d = self.defs[name]
                if d["ret"] != ret or [(p[0], p[2]) for p in d["params"]] != params:
                    raise TRError(f"line {d['line']}: entry point {name} has the wrong signature")
                self.hooks[name] = d["id"]
        if self.max_slot > 1024:
            raise TRError("more than 1024 variable slots")

    def ast_calls(self, node):
        out = set()
        if isinstance(node, tuple):
            if node and node[0] == "call":
                out.add(node[1])
            for c in node:
                out |= self.ast_calls(c)
        elif isinstance(node, list):
            for c in node:
                out |= self.ast_calls(c)
        return out

    def topo_order(self, callees):
        """Defs with every caller before its callees; recursion is reported."""
        indeg = {n: 0 for n in callees}
        for n, cs in callees.items():
            for c in cs:
                indeg[c] += 1
        ready = sorted(n for n, k in indeg.items() if k == 0)
        order = []
        while ready:
            n = ready.pop(0)
            order.append(n)
            for c in sorted(callees[n]):
                indeg[c] -= 1
                if indeg[c] == 0:
                    ready.append(c)
        if len(order) != len(callees):
            raise TRError("recursive defs are not supported: " + ", ".join(sorted(set(callees) - set(order))))
        return order

    def max_def_slot(self, d):
        """One past the highest slot the def's own frame uses (params and locals)."""
        return self.def_top

    def check_recursion(self):
        graph = {self.defs[k]["id"]: set() for k in self.defs}
        for (kind, who), callees in self.calls_from.items():
            if kind == "def":
                graph[who] |= {self.defs[c]["id"] for c in callees}
        state = {}

        def visit(v):
            if state.get(v) == 1:
                raise TRError("recursive defs are not supported")
            if state.get(v) == 2:
                return
            state[v] = 1
            for w in graph[v]:
                visit(w)
            state[v] = 2
        for v in graph:
            visit(v)


# ------------------------------------------------------------------------ emitters
def emit_header(lw, path, source):
    out = [f"// Generated by trc.py from {source}. Do not edit.",
           "// The program image: nodes, extra lists, rules and defs (see tr_runtime.hpp).", "#pragma once"]
    out.append(f"inline constexpr int TRP_NODES = {len(lw.nodes)}, TRP_EXTRA = {max(1, len(lw.extra))}, "
               f"TRP_RULES = {len(lw.rules)}, TRP_DEFS = {max(1, len(lw.def_list))}, "
               f"TRP_SLOTS = {lw.max_slot}, TRP_CTX = {lw.nctx};")
    out.append("inline constexpr int " + ", ".join(f"TRP_HOOK_{h.upper()} = {lw.hooks[h]}" for h in HOOKS) + ";")
    q = lw.query_ids or [-1]
    out.append(f"inline constexpr int TRP_QUERIES = {len(lw.query_ids)};")
    out.append("inline constexpr int32_t trp_query[" + str(len(q)) + "] = {" + ",".join(map(str, q)) + "};")
    out.append("inline constexpr TRNode trp_nodes[TRP_NODES] = {")
    for i in range(0, len(lw.nodes), 6):
        out.append(" " + "".join("{%d,%d,%d,%d,%d,%d}," % n for n in lw.nodes[i:i + 6]))
    out.append("};")
    out.append("inline constexpr int32_t trp_extra[TRP_EXTRA] = {")
    ex = lw.extra or [0]
    for i in range(0, len(ex), 24):
        out.append(" " + ",".join(str(v) for v in ex[i:i + 24]) + ",")
    out.append("};")
    out.append("inline constexpr TRRule trp_rules[TRP_RULES] = {")
    out.append(" " + "".join("{%d,%d}," % (r["root"], r["module"]) for r in lw.rules))
    out.append("};")
    defs = lw.def_list or [dict(root=-1, params=[], slot0=0, ret=VOID)]
    out.append("inline constexpr TRDef trp_defs[TRP_DEFS] = {")
    out.append(" " + "".join("{%d,%d,%d,%d}," % (d["root"], len(d["params"]), d["slot0"], d["ret"]) for d in defs))
    out.append("};")
    out.append("#define TRP_RULE_NAMES \\")
    for i in range(0, len(lw.rules), 6):
        out.append(" " + "".join('"%s",' % r["name"] for r in lw.rules[i:i + 6]) + " \\")
    out.append("")
    out.append("#define TRP_RULE_MODULES " + ",".join(str(r["module"]) for r in lw.rules))
    open(path, "w").write("\n".join(out) + "\n")


def emit_image(lw, path):
    """Binary image for loading at run time: header, nodes, extra, rules, defs, names."""
    names = b"".join(r["name"].encode() + b"\0" for r in lw.rules)
    defs = lw.def_list or [dict(root=-1, params=[], slot0=0, ret=VOID)]
    blob = struct.pack("<4s12i", b"TRP3", len(lw.nodes), len(lw.extra or [0]), len(lw.rules), len(defs),
                       lw.max_slot, lw.nctx, len(names), len(lw.query_ids), *(lw.hooks[h] for h in HOOKS))
    blob += b"".join(struct.pack("<i", q) for q in lw.query_ids)
    blob += b"".join(struct.pack("<BBHiii", *n) for n in lw.nodes)
    blob += b"".join(struct.pack("<i", v) for v in (lw.extra or [0]))
    blob += b"".join(struct.pack("<ii", r["root"], r["module"]) for r in lw.rules)
    blob += b"".join(struct.pack("<iiii", d["root"], len(d["params"]), d["slot0"], d["ret"]) for d in defs)
    blob += names
    open(path, "wb").write(blob)


def stats(lw):
    return dict(nodes=len(lw.nodes), extra=len(lw.extra), rules=len(lw.rules), defs=len(lw.def_list),
                slots=lw.max_slot, ctx=lw.nctx, consts=len(lw.consts), tables=len(lw.tables),
                hooks=[h for h in HOOKS if lw.hooks[h] >= 0], queries=sum(q >= 0 for q in lw.query_ids))


def compile_file(src_paths, interp_dir):
    """A program may span several files, read in order (constants are declared before use)."""
    if isinstance(src_paths, str):
        src_paths = [src_paths]
    decls = []
    for path in src_paths:
        try:
            decls += Parser(lex(open(path).read())).program()
        except TRError as ex:
            raise TRError(f"{os.path.basename(path)}: {ex}")
    lw = Lowerer(Interp(interp_dir))
    lw.program(decls)
    return lw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("program", nargs="+")
    ap.add_argument("--interp", required=True, help="interpreter directory (schema.json, natives.json)")
    ap.add_argument("-o", "--out", required=True, help="generated C++ header")
    ap.add_argument("--image", help="also write the binary image")
    args = ap.parse_args()
    try:
        lw = compile_file(args.program, args.interp)
    except TRError as ex:
        sys.exit(f"{args.program}: {ex}")
    emit_header(lw, args.out, " ".join(os.path.basename(p) for p in args.program))
    if args.image:
        emit_image(lw, args.image)
    print(json.dumps(stats(lw)))


if __name__ == "__main__":
    main()

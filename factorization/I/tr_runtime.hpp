// tr_runtime.hpp: the TR interpreter, I's generic decision layer.
//
// A TR program (see trc/trc.py) is a flat image of typed nodes. Its rules run
// in order every decision; the first that emits an action wins, and the policy
// then commits that action exactly as the hand-written rule table did
// (rule_fires, emit(), the shield). The interpreter knows the record layouts,
// its natives and the machine constants, never a particular program.
//
// Two evaluators share every per-operation helper below:
//   - dynamic (default): the image is run-time data, reached through an opaque
//     pointer, so the compiler cannot specialise on it;
//   - static (TR_SPEC): every node index is a template argument and every node a
//     constexpr value, so instantiating tr_sel<0> makes clang partially evaluate
//     the interpreter on the program (Futamura's first projection, with the C++
//     compiler as the specialiser).
#pragma once
#include "tr_ops.gen.hpp"

struct TRNode { uint8_t op, ty; uint16_t w; int32_t x, y, z; };
struct TRRule { int32_t root, module; };
struct TRDef { int32_t root, nparams, slot0, ret; };
union TRVal { int32_t i; float f; };

// Statement results: fall through, loop control, leave the rule, emit, return from a def.
enum { TR_NEXT, TR_BRK, TR_CONT, TR_PASS, TR_EMIT, TR_RET };
// Memory widths (low 4 bits of TRNode::w; bitfields add shift << 4 and width << 8).
enum { TRW_I32 = 1, TRW_F32, TRW_U8, TRW_I8, TRW_U16, TRW_I16, TRW_BIT };
enum { TR_MAX_SLOTS = 1024, TR_MAX_ARGS = 12 };

struct TREnv {
 FPAgent *a; const FPObservation *o; const FPConfig *cfg; FPScratch *s;
 int action, skill;
 TRVal ret;
 TRVal loc[TR_MAX_SLOTS];
};

static inline TRVal tr_i(int32_t v) { TRVal r; r.i = v; return r; }
static inline TRVal tr_f(float v) { TRVal r; r.f = v; return r; }

static inline char *tr_base(TREnv &e, int root) {
 return root == 0 ? (char *)e.a : root == 1 ? (char *)e.o : (char *)e.cfg;
}
// Loads and stores follow C: narrow integers promote to int; a store converts
// to the field's type and the assignment's value is the stored value.
static inline TRVal tr_load(const char *p, int w) {
 switch (w & 15) {
 case TRW_I32: return tr_i(*(const int32_t *)p);
 case TRW_F32: return tr_f(*(const float *)p);
 case TRW_U8: return tr_i(*(const uint8_t *)p);
 case TRW_I8: return tr_i(*(const int8_t *)p);
 case TRW_U16: return tr_i(*(const uint16_t *)p);
 case TRW_I16: return tr_i(*(const int16_t *)p);
 default: return tr_i((*(const uint8_t *)p >> ((w >> 4) & 15)) & ((1 << (w >> 8)) - 1));
 }
}
static inline TRVal tr_store(char *p, int w, TRVal v) {
 switch (w & 15) {
 case TRW_I32: *(int32_t *)p = v.i; return v;
 case TRW_F32: *(float *)p = v.f; return v;
 case TRW_U8: *(uint8_t *)p = (uint8_t)v.i; return tr_i((uint8_t)v.i);
 case TRW_I8: *(int8_t *)p = (int8_t)v.i; return tr_i((int8_t)v.i);
 case TRW_U16: *(uint16_t *)p = (uint16_t)v.i; return tr_i((uint16_t)v.i);
 case TRW_I16: *(int16_t *)p = (int16_t)v.i; return tr_i((int16_t)v.i);
 default: {
  int sh = (w >> 4) & 15, mask = (1 << (w >> 8)) - 1;
  uint8_t m = (uint8_t)(mask << sh);
  *(uint8_t *)p = (uint8_t)((*(uint8_t *)p & ~m) | ((v.i << sh) & m));
  return tr_i(v.i & mask);
 }
 }
}
// Pure operators, shared by both evaluators.
static inline TRVal tr_un(int op, TRVal a) {
 switch (op) {
 case OP_NEG_I: return tr_i(-a.i);
 case OP_NEG_F: return tr_f(-a.f);
 case OP_NOT_I: return tr_i(!a.i);
 case OP_NOT_F: return tr_i(!a.f);
 case OP_BNOT: return tr_i(~a.i);
 case OP_I2F: return tr_f((float)a.i);
 case OP_F2I: return tr_i((int)a.f);
 default: return tr_i(a.f != 0);   // OP_TRUTH_F
 }
}
static inline TRVal tr_bin(int op, TRVal a, TRVal b) {
 switch (op) {
 case OP_ADD_I: return tr_i(a.i + b.i);
 case OP_SUB_I: return tr_i(a.i - b.i);
 case OP_MUL_I: return tr_i(a.i * b.i);
 case OP_DIV_I: return tr_i(a.i / b.i);
 case OP_MOD_I: return tr_i(a.i % b.i);
 case OP_SHL: return tr_i(a.i << b.i);
 case OP_SHR: return tr_i(a.i >> b.i);
 case OP_BAND: return tr_i(a.i & b.i);
 case OP_BOR: return tr_i(a.i | b.i);
 case OP_BXOR: return tr_i(a.i ^ b.i);
 case OP_EQ_I: return tr_i(a.i == b.i);
 case OP_NE_I: return tr_i(a.i != b.i);
 case OP_LT_I: return tr_i(a.i < b.i);
 case OP_LE_I: return tr_i(a.i <= b.i);
 case OP_GT_I: return tr_i(a.i > b.i);
 case OP_GE_I: return tr_i(a.i >= b.i);
 case OP_ADD_F: return tr_f(a.f + b.f);
 case OP_SUB_F: return tr_f(a.f - b.f);
 case OP_MUL_F: return tr_f(a.f * b.f);
 case OP_DIV_F: return tr_f(a.f / b.f);
 case OP_EQ_F: return tr_i(a.f == b.f);
 case OP_NE_F: return tr_i(a.f != b.f);
 case OP_LT_F: return tr_i(a.f < b.f);
 case OP_LE_F: return tr_i(a.f <= b.f);
 case OP_GT_F: return tr_i(a.f > b.f);
 default: return tr_i(a.f >= b.f);   // OP_GE_F
 }
}
static inline bool tr_is_un(int op) { return op >= OP_NEG_I && op <= OP_TRUTH_F; }
static inline bool tr_is_bin(int op) { return op >= OP_ADD_I && op <= OP_GE_F; }

#include "tr_natives.gen.hpp"   // TRVal trn_NAME(TREnv &, const TRVal *), tr_native_fn[], tr_tables[]

// ------------------------------------------------------------- dynamic evaluator
struct TRImage {
 const TRNode *nodes; const int32_t *extra; const TRRule *rules; const TRDef *defs;
 int nrules, nslots, nctx;
 int hook[4];   // def ids of the entry points init, observe, filter, commit (-1: absent)
 const int32_t *query; int nquery;   // def id of each query (-1: absent, use its default)
};
enum { TR_HOOK_INIT, TR_HOOK_OBSERVE, TR_HOOK_FILTER, TR_HOOK_COMMIT };

static TRVal tr_ev(TREnv &e, const TRImage &P, int n);
static int tr_ex(TREnv &e, const TRImage &P, int n);

static char *tr_addr(TREnv &e, const TRImage &P, const TRNode &n) {
 char *p = tr_base(e, n.x) + n.y;
 const int32_t *x = P.extra + n.z;
 for (int k = 0; k < x[0]; k++) p += (ptrdiff_t)tr_ev(e, P, x[1 + 2 * k]).i * x[2 + 2 * k];
 return p;
}
static TRVal tr_invoke(TREnv &e, const TRImage &P, int def, const TRVal *args) {
 const TRDef &d = P.defs[def];
 for (int k = 0; k < d.nparams; k++) e.loc[d.slot0 + k] = args[k];
 e.ret = tr_i(0);
 tr_ex(e, P, d.root);
 return e.ret;
}
static TRVal tr_call_def(TREnv &e, const TRImage &P, const TRNode &n) {
 TRVal args[TR_MAX_ARGS];
 for (int k = 0; k < n.w; k++) args[k] = tr_ev(e, P, P.extra[n.z + k]);
 return tr_invoke(e, P, n.x, args);
}
static TRVal tr_ev(TREnv &e, const TRImage &P, int i) {
 const TRNode &n = P.nodes[i];
 switch (n.op) {
 case OP_LDR: return e.loc[e.loc[n.x].i];
 case OP_ASG_R: return e.loc[e.loc[n.x].i] = tr_ev(e, P, n.y);
 case OP_LDX: return e.loc[n.x + tr_ev(e, P, n.y).i];
 case OP_ASG_X: {
  TRVal v = tr_ev(e, P, n.z);
  return e.loc[n.x + tr_ev(e, P, n.y).i] = v;
 }
 case OP_TABP: return tr_i(P.extra[n.x + tr_ev(e, P, n.y).i]);
 case OP_COPY: {
  char *src = tr_addr(e, P, P.nodes[n.y]);
  memmove(tr_addr(e, P, P.nodes[n.x]), src, n.z);
  return tr_i(0);
 }
 case OP_ZERO: memset(tr_addr(e, P, P.nodes[n.x]), 0, n.z); return tr_i(0);
 case OP_KI: return tr_i(n.x);
 case OP_KF: return tr_f(__builtin_bit_cast(float, n.x));
 case OP_LOC: return e.loc[n.x];
 case OP_LOCREF: return tr_i(n.x);
 case OP_LDM: return tr_load(tr_addr(e, P, n), n.w);
 case OP_TAB: return tr_i(tr_tables[n.x][tr_ev(e, P, n.y).i]);
 case OP_LAND: return tr_i(tr_ev(e, P, n.x).i && tr_ev(e, P, n.y).i);
 case OP_LOR: return tr_i(tr_ev(e, P, n.x).i || tr_ev(e, P, n.y).i);
 case OP_COND: return tr_ev(e, P, n.x).i ? tr_ev(e, P, n.y) : tr_ev(e, P, n.z);
 case OP_ASG_L: return e.loc[n.x] = tr_ev(e, P, n.y);
 case OP_ASG_M: {
  TRVal v = tr_ev(e, P, P.extra[n.z + 1 + 2 * P.extra[n.z]]);   // C++17: the value before the target
  return tr_store(tr_addr(e, P, n), n.w, v);
 }
 case OP_CALLN: {
  TRVal args[TR_MAX_ARGS];
  for (int k = 0; k < n.w; k++) args[k] = tr_ev(e, P, P.extra[n.z + k]);
  return tr_native_fn[n.x](e, args);
 }
 case OP_CALLD: return tr_call_def(e, P, n);
 default:
  if (tr_is_un(n.op)) return tr_un(n.op, tr_ev(e, P, n.x));
  {
   TRVal a = tr_ev(e, P, n.x);
   return tr_bin(n.op, a, tr_ev(e, P, n.y));
  }
 }
}
static int tr_ex(TREnv &e, const TRImage &P, int i) {
 const TRNode &n = P.nodes[i];
 switch (n.op) {
 case OP_SEQ:
  for (int k = 0; k < n.w; k++) {
   int st = tr_ex(e, P, P.extra[n.z + k]);
   if (st != TR_NEXT) return st;
  }
  return TR_NEXT;
 case OP_IF:
  if (tr_ev(e, P, n.x).i) return tr_ex(e, P, n.y);
  return n.z >= 0 ? tr_ex(e, P, n.z) : TR_NEXT;
 case OP_FOR: {
  int step = P.extra[n.z], body = P.extra[n.z + 1];
  if (n.x >= 0) tr_ex(e, P, n.x);
  while (n.y < 0 || tr_ev(e, P, n.y).i) {
   int st = tr_ex(e, P, body);
   if (st == TR_BRK) break;
   if (st != TR_NEXT && st != TR_CONT) return st;
   if (step >= 0) tr_ex(e, P, step);
  }
  return TR_NEXT;
 }
 case OP_TRY: {
  int act = tr_ev(e, P, n.x).i;
  if (act < 0) return TR_NEXT;
  e.action = act;
  e.skill = tr_ev(e, P, n.y).i;
  return TR_EMIT;
 }
 case OP_EMIT:
  e.action = tr_ev(e, P, n.x).i;
  e.skill = tr_ev(e, P, n.y).i;
  return TR_EMIT;
 case OP_PASS: return TR_PASS;
 case OP_BRK: return TR_BRK;
 case OP_CONT: return TR_CONT;
 case OP_RET:
  if (n.x >= 0) e.ret = tr_ev(e, P, n.x);
  return TR_RET;
 case OP_EXPR: tr_ev(e, P, n.x); return TR_NEXT;
 default: return TR_NEXT;
 }
}
// Runs the rules in order; returns the index of the rule that emitted, or -1.
static int tr_select_dynamic(TREnv &e, const TRImage &P) {
 for (int k = 0; k < P.nctx; k++) e.loc[k] = tr_i(0);
 for (int r = 0; r < P.nrules; r++)
  if (tr_ex(e, P, P.rules[r].root) == TR_EMIT) return r;
 return -1;
}

// ----------------------------------------------------------------- the program
#include "program.gen.hpp"   // trp_nodes, trp_extra, trp_rules, trp_defs (trc.py)
const int fp_rule_count = TRP_RULES;
const char *const fp_rule_names[] = {TRP_RULE_NAMES};
const unsigned char fp_rule_module[] = {TRP_RULE_MODULES};

#ifndef TR_SPEC
// Dynamic mode: the interpreter reaches the program only through a volatile
// pointer, so nothing about it is known when the interpreter is compiled.
static_assert(TRP_QUERIES == TRQ_COUNT, "the program was compiled against another export list");
static const TRImage tr_embedded = {trp_nodes, trp_extra, trp_rules, trp_defs, TRP_RULES, TRP_SLOTS, TRP_CTX,
                                    {TRP_HOOK_INIT, TRP_HOOK_OBSERVE, TRP_HOOK_FILTER, TRP_HOOK_COMMIT},
                                    trp_query, TRP_QUERIES};
static const TRImage *volatile tr_image = &tr_embedded;
static_assert(TRP_SLOTS <= TR_MAX_SLOTS, "the program needs more variable slots than the interpreter has");
#include <stdio.h>
// TR_PROGRAM=path.trb replaces the embedded program with an image written by
// trc.py --image, loaded once per process: the same compiled interpreter runs
// any program. (Rule names and counts reported through the C ABI stay those of
// the embedded program.)
static const TRImage *tr_load_image(const char *path) {
 FILE *f = fopen(path, "rb");
 if (!f) { perror(path); abort(); }
 char magic[4]; int32_t h[12];
 if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "TRP3", 4) || fread(h, 4, 12, f) != 12) {
  fprintf(stderr, "%s: not a TR image\n", path); abort();
 }
 TRNode *nodes = (TRNode *)malloc(sizeof(TRNode) * h[0]);
 int32_t *extra = (int32_t *)malloc(4 * (size_t)h[1]);
 TRRule *rules = (TRRule *)malloc(sizeof(TRRule) * h[2]);
 TRDef *defs = (TRDef *)malloc(sizeof(TRDef) * h[3]);
 int32_t *query = (int32_t *)malloc(4 * (size_t)(h[7] ? h[7] : 1));
 // trc.py writes the query table right after the header, then nodes, extra, rules, defs, rule names
 size_t ok = fread(query, 4, h[7], f) + fread(nodes, sizeof(TRNode), h[0], f) + fread(extra, 4, h[1], f) +
             fread(rules, sizeof(TRRule), h[2], f) + fread(defs, sizeof(TRDef), h[3], f);
 fclose(f);
 if (ok != (size_t)h[0] + h[1] + h[2] + h[3] + h[7] || h[4] > TR_MAX_SLOTS || h[2] > FP_RULES_MAX || h[7] != TRQ_COUNT) {
  fprintf(stderr, "%s: truncated image or too many slots/rules\n", path); abort();
 }
 TRImage *img = (TRImage *)malloc(sizeof(TRImage));
 *img = TRImage{nodes, extra, rules, defs, h[2], h[4], h[5], {h[8], h[9], h[10], h[11]}, query, h[7]};
 return img;
}
static const TRImage *tr_program(void) {
 static const TRImage *const loaded = getenv("TR_PROGRAM") ? tr_load_image(getenv("TR_PROGRAM")) : nullptr;
 return loaded ? loaded : tr_image;
}
static int tr_select(TREnv &e) { return tr_select_dynamic(e, *tr_program()); }
static int tr_query_run(TREnv &e, int q) {
 const TRImage &P = *tr_program();
 if (P.query[q] < 0) return tr_query_default[q];
 for (int k = 0; k < P.nctx; k++) e.loc[k] = tr_i(0);
 return tr_invoke(e, P, P.query[q], nullptr).i;
}
// Entry points: run def `hook` with these arguments (no-op when the program lacks it).
static TRVal tr_hook(TREnv &e, int hook, const TRVal *args, TRVal absent) {
 const TRImage &P = *tr_program();
 if (P.hook[hook] < 0) return absent;
 for (int k = 0; k < P.nctx; k++) e.loc[k] = tr_i(0);
 return tr_invoke(e, P, P.hook[hook], args);
}
#else
// -------------------------------------------------------------- static evaluator
#define TR_INLINE __attribute__((always_inline)) inline
template <int N> static TR_INLINE TRVal tr_sev(TREnv &e);
template <int N> static int tr_sex(TREnv &e);

template <int Z, int K, int Count> static TR_INLINE char *tr_saddr_idx(TREnv &e, char *p) {
 if constexpr (K == Count) return p;
 else return tr_saddr_idx<Z, K + 1, Count>(e, p + (ptrdiff_t)tr_sev<trp_extra[Z + 1 + 2 * K]>(e).i * trp_extra[Z + 2 + 2 * K]);
}
template <int N> static TR_INLINE char *tr_saddr(TREnv &e) {
 constexpr TRNode n = trp_nodes[N];
 return tr_saddr_idx<n.z, 0, trp_extra[n.z]>(e, tr_base(e, n.x) + n.y);
}
template <int Z, int K, int Count> static TR_INLINE void tr_sargs(TREnv &e, TRVal *args) {
 if constexpr (K < Count) {
  args[K] = tr_sev<trp_extra[Z + K]>(e);
  tr_sargs<Z, K + 1, Count>(e, args);
 }
}
template <int D> static TRVal tr_scall(TREnv &e, const TRVal *args) {
 constexpr TRDef d = trp_defs[D];
 for (int k = 0; k < d.nparams; k++) e.loc[d.slot0 + k] = args[k];
 e.ret = tr_i(0);
 tr_sex<d.root>(e);
 return e.ret;
}
template <int N> static TR_INLINE TRVal tr_sev(TREnv &e) {
 constexpr TRNode n = trp_nodes[N];
 if constexpr (n.op == OP_KI) return tr_i(n.x);
 else if constexpr (n.op == OP_KF) return tr_f(__builtin_bit_cast(float, n.x));
 else if constexpr (n.op == OP_LOC) return e.loc[n.x];
 else if constexpr (n.op == OP_LOCREF) return tr_i(n.x);
 else if constexpr (n.op == OP_LDR) return e.loc[e.loc[n.x].i];
 else if constexpr (n.op == OP_ASG_R) return e.loc[e.loc[n.x].i] = tr_sev<n.y>(e);
 else if constexpr (n.op == OP_LDX) return e.loc[n.x + tr_sev<n.y>(e).i];
 else if constexpr (n.op == OP_ASG_X) {
  TRVal v = tr_sev<n.z>(e);
  return e.loc[n.x + tr_sev<n.y>(e).i] = v;
 } else if constexpr (n.op == OP_TABP) return tr_i(trp_extra[n.x + tr_sev<n.y>(e).i]);
 else if constexpr (n.op == OP_COPY) {
  char *src = tr_saddr<n.y>(e);
  memmove(tr_saddr<n.x>(e), src, n.z);
  return tr_i(0);
 } else if constexpr (n.op == OP_ZERO) { memset(tr_saddr<n.x>(e), 0, n.z); return tr_i(0); }
 else if constexpr (n.op == OP_LDM) return tr_load(tr_saddr<N>(e), n.w);
 else if constexpr (n.op == OP_TAB) return tr_i(tr_tables[n.x][tr_sev<n.y>(e).i]);
 else if constexpr (n.op == OP_LAND) return tr_i(tr_sev<n.x>(e).i && tr_sev<n.y>(e).i);
 else if constexpr (n.op == OP_LOR) return tr_i(tr_sev<n.x>(e).i || tr_sev<n.y>(e).i);
 else if constexpr (n.op == OP_COND) return tr_sev<n.x>(e).i ? tr_sev<n.y>(e) : tr_sev<n.z>(e);
 else if constexpr (n.op == OP_ASG_L) return e.loc[n.x] = tr_sev<n.y>(e);
 else if constexpr (n.op == OP_ASG_M) {
  TRVal v = tr_sev<trp_extra[n.z + 1 + 2 * trp_extra[n.z]]>(e);
  return tr_store(tr_saddr<N>(e), n.w, v);
 } else if constexpr (n.op == OP_CALLN) {
  TRVal args[n.w ? n.w : 1];
  tr_sargs<n.z, 0, n.w>(e, args);
  return tr_native_fn[n.x](e, args);
 } else if constexpr (n.op == OP_CALLD) {
  TRVal args[n.w ? n.w : 1];
  tr_sargs<n.z, 0, n.w>(e, args);
  return tr_scall<n.x>(e, args);
 } else if constexpr (n.op >= OP_NEG_I && n.op <= OP_TRUTH_F) return tr_un(n.op, tr_sev<n.x>(e));
 else {
  static_assert(n.op >= OP_ADD_I && n.op <= OP_GE_F, "not an expression node");
  TRVal a = tr_sev<n.x>(e);
  return tr_bin(n.op, a, tr_sev<n.y>(e));
 }
}
template <int Z, int K, int Count> static TR_INLINE int tr_sseq(TREnv &e) {
 if constexpr (K == Count) return TR_NEXT;
 else {
  int st = tr_sex<trp_extra[Z + K]>(e);
  if (st != TR_NEXT) return st;
  return tr_sseq<Z, K + 1, Count>(e);
 }
}
template <int N> static int tr_sex(TREnv &e) {
 constexpr TRNode n = trp_nodes[N];
 if constexpr (n.op == OP_SEQ) return tr_sseq<n.z, 0, n.w>(e);
 else if constexpr (n.op == OP_IF) {
  if (tr_sev<n.x>(e).i) return tr_sex<n.y>(e);
  if constexpr (n.z >= 0) return tr_sex<n.z>(e);
  else return TR_NEXT;
 } else if constexpr (n.op == OP_FOR) {
  constexpr int step = trp_extra[n.z], body = trp_extra[n.z + 1];
  if constexpr (n.x >= 0) tr_sex<n.x>(e);
  for (;;) {
   if constexpr (n.y >= 0) if (!tr_sev<n.y>(e).i) break;
   int st = tr_sex<body>(e);
   if (st == TR_BRK) break;
   if (st != TR_NEXT && st != TR_CONT) return st;
   if constexpr (step >= 0) tr_sex<step>(e);
  }
  return TR_NEXT;
 } else if constexpr (n.op == OP_TRY) {
  int act = tr_sev<n.x>(e).i;
  if (act < 0) return TR_NEXT;
  e.action = act;
  e.skill = tr_sev<n.y>(e).i;
  return TR_EMIT;
 } else if constexpr (n.op == OP_EMIT) {
  e.action = tr_sev<n.x>(e).i;
  e.skill = tr_sev<n.y>(e).i;
  return TR_EMIT;
 } else if constexpr (n.op == OP_PASS) return TR_PASS;
 else if constexpr (n.op == OP_BRK) return TR_BRK;
 else if constexpr (n.op == OP_CONT) return TR_CONT;
 else if constexpr (n.op == OP_RET) {
  if constexpr (n.x >= 0) e.ret = tr_sev<n.x>(e);
  return TR_RET;
 } else if constexpr (n.op == OP_EXPR) { tr_sev<n.x>(e); return TR_NEXT; }
 else return TR_NEXT;
}
template <int R> static TR_INLINE int tr_ssel(TREnv &e) {
 if constexpr (R == TRP_RULES) return -1;
 else {
  if (tr_sex<trp_rules[R].root>(e) == TR_EMIT) return R;
  return tr_ssel<R + 1>(e);
 }
}
static int tr_select(TREnv &e) {
 for (int k = 0; k < TRP_CTX; k++) e.loc[k] = tr_i(0);
 return tr_ssel<0>(e);
}
template <int D> static TRVal tr_shook(TREnv &e, const TRVal *args, TRVal absent) {
 if constexpr (D < 0) return absent;
 else {
  for (int k = 0; k < TRP_CTX; k++) e.loc[k] = tr_i(0);
  return tr_scall<D>(e, args);
 }
}
template <int Q> static TR_INLINE int tr_squery(TREnv &e) {
 if constexpr (trp_query[Q] < 0) return tr_query_default[Q];
 else {
  for (int k = 0; k < TRP_CTX; k++) e.loc[k] = tr_i(0);
  return tr_scall<trp_query[Q]>(e, nullptr).i;
 }
}
template <int Q> static TR_INLINE int tr_squery_at(TREnv &e, int q) {
 if constexpr (Q >= TRP_QUERIES) return 0;
 else return q == Q ? tr_squery<Q>(e) : tr_squery_at<Q + 1>(e, q);
}
static TR_INLINE int tr_query_run(TREnv &e, int q) { return tr_squery_at<0>(e, q); }
static TRVal tr_hook(TREnv &e, int hook, const TRVal *args, TRVal absent) {
 switch (hook) {
 case TR_HOOK_INIT: return tr_shook<TRP_HOOK_INIT>(e, args, absent);
 case TR_HOOK_OBSERVE: return tr_shook<TRP_HOOK_OBSERVE>(e, args, absent);
 case TR_HOOK_FILTER: return tr_shook<TRP_HOOK_FILTER>(e, args, absent);
 default: return tr_shook<TRP_HOOK_COMMIT>(e, args, absent);
 }
}
#endif

// ---------------------------------------------------------------- entry points
// Called by I's reset, observation, action and commit paths. The program sees
// the agent everywhere; the observation and scratch where the caller has them
// (not in init); never the configuration outside the rules.
static void tr_init_hook(FPAgent *a) {
 TREnv env; env.a = a; env.o = nullptr; env.cfg = nullptr; env.s = nullptr;
 tr_hook(env, TR_HOOK_INIT, nullptr, tr_i(0));
}
static void tr_observe_hook(FPAgent *a, const FPObservation *o, FPScratch *s, int changed) {
 TREnv env; env.a = a; env.o = o; env.cfg = nullptr; env.s = s;
 TRVal args[1] = {tr_i(changed)};
 tr_hook(env, TR_HOOK_OBSERVE, args, tr_i(0));
}
static int tr_filter_hook(FPAgent *a, const FPObservation *o, FPScratch *s, int action, int *skill) {
 TREnv env; env.a = a; env.o = o; env.cfg = nullptr; env.s = s;
 // the skill travels in a variable slot just above the program's own (the
 // interpreter's frame is TR_MAX_SLOTS wide; the program uses TRP_SLOTS)
 env.loc[TR_MAX_SLOTS - 1] = tr_i(*skill);
 TRVal args[2] = {tr_i(action), tr_i(TR_MAX_SLOTS - 1)};
 int out = tr_hook(env, TR_HOOK_FILTER, args, tr_i(action)).i;
 *skill = env.loc[TR_MAX_SLOTS - 1].i;
 return out;
}
// Queries: the machine asks the program (a def of no arguments) and gets an int.
static int tr_query(const FPAgent *a, const FPObservation *o, FPScratch *s, int q) {
 TREnv env; env.a = (FPAgent *)a; env.o = o; env.cfg = nullptr; env.s = s;
 return tr_query_run(env, q);
}
static void tr_commit_hook(FPAgent *a, const FPObservation *o, FPScratch *s, int action) {
 TREnv env; env.a = a; env.o = o; env.cfg = nullptr; env.s = s;
 TRVal args[1] = {tr_i(action)};
 tr_hook(env, TR_HOOK_COMMIT, args, tr_i(0));
}

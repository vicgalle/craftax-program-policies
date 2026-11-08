// lean.hpp: typed C++ views over the compact storage that full_policy.h
// declares with plain C types. The C header keeps the layout (C callers only
// take sizeof), and the policy reads and writes it through these views, so the
// algorithms keep their per-cell form while the bytes shrink.
//
// Language features only: under the fixed flags this toolchain's include path
// finds no C++ standard headers (a stale CommandLineTools c++/v1 directory
// shadows the SDK's libc++), so there is no <bit>/<span>; the bit scan uses
// __builtin_ctzll, as the base already does in next_block.
#pragma once

// One bit per atlas cell, stored in 64-bit words.
inline constexpr int kCellWords = FP_CELLS / 64;
static_assert(FP_CELLS % 64 == 0, "the atlas is a whole number of words");

template <class Word>  // uint64_t, or const uint64_t for a read-only view
class CellBitsOf {
 Word *w_;
 static constexpr uint64_t bit(int q) { return uint64_t{1} << (q & 63); }
public:
 explicit CellBitsOf(Word (&w)[kCellWords]) : w_(w) {}
 bool operator[](int q) const { return (w_[q >> 6] & bit(q)) != 0; }
 void set(int q) { w_[q >> 6] |= bit(q); }
 void reset(int q) { w_[q >> 6] &= ~bit(q); }
 void clear() { for (int i = 0; i < kCellWords; i++) w_[i] = 0; }
};
using CellBits = CellBitsOf<uint64_t>;
using ConstCellBits = CellBitsOf<const uint64_t>;

// Index of the lowest set bit of a nonzero word.
constexpr int lowest_bit(uint64_t w) { return __builtin_ctzll(w); }

// The set bits of words[0..n), as their indices in increasing order:
//   for (int p : SetBits(words, n)) ...
class SetBits {
 const uint64_t *w_;
 int n_;
public:
 SetBits(const uint64_t *w, int n) : w_(w), n_(n) {}
 struct End {};
 class It {
  const uint64_t *w_;
  int n_, i_;
  uint64_t cur_;
  void skip() { while (!cur_ && ++i_ < n_) cur_ = w_[i_]; }
 public:
  It(const uint64_t *w, int n) : w_(w), n_(n), i_(0), cur_(n > 0 ? w[0] : 0) { skip(); }
  int operator*() const { return i_ * 64 + lowest_bit(cur_); }
  It &operator++() { cur_ &= cur_ - 1; skip(); return *this; }
  bool operator!=(End) const { return i_ < n_; }
 };
 It begin() const { return It(w_, n_); }
 End end() const { return {}; }
};

// A per-cell layer that can be nonzero only on the 9x11 view (FP_VIEW cells)
// around the agent: wm_observe zeroes the layer and then writes view cells
// only. Storage index = view index i, whose cell is (r0 + i/11, c0 + i%11),
// so reading by atlas cell returns the stored value inside the view and the
// zero every other cell of the original FP_CELLS array held.
inline constexpr int kViewRows = 9, kViewCols = 11;
static_assert(kViewRows * kViewCols == FP_VIEW);

template <class T>
struct ViewLayer {
 const T *v;
 int r0, c0;
 T at(int r, int c) const {
  unsigned vr = unsigned(r - r0), vc = unsigned(c - c0);
  return vr < kViewRows && vc < kViewCols ? v[vr * kViewCols + vc] : T{};
 }
 T operator[](int q) const { return at(q / FP_SIDE, q % FP_SIDE); }
};

// FPFloor.marks: one byte per cell. Every reader of visits takes
// imin(visits, 50), so a counter saturating at 50 reads exactly as the former
// 16-bit one saturating at 65535: min(min(n, 50), 50) == min(min(n, 65535), 50).
inline constexpr int kVisitCap = 50;
static_assert(kVisitCap < (1 << 6), "visits is a 6-bit field");
static_assert(sizeof(FPMark) == 1, "item, lit and visits share one byte");

// FPScratch value ranges behind the narrowed arrays.
static_assert(FP_CELLS - 1 <= 0xFFFF, "cell indices (heap, targets, unseen_cand) fit uint16_t");
static_assert(FP_SIDE * FP_SIDE <= 0xFFFF, "integral-image counts (<= one per cell) fit uint16_t prefix");

// The mob layers of the current view and the projectile danger bits.
static ViewLayer<uint8_t> mobs_of(const FPScratch *s) { return {s->mobs, s->view_r0, s->view_c0}; }
static ViewLayer<uint8_t> melee_of(const FPScratch *s) { return {s->melee, s->view_r0, s->view_c0}; }
static ViewLayer<uint8_t> ranged_of(const FPScratch *s) { return {s->ranged, s->view_r0, s->view_c0}; }
static ViewLayer<uint8_t> passive_of(const FPScratch *s) { return {s->passive, s->view_r0, s->view_c0}; }
// Read only where melee or ranged is nonzero, i.e. on a view cell.
static ViewLayer<float> enemy_health_of(const FPScratch *s) { return {s->enemy_health, s->view_r0, s->view_c0}; }
static ConstCellBits arrows_of(const FPScratch *s) { return ConstCellBits{s->arrow_next}; }
static void arrow_mark(FPScratch *s, int q) { CellBits{s->arrow_next}.set(q); }

// The FPScratch reader workspaces (targets, prefix, unseen, route) share one
// union. Live ranges, each inside one function, and the only calls made
// while live:
//   targets: seek/hunt fill, then cheapest_stand reads (paths_* and wm_risk);
//   unseen:  unseen_rows fills, then graveyard_light, light_unseen, dark_probe
//            and sgl_probe read through unseen_gain (torch_valid, mobs_of,
//            path_dist_within);
//   prefix:  explore/frontier_walk (settled_box), fire_search (travel),
//            mid_pen_stand, mid_fscout, recover (water_distances; room_has,
//            chamber_walls, wm_risk);
//   route:   rm_search inside rm_cache_floor (rm_enter_cost, rm_is_source).
// None of the calls listed writes any workspace (paths_* write dist, first,
// heap, heap_key and path_*; the rest only read), and every read lies inside
// the entries its own fill wrote (the bounds noted at each fill).
static_assert(FP_SIDE == 96 && (2 * FP_SIDE) % 64 == 0, "an atlas row is 1.5 words: it starts at bit 0 or 32 (unseen_rows)");

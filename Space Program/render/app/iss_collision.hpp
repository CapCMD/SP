// render/app/iss_collision.hpp
//
// COLLISION MAILLEE de l'interieur ISS : l'oeil (premiere personne) est une SPHERE
// qu'on empeche de penetrer la geometrie du modele. Apesanteur -> pas de gravite ni
// de sol : on RESOUT simplement les penetrations (push-out + glissement), ce qui
// suffit a garder le joueur DANS les couloirs sans passer a travers les parois.
//
// Autonome : ne depend que de <vector>/<cmath> et d'un petit vecteur double local.
// Les triangles sont en REPERE STATION (transform du modele deja appliquee). Un BVH
// (median-split sur les centroides) accelere la requete sphere -> triangles proches.
//
// FAIL-SAFE : si aucun triangle n'est fourni, resolve() ne fait rien.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace isscol {

struct V3 { double x{0}, y{0}, z{0}; };
inline V3   operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3   operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3   operator*(const V3& a, double s)    { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(const V3& a, const V3& b)     { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double len(const V3& a)                  { return std::sqrt(dot(a, a)); }

// Point du triangle (a,b,c) le plus proche de p (Ericson, Real-Time Collision Detection).
inline V3 closest_on_tri(const V3& p, const V3& a, const V3& b, const V3& c) {
  const V3 ab = b - a, ac = c - a, ap = p - a;
  const double d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  const V3 bp = p - b;
  const double d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) { const double v = d1 / (d1 - d3); return a + ab * v; }
  const V3 cp = p - c;
  const double d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) { const double w = d2 / (d2 - d6); return a + ac * w; }
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return b + (c - b) * w;
  }
  const double denom = 1.0 / (va + vb + vc);
  const double v = vb * denom, w = vc * denom;
  return a + ab * v + ac * w;
}

class Mesh {
 public:
  bool empty() const { return tris_.empty(); }
  std::size_t triangle_count() const { return tris_.size(); }

  // Construit a partir de triangles a plat (3 sommets consecutifs par triangle).
  // On IGNORE les triangles degeneres (aire ~0) : ils ne collisionnent pas et
  // polluent le BVH.
  void build(const std::vector<V3>& flat_tris) {
    tris_.clear(); nodes_.clear();
    const std::size_t nt = flat_tris.size() / 3;
    tris_.reserve(nt);
    for (std::size_t t = 0; t < nt; ++t) {
      Tri tr{flat_tris[t * 3], flat_tris[t * 3 + 1], flat_tris[t * 3 + 2], V3{}};
      const V3 e1 = tr.b - tr.a, e2 = tr.c - tr.a;
      const V3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
      if (len(n) < 1e-9) continue;                 // triangle degenere
      tr.c0 = (tr.a + tr.b + tr.c) * (1.0 / 3.0);   // centroide (pour le tri BVH)
      tris_.push_back(tr);
    }
    if (tris_.empty()) return;
    order_.resize(tris_.size());
    for (std::size_t i = 0; i < order_.size(); ++i) order_[i] = static_cast<std::uint32_t>(i);
    nodes_.reserve(tris_.size() * 2);
    build_node(0, static_cast<int>(order_.size()));
  }

  // Repousse `eye` (sphere de rayon `r`) hors de la geometrie. Plusieurs passes :
  // chaque passe resout le contact le PLUS profond -> convergence dans les coins.
  // Renvoie true si l'oeil a ete deplace.
  bool resolve(double& ex, double& ey, double& ez, double r) const {
    if (nodes_.empty()) return false;
    bool moved = false;
    for (int pass = 0; pass < 6; ++pass) {
      V3 p{ex, ey, ez};
      double best_pen = 1e-4;   // ignore les contacts negligeables (anti-jitter)
      V3 best_dir{0, 0, 0};
      query_deepest(0, p, r, best_pen, best_dir);
      if (best_pen <= 1e-4) break;
      ex += best_dir.x * best_pen; ey += best_dir.y * best_pen; ez += best_dir.z * best_pen;
      moved = true;
    }
    return moved;
  }

 private:
  struct Tri { V3 a, b, c, c0; };
  struct Node { double bmin[3], bmax[3]; int left{-1}, right{-1}; int start{0}, count{0}; };

  std::vector<Tri>          tris_;
  std::vector<std::uint32_t> order_;   // indices de tris_ ordonnes par les feuilles
  std::vector<Node>         nodes_;

  static void expand(Node& nd, const V3& p) {
    nd.bmin[0] = std::min(nd.bmin[0], p.x); nd.bmax[0] = std::max(nd.bmax[0], p.x);
    nd.bmin[1] = std::min(nd.bmin[1], p.y); nd.bmax[1] = std::max(nd.bmax[1], p.y);
    nd.bmin[2] = std::min(nd.bmin[2], p.z); nd.bmax[2] = std::max(nd.bmax[2], p.z);
  }

  // Construit le noeud couvrant order_[start..start+count) ; renvoie son index.
  int build_node(int start, int count) {
    const int idx = static_cast<int>(nodes_.size());
    nodes_.push_back(Node{});
    Node nd{}; nd.start = start; nd.count = count;
    for (int i = 0; i < 3; ++i) { nd.bmin[i] = 1e30; nd.bmax[i] = -1e30; }
    for (int i = 0; i < count; ++i) {
      const Tri& t = tris_[order_[start + i]];
      expand(nd, t.a); expand(nd, t.b); expand(nd, t.c);
    }
    if (count <= 8) { nodes_[idx] = nd; return idx; }   // feuille
    // axe le plus long
    const double ex = nd.bmax[0] - nd.bmin[0], ey = nd.bmax[1] - nd.bmin[1], ez = nd.bmax[2] - nd.bmin[2];
    const int axis = (ex >= ey && ex >= ez) ? 0 : (ey >= ez ? 1 : 2);
    const double mid = 0.5 * (nd.bmin[axis] + nd.bmax[axis]);
    auto centroid_axis = [&](std::uint32_t ti) {
      const V3& c = tris_[ti].c0; return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
    };
    auto beg = order_.begin() + start;
    auto mid_it = std::partition(beg, beg + count, [&](std::uint32_t ti) { return centroid_axis(ti) < mid; });
    int nleft = static_cast<int>(mid_it - beg);
    if (nleft == 0 || nleft == count) nleft = count / 2;   // repli : coupe mediane
    const int l = build_node(start, nleft);
    const int rgt = build_node(start + nleft, count - nleft);
    nd.left = l; nd.right = rgt;
    nodes_[idx] = nd;
    return idx;
  }

  static bool sphere_hits_box(const Node& nd, const V3& p, double r) {
    double d2 = 0.0;
    const double px[3] = {p.x, p.y, p.z};
    for (int i = 0; i < 3; ++i) {
      const double v = px[i];
      if (v < nd.bmin[i]) d2 += (nd.bmin[i] - v) * (nd.bmin[i] - v);
      else if (v > nd.bmax[i]) d2 += (v - nd.bmax[i]) * (v - nd.bmax[i]);
    }
    return d2 <= r * r;
  }

  // Trouve le contact le plus profond (penetration + direction de sortie normalisee).
  void query_deepest(int node, const V3& p, double r, double& best_pen, V3& best_dir) const {
    const Node& nd = nodes_[node];
    if (!sphere_hits_box(nd, p, r)) return;
    if (nd.left < 0) {   // feuille
      for (int i = 0; i < nd.count; ++i) {
        const Tri& t = tris_[order_[nd.start + i]];
        const V3 q = closest_on_tri(p, t.a, t.b, t.c);
        const V3 d = p - q;
        const double dist = len(d);
        if (dist < r && dist > 1e-9) {
          const double pen = r - dist;
          if (pen > best_pen) { best_pen = pen; best_dir = d * (1.0 / dist); }
        }
      }
      return;
    }
    query_deepest(nd.left, p, r, best_pen, best_dir);
    query_deepest(nd.right, p, r, best_pen, best_dir);
  }
};

}  // namespace isscol

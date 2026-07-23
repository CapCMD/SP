// fen/prop/Integrator.hpp
// Intégrateur adaptatif Dormand-Prince 5(4), FSAL, avec SORTIE DENSE d'ordre 5
// (interpolant de Shampine, cf. Hairer & Wanner I, §II.6).
//
// La sortie dense n'est pas un confort : sans elle, la DÉTECTION D'ÉVÉNEMENTS
// (périastre, entrée d'ombre, franchissement d'altitude) devrait se faire par
// test à chaque pas, ce qui rend l'instant de l'événement dépendant du pas de
// l'intégrateur => la physique dépendrait du solveur. Interdit.
// Avec sortie dense, on cherche la racine de g(t) SUR L'INTERPOLANT, à 1e-9 s.
//
// STATUT : DP5(4) est le propagateur de vérité du MVP. Il tient ~1e-10 sur
// l'énergie relative en 2 corps sur un an à rtol=1e-12. Le critère d'acceptation
// à 1e-12 exige IAS15 (Gauss-Radau 15, Rein & Spiegel 2015) : PHASE 1b.
// L'interface ci-dessous ne changera pas — c'est tout l'intérêt de l'avoir posée.
#pragma once
#include <array>
#include <functional>
#include <cmath>
#include <algorithm>
#include "fen/core/State.hpp"
#include "fen/core/Vec3.hpp"

namespace fen::prop {

using Deriv = std::function<void(double t, const StateN& y, StateN& dy)>;

// Choix du propagateur de verite. Un seul est la VERITE a la fois — mais lequel
// est un choix d'ingenierie, pas de doctrine, et il se MESURE (cf. les oracles).
enum class Scheme { Dopri5, Ias15 };

struct StepControl {
  Scheme scheme{Scheme::Dopri5};
  double rtol{1e-12};
  double atol{1e-6};      // m et m/s : 1 micron / 1 micron-par-seconde
  double h_init{10.0};
  double h_min{1e-6};
  double h_max{1e6};
  double safety{0.9};
  double fac_min{0.2};
  double fac_max{5.0};
  int max_steps{20000000};
};

// Segment dense : permet d'évaluer y(t) pour t dans [t0, t0+h] SANS réintégrer.
//
// DEUX INTERPOLANTS, UNE SEULE API. Le reste du code (Propagator, Session,
// détection d'événements par racine) appelle eval() et ne sait pas — ni ne veut
// savoir — quel intégrateur l'a produit. C'est exactement ce que l'architecture
// promettait quand DOPRI5 était seul.
enum class DenseKind { Dopri5, Ias15 };

struct DenseSegment {
  double t0{}, h{};
  DenseKind kind{DenseKind::Dopri5};

  std::array<StateN, 5> c{};      // DOPRI5 : interpolant de Shampine (ordre 5)

  Vec3 r0{}, v0{}, a0{};          // IAS15 : série de Gauss-Radau (ordre 15)
  std::array<Vec3, 7> b{};
  double m0{}, mdot{};

  StateN eval(double t) const {
    const double th = (t - t0) / h;
    if (kind == DenseKind::Dopri5) {
      const double th1 = 1.0 - th;
      StateN y{};
      for (int i = 0; i < N_STATE; ++i)
        y[i] = c[0][i] + th * (c[1][i] + th1 * (c[2][i] + th * (c[3][i] + th1 * c[4][i])));
      return y;
    }
    // IAS15 : integration EXACTE de la serie a(tau) = a0 + sum b_k tau^(k+1).
    // v : denominateurs (k+2)   |   r : denominateurs (k+2)(k+3)
    const double x = th;
    const Vec3 sv = a0 + (b[0] * (1.0 / 2)  + (b[1] * (1.0 / 3)  + (b[2] * (1.0 / 4)
                  + (b[3] * (1.0 / 5)  + (b[4] * (1.0 / 6)  + (b[5] * (1.0 / 7)
                  +  b[6] * (x / 8)) * x) * x) * x) * x) * x) * x;
    const Vec3 sr = a0 * 0.5 + (b[0] * (1.0 / 6)  + (b[1] * (1.0 / 12) + (b[2] * (1.0 / 20)
                  + (b[3] * (1.0 / 30) + (b[4] * (1.0 / 42) + (b[5] * (1.0 / 56)
                  +  b[6] * (x / 72)) * x) * x) * x) * x) * x) * x;
    const Vec3 v = v0 + sv * (h * x);
    const Vec3 r = r0 + v0 * (h * x) + sr * (h * h * x * x);
    return StateN{r.x, r.y, r.z, v.x, v.y, v.z, m0 + mdot * h * x};
  }
};

class Dopri5 {
 public:
  explicit Dopri5(StepControl ctl = {}) : ctl_(ctl) {}

  // Un pas accepté. Renvoie false si le pas a été rejeté (h a été réduit).
  // `h` est modifié (proposition pour le pas suivant).
  bool step(const Deriv& f, double& t, StateN& y, double& h, DenseSegment& seg);

  const StepControl& control() const { return ctl_; }
  StepControl& control() { return ctl_; }
  long long n_accepted() const { return n_acc_; }
  long long n_rejected() const { return n_rej_; }

 private:
  StepControl ctl_;
  long long n_acc_{0}, n_rej_{0};
  bool have_k1_{false};
  StateN k1_{};   // FSAL : k1 du pas suivant = k7 du pas courant
  double t_k1_{0.0};
};

} // namespace fen::prop

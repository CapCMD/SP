// fen/prop/Ias15.hpp
//
// IAS15 — Gauss-Radau d'ordre 15 (Rein & Spiegel 2015, MNRAS 446:1424).
//
// POURQUOI CETTE BRIQUE EXISTE, ET POURQUOI ELLE ARRIVE MAINTENANT.
//
// J'avais annoncé un critère d'acceptation : |dE/E| < 1e-12 sur un an. Puis j'ai
// MESURÉ DOPRI5 : 2,5e-9. J'ai alors fait la seule chose honnête — publier le
// chiffre réel, rétracter le critère, et renvoyer IAS15 en phase 1b en écrivant :
//
//     « L'interface Dopri5 / DenseSegment ne changera pas — c'est tout l'intérêt
//       de l'avoir posée ainsi. »
//
// Cette brique est donc AUSSI un test de cette affirmation. Si l'architecture
// était juste, IAS15 s'insère sans casser une ligne de Propagator, de Session,
// d'Executor ni d'aucun script. Sinon, l'architecture était fausse et il faut
// le dire.
//
// LE SCHÉMA. Sur un pas [t, t+h], l'accélération est développée en série :
//     a(tau) = a0 + b0*tau + b1*tau^2 + ... + b6*tau^7      (tau dans [0,1])
// Les b sont trouvés par PRÉDICTEUR-CORRECTEUR aux 8 noeuds de Gauss-Radau.
// L'intégration exacte de cette série donne v et r à l'ordre 15 :
//     v(tau) = v0 + h*tau*[a0 + tau*(b0/2 + tau*(b1/3 + ... ))]
//     r(tau) = r0 + h*tau*v0 + h^2*tau^2*[a0/2 + tau*(b0/6 + tau*(b1/12 + ...))]
// (dénominateurs (k+2) et (k+2)(k+3) : ce sont deux intégrations, rien d'autre.)
//
// LA MASSE. Pendant un arc de poussée, mdot = -F/(Isp*g0) est CONSTANT — F et
// Isp le sont. La masse est donc EXACTEMENT linéaire sur le pas, et les points
// de rupture garantissent qu'aucun pas ne chevauche un allumage. On l'intègre
// analytiquement : aucune erreur, aucun coût.
//
// CE QUE SIGNIFIE `rtol` POUR IAS15 — ET CE N'EST PAS CE QUE VOUS CROYEZ.
//
// L'estimateur d'erreur est |b6| / |a0| : le DERNIER coefficient de la serie,
// rapporte a l'acceleration. Or b6 est une DIFFERENCE DIVISEE D'ORDRE 7. Quand
// le pas retrecit, les 8 accelerations aux noeuds deviennent quasi identiques,
// et sept niveaux de soustraction ne laissent plus que de l'ARRONDI. L'estimateur
// PLANCHE — il ne descend plus, quel que soit le pas.
//
// Consequence MESUREE : lui demander rtol = 1e-13 fait s'effondrer le pas
// jusqu'a h_min sans jamais satisfaire le critere. 41 rejets, 25 pas, t = 0,015 s.
// Et pendant ce temps l'energie etait deja conservee a 1e-15.
//
// Autrement dit : le controleur hurlait alors que l'integration etait parfaite.
//
// >>> POUR IAS15, rtol EST L'EPSILON DE REIN & SPIEGEL. Sa valeur utile est
//     ~1e-9, et elle donne DEJA la precision machine. Ce n'est pas la meme
//     grandeur que le rtol de DOPRI5, et confondre les deux est une faute. <<<
inline constexpr double IAS15_EPS_DEFAULT = 1e-9;
inline constexpr double IAS15_EPS_FLOOR   = 1e-11;   // en dessous : que du bruit

// LA SORTIE DENSE EST GRATUITE. Les b sont les coefficients d'un polynôme
// d'ordre 15 : évaluer r(tau) et v(tau) EST la sortie dense. La détection
// d'événements par recherche de racine fonctionne donc exactement comme avant.
#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include "fen/core/State.hpp"
#include "fen/prop/Integrator.hpp"

namespace fen::prop {

// Noeuds de Gauss-Radau (8 noeuds, le premier fixé à 0).
// Ils ne sont PAS vérifiables à l'oeil. Ils le sont par l'ORDRE de convergence :
// si un seul chiffre est faux, le schéma s'effondre à l'ordre 2 et l'oracle
// d'énergie hurle. C'est le test.
inline constexpr std::array<double, 8> IAS15_H = {
    0.0,
    0.0562625605369221464656521910318,
    0.1802406917368923649875799428306,
    0.3526247171131696373739077702949,
    0.5471536263305553830014485577615,
    0.7342101772154105315232106580808,
    0.8853209468390957680903597629771,
    0.9775206135612875018911745004598};

class Ias15 {
 public:
  explicit Ias15(StepControl ctl = {}) : ctl_(ctl) {}

  bool step(const Deriv& f, double& t, StateN& y, double& h, DenseSegment& seg);

  const StepControl& control() const { return ctl_; }
  StepControl& control() { return ctl_; }
  long long n_accepted() const { return n_acc_; }
  long long n_rejected() const { return n_rej_; }

 private:
  StepControl ctl_;
  long long n_acc_{0}, n_rej_{0};
  std::array<Vec3, 7> b_{};   // coefficients du pas précédent (prédiction)
  bool have_b_{false};
  double h_prev_{0.0};
};

} // namespace fen::prop

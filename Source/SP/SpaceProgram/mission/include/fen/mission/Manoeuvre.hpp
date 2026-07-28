// fen/mission/Manoeuvre.hpp — LA MANŒUVRE EST UN ACTE DU JOUEUR [GDD 7.4, 8.4]
//
// « Toutes les manœuvres (Hohmann, assistances, insertion, corrections) sont
// calculées PAR LE JOUEUR dans le terminal. L'assistance dépend UNIQUEMENT du
// mode, pas du rang. » [GDD 7.4]
//
// Tout ce qui précède ce fichier calculait la correction À SA PLACE : le modèle
// déterminait le Δv, l'orientait et l'exécutait, et le joueur n'avait qu'à
// provisionner la marge. C'était de la conception, pas du pilotage. Ici, la
// correction devient ce que le GDD demande : un geste, à une date, avec un
// vecteur que le joueur donne.
//
// ═══ CE QU'IL COMMANDE, ET DANS QUEL REPÈRE ═══
// Trois composantes en repère RSW — Radial / le long de la trajectoire /
// hors-plan. `fen/core/Vec3.hpp` le déclare depuis toujours comme « LE repère
// dans lequel le joueur exprime ses Delta-v » ; c'est aussi celui des opérations
// réelles. Le repère est construit sur l'état qu'il CROIT (sa solution de
// navigation), pas sur l'état vrai : commander dans un repère qu'on ne connaît
// pas exactement fait partie du problème.
//
// ═══ CE QUE LE MODÈLE FAIT ENSUITE, ET RIEN DE PLUS ═══
// Il applique LITTÉRALEMENT ce qui est commandé — avec l'erreur d'exécution de
// Gates, qui ne se commande pas — à l'état VRAI. Aucune correction silencieuse,
// aucun rattrapage. Un Δv mal orienté empire la trajectoire, et c'est juste.
//
// ═══ L'ASSISTANCE DÉPEND DU MODE, ET DE RIEN D'AUTRE ═══ [GDD 2.2]
// `solveur_correction` calcule le Δv qui annule le manque au but projeté depuis
// l'ESTIMÉ. En **Normal** il est offert au joueur (l'équivalent du nœud
// préconstruit « solveur » du graphe) ; en **Pro** l'appelant ne doit pas
// l'appeler — le joueur écrit son calcul. Le modèle expose la fonction ; c'est
// la surface de jeu qui décide de la montrer, exactement comme le GDD le veut.
//
// C++ pur, aucune dépendance UE.
#pragma once
#include <cmath>
#include <cstdint>

#include "fen/astro/Kepler.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/mission/Navigation.hpp"
#include "fen/nav/Gates.hpp"

namespace fen::mission {

// L'ÉTAT VRAI DU VOL — jamais publié au joueur [GDD 7.5]. Il vit sur la mission
// et se sauvegarde : un vol rechargé reprend là où il en était, avec l'erreur
// qu'il traîne.
struct EtatVol {
  bool   valide{false};
  double t_days{0.0};      // date de validité (jours de jeu)
  Vec3   r{}, v{};         // héliocentrique écliptique, SI
};

// Propage l'état vrai jusqu'à une date. Képlérien, comme l'arc qu'il suit.
inline bool avancer_etat_vol(EtatVol& e, double t_days) {
  if (!e.valide) return false;
  const double dt = (t_days - e.t_days) * cst::DAY;
  if (dt <= 0.0) return true;
  const auto K = astro::kepler_propagate(e.r, e.v, dt, cst::MU_SUN);
  if (!K.converged) return false;
  e.r = K.r; e.v = K.v; e.t_days = t_days;
  return true;
}

// ═══ CE QUE LE JOUEUR VOIT DE SON VOL ═══
// La solution de navigation appliquée à l'instant courant : son état ESTIMÉ, et
// ce que cet estimé prédit au point de visée. C'est la matière de sa décision —
// et c'est tout ce qu'il a. Sans poursuite, `dv_connu` est nul : son estimé EST
// le nominal, et le manque au but projeté est... zéro. Il croit tout aller bien.
struct VueNavigation {
  bool   ok{false};
  Vec3   r_estime{}, v_estime{};
  Vec3   manque_projete{};      // écart au point de visée prédit depuis l'estimé
  double manque_km{0.0};
  double sigma_r{0.0}, sigma_v{0.0};
  double reste_jours{0.0};      // avant le point de visée
  // DÉLAI DE COMMUNICATION, un sens [GDD 8.3, 9.6]. Le plan terminal le liste
  // depuis toujours parmi ce qu'il doit afficher, et rien ne le calculait. Ce
  // n'est pas qu'un affichage : ce que le joueur commande PART maintenant et
  // ARRIVE dans `delai_com_s` — le logiciel de bord, lui, est déjà sur place.
  // Rempli par l'appelant, qui seul sait où est la Terre (`Session::vue_vol`).
  double delai_com_s{0.0};
};

// `dv_connu` : l'écart de vitesse que la POURSUITE a révélé (NavSolution::
// dv_estime). Le nominal plus cet écart, c'est l'état que le joueur croit avoir.
inline VueNavigation vue_navigation(const Vec3& r_nom_dep, const Vec3& v_nom_dep,
                                    const Vec3& dv_connu, const Vec3& cible,
                                    double t_dep_days, double now_days,
                                    double t_arr_days, double sigma_r, double sigma_v) {
  VueNavigation vn;
  const double dt = (now_days - t_dep_days) * cst::DAY;
  const double reste = (t_arr_days - now_days) * cst::DAY;
  if (dt < 0.0 || reste <= 0.0) return vn;
  const auto K = astro::kepler_propagate(r_nom_dep, v_nom_dep + dv_connu, dt, cst::MU_SUN);
  if (!K.converged) return vn;
  vn.r_estime = K.r; vn.v_estime = K.v;
  const auto Kf = astro::kepler_propagate(K.r, K.v, reste, cst::MU_SUN);
  if (!Kf.converged) return vn;
  vn.manque_projete = Kf.r - cible;
  vn.manque_km = norm(vn.manque_projete) / 1000.0;
  vn.sigma_r = sigma_r; vn.sigma_v = sigma_v;
  vn.reste_jours = t_arr_days - now_days;
  vn.ok = true;
  return vn;
}

// ═══ LE SOLVEUR — assistance de mode NORMAL uniquement [GDD 2.2] ═══
// Le Δv qui annule le manque au but projeté : Δv = −Φ_rv(arr←maintenant)⁻¹ · manque.
// C'est UN nœud préconstruit, pas une automatisation : il répond à la question
// que le joueur pose, il ne décide ni du moment ni de l'opportunité.
// Rendu en RSW, le repère dans lequel le joueur commande.
inline bool solveur_correction(const VueNavigation& vn, Vec3& dv_rsw_out) {
  if (!vn.ok) return false;
  const StmBlocks Phi = kepler_stm(vn.r_estime, vn.v_estime,
                                   vn.reste_jours * cst::DAY, cst::MU_SUN);
  M3 inv;
  if (!Phi.ok || !inverse3(Phi.rv, inv)) return false;
  const Vec3 dv_inertiel = inv * (Vec3{} - vn.manque_projete);
  const Basis3 b = rsw_basis(vn.r_estime, vn.v_estime);
  dv_rsw_out = {dot(dv_inertiel, b.R), dot(dv_inertiel, b.S), dot(dv_inertiel, b.W)};
  return true;
}

// ═══ EXÉCUTER ═══
// Le Δv commandé en RSW passe dans le repère inertiel PAR LA BASE DE L'ESTIMÉ
// (le joueur ne connaît pas mieux), subit l'erreur de Gates, et s'ajoute à
// l'état VRAI. Rend la norme réellement dépensée — c'est elle qui grève la marge.
struct ResultatManoeuvre {
  bool   ok{false};
  double dv_commande{0.0};   // ce qu'il a demandé
  double dv_depense{0.0};    // ce que le moteur a réellement délivré
};

inline ResultatManoeuvre executer_correction(EtatVol& vrai, const VueNavigation& vn,
                                             const Vec3& dv_rsw, std::uint64_t seed,
                                             const nav::GatesParams& gates = {}) {
  ResultatManoeuvre res;
  if (!vrai.valide || !vn.ok) return res;
  const Basis3 b = rsw_basis(vn.r_estime, vn.v_estime);
  const Vec3 dv_cmd = b.R * dv_rsw.x + b.S * dv_rsw.y + b.W * dv_rsw.z;
  res.dv_commande = norm(dv_cmd);
  if (res.dv_commande <= 0.0) { res.ok = true; return res; }   // ne rien faire est un choix
  Rng rng(seed);
  const Vec3 dv_obtenu = nav::apply_gates(dv_cmd, gates, rng);
  vrai.v = vrai.v + dv_obtenu;
  res.dv_depense = norm(dv_obtenu);
  res.ok = true;
  return res;
}

// Le manque au but RÉEL depuis l'état vrai — ce que le débrief révélera, et ce
// dont dépend l'issue. Le joueur ne l'obtient jamais avant l'arrivée.
inline double manque_reel_km(const EtatVol& vrai, const Vec3& cible, double t_arr_days) {
  if (!vrai.valide) return 0.0;
  const double reste = (t_arr_days - vrai.t_days) * cst::DAY;
  if (reste <= 0.0) return norm(vrai.r - cible) / 1000.0;
  const auto K = astro::kepler_propagate(vrai.r, vrai.v, reste, cst::MU_SUN);
  if (!K.converged) return 0.0;
  return norm(K.r - cible) / 1000.0;
}

} // namespace fen::mission

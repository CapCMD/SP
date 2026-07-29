// fen/mission/Program.hpp
//
// LA COUCHE GESTION — le dernier axiome sans code.
//
// Jusqu'ici, une mission ne pouvait échouer que PHYSIQUEMENT : ergols épuisés,
// orbite manquée, impact. C'était une bibliothèque de dynamique du vol, pas un
// jeu. Il manquait ce qui tue le plus de missions réelles : **on n'avait pas les
// moyens**, ou **on n'avait pas le temps**.
//
// TROIS MONNAIES, ET ELLES SONT COUPLÉES :
//
//   ARGENT    lanceur, moteur, étage, poursuite, temps de calcul, revue, essais
//   TEMPS     délai d'approvisionnement, intégration, fenêtre de lancement
//   RISQUE    P(panne) par événement, fonction des heures d'essai et de l'héritage
//
// Le couplage n'est pas décoratif — il est calculé :
//   plus de poursuite  -> moins de marge de correction -> moins d'ergols
//                      -> étage plus léger -> LANCEUR MOINS CHER.
//   moteur à haut Isp  -> moins d'ergols -> lanceur moins cher
//                      -> mais délai plus long, et il peut manquer la fenêtre.
//   moteur neuf        -> moins cher à l'unité, mais 0 vol d'héritage
//                      -> il faut ACHETER de la fiabilité en heures d'essai.
//
// AUCUN de ces chiffres n'est un point de jeu. Ce sont des dollars, des mois et
// des probabilités, et ils se propagent dans l'équation de la fusée.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "fen/mission/Assemblage.hpp"
#include "fen/vehicle/Vehicle.hpp"

namespace fen::mission {

// --- CATALOGUE DE LANCEURS ---------------------------------------------------
// Un lanceur ne vend PAS du Delta-v : il vend une MASSE sur une ORBITE, pour un
// PRIX, avec une FIABILITÉ et un DÉLAI. Le joueur achète une ligne de ce tableau.
struct Launcher {
  std::string id;
  double payload_leo{};        // kg, vers 200 km / 28.5 deg
  double cost_musd{};
  double reliability{};        // P(succès), historique
  double lead_months{};
  // ═══ LE NŒUD D'ARBRE QUI LE QUALIFIE ═══ [GDD 5.4]
  // Il manquait, et la branche 1 « Accès à l'orbite » ne gardait donc RIEN :
  // `lanceur_leger`, `lanceur_moyen`, `lanceur_lourd` et `lanceur_super_lourd`
  // étaient quatre nœuds à rechercher qui ne débloquaient aucun lanceur — le
  // joueur disposait de toute la gamme dès la première mission.
  std::string tech_id;
};

inline const std::vector<Launcher>& launchers() {
  static const std::vector<Launcher> v = {
      {"L-A leger",  4200.0,   41.0, 0.920, 10.0, "lanceur_leger"},   // PSLV-XL : 3,8 t
      {"L-B moyen",  5800.0,   62.0, 0.960, 12.0, "lanceur_moyen"},   // Soyouz-2 : 5-8 t
      // ═══ UN « LOURD » À 8,3 t N'A AUCUNE LIGNÉE RÉELLE ═══ [GDD 12.1]
      // « Assemblage à partir de pièces réelles ou extrapolées de lignées
      // réelles, jamais génériques. » La classe lourde RÉELLE, c'est Falcon 9
      // Block 5 (22,8 t en LEO) ou Atlas V 541 (17 t) — pas 8,3 t, qui est un
      // moyen déguisé. Conséquence MESURÉE de cette sous-évaluation : l'échelle
      // sautait de 8,3 t à 130 t (facteur 15,7) sans rien entre les deux, si bien
      // que CAT-04 — qui dépasse le plafond de 356 kg, soit 4 % — devait acheter
      // un Saturn V à 1 457 M$ pour un contrat à 240 M$. Le prix reste à 95 M$,
      // conservateur : un Falcon 9 réel se vend ~67 M$.
      {"L-C lourd",  22800.0,  95.0, 0.980, 16.0, "lanceur_lourd"},   // Falcon 9 Block 5
      // ═══ LE SUPER-LOURD, QUE L'ARBRE NOMMAIT SANS QU'IL EXISTE ═══
      // `lanceur_super_lourd` est un nœud de fin de branche 1, et CAT-09 (Mars
      // habité) l'exige en prérequis — mais le catalogue s'arrêtait à 8,3 t.
      // Conséquence MESURÉE : cinq contrats sur onze étaient inachevables quoi
      // que fasse le joueur, dont trois dont la charge NUE dépassait déjà le
      // plafond. Un prérequis qui ne débloque rien est un prérequis qui ment.
      //
      // ANCRÉ SUR UNE LIGNÉE RÉELLE [GDD 12.1], jamais sur un besoin de gameplay :
      // Saturn V (140 t en LEO, 13 vols sur 13) et SLS Block 2 (130 t). On retient
      // 130 t. Le coût suit le même $/kg que le reste du tableau (~11 M$/t, contre
      // 8,6 pour Saturn V et 15,4 pour SLS). La fiabilité est PLUS BASSE que celle
      // du L-C lourd, et c'est le fait réel : un lanceur géant vole peu, donc il
      // démontre peu — on n'achète pas de la fiabilité en achetant de la taille.
      {"L-D super-lourd", 130000.0, 1400.0, 0.970, 28.0, "lanceur_super_lourd"},
  };
  return v;
}

// ═══ CE QUE L'AGENCE PEUT ACHETER ═══ [GDD 5.4]
// Filtre optionnel sur les lanceurs disponibles. `nullptr` = AUCUN filtre, et
// c'est le mode MODÈLE (oracles de physique pure, où l'arbre n'existe pas) —
// jamais le mode JEU, où `Session::evaluer_plan` passe la liste qualifiée.
using LauncherFilter = std::function<bool(const Launcher&)>;
inline bool launcher_autorise(const Launcher& L, const LauncherFilter* f) {
  return !f || !*f || (*f)(L);
}

// --- CATALOGUE DE MOTEURS ----------------------------------------------------
struct EngineOption {
  vehicle::Engine eng;
  double tank_dry_fraction{};  // l'hydrogene est peu dense : reservoirs enormes
  double unit_cost_musd{};
  double dev_cost_musd{};      // 0 si le moteur existe deja
  double lead_months{};
  int    flight_heritage{};    // vols reussis
  double R0{};                 // fiabilite par allumage, sans essais supplementaires
  double Rmax{};               // asymptote atteignable en achetant des essais
  double h_char{};             // heures d'essai caracteristiques
  std::string note;
};

// Fiabilite par allumage apres avoir achete `h` heures d'essai.
// R(h) = Rmax - (Rmax - R0) * exp(-h / h_char)
// Un moteur mature (R0 proche de Rmax) ne gagne presque rien a etre teste :
// on n'achete pas de la chance, on achete de la PROBABILITE, et elle sature.
inline double engine_reliability(const EngineOption& e, double test_hours) {
  return e.Rmax - (e.Rmax - e.R0) * std::exp(-test_hours / e.h_char);
}

inline const std::vector<EngineOption>& engines() {
  static const std::vector<EngineOption> v = [] {
    std::vector<EngineOption> out;
    {
      EngineOption o;
      o.eng.id = "RL10C-1"; o.eng.thrust_vac = 101800.0; o.eng.isp_vac = 449.7;
      o.eng.mass = 190.0; o.eng.mixture_ratio = 5.88;
      o.tank_dry_fraction = 0.12;      // LOX/LH2 : hydrogene peu dense
      o.unit_cost_musd = 12.0; o.dev_cost_musd = 0.0; o.lead_months = 14.0;
      o.flight_heritage = 500; o.R0 = 0.9980; o.Rmax = 0.9995; o.h_char = 500.0;
      o.note = "le meilleur Isp. Cher, long, et l'hydrogene gonfle les reservoirs.";
      out.push_back(o);
    }
    {
      EngineOption o;
      o.eng.id = "Aestus"; o.eng.thrust_vac = 29400.0; o.eng.isp_vac = 324.0;
      o.eng.mass = 111.0; o.eng.mixture_ratio = 2.05;
      o.tank_dry_fraction = 0.06;      // stockables : reservoirs compacts
      o.unit_cost_musd = 5.0; o.dev_cost_musd = 0.0; o.lead_months = 7.0;
      o.flight_heritage = 120; o.R0 = 0.9950; o.Rmax = 0.9990; o.h_char = 400.0;
      o.note = "Isp mediocre, mais pas cher, rapide, et des reservoirs compacts.";
      out.push_back(o);
    }
    {
      EngineOption o;
      o.eng.id = "MTX-1 (neuf)"; o.eng.thrust_vac = 60000.0; o.eng.isp_vac = 380.0;
      o.eng.mass = 150.0; o.eng.mixture_ratio = 3.6;
      o.tank_dry_fraction = 0.08;      // LOX/methane
      o.unit_cost_musd = 3.0; o.dev_cost_musd = 25.0; o.lead_months = 20.0;
      o.flight_heritage = 0; o.R0 = 0.9000; o.Rmax = 0.9950; o.h_char = 800.0;
      o.note = "bon compromis Isp/densite, pas cher a l'unite. ZERO vol d'heritage.";
      out.push_back(o);
    }
    return out;
  }();
  return v;
}

// --- LE CONTRAT --------------------------------------------------------------
// Les objectifs sont des GRANDEURS PHYSIQUES. Le budget est en dollars. Le delai
// est en mois. Rien de tout ca n'est un point de jeu.
// ═══ CE QU'ARES DEMANDE — L'OBJECTIF ET L'ENVELOPPE, RIEN D'AUTRE ═══ [GDD 3.1]
// « L'Architecte décide COMMENT concevoir, dans des enveloppes imposées par ARES.
// Il ne fixe pas le budget. » Ce qui est ici est donc du côté du CLIENT :
//   . `payload_kg`     — la charge qu'il FOURNIT (satellite, fret, instruments).
//                        Là, la masse EST l'objectif. Elle exclut le véhicule,
//                        la coque pressurisée, les ergols et les consommables,
//                        qui sont le métier de l'architecte.
//   . `crew_required`  — combien de personnes l'objectif demande sur place.
//   . budget / délai / P(succès) — l'enveloppe, littéralement.
struct Contract {
  double payload_kg{};
  double budget_musd{};
  double deadline_months{};
  double min_success_prob{};   // ce que le client exige
  // ═══ L'ÉQUIPAGE EST UN OBJECTIF, PAS UNE DÉCISION D'ARCHITECTE ═══
  // Il vit donc ici plutôt que dans une table indexée sur une chaîne de famille.
  // Et la raison est le MODÈLE, pas une préférence : embarquer plus de monde
  // n'apporte aucun avantage modélisé — ni la réparation ni la redondance
  // médicale ne dépendent de l'effectif — et coûte de la coque, des vivres et du
  // blindage. Un « choix » à une seule bonne réponse n'en est pas un. Le jour où
  // l'effectif achètera quelque chose, il deviendra une décision et déménagera
  // dans `MissionPlan`.
  //
  // ⚠ EN QUEUE DE STRUCTURE, ET CE N'EST PAS ARBITRAIRE : la liste de contrats
  // du prototype (`app/jeu.cpp`) initialise `spec` de façon POSITIONNELLE
  // (`{1200.0, 115.0, 18.0, 0.85}`). Insérer un `int` entre deux `double` y
  // faisait échouer quinze agrégats sur un rétrécissement de conversion. Tout
  // nouveau champ va donc EN QUEUE, ou alors ces quinze sites passent en
  // initialisation nommée d'abord.
  int    crew_required{0};
};

// --- LE PROGRAMME : ce que le joueur achete ----------------------------------
struct Program {
  int engine_index{};
  // LE LANCEUR EST UN CHOIX, PAS UNE DEDUCTION. Prendre le moins cher qui souleve
  // la masse, c'est ignorer qu'on peut PAYER POUR DE LA FIABILITE : 33 M$ de plus
  // achetent 2 points de P(succes). Sur un programme a 100 M$, ce n'est pas
  // evident — c'est un calcul.
  int launcher_index{-1};      // -1 = le moins cher qui souleve
  double test_hours{0.0};      // achete de la fiabilite
  double tracking_musd{0.0};   // achete de la connaissance
  double tracking_days{0.0};   // ...et ca prend du temps
  double compute_musd{0.0};    // achete de la fidelite de modele
  bool   review{false};        // achete une relecture independante
  double dv_margin{};          // marge de correction provisionnee (m/s), issue du Monte-Carlo
};

struct Assessment {
  // physique
  double dv_design{}, propellant_kg{}, dry_kg{}, m0_kg{};
  int    launcher_index{-1};
  // argent
  double cost_launcher{}, cost_engine{}, cost_stage{}, cost_tracking{},
         cost_compute{}, cost_tests{}, cost_review{}, cost_ops{}, cost_total{};
  // temps
  double schedule_months{};
  // risque
  double p_launcher{}, p_engine{}, p_blunder{}, p_physics{}, p_success{};
  // verdict
  bool fits_mass{false}, fits_budget{false}, fits_schedule{false}, fits_risk{false};
  bool ok{false};
  std::string why;
  // LA CAMPAGNE DE MISE EN ORBITE [GDD 5.2 branche 1] : un seul tir dans le cas
  // courant, une campagne assemblée quand la masse l'exige. Gardée pour
  // l'affichage — le joueur doit voir ce que son architecture lui coûte en tirs.
  PlanAssemblage assemblage{};
};

// COÛTS. Chaque ligne est une hypothese assumee, pas une constante de gameplay.
inline constexpr double COST_PER_KG_DRY   = 0.010;  // M$/kg de masse seche (integration)
inline constexpr double COST_STAGE_FIXED  = 2.0;    // M$
inline constexpr double COST_TEST_PER_H   = 0.020;  // M$/heure d'essai a feu
inline constexpr double COST_OPS_PER_MONTH= 0.45;   // M$/mois (equipe, installations)
inline constexpr double COST_REVIEW       = 3.0;    // M$
inline constexpr double MONTHS_INTEGRATION= 4.0;
inline constexpr double P_BLUNDER_NO_REVIEW = 0.050;  // erreur grossiere non attrapee
inline constexpr double P_BLUNDER_REVIEW    = 0.005;  // (Mars Climate Orbiter, 1999)

inline Assessment assess(const Contract& c, const Program& pr, int n_burns,
                         double dv_nominal, double finite_loss) {
  Assessment a;
  const auto& E = engines()[pr.engine_index];

  // ---- 1) PHYSIQUE : le Delta-v a provisionner, puis la masse ---------------
  a.dv_design = dv_nominal + finite_loss + pr.dv_margin;
  auto sz = vehicle::size_stage_for_dv(a.dv_design, c.payload_kg, E.eng,
                                       E.tank_dry_fraction, 150.0, 0.02);
  a.propellant_kg = sz.propellant;
  a.dry_kg = sz.stage_dry;
  a.m0_kg = sz.m0;

  // ---- 2) LE LANCEUR --------------------------------------------------------
  if (pr.launcher_index >= 0) {
    if (launchers()[pr.launcher_index].payload_leo < a.m0_kg) {
      a.why = "LE LANCEUR CHOISI NE SOULEVE PAS CETTE MASSE";
      return a;
    }
    a.launcher_index = pr.launcher_index;
  } else {
    double best_cost = 1e300;
    for (std::size_t i = 0; i < launchers().size(); ++i) {
      const auto& L = launchers()[i];
      if (L.payload_leo < a.m0_kg) continue;
      if (L.cost_musd < best_cost) { best_cost = L.cost_musd; a.launcher_index = static_cast<int>(i); }
    }
  }
  if (a.launcher_index < 0) {
    a.why = "AUCUN LANCEUR NE SOULEVE CETTE MASSE";
    return a;   // echec PHYSIQUE ET COMMERCIAL a la fois
  }
  a.fits_mass = true;
  const auto& L = launchers()[a.launcher_index];

  // ---- 3) ARGENT ------------------------------------------------------------
  a.cost_launcher = L.cost_musd;
  a.cost_engine   = E.unit_cost_musd + E.dev_cost_musd;
  a.cost_stage    = COST_STAGE_FIXED + COST_PER_KG_DRY * a.dry_kg;
  a.cost_tracking = pr.tracking_musd;
  a.cost_compute  = pr.compute_musd;
  a.cost_tests    = COST_TEST_PER_H * pr.test_hours;
  a.cost_review   = pr.review ? COST_REVIEW : 0.0;

  // ---- 4) TEMPS -------------------------------------------------------------
  a.schedule_months = std::max(E.lead_months, L.lead_months) + MONTHS_INTEGRATION
                    + pr.tracking_days / 30.44;
  a.cost_ops = COST_OPS_PER_MONTH * a.schedule_months;

  a.cost_total = a.cost_launcher + a.cost_engine + a.cost_stage + a.cost_tracking
               + a.cost_compute + a.cost_tests + a.cost_review + a.cost_ops;

  a.fits_budget   = (a.cost_total <= c.budget_musd);
  a.fits_schedule = (a.schedule_months <= c.deadline_months);

  // ---- 5) RISQUE ------------------------------------------------------------
  a.p_launcher = L.reliability;
  a.p_engine   = std::pow(engine_reliability(E, pr.test_hours), n_burns);
  a.p_blunder  = pr.review ? P_BLUNDER_REVIEW : P_BLUNDER_NO_REVIEW;
  // a.p_physics est injecte par l'appelant (issu du Monte-Carlo de navigation)
  return a;
}

// --- ASSESS MULTI-ETAGES -----------------------------------------------------
// Meme contrat que assess(), mais le vehicule est un empilement de `n_stages`
// etages IDENTIQUES (meme moteur). Pour des etages identiques, le partage EGAL du
// Delta-v est l'OPTIMUM mathematique (il minimise m0) : ce n'est donc pas le jeu
// qui decide a la place du joueur, c'est la physique. Cela permet de franchir le
// mur du mono-etage chimique (dv <~ ve*ln(1/frac_seche)) sur les missions lourdes.
// assess_multistage(...,1) reproduit EXACTEMENT assess(...).
inline Assessment assess_multistage(const Contract& c, const Program& pr, int n_burns,
                                    double dv_nominal, double finite_loss, int n_stages,
                                    const LauncherFilter* dispo = nullptr,
                                    const CapaciteAssemblage& assemblage = {}) {
  Assessment a;
  const auto& E = engines()[pr.engine_index];
  a.dv_design = dv_nominal + finite_loss + pr.dv_margin;
  const int ns = (n_stages < 1) ? 1 : n_stages;

  // ---- 1) PHYSIQUE : dimensionnement inverse multi-etages (exact) -----------
  std::vector<vehicle::StageSpec> specs;
  specs.reserve(ns);
  for (int s = 0; s < ns; ++s) {
    vehicle::StageSpec sp;
    sp.dv_target = a.dv_design / ns;          // partage egal = optimum (etages identiques)
    sp.engine = E.eng; sp.tank_dry_fraction = E.tank_dry_fraction;
    sp.structure_mass = 150.0; sp.residual_fraction = 0.02;
    specs.push_back(sp);
  }
  auto sz = vehicle::size_multistage_for_dv(specs, c.payload_kg);
  if (!sz.converged || sz.m0 <= c.payload_kg) {
    a.why = (ns >= 3) ? "DELTA-V INFAISABLE MEME EN 3 ETAGES"
                      : "DELTA-V TROP GRAND POUR CE NOMBRE D'ETAGES : AJOUTE UN ETAGE";
    return a;
  }
  a.m0_kg = sz.m0;
  double prop = 0.0, dry = 0.0;
  for (const auto& r : sz.stages) { prop += r.propellant; dry += r.stage_dry; }
  a.propellant_kg = prop; a.dry_kg = dry;

  // ---- 2) LE LANCEUR (pour la masse multi-etages) --------------------------
  // ═══ UN LANCEUR PEUT DÉSORMAIS S'Y REPRENDRE À PLUSIEURS FOIS ═══
  // [GDD 5.2 branche 1] L'assemblage en orbite ne CONTOURNE pas le plafond de
  // masse : il l'échange contre du prix, du risque et du temps (Assemblage.hpp).
  // Le meilleur lanceur n'est donc plus « le moins cher qui soulève » mais
  // « celui dont la CAMPAGNE coûte le moins » — et une campagne de neuf tirs
  // peut coûter plus cher qu'un seul gros.
  auto campagne = [&](const Launcher& L) {
    return planifier_assemblage(a.dry_kg + c.payload_kg, a.propellant_kg,
                                L.payload_leo, L.reliability, assemblage);
  };
  PlanAssemblage plan;
  if (pr.launcher_index >= 0) {
    const auto& Lc = launchers()[pr.launcher_index];
    if (!launcher_autorise(Lc, dispo)) {
      a.why = "LE LANCEUR CHOISI N'EST PAS QUALIFIE : " + Lc.tech_id; return a;
    }
    plan = campagne(Lc);
    if (!plan.possible) { a.why = plan.why; return a; }
    a.launcher_index = pr.launcher_index;
  } else {
    // ═══ ON N'ACHÈTE PAS UNE CAMPAGNE QUI NE PEUT PAS TENIR LE CONTRAT ═══
    // Depuis l'assemblage, « le moins cher qui soulève » est devenu un mauvais
    // conseil : deux tirs légers coûtent 13 M$ de moins qu'un lourd et rendent
    // 0,843 au lieu de 0,980. Or `p_success = p_segment · p_moteur · … ≤
    // p_segment` : une campagne dont le SEGMENT est déjà sous l'exigence du
    // client est GARANTIE d'échouer, quoi qu'on fasse ensuite. L'écarter ne
    // retire donc aucune option viable — c'est une déduction, pas un réglage.
    // C'est aussi la raison réelle pour laquelle on construit un lanceur géant
    // plutôt que d'empiler les tirs moyens quand un équipage est à bord.
    double best_cost = 1e300, best_p = -1.0;
    int idx_p = -1;
    PlanAssemblage plan_p;
    for (std::size_t i = 0; i < launchers().size(); ++i) {
      const auto& L = launchers()[i];
      if (!launcher_autorise(L, dispo)) continue;
      const PlanAssemblage pl = campagne(L);
      if (!pl.possible) continue;
      // Le repli : la campagne la PLUS SÛRE, gardée pour que le diagnostic porte
      // sur la meilleure tentative et non sur la moins chère.
      if (pl.p_segment > best_p) {
        best_p = pl.p_segment; idx_p = static_cast<int>(i); plan_p = pl;
      }
      if (pl.p_segment < c.min_success_prob) continue;
      const double cout = pl.n_lancements * L.cost_musd;
      if (cout < best_cost) {
        best_cost = cout; a.launcher_index = static_cast<int>(i); plan = pl;
      }
    }
    if (a.launcher_index < 0 && idx_p >= 0) { a.launcher_index = idx_p; plan = plan_p; }
  }
  if (a.launcher_index < 0) {
    // ═══ LE REFUS NOMME CE QUI MANQUE ═══ (piège n°42) : « aucun lanceur » est
    // une impasse ; « le lanceur qui le soulèverait existe, il n'est pas
    // qualifié » est une DIRECTION. Deux situations que le joueur doit
    // distinguer, parce qu'une seule des deux se résout en cherchant. Et depuis
    // l'assemblage, une troisième existe : le lanceur EST qualifié, mais la
    // campagne ne converge pas — c'est `PlanAssemblage::why` qui parle alors.
    for (const auto& L : launchers())
      if (!launcher_autorise(L, dispo) && campagne(L).possible) {
        a.why = "LANCEUR NON QUALIFIE : RECHERCHER " + L.tech_id;
        return a;
      }
    for (const auto& L : launchers())
      if (launcher_autorise(L, dispo)) { a.why = campagne(L).why; break; }
    if (a.why.empty()) a.why = "AUCUN LANCEUR NE SOULEVE CETTE MASSE";
    return a;
  }
  a.assemblage = plan;
  a.fits_mass = true;
  const auto& L = launchers()[a.launcher_index];

  // ---- 3) ARGENT : moteur DEVELOPPE une fois, N unites ; etage fixe+seche/etage
  // LA CAMPAGNE, PAS LE TIR : N lancements se paient N fois [GDD 5.2 branche 1].
  a.cost_launcher = plan.n_lancements * L.cost_musd;
  a.cost_engine   = E.dev_cost_musd + ns * E.unit_cost_musd;
  a.cost_stage    = ns * COST_STAGE_FIXED + COST_PER_KG_DRY * a.dry_kg;
  a.cost_tracking = pr.tracking_musd;
  a.cost_compute  = pr.compute_musd;
  a.cost_tests    = COST_TEST_PER_H * pr.test_hours;
  a.cost_review   = pr.review ? COST_REVIEW : 0.0;

  // ---- 4) TEMPS ------------------------------------------------------------
  // L'ASSEMBLAGE DURE, et ce temps se paie sur le calendrier du contrat : une
  // campagne de neuf tirs à un mois d'intervalle, c'est huit mois de plus.
  a.schedule_months = std::max(E.lead_months, L.lead_months) + MONTHS_INTEGRATION
                    + pr.tracking_days / 30.44
                    + plan.duree_jours / 30.44;
  a.cost_ops = COST_OPS_PER_MONTH * a.schedule_months;
  a.cost_total = a.cost_launcher + a.cost_engine + a.cost_stage + a.cost_tracking
               + a.cost_compute + a.cost_tests + a.cost_review + a.cost_ops;
  a.fits_budget   = (a.cost_total <= c.budget_musd);
  a.fits_schedule = (a.schedule_months <= c.deadline_months);

  // ---- 5) RISQUE : ignitions totales inchangees, + SEPARATION par etage largue
  // LA MASSE S'ACHÈTE EN RISQUE : le segment de mise en orbite n'est plus un
  // tir mais N tirs et N−1 amarrages, tous à réussir. C'est l'arbitrage central
  // de l'assemblage, et il est exponentiel — neuf tirs à 0,98 rendent 0,83.
  a.p_launcher = plan.p_segment;
  const double R_sep = 0.99;   // fiabilite par separation d'etage (modele DECLARE)
  a.p_engine   = std::pow(engine_reliability(E, pr.test_hours), n_burns)
               * std::pow(R_sep, ns - 1);
  a.p_blunder  = pr.review ? P_BLUNDER_REVIEW : P_BLUNDER_NO_REVIEW;
  return a;
}

inline void finalize(Assessment& a, const Contract& c, double p_physics) {
  // ═══ UN ARRÊT PRÉCOCE N'EST PAS QUATRE ÉCHECS ═══ [GDD 4.1, piège n°42]
  // TROUVÉ EN AUDITANT UNE CAPTURE. `assess` s'arrête net dès qu'aucun lanceur ne
  // soulève la masse, et il DIT pourquoi (« AUCUN LANCEUR NE SOULEVE CETTE
  // MASSE ») : à ce moment-là, le coût, le calendrier et la fiabilité n'ont pas
  // été calculés du tout, ils valent zéro parce qu'on n'y est jamais arrivé.
  // Reconstruire `why` à partir des quatre `fits_*` écrasait donc ce diagnostic
  // précis par une liste de SYMPTÔMES — « MASSE BUDGET CALENDRIER RISQUE » — dont
  // trois quarts étaient de l'ignorance déguisée en verdict. Un refus doit nommer
  // ce qui manque, pas énumérer tout ce qu'on ignore.
  if (!a.fits_mass) { a.ok = false; return; }   // `why` porte déjà la vraie cause
  a.p_physics = p_physics;
  a.p_success = a.p_launcher * a.p_engine * (1.0 - a.p_blunder) * a.p_physics;
  a.fits_risk = (a.p_success >= c.min_success_prob);
  a.ok = a.fits_mass && a.fits_budget && a.fits_schedule && a.fits_risk;
  if (a.ok) { a.why = "PROGRAMME VIABLE"; return; }
  a.why.clear();
  if (!a.fits_budget)   a.why += "BUDGET ";
  if (!a.fits_schedule) a.why += "CALENDRIER ";
  if (!a.fits_risk)     a.why += "RISQUE ";
}

} // namespace fen::mission

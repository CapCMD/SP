// fen/mission/MissionLoop.hpp — LA BOUCLE DE MISSION [GDD 4.1]
//
// Le cycle qui RELIE les systèmes : réception → prérequis → conception →
// fenêtre → qualification → lancement → exploitation → débrief. La FSM
// (MissionFsm.hpp) porte les états ; ce fichier porte les GATES (chaque
// transition a une condition physique/programmatique réelle) et l'ISSUE du vol.
//
// DOCTRINE : aucune transition n'est gratuite. On ne passe en conception que si
// les prérequis sont là ; on ne cherche une fenêtre que si la conception est
// VIABLE (masse/budget/calendrier/risque, via mission::assess) ; on ne lance
// que qualifié ; et l'issue du vol est DÉTERMINISTE, tirée contre la P(succès)
// évaluée — « les pannes suivent des probabilités calibrées » [GDD 7.3, 9.4].
// Rien n'est un coup de dé nu : la graine vient de l'agence, la probabilité de
// la physique et de l'argent.
//
// C++ pur. Ne connaît PAS GameState (qui l'inclut) : l'issue produit un
// AnomalyEvent que l'appelant passe à GameState::apply_anomaly [GDD 10.4].
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

#include "fen/astro/LaunchWindow.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Rng.hpp"
#include "fen/flight/Descent.hpp"        // [GDD 7.6] l'alunissage se calcule
#include "fen/mission/Crew.hpp"
#include "fen/mission/FlightTimeline.hpp"
#include "fen/mission/MissionFsm.hpp"
#include "fen/mission/Program.hpp"
#include "fen/mission/Severity.hpp"

namespace fen::mission {

// ═══ LE Δv DE TRAJECTOIRE PAR TYPE DE MISSION ═══
// Budgets de trajectoire (m/s) depuis une orbite de parking, ordres de grandeur
// RÉELS et DÉCLARÉS [GDD 6.8]. Ce n'est pas le Δv de mise en orbite (le lanceur
// s'en charge) mais celui que le VÉHICULE doit fournir ensuite.
inline double trajectory_dv_for_family(const std::string& family) {
  // ═══ LE CISLUNAIRE, QUE LE GDD NOMMAIT SANS QU'IL EXISTE ═══
  // [GDD 3.3] le rang Principal est DÉFINI par « vol habité cislunaire », [19.7]
  // lui donne ses cinq verrous, [5.9] et [5.10] le nomment encore — et le
  // catalogue n'avait AUCUNE mission lunaire sur ses onze entrées.
  // Ce qui est ici est la part qui NE DÉPEND PAS du véhicule, et chaque terme a
  // sa source : injection translunaire **3,1 km/s** est la ligne de l'annexe Δv
  // du GDD ; insertion en orbite lunaire (~900 m/s) et injection retour
  // (~1 000 m/s) sont les valeurs d'Apollo. L'ALUNISSAGE, lui, se DÉRIVE du
  // moteur choisi — voir `evaluate` : il n'est pas dans ce forfait.
  if (family == "lunaire_habite") return 5000.0;  // TLI 3100 + LOI 900 + TEI 1000
  if (family == "sat")          return 1800.0;   // LEO -> GEO (GTO + circularisation)
  if (family == "science")      return 3600.0;   // echappee + corrections
  if (family == "surface")      return 4300.0;   // interplanetaire + insertion/EDL
  if (family == "logistique")   return 200.0;    // rendez-vous LEO
  if (family == "service")      return 400.0;    // inspection/maintenance orbitale
  if (family == "habite")       return 300.0;    // LEO habite (rejoindre la station)
  if (family == "mars")         return 4800.0;   // orbiteur martien
  if (family == "mars_habite")  return 6000.0;   // aller habite + insertion
  if (family == "nep")          return 9000.0;   // cargo lointain (poussee continue)
  if (family == "relativiste")  return 30000.0;  // fin de jeu
  return 3000.0;                                  // defaut prudent
}

// ═══ LES TERMES PHYSIQUES DU CONTRAT PAR FAMILLE ═══ [GDD 4.1]
// Masse à emporter, budget, délai, P(succès) exigée. Sans eux, `assess()` n'a
// rien à évaluer et la boucle de mission est creuse. Budgets calés pour qu'un
// plan raisonnable au rang requis soit VIABLE (vérifié par oracle sur le contrat
// de départ) — le joueur garde la marge non dépensée [GDD 3.1]. Valeurs
// DÉCLARÉES et PROVISOIRES : la « matrice mission × technologies » chiffrée est
// différée [GDD 20].
//
// ICI, ET PLUS DANS UNE BOUCLE INTERNE AU CATALOGUE. Cette table vivait cachée
// dans `seed_catalogue`, si bien que TOUTE mission construite hors catalogue
// naissait avec des termes NULS : le harnais de capture fabriquait la sienne à la
// main, et chaque capture en vol affichait « 0 / 0 M EUR », « 0 / 0 mois » et un
// VERROU rouge parfaitement faux. Une alarme fausse dans chaque image est pire
// qu'une alarme absente — elle apprend à ne plus les lire, et elle rendait le
// bilan de viabilité INVÉRIFIABLE par capture. Une table, un appelant possible.
inline Contract contract_terms_for_family(const std::string& f) {
  Contract t;
  // L'EFFECTIF DEMANDÉ PAR L'OBJECTIF. La table de référence par famille reste
  // la SOURCE (elle porte ses sources réelles, une par ligne), mais elle
  // alimente désormais le CONTRAT au lieu d'être relue partout : tout le reste
  // du moteur lit `terms.crew_required`, un seul endroit décide.
  t.crew_required = crew_size_for_family(f);
  if (f == "sat")            { t.payload_kg = 3000;  t.budget_musd = 175;  t.deadline_months = 30;  t.min_success_prob = 0.85; }
  else if (f == "science")   { t.payload_kg = 1200;  t.budget_musd = 150;  t.deadline_months = 40;  t.min_success_prob = 0.80; }
  else if (f == "surface")   { t.payload_kg = 1800;  t.budget_musd = 240;  t.deadline_months = 48;  t.min_success_prob = 0.75; }
  else if (f == "logistique"){ t.payload_kg = 5000;  t.budget_musd = 200;  t.deadline_months = 24;  t.min_success_prob = 0.90; }
  else if (f == "service")   { t.payload_kg = 2000;  t.budget_musd = 190;  t.deadline_months = 30;  t.min_success_prob = 0.85; }
  else if (f == "habite")    { t.payload_kg = 8000;  t.budget_musd = 360;  t.deadline_months = 36;  t.min_success_prob = 0.95; }
  else if (f == "mars")      { t.payload_kg = 2200;  t.budget_musd = 380;  t.deadline_months = 54;  t.min_success_prob = 0.80; }
  // CISLUNAIRE HABITÉ. La charge que le CLIENT fournit est de l'instrumentation
  // de surface et des conteneurs d'échantillons — l'ALSEP d'Apollo pesait ~100 kg,
  // et le séjour est court : on reste sous la charge martienne de CAT-04.
  // Le budget est ancré sur Artemis III (~4 100 M$ pour UN lancement), déjà cité
  // par la ligne martienne ci-dessous ; on prend 5 000 M$ parce que le profil
  // comprend un ATTERRISSEUR et sa remontée, ce qu'Artemis III achète en plus.
  else if (f == "lunaire_habite"){t.payload_kg = 900; t.budget_musd = 5000; t.deadline_months = 66; t.min_success_prob = 0.90; }
  // MARS HABITÉ. Deux corrections successives, toutes deux mesurées.
  // (1) BUDGET : 1 200 M$ ne payaient même pas le lanceur (1 400 M$ à lui seul),
  //     ce qui violait l'invariant DÉCLARÉ de cette table. Porté à 3 000 M$ —
  //     très en dessous du réel (Artemis III : ~4 100 M$ pour UN lancement).
  // (2) CHARGE UTILE : 20 000 kg décrivaient un HABITAT, c'est-à-dire une
  //     architecture déjà choisie. Or ARES dit « aller là pour faire ça » et
  //     n'impose que l'enveloppe [GDD 3.1] : la coque pressurisée est désormais
  //     DÉRIVÉE de l'équipage et du volume que l'architecte lui alloue. Ce qui
  //     reste ici est la charge que le CLIENT fournit — instruments de surface
  //     et conteneurs d'échantillons, du même ordre que CAT-04 (1 800 kg).
  else if (f == "mars_habite"){t.payload_kg = 2000;  t.budget_musd = 3000; t.deadline_months = 72;  t.min_success_prob = 0.90; }
  // CARGO NEP : même défaut que Mars habité, mesuré de la même façon. 181 t au
  // décollage demandent DEUX super-lourds (2 800 M$ de lanceurs à eux seuls) ;
  // 650 M$ de budget ne payaient pas le tiers de la campagne. Porté à 4 500 M$,
  // soit ~1,5× le coût mesuré — et cela reste conservateur pour une mission
  // phare vers Jupiter (Europa Clipper, sans propulsion nucléaire : ~5 200 M$).
  else if (f == "nep")       { t.payload_kg = 12000; t.budget_musd = 4500; t.deadline_months = 60;  t.min_success_prob = 0.85; }
  // ORBITEUR DU SYSTÈME SOLAIRE EXTERNE (CAT-13). La charge est celle du CLIENT,
  // c'est-à-dire l'instrumentation : Juno emportait **173 kg** de charge utile
  // scientifique, Galileo ~118 kg pour l'orbiteur plus une sonde atmosphérique de
  // 339 kg — on prend 400 kg, la classe Galileo. Le budget est ancré sur le coût
  // publié de Juno (**1 460 M$** sur tout le cycle de vie) ; il reste bien en
  // dessous d'Europa Clipper (~5 200 M$), déjà cité par la ligne NEP. Le délai est
  // celui du DÉVELOPPEMENT (`schedule_months` ne compte pas la croisière — c'est
  // le calendrier de l'agence, pas celui du vol).
  else if (f == "externe")   { t.payload_kg = 400;   t.budget_musd = 1460; t.deadline_months = 60;  t.min_success_prob = 0.80; }
  // RELATIVISTE : même principe, la coque de l'équipage est dérivée. Ce qui
  // reste est l'instrumentation scientifique que le client veut voir partir.
  else if (f == "relativiste"){t.payload_kg = 2000;  t.budget_musd = 2500; t.deadline_months = 120; t.min_success_prob = 0.80; }
  else                       { t.payload_kg = 1500;  t.budget_musd = 150;  t.deadline_months = 36;  t.min_success_prob = 0.80; }
  return t;
}

// Nombre d'allumages typiques (ignitions) — module la fiabilité moteur.
inline int burns_for_family(const std::string& family) {
  if (family == "sat") return 2;                  // injection + circularisation
  if (family == "logistique" || family == "service" || family == "habite") return 3;
  if (family == "surface" || family == "mars" || family == "mars_habite") return 4;
  // Orbiteur du système solaire externe : injection, manœuvre en espace profond
  // (tout tour en porte une, et un transfert direct garde ses corrections), puis
  // insertion. Trois allumages — le profil de Juno comme celui de Galileo.
  if (family == "externe") return 3;
  // Cislunaire habité : TLI, insertion lunaire, freinage de descente, remontée,
  // injection retour. Cinq allumages, c'est le profil d'Apollo.
  if (family == "lunaire_habite") return 5;
  return 2;
}

// ═══════════════════════════════════════════════════════════════════════════
// LE BILAN DE MASSE D'UN VOL HABITÉ RELATIVISTE EST UN POINT FIXE [GDD 19.1]
// ═══════════════════════════════════════════════════════════════════════════
// Et il PEUT DIVERGER — même structure que l'ébullition des ergols
// (`Assemblage.hpp`), et pour la même raison de fond : la grandeur qu'on ajoute
// dépend de la durée, et la durée dépend de ce qu'on a ajouté.
//
//     m_sec → β(m_sec, stock) → durée = 2·d/(βc) → vivres(durée) → m_sec
//
// Alourdir le vaisseau le RALENTIT (β décroît avec la masse sèche), ce qui
// ALLONGE le voyage, ce qui demande PLUS de vivres, qui l'alourdissent encore.
// La boucle se referme sur elle-même, et pour une cible à 4,25 al elle diverge.
//
// C'EST LE VERROU QUE [GDD 19.1] ANNONCE : « le vol habité lointain dépend
// AUTANT du support-vie, de la médecine et des radiations que du moteur ».
// Sans cette détection, `assess` produit un véhicule de 1 742 t et refuse par
// « AUCUN LANCEUR NE SOULÈVE CETTE MASSE » — un verdict exact et inutilisable,
// exactement le piège n°42 : le symptôme à la place de la cause.
struct BilanRelativiste {
  bool   converge{false};
  double masse_seche_kg{0.0};   // point fixe atteint, si convergence
  double duree_jours{0.0};      // aller-retour à ce point fixe
  double beta{0.0};
  int    iterations{0};
  const char* pourquoi{""};     // motif de refus, vide si convergence
};

// `m_structure_kg` : tout ce qui ne dépend PAS de la durée (coque, blindage,
// charge utile). Les vivres, eux, en dépendent — c'est ce qui ferme la boucle.
inline BilanRelativiste bilan_relativiste(double m_structure_kg,
                                          double antimatiere_g, int n_crew,
                                          const RecyclingLoops& loops,
                                          int n_burns = rel::BURNS_ROUND_TRIP) {
  BilanRelativiste b;
  if (!(m_structure_kg > 0.0) || !(antimatiere_g > 0.0) || n_crew <= 0) {
    b.pourquoi = "bilan relativiste : architecture incomplete";
    return b;
  }
  // Garde-fou DÉCLARÉ, et il a un sens physique : au-delà de mille fois la
  // structure, le véhicule n'est plus un vaisseau mais un réservoir de vivres.
  const double PLAFOND = 1000.0 * m_structure_kg;
  double m = m_structure_kg;
  for (int i = 1; i <= 200; ++i) {
    const double beta = rel::beta_from_antimatter(m, antimatiere_g, n_burns);
    if (!(beta > 0.0)) { b.pourquoi = "bilan relativiste : aucune vitesse atteignable"; return b; }
    const double aller_s =
        rel::relativistic_transit(rel::PROXIMA_DISTANCE_M, beta).t_earth_s;
    const double duree_j = 2.0 * aller_s / cst::DAY;
    const double vivres = vital_budget(n_crew, duree_j, loops).total_kg();
    const double m2 = m_structure_kg + vivres;
    if (m2 > PLAFOND) {
      b.iterations = i;
      b.pourquoi = "LE BILAN DE MASSE DIVERGE : les vivres du voyage ralentissent "
                   "le vaisseau, qui allonge le voyage, qui demande plus de vivres";
      return b;
    }
    if (std::fabs(m2 - m) <= 1e-6 * m) {
      b.converge = true; b.masse_seche_kg = m2; b.beta = beta;
      b.duree_jours = duree_j; b.iterations = i;
      return b;
    }
    m = m2;
  }
  b.pourquoi = "LE BILAN DE MASSE NE CONVERGE PAS en 200 iterations";
  return b;
}

// ═══ LE PLAN DE MISSION ═══ — les décisions de conception + leur évaluation.
struct MissionPlan {
  Program    program;            // moteur, lanceur, essais, poursuite, revue, marge
  int        n_stages{2};
  double     finite_loss{150.0}; // pertes de poussée finie (m/s), provisionnées
  double     p_physics{0.985};   // fidélité de navigation (issue du MC, simplifiée)
  double     dv_traj_override{0.0}; // >0 : Δv de trajectoire imposé (fenêtre réelle)
  // ═══ CE QUE PÈSE UN ÉQUIPAGE ═══ [GDD 9.4, 6.1]
  // Posés par le driver, exactement comme `dv_traj_override` et pour la même
  // raison : la géométrie du ciel et l'arbre technologique ne sont pas dans cette
  // signature pure. `crew_round_trip_days` > 0 ⇒ aller-retour daté ; 0 ⇒ séjour.
  double         crew_round_trip_days{0.0};
  RecyclingLoops crew_loops{};       // tiré de la branche 4 (loops_from_tech)
  VitalBudget    vital{};            // RÉSULTAT, gardé pour l'affichage
  // ═══ LE BLINDAGE EST UNE DÉCISION DE CONCEPTION ═══ [GDD 6.6]
  // Au même titre que `dv_margin` ou `test_hours` : le joueur achète de la
  // protection, et il la paie en masse. 0 = rien, et c'est un choix légitime
  // pour un vol court — pas pour deux ans de croisière.
  env::Shielding blindage{};
  double         masse_blindage_kg_{0.0};   // RÉSULTAT, gardé pour l'affichage
  // ═══ COMBIEN DE VOLUME PAR PERSONNE ═══ [GDD 3.1] — décision d'architecte au
  // même titre que la marge de Δv. Serrer l'habitat allège la coque ET la surface
  // à blinder ; l'élargir se paie en masse. ARES ne dit pas comment loger un
  // équipage, il dit où aller et pour quoi faire.
  double         volume_par_personne_m3{VOLUME_HABITABLE_M3_PAR_PERSONNE};
  double         masse_habitat_kg_{0.0};    // RÉSULTAT, gardé pour l'affichage
  // ═══ LES LANCEURS QUE L'AGENCE A QUALIFIÉS ═══ [GDD 5.4]
  // Posé par le driver depuis l'arbre, comme `dv_traj_override` et les boucles :
  // le plan pur ne connaît pas l'arbre. Vide/non posé = aucun filtre, c'est le
  // mode MODÈLE des oracles de physique — jamais le mode JEU.
  LauncherFilter lanceurs_qualifies{};
  // ═══ ET LES MOTEURS QU'ELLE SAIT QUALIFIER ═══ [GDD 5.4] — posé par le driver
  // depuis l'arbre, exactement comme les lanceurs. Les DIX-HUIT pièces du
  // catalogue sont commandables ; celles de la branche 6 (électrique, NTP, NEP,
  // fusion) demandent leur nœud, sans quoi elle serait disponible d'emblée.
  EngineFilter moteurs_qualifies{};
  // ═══ LE VÉHICULE CONÇU AU POSTE CONCEPTION ═══ [GDD 4.1, 12.2]
  // Posé par le driver, comme les lanceurs et les moteurs qualifiés : le plan
  // pur ne connaît pas l'atelier. VIDE = mode MODÈLE (N étages identiques du
  // moteur de programme), celui des oracles de physique pure. Non vide, c'est
  // l'architecture du joueur qui vole — nombre d'étages, moteur et réservoir de
  // chacun, source d'énergie, et le PARTAGE du Δv entre eux.
  std::vector<vehicle::StageChoice> pile{};
  // ═══ CE QUE L'AGENCE SAIT FAIRE EN ORBITE ═══ [GDD 5.2 branche 1]
  // Rendez-vous automatisé, robotique d'assemblage, transfert d'ergols : trois
  // nœuds que l'arbre portait sans qu'ils débloquent rien. Posés par le driver,
  // comme les lanceurs qualifiés — le plan pur ne connaît pas l'arbre.
  CapaciteAssemblage assemblage{};
  // ═══ L'ANTIMATIÈRE EMBARQUÉE ═══ [GDD 5.12.12] — posée par le driver, comme
  // les boucles et les lanceurs : le plan pur ne connaît pas le stock de
  // l'agence. Elle ne sert qu'aux architectures relativistes, où elle décide de
  // la VITESSE, donc de la DURÉE, donc des vivres — la boucle de
  // `bilan_relativiste`. 0 ailleurs, sans effet.
  double     antimatiere_g{0.0};
  // ═══ LA QUALITÉ DU CONFINEMENT, TELLE QUE LE PALIER LA DÉCLARE ═══
  // [GDD 12.4, 5.12.12] Posée par le driver depuis `AntimatterProduction`, qui
  // la porte DÉJÀ (`loss_rate_per_day`, calibré avec le reste de la fin de jeu).
  // Un second taux écrit ici serait un nombre que personne n'a calibré.
  double     antimatiere_fuite_par_jour{0.0};
  // Durée d'exposition et couloir traversé [GDD 12.4, 7.8] — posés par le driver
  // comme la fenêtre et l'arbre : une évaluation de plan reste pure.
  EnvironnementMission env_mission{};
  Assessment assessment;
  bool       evaluated{false};

  // Évalue le plan contre le contrat de la mission : c'est le GATE de conception.
  // `dv_traj_override` (posé par le driver depuis la géométrie de la fenêtre)
  // prime sur le forfait par famille — c'est ce qui rend le budget Mars réel.
  void evaluate(const Mission& m) {
    const double dv = dv_traj_override > 0.0 ? dv_traj_override
                                             : trajectory_dv_for_family(m.contract.family);
    const int burns = burns_for_family(m.contract.family);

    // ═══ LES VIVRES SONT UNE MASSE, ET TSIOLKOVSKY LA PAIE ═══ [GDD 6.1, 9.4]
    // « Une mission mal calculée AVANT LANCEMENT se traduit en dérives coûteuses,
    // voire en échec si les réserves ne suffisent pas » [GDD 9.4]. Cette phrase
    // était inapplicable : `Contract::payload_kg` était un forfait par famille, si
    // bien qu'un vol habité de deux ans emportait exactement le même poids qu'un
    // vol de deux semaines, et que la branche 4 n'achetait rien de mesurable.
    //
    // La charge utile du CONTRAT reste ce que le client veut voir livré ; les
    // consommables n'en font pas partie — ce sont une conséquence de
    // l'architecture. On les ajoute donc à la masse à propulser, sans jamais
    // toucher aux termes du contrat.
    // L'EFFECTIF VIENT DE L'OBJECTIF [GDD 3.1], plus d'une table indexée sur la
    // famille : deux contrats de la même filière peuvent demander des équipages
    // différents, et c'est ARES qui le dit.
    const int n_crew = m.contract.terms.crew_required;
    vital = crew_consumables(n_crew, m.contract.family, crew_round_trip_days, crew_loops);
    // ET LE BLINDAGE PÈSE AVEC EUX [GDD 6.6] — « l'arbitrage masse / protection /
    // mission ». Les deux postes tirent sur le MÊME budget de masse : protéger
    // l'équipage se paie en ergols, exactement comme le nourrir. C'est ce qui
    // rend l'arbitrage réel au lieu d'être une phrase du GDD.
    masse_blindage_kg_ = masse_blindage_kg(n_crew, blindage.areal_density_gcm2,
                                           volume_par_personne_m3);
    // ═══ L'HABITAT EST UNE CONSÉQUENCE, PAS UN TERME DU CONTRAT ═══ [GDD 3.1]
    // « ARES dit : on doit aller là pour faire ça. » Il impose l'ENVELOPPE
    // (budget, délai, fiabilité exigée) et l'OBJECTIF (combien de personnes, pour
    // quoi) — pas la masse du véhicule, qui est le métier de l'architecte. Le
    // forfait `payload_kg` des familles habitées décrivait donc une architecture
    // déjà choisie, et se superposait à des consommables déjà calculés sans
    // qu'on sache ce qu'il contenait. Il est désormais DÉRIVÉ de deux décisions
    // (combien d'équipage, combien de volume chacun) et d'un fait mesuré
    // (137 kg/m³, les modules pressurisés de l'ISS).
    // Ce que le CONTRAT garde, c'est la charge utile que le CLIENT fournit —
    // un satellite, du fret, des instruments : là, la masse EST l'objectif.
    // MAIS SEULEMENT SI LE VÉHICULE EST LE DOMICILE. Un vol habité near-Earth
    // s'amarre à une station existante — c'est très exactement à ça qu'une
    // station sert, et son équipage voyage en CAPSULE. Un véhicule de croisière
    // interplanétaire, lui, emporte sa propre coque pressurisée pour deux ans.
    // Le critère n'est pas une liste de familles mais le fait déjà calculé :
    // y a-t-il un aller-retour daté vers une cible nommée.
    masse_habitat_kg_ = crew_round_trip_days > 0.0
                          ? masse_habitat_kg(n_crew, volume_par_personne_m3)
                          : 0.0;
    Contract terms = m.contract.terms;
    terms.payload_kg += vital.total_kg() + masse_blindage_kg_ + masse_habitat_kg_;

    // ═══ LA BOUCLE VIVRES ↔ VITESSE SE FERME AVANT TOUT LE RESTE ═══ [GDD 19.1]
    // Pour une architecture relativiste HABITÉE, la masse et la durée se
    // déterminent l'une l'autre, et le point fixe peut DIVERGER. On le teste
    // AVANT `assess_multistage`, et le refus court-circuite `finalize` —
    // exactement ce que fait `Assemblage.hpp` pour l'ébullition des ergols.
    // Sans ce court-circuit, le joueur lirait « AUCUN LANCEUR NE SOULÈVE CETTE
    // MASSE » : un verdict exact, et parfaitement inutilisable (piège n°42).
    if (m.contract.family == "relativiste" && n_crew > 0 && antimatiere_g > 0.0) {
      // La STRUCTURE est tout ce qui ne dépend pas de la durée. Les vivres en
      // dépendent : ce sont eux qu'on laisse le point fixe déterminer.
      const double structure_kg = m.contract.terms.payload_kg
                                + masse_blindage_kg_ + masse_habitat_kg_;
      const BilanRelativiste b =
          bilan_relativiste(structure_kg, antimatiere_g, n_crew, crew_loops);
      if (!b.converge) {
        assessment = Assessment{};
        assessment.ok = false;
        assessment.why = b.pourquoi;
        evaluated = true;
        return;
      }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // L'ALUNISSAGE SE CALCULE, IL NE SE FORFAITE PAS [GDD 7.6, 19.7, 3.3]
    // ═══════════════════════════════════════════════════════════════════════
    // `flight/Descent.hpp` était le seul module du cœur qu'aucune suite
    // n'exerçait, et ce qui lui manquait n'était pas un appelant mais une
    // MISSION : le GDD nomme le cislunaire quatre fois et le catalogue n'en avait
    // aucune. Le voici, et le Δv de freinage n'est PAS un forfait — il sort de
    // l'intégration d'une descente à poussée constante en guidage gravity-turn,
    // sous gravité centrale exacte.
    //
    // ET IL DÉPEND DU MOTEUR QUE LE JOUEUR A CHOISI : le rapport poussée/poids de
    // surface décide de tout. Un moteur faible brûle son Δv à lutter contre la
    // pesanteur (2 028 m/s à TWR 1,2), un moteur fort s'approche de la limite
    // impulsionnelle (1 691 m/s à TWR 6, pour 1 680 de vitesse orbitale rasante).
    // C'est « le vrai arbitrage d'ingénierie de tout alunisseur », et il devient
    // celui du joueur — comme la centrale d'une filière alimentée.
    double dv_alunissage = 0.0;
    if (m.contract.family == "lunaire_habite" && !pile.empty()) {
      const double g_lune = cst::MU_MOON / (cst::R_MOON * cst::R_MOON);
      // Le DERNIER étage est l'atterrisseur : c'est lui qui pose et qui remonte.
      const vehicle::EnginePart& lander = pile.back().engine_part();
      // ═══ LE T/W SE MESURE SUR LA MASSE ALLUMÉE, ET ELLE EST UN POINT FIXE ═══
      // Première rédaction : T/W calculé sur la seule charge utile (1 181 kg pour
      // un RL10 de 102 kN) → **T/W 53**, un chiffre qui n'a aucun sens physique et
      // qui poussait `descent_dv_required` hors de son domaine. La masse qui
      // compte est celle de l'atterrisseur ALLUMÉ : sa structure et ses ergols de
      // descente comprises. Elle dépend du Δv qu'on cherche, donc on la résout
      // comme partout ailleurs dans ce fichier — par une passe préalable, amorcée
      // sur la **limite impulsionnelle**, qui est un plancher exact.
      const double v_circ = std::sqrt(cst::MU_MOON / cst::R_MOON);
      const Assessment a0 = assess_multistage(terms, program, burns,
                                              dv + 2.0 * v_circ, finite_loss, n_stages,
                                              &lanceurs_qualifies, assemblage,
                                              &moteurs_qualifies, &pile, &env_mission);
      const double masse_allumee = a0.m0_dernier_etage_kg > 0.0
                                     ? a0.m0_dernier_etage_kg : terms.payload_kg;
      const double twr = (masse_allumee > 0.0)
          ? lander.thrust_vac_n / (masse_allumee * g_lune) : 0.0;
      if (twr <= 1.05) {
        // REFUS AVANT TOUT LE RESTE, et il dit la direction (piège n°42) : sans
        // T/W > 1 au sol lunaire, il n'y a pas d'atterrissage à dimensionner.
        assessment = Assessment{};
        assessment.ok = false;
        assessment.why = "LE MOTEUR NE SOULEVE PAS L ATTERRISSEUR SUR LA LUNE (T/W < 1) : "
                         "PRENDRE UN MOTEUR PLUS POUSSANT AU DERNIER ETAGE [GDD 6.3]";
        evaluated = true;
        return;
      }
      const double dv_desc = flight::descent_dv_required(
          cst::MU_MOON, cst::R_MOON, twr, lander.isp_vac_s);
      // DESCENTE **ET** REMONTÉE. Le modèle ne sait intégrer que le freinage ;
      // on prend la remontée égale, et c'est une approximation DÉCLARÉE
      // [GDD 12.5] : sur Apollo la remontée coûtait ~1 850 m/s pour ~2 050 de
      // descente, donc l'écart réel est de l'ordre de 10 %. Le contrôle est le
      // TOTAL — TLI + LOI + descente + remontée + TEI doit retrouver le budget
      // post-LEO d'Apollo (~8,9 km/s), et c'est l'oracle qui le tient.
      dv_alunissage = 2.0 * dv_desc;
    }

    assessment = assess_multistage(terms, program, burns, dv + dv_alunissage, finite_loss, n_stages,
                                   &lanceurs_qualifies, assemblage, &moteurs_qualifies,
                                   &pile, &env_mission);
    // ═══ LE CONFINEMENT DE L'ANTIMATIÈRE EST UN RISQUE PERMANENT ═══
    // [GDD 12.4] « Perte de confinement = ÉVÉNEMENT CATASTROPHIQUE. » On ne
    // modélise donc pas une performance qui se dégrade mais la PROBABILITÉ
    // qu'aucune perte n'ait lieu de tout le vol — un processus de Poisson, sur
    // une durée qui, pour un aller-retour relativiste, se compte en DÉCENNIES.
    //
    // LE TAUX N'EST PAS INVENTÉ ICI : c'est celui que le palier d'antimatière
    // DÉCLARE (`AntimatterProduction::loss_rate_per_day`), déjà calibré avec la
    // fin de jeu. On identifie donc le risque d'une perte CATASTROPHIQUE au taux
    // de perte déclaré du même confinement — une seule qualité de confinement,
    // un seul nombre. C'est une approximation, et elle est DÉCLARÉE [GDD 12.5] ;
    // l'autre voie serait une seconde constante que personne n'aurait calibrée.
    if (antimatiere_g > 0.0 && antimatiere_fuite_par_jour > 0.0
        && crew_round_trip_days > 0.0) {
      const double survie = reliability::antimatter_confinement_survival(
          crew_round_trip_days, 1.0, antimatiere_fuite_par_jour);
      if (survie < assessment.p_filieres)
        assessment.cause_filieres = "confinement de l antimatiere";
      assessment.p_filieres *= survie;
    }
    finalize(assessment, terms, p_physics);
    evaluated = true;
  }
};

// ═══ LE GATE D'UNE TRANSITION ═══ — légalité FSM + condition réelle.
struct GateResult {
  bool        allowed{false};
  std::string reason;           // le POURQUOI d'un refus, affichable
};

inline GateResult mission_gate(const Mission& m, const MissionPlan& plan,
                               MissionState target) {
  if (!m.can_advance_to(target))
    return {false, "transition illegale a cette phase"};
  switch (target) {
    case MissionState::Prerequisites:
    case MissionState::Design:
      return {true, ""};                          // toujours autorisé de concevoir
    case MissionState::WindowSearch:
      if (!plan.evaluated)       return {false, "conception non evaluee"};
      if (!plan.assessment.ok)   return {false, std::string("conception non viable : ") + plan.assessment.why};
      return {true, ""};
    case MissionState::Qualification:
      // Fenêtre de lancement. La condition PROGRAMMATIQUE est toujours ouverte
      // ici ; la condition GÉOMÉTRIQUE réelle (positions des corps) est portée
      // par `launch_window_gate` ci-dessous, que le driver applique EN PLUS à
      // cette transition — car elle exige l'éphéméride, absente de cette
      // signature pure.
      return {true, ""};
    case MissionState::Launched:
      // Qualification : une revue indépendante OU des essais à feu. Sans l'un
      // des deux, on ne signe pas le feu vert [GDD 12.3].
      if (!(plan.program.review || plan.program.test_hours > 0.0))
        return {false, "qualification requise : revue ou essais a feu"};
      return {true, ""};
    case MissionState::Debrief:
      return {true, ""};                          // le vol s'exécute
    default:
      return {true, ""};
  }
}

// ═══ LE GATE DE FENÊTRE DE LANCEMENT ═══ [GDD 7.3]
// Séparé de `mission_gate` : il exige l'éphéméride (positions réelles des
// corps), que tout appelant du cœur n'a pas sous la main. Le driver l'applique
// à la transition WindowSearch -> Qualification, EN PLUS du gate programmatique.
//
// Familles à fenêtre synodique RÉELLE : transferts impulsifs vers Mars
// (mars / mars_habite / surface = rover, retour d'échantillons). Les autres
// gardent une fenêtre permanente en V1, pour des raisons PHYSIQUES, pas par
// paresse : near-Earth (sat/logistique/service/habité) le sont vraiment ; NEP
// est à poussée continue (pas de fenêtre impulsive étroite) ; « science » n'a
// pas de destination nommée par le contrat ; « relativiste » est un régime de
// fin de jeu. DÉCLARÉ, et extensible dès qu'un contrat nomme sa cible.
struct WindowTarget { bool impose{false}; ephem::Body dep{}; ephem::Body arr{}; };

inline WindowTarget window_target_for_family(const std::string& family) {
  if (family == "mars" || family == "mars_habite" || family == "surface")
    return {true, ephem::Body::EarthBary, ephem::Body::Mars};
  // ORBITEUR DU SYSTÈME SOLAIRE EXTERNE : le contrat NOMME sa cible, donc la
  // fenêtre est réelle — c'est justement la famille pour laquelle l'assistance
  // gravitationnelle existe [GDD 5.11, compétences Senior]. La synodique
  // Terre-Jupiter fait 398,9 jours : elle revient vite, mais elle décide de
  // TOUT le reste (v∞ de départ, durée de transit, v∞ d'arrivée).
  if (family == "externe")
    return {true, ephem::Body::EarthBary, ephem::Body::Jupiter};
  return {false, {}, {}};
}

// ═══ L'ORBITE DE CAPTURE VISÉE, PAR CORPS D'ARRIVÉE ═══ [GDD 7.2, 6.8]
// Une insertion n'est pas un chiffre : c'est le choix d'une orbite. Mars →
// périastre bas (400 km) et apoastre très haut (30 000 km), le choix réel d'un
// orbiteur martien. Géante → 10 rayons de périastre et 100 de demi-grand axe,
// exactement l'orbite que `mission/Assistance.hpp` fait viser à un tour : une
// capture jovienne basse coûterait des kilomètres par seconde que personne ne
// dépense (Galileo : périjove ~4 R_J, apojove ~ 260 R_J).
struct CaptureOrbit { double rp_m{}, a_m{}, mu{}; };

inline CaptureOrbit capture_orbit_for(ephem::Body arr) {
  if (arr == ephem::Body::Mars)
    return {cst::R_MARS + 400.0e3, cst::R_MARS + 30000.0e3, cst::MU_MARS};
  const double R = ephem::body_radius(arr);
  return {10.0 * R, 100.0 * R, ephem::body_mu(arr)};
}

// ═══ UN SEUL RÉGLAGE DE FENÊTRE POUR TOUT LE JEU ═══ [défaut du 2026-08-01]
// IL Y EN AVAIT DEUX, ET C'ÉTAIT LA VRAIE CAUSE. Le GATE de lancement utilisait
// les paramètres par défaut (`slop_days` = 60) tandis que le calcul de la durée de
// transit resserrait à un pas de balayage (10 j). Le gate ouvrait donc parce qu'un
// bon transfert existait dans les 60 jours, pendant que la trajectoire, elle,
// partait le jour même sur le meilleur transfert des 10 jours — c'est-à-dire un
// mauvais. Mesuré le 2026-08-01 : `open = 1` avec `next_open_days = 50,6`
// (contradiction visible), arc plongeant à **0,862 UA**, injection **5 827 m/s**
// au lieu des ~3 600 attendus.
// Deux couches qui datent le même vol avec deux réglages différents finissent
// toujours par se contredire ; il n'y en a plus qu'un.
inline astro::WindowParams mission_window_params() {
  astro::WindowParams p;
  p.slop_days = p.horizon_days / static_cast<double>(p.n_dep);   // un pas de balayage
  return p;
}

// ═══ ET CES RÉGLAGES SONT CEUX DE MARS ═══ [défaut du 2026-07-31]
// Les valeurs par défaut de `WindowParams` le DISENT dans leurs commentaires :
// horizon 800 j « >= 1 période synodique Terre-Mars », durées explorées 150 à
// 400 j. Elles décrivent donc UNE paire de corps. Appliquées à Jupiter, dont le
// transfert de Hohmann dure **997 jours**, elles cherchent un transfert là où il
// n'y en a pas : le balayage bute sur son propre plafond et rend le seul arc
// qu'il connaisse — 400 jours, **17 621 m/s** d'injection, un manque au but de
// 2,3 millions de km. Aucune alerte : la structure est cohérente avec elle-même,
// exactement comme les deux réglages de fenêtre du piège n°94.
//
// LA RÉPONSE N'EST PAS UNE SECONDE TABLE, C'EST UNE DÉRIVATION. La durée d'un
// transfert de Hohmann entre deux rayons est une identité képlérienne
// (t = π√(a³/µ) avec a = (r1+r2)/2), et la période synodique aussi. Les bornes
// sortent donc de la GÉOMÉTRIE des deux corps, jamais d'un réglage.
//
// ⚠ MARS NE BOUGE PAS D'UN BIT, ET C'EST VOULU : quand les bornes par défaut
// contiennent déjà le Hohmann de la paire (258,9 j pour Terre-Mars, dans
// [150, 400]), on les rend TELLES QUELLES. Toute la calibration martienne — 3 636
// m/s d'injection, 4 686 m/s de trajectoire, 779,9 j de récurrence — est mesurée
// avec ces valeurs-là ; les déplacer pour élargir un domaine qu'elles couvrent
// déjà serait recalibrer sans raison.
// Les deux briques ci-dessous sont définies plus bas dans ce même en-tête (elles
// servent déjà aux horloges et à l'aller-retour d'équipage) : on les DÉCLARE ici
// plutôt que d'en écrire une seconde version, qui pourrait en diverger.
inline double demi_grand_axe_helio_m(const ephem::IEphemeris& eph, ephem::Body b,
                                     Epoch now);
inline double heliocentric_period_days(const ephem::IEphemeris& eph, ephem::Body b,
                                       Epoch now);

inline astro::WindowParams mission_window_params_for(ephem::Body dep, ephem::Body arr,
                                                    const ephem::IEphemeris& eph,
                                                    Epoch now) {
  astro::WindowParams p = mission_window_params();
  // ⚠ LE RAYON DU MOMENT N'EST PAS LE DEMI-GRAND AXE, et sur Mars (e = 0,093)
  // l'écart décide : une première rédaction prenait |r| et rendait une période
  // synodique qui variait de 700 à 920 jours selon la date, si bien que Mars
  // basculait ARBITRAIREMENT hors de ses bornes par défaut. Avec `a` lu par
  // vis-viva, on retrouve les 779,9 j documentés — et le 258,9 j de Hohmann.
  const double a1 = demi_grand_axe_helio_m(eph, dep, now);
  const double a2 = demi_grand_axe_helio_m(eph, arr, now);
  if (!(a1 > 0.0) || !(a2 > 0.0)) return p;
  const double a_t = 0.5 * (a1 + a2);
  const double t_hohmann_j =
      cst::PI * std::sqrt(a_t * a_t * a_t / cst::MU_SUN) / cst::DAY;
  const double T1 = heliocentric_period_days(eph, dep, now);
  const double T2 = heliocentric_period_days(eph, arr, now);
  const double dn = (T1 > 0.0 && T2 > 0.0) ? std::fabs(1.0 / T1 - 1.0 / T2) : 0.0;
  const double t_syn_j = dn > 0.0 ? 1.0 / dn : T1;
  // Le domaine par défaut décrit-il cette paire ? (Mars : oui, à l'identique.)
  if (t_hohmann_j >= p.tof_min_days && t_hohmann_j <= p.tof_max_days
      && p.horizon_days >= t_syn_j)
    return p;
  // Sinon on l'ancre sur la géométrie. Un transfert utile va d'une trajectoire
  // rapide et chère (0,5 × Hohmann) à une lente et économe (1,6 ×) ; l'horizon
  // couvre une récurrence complète, sans quoi « la prochaine fenêtre » n'aurait
  // pas de réponse.
  p.tof_min_days = 0.5 * t_hohmann_j;
  p.tof_max_days = 1.6 * t_hohmann_j;
  p.horizon_days = std::max(1.05 * t_syn_j, 200.0);
  p.slop_days = p.horizon_days / static_cast<double>(p.n_dep);
  return p;
}

// `now` : l'époque courante (s TDB, via Epoch). L'issue est un GateResult : si
// la fenêtre est fermée, `reason` chiffre l'attente — « rater = 25.6 mois ».
inline GateResult launch_window_gate(const Mission& m, Epoch now,
                                     const ephem::IEphemeris& eph,
                                     const astro::WindowParams* params = nullptr) {
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose) return {true, ""};   // fenêtre permanente (voir supra)

  const astro::WindowParams p =
      params ? *params : mission_window_params_for(wt.dep, wt.arr, eph, now);
  const astro::WindowResult w = astro::launch_window(eph, wt.dep, wt.arr, now, p);
  if (!w.ok) return {false, "fenetre : aucune solution de transfert calculable"};
  if (w.open) return {true, ""};   // cf. `mission_window_params` : MÊME réglage partout

  GateResult r;
  r.allowed = false;
  char buf[112];
  std::snprintf(buf, sizeof buf,
                "fenetre de lancement fermee : prochaine dans %.0f jours",
                w.next_open_days);
  r.reason = buf;
  return r;
}

// Δv DE TRAJECTOIRE RÉEL d'une mission [GDD 6.8, 7.3] — plus d'arcade pour Mars.
// Pour une famille à fenêtre synodique, on le tire de la GÉOMÉTRIE de la fenêtre
// courante : injection hyperbolique (Oberth) depuis une orbite de parking LEO +
// insertion elliptique à Mars + une marge de mi-parcours DÉCLARÉE. Le coût
// devient donc SENSIBLE à la qualité de la fenêtre. Familles sans fenêtre
// imposée : on garde le forfait par famille (identique à avant).
inline double trajectory_dv_for_mission(const Mission& m, Epoch now,
                                        const ephem::IEphemeris& eph) {
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose) return trajectory_dv_for_family(m.contract.family);

  // MÊME RÉGLAGE QUE LE GATE ET QUE LA DURÉE DE TRANSIT — c'était le troisième
  // appel de `launch_window` du jeu, et le seul qui gardait encore les valeurs
  // par défaut. Pour Mars ça ne changeait rien (il lit l'optimum synodique, que
  // `slop_days` ne déplace pas) ; pour Jupiter ça décidait de tout.
  const astro::WindowResult w = astro::launch_window(
      eph, wt.dep, wt.arr, now, mission_window_params_for(wt.dep, wt.arr, eph, now));
  if (!w.ok) return trajectory_dv_for_family(m.contract.family);   // repli prudent

  // Le VÉHICULE part d'une orbite de parking (le lanceur l'y a mis) : il paie
  // l'injection réduite par Oberth, pas le v_inf nu. MÊME orbite de parking que
  // celle dont la chronologie de vol tire la durée d'attente avant injection
  // (FlightTimeline.hpp) : un chiffre, une source.
  const double injection = astro::injection_dv_from_circular(
      w.vinf_dep, parking_radius_m(), cst::MU_EARTH);
  // Insertion : capture elliptique (rp bas, ra très haut) — le choix réel, bien
  // moins cher qu'une circularisation basse. L'orbite visée dépend du corps
  // (`capture_orbit_for`) : Mars garde EXACTEMENT ses deux valeurs d'avant.
  const CaptureOrbit co = capture_orbit_for(wt.arr);
  const double insertion = astro::capture_dv_to_ellipse(
      w.vinf_arr, co.rp_m, co.a_m, co.mu);
  const double midcourse = 150.0;   // corrections de mi-parcours [7.5], DÉCLARÉ
  return injection + insertion + midcourse;
}

// LA DURÉE DE TRANSIT VISÉE, à capturer AU FEU VERT [GDD 7.3, 9]. C'est ce qui
// DATE l'arrivée, donc l'insertion et l'EDL — les phases critiques que le moteur
// savait modéliser sans savoir quand elles ont lieu. On prend la durée du
// meilleur transfert de la fenêtre COURANTE (`local_tof_days`), pas celle de
// l'optimum synodique : le vol part maintenant, il vole le transfert de
// maintenant. 0 = famille sans cible nommée (croisière non datée, déclaré).
// ═══ LA DURÉE DOIT ALLER AVEC LA DATE DE DÉPART ═══
// `slop_days` par défaut vaut 60 j : c'est la largeur opérationnelle de
// « maintenant » pour décider si la fenêtre est OUVERTE — la bonne question là,
// la mauvaise ici. Le meilleur transfert de ces 60 jours peut partir 6 semaines
// plus tard ; appliquer SA durée à un départ aujourd'hui donne un couple
// (départ, arrivée) que rien ne relie, et Lambert répond quand même : par un arc
// valide mais absurde, qui plonge à 0,26 UA du Soleil pour rejoindre Mars.
// Trouvé en RENDU, l'arc partant hors du champ (piège n°63). On resserre donc le
// slop à un pas de balayage de la carte porkchop : la durée rendue est celle
// d'un transfert qui part BIEN au moment du feu vert.
inline double transfer_tof_days(const Mission& m, Epoch now,
                                const ephem::IEphemeris& eph) {
  // ═══ LA CIBLE RELATIVISTE N'EST PAS DANS L'ÉPHÉMÉRIDE ═══ [GDD 3.4, 9.3]
  // Une étoile ne se propage pas comme une planète et n'a pas de fenêtre
  // synodique : elle est toujours là. Sa distance est un FAIT MESURÉ (Proxima,
  // parallaxe Gaia DR3), et le transit est le trajet rectiligne que β autorise —
  // donc la durée dépend de l'ARCHITECTURE et non du ciel [décision 10]. C'est la
  // seule famille dont la durée ne sort pas d'un Lambert, et c'est pour cela
  // qu'elle est traitée AVANT le test de fenêtre : `window_target_for_family` ne
  // lui nommait aucune cible, si bien qu'elle repartait avec 0 — un vol qui
  // n'arrivait jamais, alors que toute la chaîne antimatière existe pour lui.
  if (m.contract.family == "relativiste") {
    if (!(m.beta_croisiere > 0.0)) return 0.0;   // pas d'antimatière : rien à dater
    return rel::relativistic_transit(rel::PROXIMA_DISTANCE_M, m.beta_croisiere)
               .t_earth_s / cst::DAY;
  }
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose) return 0.0;
  const astro::WindowResult w =
      astro::launch_window(eph, wt.dep, wt.arr, now,
                           mission_window_params_for(wt.dep, wt.arr, eph, now));
  return w.ok ? w.local_tof_days : 0.0;
}

// ═══ COMBIEN DE TEMPS FAUT-IL ATTENDRE AVANT DE PARTIR ═══ [GDD 7.3]
// Zéro si la fenêtre est ouverte ; sinon le délai jusqu'à son ouverture, calculé
// avec le MÊME réglage que la durée de transit et que le gate. C'est cette
// cohérence-là qui manquait : la durée rendue par `transfer_tof_days` est celle du
// meilleur transfert d'un instant donné, et la faire voler à une AUTRE date fait
// voler un arc que personne n'a calculé — mesuré, un plongeon à 0,862 UA pour une
// injection de 5 827 m/s au lieu des ~3 600 d'un vrai transfert martien.
// USAGE : `attente = transfer_wait_days(m, now)`, puis
//         `tof = transfer_tof_days(m, now + attente)`. Les deux au même instant.
inline double transfer_wait_days(const Mission& m, Epoch now,
                                 const ephem::IEphemeris& eph) {
  if (m.contract.family == "relativiste") return 0.0;   // pas de fenêtre synodique
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose) return 0.0;
  const astro::WindowResult w =
      astro::launch_window(eph, wt.dep, wt.arr, now,
                           mission_window_params_for(wt.dep, wt.arr, eph, now));
  if (!w.ok || w.open) return 0.0;
  return w.next_open_days > 0.0 ? w.next_open_days : 0.0;
}

// ═══ COMBIEN DE TEMPS L'ÉQUIPAGE RESTE DEHORS ═══ [GDD 9.4]
// Une mission habitée vers une cible datée est un ALLER-RETOUR : l'équipage part
// à une fenêtre et ne peut revenir qu'à la suivante. La durée n'est donc pas un
// réglage — c'est la PÉRIODE SYNODIQUE des deux corps, celle-là même qui rythme
// déjà `launch_window_gate` (« récurrence 779,9 j » pour Terre-Mars). Classe
// conjonction : le voyage complet dure à peu près un retour de géométrie.
//
// Les deux périodes orbitales se LISENT sur l'état héliocentrique du moment
// (vis-viva → demi-grand axe → Kepler) : rien à tabuler, et un corps qu'on
// ajouterait demain répondrait tout seul.
// Rend 0 pour une famille sans cible nommée : on n'oppose pas une durée qu'on ne
// sait pas calculer [GDD 6.8] — l'appelant retombe alors sur le SÉJOUR.
// Le demi-grand axe héliocentrique d'un corps, LU sur son état du moment
// (vis-viva inversée). Rend 0 si la trajectoire n'est pas liée. C'est la seule
// grandeur d'orbite dont on ait besoin ici — la période en découle par Kepler, et
// le RYTHME DES HORLOGES de bord aussi [GDD 6.7] : les moyennes ⟨1/r⟩ et ⟨v²⟩ ne
// dépendent que de `a`.
inline double demi_grand_axe_helio_m(const ephem::IEphemeris& eph,
                                     ephem::Body b, Epoch now) {
  const ephem::PosVel s = eph.state(b, ephem::Body::Sun, now);
  const double r = norm(s.r), v2 = norm2(s.v);
  const double inv_a = 2.0 / r - v2 / cst::MU_SUN;
  return inv_a > 0.0 ? 1.0 / inv_a : 0.0;           // <= 0 : trajectoire non liée
}

inline double heliocentric_period_days(const ephem::IEphemeris& eph,
                                       ephem::Body b, Epoch now) {
  const double a = demi_grand_axe_helio_m(eph, b, now);
  if (!(a > 0.0)) return 0.0;
  return orbital_period_s(a, cst::MU_SUN) / cst::DAY;
}

// ═══ LA GÉOMÉTRIE QUI DÉCIDE DU RYTHME DES HORLOGES ═══ [GDD 6.7, 14.4]
// Lue UNE FOIS, à l'embarquement, sur les éphémérides réelles — même source que
// la fenêtre de lancement et que le Δv. L'ellipse de croisière est le transfert
// de Hohmann entre les deux rayons héliocentriques du moment : a = (r₁ + r₂)/2.
// C'est la trajectoire que le véhicule suit vraiment entre deux impulsions, et
// c'est de son demi-grand axe — de rien d'autre — que dépendent les moyennes.
// Sans cible nommée (near-Earth, NEP, science), le véhicule reste au voisinage
// héliocentrique de la Terre : même orbite, donc rapport 1 hors phases LEO.
inline GeometrieHorloge geometrie_horloge(const Mission& m, Epoch now,
                                          const ephem::IEphemeris& eph) {
  GeometrieHorloge g;
  g.r_parking_m = parking_radius_m();
  g.a_terre_m   = demi_grand_axe_helio_m(eph, ephem::Body::EarthBary, now);
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose || g.a_terre_m <= 0.0) {
    g.a_croisiere_m = g.a_sejour_m = g.a_terre_m;
    return g;
  }
  const double r1 = norm(eph.state(wt.dep, ephem::Body::Sun, now).r);
  const double r2 = norm(eph.state(wt.arr, ephem::Body::Sun, now).r);
  g.a_croisiere_m = 0.5 * (r1 + r2);                // ellipse de transfert
  g.a_sejour_m    = demi_grand_axe_helio_m(eph, wt.arr, now);
  if (g.a_sejour_m <= 0.0) g.a_sejour_m = g.a_croisiere_m;
  return g;
}

inline double crew_round_trip_days(const Mission& m, Epoch now,
                                   const ephem::IEphemeris& eph) {
  if (!m.contract.crewed) return 0.0;
  // ═══ LA CIBLE STELLAIRE N'A PAS DE PÉRIODE SYNODIQUE ═══ [GDD 3.4, 9.3]
  // Elle ne tourne pas autour du Soleil : on ne l'attend pas, on y va. La durée
  // d'occupation de l'équipage est donc DEUX FOIS le transit — aller et retour,
  // les deux poussées de plus que compte [GDD 6.7.4]. Sans cette branche, la
  // famille retombait sur `crew_stay_days_for_family` et l'équipage d'un vol de
  // SEIZE ANS emportait **trente jours** de vivres : le vol le plus long du jeu
  // n'était même pas classé « mission longue » [GDD 9.2], et son véhicule ne
  // portait aucune coque pressurisée (`masse_habitat_kg_` est conditionnée à
  // cette durée). Trois conséquences d'un seul manque.
  // TEMPS SUR PLACE NON COMPTÉ, et déclaré [GDD 6.8] : le contrat ne dit pas
  // combien de temps on reste, et 2× le transit domine de toute façon.
  if (m.contract.family == "relativiste") {
    if (!(m.beta_croisiere > 0.0)) return 0.0;   // pas d'antimatière : rien à dater
    return 2.0 * rel::relativistic_transit(rel::PROXIMA_DISTANCE_M, m.beta_croisiere)
                     .t_earth_s / cst::DAY;
  }
  // MÊME source de couple (départ, arrivée) que la fenêtre et que le Δv réel :
  // un seul endroit décide quelles familles ont une cible nommée.
  const WindowTarget wt = window_target_for_family(m.contract.family);
  if (!wt.impose) return 0.0;
  const double t_dep = heliocentric_period_days(eph, wt.dep, now);
  const double t_arr = heliocentric_period_days(eph, wt.arr, now);
  if (t_dep <= 0.0 || t_arr <= 0.0) return 0.0;
  return astro::synodic_period(t_dep, t_arr);
}

// ═══ LE GATE D'ARRIVÉE ═══ [GDD 4.1, 9]
// « Lancer » n'a jamais voulu dire « avoir réussi » : entre le feu vert et le
// débrief il y a un VOL, et il dure. Tant que la chronologie n'est pas arrivée à
// sa manœuvre d'arrivée, le débrief est refusé — et comme le gate de fenêtre, le
// refus CHIFFRE l'attente au lieu de la subir. Une croisière non datée (cible
// non nommée, poussée continue) ne bloque rien : on n'oppose pas une date qu'on
// ne sait pas calculer.
inline GateResult arrival_gate(const Mission& m, double now_days) {
  const ArrivalStatus a = flight_arrival(m, now_days);
  if (!a.dated || a.arrived) return {true, ""};
  GateResult r;
  r.allowed = false;
  char buf[112];
  std::snprintf(buf, sizeof buf, "vol en cours : arrivee dans %.0f jours",
                a.reste_jours);
  r.reason = buf;
  return r;
}

// ═══ L'ISSUE DU VOL ═══ — déterministe, tirée contre la P(succès) évaluée.
struct FlightOutcome {
  bool         success{false};
  Severity     severity{Severity::Minor};
  std::string  cause;
  AnomalyEvent anomaly;         // rempli si échec, à passer à apply_anomaly
  bool         has_anomaly{false};
};

// TOLÉRANCE D'ARRIVÉE : le point de visée doit être atteint pour que la capture
// soit possible. 1 000 km est un ordre de grandeur de couloir de capture ; à
// CALIBRER [GDD Annexe E]. Repère : une campagne bien poursuivie arrive à
// ~100 km, une campagne aveugle à ~90 000 km — le seuil sépare deux régimes, il
// n'arbitre pas un cas limite.
// AU NIVEAU DE L'ESPACE DE NOMS, et non plus dans le corps de `fly_mission` : la
// tolérance que le code de vol du joueur reçoit dans ses entrées [GDD 15.3] doit
// être CELLE-CI, pas une copie. Un chiffre, une source.
inline constexpr double ARRIVEE_TOLERANCE_KM = 1000.0;

// `seed` : combine la graine d'agence et l'identité de la mission -> rejouable.
inline FlightOutcome fly_mission(const Mission& m, const MissionPlan& plan,
                                 std::uint64_t seed) {
  FlightOutcome out;
  const Assessment& a = plan.assessment;
  Rng rng(seed);

  // ═══ 0 bis) EXÉCUTER HORS DU DOMAINE DE VALIDITÉ ═══ [GDD 15.5, ch.10]
  // « Un code qualifié en orbite basse n'est PAS qualifié pour Mars ; exécuter
  // hors du domaine = comportement NON COUVERT = cause d'anomalie légitime. »
  // C'est ce qui donne son prix au banc d'essai : sans cette porte, acheter des
  // heures d'essai ne changeait rien à l'issue et la qualification n'était qu'un
  // décor payant.
  //
  // AVANT le verdict de navigation, et pas après : quand les deux tombent, la
  // cause PROXIMALE est le logiciel. Un code dont on ne sait rien a pu commander
  // n'importe quoi — le manque au but qu'on mesurerait ensuite serait sa
  // conséquence, pas une seconde faute.
  //
  // CE N'EST PAS UNE PÉNALITÉ POUR AVOIR ÉCRIT DU CODE. Rester dans son domaine
  // est gratuit, visible au poste avant le feu vert, et le joueur peut toujours
  // élargir la plage exercée (en payant des heures) ou ne rien embarquer. Ce qui
  // se paie ici, c'est d'avoir embarqué un logiciel sur un vol qu'il n'a jamais
  // vu au banc.
  if (m.code_embarque && m.code_non_couvert) {
    AnomalyEvent ev;
    ev.mission_id = m.contract.id;
    ev.date_days = m.state_entered_days;
    ev.what = "logiciel de vol execute hors de son domaine de validite";
    ev.severity = Severity::Major;
    ev.modifiers.primary_objective_lost = true;
    // Cause racine documentée [GDD 10.3] : la fiche disait ce qu'elle couvrait.
    ev.modifiers.player_error_causal = true;
    if (m.contract.crewed) ev.modifiers.human_lethal_exposure = true;
    out.cause = "comportement non couvert du logiciel de vol";
    out.severity = ev.severity;
    out.anomaly = ev;
    out.has_anomaly = true;
    out.success = false;
    return out;
  }

  // ═══ 0) LA NAVIGATION N'EST PLUS UN DÉ ═══ [GDD 8.1, 8.4]
  // Quand le vol a une navigation calculée, l'erreur d'injection a été TIRÉE au
  // feu vert et le Δv de correction qu'elle exige est un NOMBRE. La question
  // devient donc factuelle : la marge provisionnée le couvre-t-elle ? C'est la
  // différence entre estimer un risque (à la conception) et subir un résultat
  // (en vol) — et c'est ce qui fait qu'une marge trop courte se paie, au lieu
  // de se diluer dans une probabilité.
  if (m.nav_evaluee && (m.nav_dv_required > plan.program.dv_margin ||
                        m.nav_miss_km > ARRIVEE_TOLERANCE_KM)) {
    AnomalyEvent ev;
    ev.mission_id = m.contract.id;
    ev.date_days = m.state_entered_days;
    ev.what = (m.nav_dv_required > plan.program.dv_margin)
                  ? "campagne de correction au-dela de la marge provisionnee"
                  : "point de visee manque : capture impossible";
    ev.severity = Severity::Major;
    ev.modifiers.primary_objective_lost = true;
    // Le joueur a sous-provisionné : c'est une décision de conception, donc une
    // cause racine documentée [GDD 10.3].
    ev.modifiers.player_error_causal = true;
    if (m.contract.crewed) ev.modifiers.human_lethal_exposure = true;
    out.cause = "derive de navigation hors corridor";
    out.severity = ev.severity;
    out.anomaly = ev;
    out.has_anomaly = true;
    out.success = false;
    return out;
  }

  // 1) LA MISSION RÉUSSIT-ELLE ? Tirage contre la P(succès) évaluée. Quand la
  // navigation est résolue, elle ne doit plus peser une SECONDE fois dans le
  // tirage : on retire son facteur, sinon le même risque serait compté deux
  // fois — une estimation ET un fait.
  const double p_tirage =
      (m.nav_evaluee && a.p_physics > 0.0) ? a.p_success / a.p_physics : a.p_success;
  if (rng.uniform01() <= p_tirage) {
    out.success = true;
    out.severity = Severity::Minor;
    out.cause = "mission nominale";
    return out;
  }

  // 2) ÉCHEC : à QUOI est-il dû ? On attribue la cause proportionnellement aux
  // probabilités de défaillance de chaque poste (1-p). Le poste le plus fragile
  // est le plus probable — c'est la lecture d'ingénieur, pas un dé.
  const double f_launcher = 1.0 - a.p_launcher;
  const double f_engine   = 1.0 - a.p_engine;
  const double f_blunder  = a.p_blunder;
  const double f_physics  = 1.0 - a.p_physics;
  const double ftot = f_launcher + f_engine + f_blunder + f_physics;
  double pick = rng.uniform(0.0, ftot > 0.0 ? ftot : 1.0);

  AnomalyEvent ev;
  ev.mission_id = m.contract.id;
  ev.date_days = m.state_entered_days;

  if (pick < f_launcher) {
    // Défaillance lanceur : perte au décollage ou en ascension. Véhicule perdu,
    // débris possibles en LEO si la fragmentation a lieu en altitude.
    out.cause = "defaillance du lanceur";
    ev.what = "echec du lanceur en ascension";
    ev.severity = Severity::Critical;
    ev.modifiers.unique_vehicle_lost = true;
    ev.breakup_mass_kg = a.m0_kg * 0.3;   // etage superieur + charge utile
    ev.breakup_alt_km = 200.0;
    ev.breakup_is_collision = false;
  } else if ((pick -= f_launcher) < f_engine) {
    out.cause = "defaillance moteur en vol";
    ev.what = "extinction ou explosion moteur";
    ev.severity = Severity::Major;
    ev.modifiers.unique_vehicle_lost = true;
  } else if ((pick -= f_engine) < f_blunder) {
    // Erreur grossière de conception/calcul non rattrapée : le joueur en est la
    // cause documentée [GDD 10.3 modificateur].
    out.cause = "erreur de conception non rattrapee";
    ev.what = "faute de calcul : trajectoire ou budget errone";
    ev.severity = Severity::Major;
    ev.modifiers.player_error_causal = true;
    ev.modifiers.primary_objective_lost = true;
  } else {
    // Écart de navigation au-delà du corridor : objectif manqué, mais souvent
    // récupérable — gravité moindre.
    out.cause = "derive de navigation hors corridor";
    ev.what = "insertion hors tolerance : objectif degrade";
    ev.severity = Severity::Moderate;
  }

  // Une mission HABITÉE expose un équipage : le palier monte [GDD 10.3].
  if (m.contract.crewed) ev.modifiers.human_lethal_exposure = true;

  out.severity = ev.severity;
  out.anomaly = ev;
  out.has_anomaly = true;
  out.success = false;
  return out;
}

// Graine rejouable pour une mission : agence + identité de la mission.
inline std::uint64_t mission_seed(std::uint64_t agency_seed, const std::string& mission_id) {
  std::uint64_t h = 1469598103934665603ull;      // FNV-1a offset
  for (char ch : mission_id) { h ^= static_cast<unsigned char>(ch); h *= 1099511628211ull; }
  return agency_seed ^ h;
}

} // namespace fen::mission

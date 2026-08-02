// fen/career/Career.hpp — carrière, vieillissement, passation [GDD 3]
//
// Le rang est un FILTRE INSTITUTIONNEL : il détermine ce que le personnage est
// AUTORISÉ à concevoir, jamais ce que le monde sait faire [GDD 5.3]. La
// promotion vient du SCORE (réussites, budget, crises), pas du temps [GDD 3.3].
// Pas de fin scriptée : mort naturelle (~85 ans), mort opérationnelle (Game
// Over, niveau 5) ou licenciement (trésorerie) [GDD 3.4, 13.2].
// INVARIANT : la passation n'annule JAMAIS un décès opérationnel [GDD 3.5].
#pragma once
#include <string>
#include <vector>
#include "fen/core/Constants.hpp"

namespace fen::career {

enum class Rank {
  Stagiaire = 0, Junior = 1, Senior = 2, Principal = 3, Directeur = 4,
};
inline const char* rank_name(Rank r) {
  switch (r) {
    case Rank::Stagiaire: return "Stagiaire";
    case Rank::Junior:    return "Architecte Junior";
    case Rank::Senior:    return "Architecte Senior";
    case Rank::Principal: return "Architecte Principal";
    default:              return "Directeur de Programme";
  }
}

// Recherches simultanées autorisées par rang [GDD 4.3]. Senior : 2 de base,
// +1 si un programme critique le justifie (fenêtre proche) — d'où le "2 à 3".
inline int max_parallel_research(Rank r, bool critical_program = false) {
  switch (r) {
    case Rank::Stagiaire: return 1;
    case Rank::Junior:    return 2;
    case Rank::Senior:    return critical_program ? 3 : 2;
    case Rank::Principal: return 3;
    default:              return 4;   // maximum ABSOLU [GDD 4.3]
  }
}

// Seuils de promotion (score cumulé). Valeurs de design, déclarées ici pour
// être visibles ; le déclencheur reste le SCORE, jamais l'ancienneté.
inline constexpr double PROMOTION_THRESHOLDS[4] = {100.0, 300.0, 700.0, 1500.0};

// LE HAUT DE L'ÉCHELLE — « le personnage ne quitte ARES que lorsqu'il n'a plus
// de carrière à construire » [GDD 9.2]. Cette phrase désigne UN état, et c'est
// celui-ci : le rang terminal. Nommé une fois, lu par `promotion_ready` (qui
// l'exprimait en dur) et par la porte d'embarquement d'une mission vécue.
inline bool terminal_rank(Rank r) { return r == Rank::Directeur; }

struct CareerState {
  Rank   rank{Rank::Stagiaire};
  double score{0.0};
  double confidence_ares{50.0};   // réputation interne [GDD 10.3], 0..100
  bool   promotion_frozen{false}; // gel suite à incident niveau 3+ [GDD 10.3]
  double frozen_until_days{0.0};  // temps de jeu (jours depuis création partie)

  void add_score(double pts) { score += pts; }
  bool promotion_ready(double now_days) const {
    if (terminal_rank(rank)) return false;
    if (promotion_frozen && now_days < frozen_until_days) return false;
    return score >= PROMOTION_THRESHOLDS[static_cast<int>(rank)];
  }
  void promote() { if (rank != Rank::Directeur) rank = static_cast<Rank>(static_cast<int>(rank) + 1); }
};

// ═══════════════════════════════════════════════════════════════════════════
// LE SCORE DE PROMOTION — TROIS CRITÈRES À PONDÉRATION ÉGALE [GDD 3.3]
// ═══════════════════════════════════════════════════════════════════════════
// « Score cumulé à PONDÉRATION ÉGALE de trois critères » : réussite de mission,
// respect budgétaire, gestion de crise. Le code n'en portait qu'UN — « +40 par
// réussite, −10 par échec », compté sur les compteurs de l'agence —, si bien que
// dépenser deux fois son enveloppe ou perdre un équipage par impréparation ne
// pesait rien sur la carrière. Les deux tiers du barème n'existaient pas.
//
// CE QUI EST DIFFÉRÉ ET CE QUI NE L'EST PAS. [Annexe E] diffère « le barème de
// points », qui dépend du rythme de progression visé : c'est `POINTS_PAR_MISSION`
// ci-dessous, et il est CHOISI pour ne rien déplacer (une mission nominale vaut
// toujours 40 points, comme avant). Ce que [3.3] fixe, en revanche, est la
// STRUCTURE — trois critères, pondération égale —, et elle n'est pas différée.
//
// CHAQUE CRITÈRE REND UNE NOTE DANS [−1, +1]. La somme, divisée par trois, donne
// la fraction du barème. Aucun critère ne peut donc en dominer un autre, ce qui
// est exactement ce que « pondération égale » veut dire.
inline constexpr double POINTS_PAR_MISSION = 40.0;

// Ce qu'une mission achevée rapporte à la carrière. Structure PURE : elle ne lit
// que des faits déjà établis par le modèle, et n'en calcule aucun.
struct MissionScore {
  double reussite{0.0};   // [−1, +1] objectifs atteints, conformité au profil
  double budget{0.0};     // [−1, +1] écart au contrat, tenue des marges
  double crise{0.0};      // [−1, +1] qualité de la réponse aux anomalies
  double total() const {
    return POINTS_PAR_MISSION * (reussite + budget + crise) / 3.0;
  }
};

// Les faits que la mission a laissés derrière elle. Tous EXISTENT déjà dans le
// modèle : on ne demande pas au joueur de renseigner quoi que ce soit.
struct MissionBilan {
  bool   succes{false};
  double budget_contrat_musd{0.0};
  double cout_engage_musd{0.0};
  int    gravite{0};          // mission::Severity, 0 = aucune anomalie
  int    avaries_subies{0};   // pannes survenues en vol
  int    avaries_reparees{0}; // ... et réparées avant qu'elles ne coûtent
};

inline double clamp_note(double v) { return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v); }

inline MissionScore score_mission(const MissionBilan& b) {
  MissionScore s;

  // ─── 1. RÉUSSITE ─── « atteinte des objectifs primaires et secondaires ».
  // Binaire par nature : une mission perdue n'a pas atteint son objectif.
  s.reussite = b.succes ? 1.0 : -1.0;

  // ─── 2. RESPECT BUDGÉTAIRE ─── « écart entre coût engagé et enveloppe
  // contractuelle, TENUE DES MARGES ». La note est la marge relative laissée sur
  // l'enveloppe : dépenser exactement son budget, c'est le respecter sans marge —
  // note nulle, aucun mérite. Le facteur 4 dit qu'une marge du QUART de
  // l'enveloppe vaut la note pleine ; au-delà, on ne récompense plus de
  // sous-dimensionner un programme. Un dépassement du quart coûte la note pleine
  // en négatif, symétriquement.
  if (b.budget_contrat_musd > 0.0) {
    const double marge = (b.budget_contrat_musd - b.cout_engage_musd) / b.budget_contrat_musd;
    s.budget = clamp_note(4.0 * marge);
  }

  // ─── 3. GESTION DE CRISE ─── « qualité de la réponse aux anomalies :
  // diagnostic, arbitrage, sauvegarde d'objectifs ou d'équipage ». Une mission
  // sans anomalie part de la note pleine : il n'y a rien eu à mal gérer. Chaque
  // cran de gravité subi en retire ; chaque panne RÉPARÉE avant qu'elle ne coûte
  // en rend — c'est précisément ce que [GDD 10.3] appelle le demi-palier de
  // rétrogradation, et [3.3] dit qu'il « alimente DIRECTEMENT ce critère ».
  double crise = 1.0;
  crise -= 0.5 * static_cast<double>(b.gravite);          // 1 = mineur ... 5 = catastrophe
  if (b.avaries_subies > 0) {
    const double part = static_cast<double>(b.avaries_reparees) /
                        static_cast<double>(b.avaries_subies);
    // Réparer TOUT ce qui est tombé rattrape un demi-palier (+0,5) ; ne rien
    // réparer n'en rattrape aucun. La proportion est la mesure de la réponse.
    crise += 0.5 * part;
  }
  s.crise = clamp_note(crise);
  return s;
}

// --- Le personnage [GDD 3.4] -------------------------------------------------
// L'âge biologique suit le TEMPS PROPRE (τ) ; l'âge "historique" suit le temps
// terrestre. Ils divergent uniquement en régime relativiste (rel::DualClock).
inline constexpr double LIFE_EXPECTANCY_Y = 85.0;
inline constexpr double YEAR_S = 365.25 * cst::DAY;
// ÂGE D'ENTRÉE EN FONCTION. Celui du premier Architecte (`AresLayer::fonder`),
// donc celui de tout successeur : le poste ne se prend pas plus jeune, et le
// GDD ne donne pas d'autre nombre. Une seule source, pour que la durée d'une
// génération (≈ 53 ans de jeu) ne dépende pas de l'endroit où on la lit.
inline constexpr double ENTRY_AGE_Y = 32.0;

struct Character {
  std::string name;
  double age_bio_s{};           // âge biologique (avance en temps propre)
  double birth_world_s{};       // date monde de naissance (temps terrestre)
  bool   alive{true};
  bool   operational_death{false};  // mort en mission : Game Over, IRRÉVOCABLE

  double age_bio_years() const { return age_bio_s / YEAR_S; }
  // Vieillit du temps PROPRE écoulé (== temps de jeu hors relativiste).
  void age_by_proper_time(double dtau_s) { age_bio_s += dtau_s; }
  bool natural_death_due() const { return age_bio_years() >= LIFE_EXPECTANCY_Y; }
  // Décalage bord/Terre accumulé : le personnage est plus JEUNE que le monde.
  double historical_age_s(double world_now_s) const { return world_now_s - birth_world_s; }
};

// --- Carnet de notes [GDD 3.5, 15.3] -----------------------------------------
// Documentation "amateur" : formules et procédures notées par le personnage.
// C'est LE bien transmissible en passation, avec l'accès technologique du monde.
struct NotebookEntry {
  std::string title;
  std::string body;           // texte libre, style personnel
  double      date_days{};    // temps de jeu à l'écriture
  std::string mission_ref;    // mission associée (retour d'expérience)
};
struct Notebook {
  std::vector<NotebookEntry> entries;
  void write(NotebookEntry e) { entries.push_back(std::move(e)); }
};

// --- Passation [GDD 3.5] -----------------------------------------------------
// Optionnelle, proposée SEULEMENT en fin de vie naturelle. Le successeur hérite
// de l'accès techno (propriété d'ARES, pas du personnage) et du carnet.
//
// ⚠ CORRIGÉ SUR LE GDD v1.2 : ce bloc appliquait encore la règle v1.1 (rang
// ramené à Junior, confiance à 40). Les décisions 6 et 7 du journal v1.2 disent
// l'inverse, et le tableau de 3.5 le répète ligne à ligne :
//   . RANG      — « Transmis : Oui », « propriété du POSTE, non de la personne » ;
//   . CONFIANCE — « Transmis : Non — remise à 70 », la valeur de départ de 13.4.
// Ce qui ne s'hérite pas, c'est la crédibilité PERSONNELLE ; le rang, lui, est un
// droit institutionnel durable [GDD 3.2].
struct Succession {
  // Renvoie faux si la passation est illégale (décès opérationnel).
  static bool allowed(const Character& c) {
    return !c.alive ? !c.operational_death : c.natural_death_due();
  }
  static CareerState inherit_career(Rank rank_atteint) {
    CareerState s;
    s.rank = rank_atteint;        // [GDD 3.5] le successeur CONSERVE le rang
    s.score = 0.0;                // le score, lui, est personnel
    s.confidence_ares = 70.0;     // [GDD 13.4] valeur initiale de la confiance
    return s;
  }

  // ═══ LE SUCCESSEUR PREND LE POSTE ═══ [GDD 3.5]
  // Ce qui change de personne, et RIEN d'autre : l'état programmatique (arbre,
  // finances, missions, station, catalogue) appartient à ARES et ne se touche
  // pas ici — c'est même toute la ligne « État programmatique : Oui —
  // INTÉGRALEMENT » du tableau de 3.5. Le carnet non plus (il se transmet).
  // Le nouvel Architecte entre en fonction à l'âge d'entrée, avec le calendrier
  // du monde pour date de naissance : l'écart d'âge accumulé par son
  // prédécesseur en régime relativiste ne se lègue pas plus que sa crédibilité.
  static Character inherit_character(const std::string& nom, double world_now_s) {
    Character c;
    c.name = nom;
    c.age_bio_s = ENTRY_AGE_Y * YEAR_S;
    c.birth_world_s = world_now_s - c.age_bio_s;
    c.alive = true;
    c.operational_death = false;
    return c;
  }
};

} // namespace fen::career

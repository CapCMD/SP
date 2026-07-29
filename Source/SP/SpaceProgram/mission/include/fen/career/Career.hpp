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

// --- Le personnage [GDD 3.4] -------------------------------------------------
// L'âge biologique suit le TEMPS PROPRE (τ) ; l'âge "historique" suit le temps
// terrestre. Ils divergent uniquement en régime relativiste (rel::DualClock).
inline constexpr double LIFE_EXPECTANCY_Y = 85.0;
inline constexpr double YEAR_S = 365.25 * cst::DAY;

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
};

} // namespace fen::career

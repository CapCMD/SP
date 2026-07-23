// fen/reliability/Reliability.hpp — base de données de fiabilité [GDD 12.3-12.5]
//
// L'UNE DES TROIS DETTES À NE JAMAIS DIFFÉRER [carte P4] : le schéma de fiche,
// l'historique immuable et la validation de provenance sont figés ICI, avant
// tout remplissage. Règles gravées :
//   TRAÇABLE      valeur sans provenance = INVALIDE, refusée par la base ;
//   CONTEXTUELLE  une donnée LEO ne sert pas en cislunaire sans facteur explicite ;
//   JAMAIS BRUTE  la nominale passe TOUJOURS par les modificateurs avant calcul ;
//   CONSERVATRICE l'absence de donnée n'est JAMAIS une bonne fiabilité [12.5] ;
//   ÉVOLUTIVE     décroît, se requalifie ; l'historique n'est JAMAIS supprimé.
// Le rollup mission N'EST PAS une somme : série/parallèle/k-parmi-n explicites.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace fen::reliability {

// --- Provenance [GDD 12.3.1] -------------------------------------------------
// Hiérarchie stricte : mission > constructeur > estimation.
enum class SourceType { MissionData = 0, Manufacturer = 1, Estimate = 2 };

// Niveau de confiance [GDD 12.3.2]. D = exceptionnel, marqué dans tout calcul.
enum class Confidence { A = 0, B = 1, C = 2, D = 3 };

// Contexte de validité : une fiabilité n'existe QUE dans un cadre.
struct ValidityContext {
  bool interplanetary{false};   // faux = orbital proche Terre
  bool crewed{false};           // habité : exigences et défaillances différentes
  double mission_days{30.0};    // durée de référence de la donnée
  double radiation_gcm2{0.0};   // blindage du contexte de référence
  double thermal_cycles{0.0};   // cycles thermiques de référence
};

// --- La fiche [GDD 12.3.2] ---------------------------------------------------
// {nominale, basse, haute} : le triplet est OBLIGATOIRE — pas de précision
// artificielle [12.3.4]. Toute fiche incomplète est PROVISOIRE et hors calculs.
struct Revision {
  std::string date_iso;         // date de la révision
  double nominal{}, lo{}, hi{};
  std::string source;           // référence précise (rapport, publication)
  SourceType source_type{};
  Confidence confidence{};
  std::string cause;            // pourquoi la valeur a changé
};

struct ReliabilityRecord {
  std::string id;               // identifiant unique
  std::string name;             // nom lisible
  std::string family;           // moteur / réservoir / avionique / radiateur...
  std::string function;         // fonction opérationnelle
  ValidityContext context;      // domaine de validité de la nominale

  double nominal{}, lo{}, hi{}; // P(succès) sur la durée de référence
  std::string source;
  SourceType source_type{SourceType::Estimate};
  Confidence confidence{Confidence::D};
  std::string date_ref, date_revised;
  std::string degradation_notes;
  std::vector<std::string> critical_dependencies;

  // JAMAIS supprimé [GDD 12.3.4] : append-only, ordre chronologique.
  std::vector<Revision> history;

  bool complete() const {
    return !id.empty() && !source.empty() && nominal > 0.0
        && lo > 0.0 && hi >= nominal && lo <= nominal;
  }
};

// --- Modificateurs [GDD 12.3.3] ----------------------------------------------
// Appliqués AVANT tout calcul. Chacun multiplie la P(défaillance), jamais la
// P(succès) : un facteur > 1 DÉGRADE. Monotonie testée en CI [carte P3].
struct Modifiers {
  double environment{1.0};      // écart contexte réel / contexte de référence
  double maintenance{1.0};      // < 1 si maintenance de qualité (Novellus atelier)
  double aging_calendar{1.0};   // vieillissement calendaire
  double aging_service{1.0};    // heures de service, cycles
  double integration{1.0};      // qualité d'intégration (revue, essais système)
  double anomaly_history{1.0};  // anomalies passées non résolues
};

// Facteur d'environnement dérivé du contexte : passer LEO -> interplanétaire
// habité longue durée coûte, EXPLICITEMENT [GDD 12.3.3].
inline double context_factor(const ValidityContext& ref, const ValidityContext& use) {
  double f = 1.0;
  if (use.interplanetary && !ref.interplanetary) f *= 1.8;
  if (use.crewed && !ref.crewed)                 f *= 1.3;
  if (use.mission_days > ref.mission_days)
    f *= 1.0 + 0.3 * std::log(use.mission_days / ref.mission_days);
  return f;
}

// --- Évaluateur --------------------------------------------------------------
// PRINCIPE CONSERVATEUR [GDD 12.5] : la valeur de départ dépend de la
// confiance — A part de la nominale, D part de la BORNE BASSE.
struct Effective {
  double p_success{};           // fiabilité effective sur la durée d'emploi
  Confidence confidence{};
  bool provisional{};           // fiche incomplète : affichée, jamais calculée
};

inline Effective evaluate(const ReliabilityRecord& r, const Modifiers& m,
                          double use_days) {
  Effective e;
  e.confidence = r.confidence;
  e.provisional = !r.complete();
  if (e.provisional) { e.p_success = 0.0; return e; }

  double base;
  switch (r.confidence) {                      // conservateur PAR CONSTRUCTION
    case Confidence::A: base = r.nominal; break;
    case Confidence::B: base = 0.75 * r.nominal + 0.25 * r.lo; break;
    case Confidence::C: base = 0.50 * r.nominal + 0.50 * r.lo; break;
    default:            base = r.lo; break;    // D : borne basse, point final
  }
  // Échelle de durée : taux de panne constant sur la durée de référence.
  const double lambda_ref = -std::log(std::clamp(base, 1e-12, 1.0 - 1e-12))
                          / r.context.mission_days;
  double p_fail_use = 1.0 - std::exp(-lambda_ref * use_days);

  const double k = m.environment * m.maintenance * m.aging_calendar
                 * m.aging_service * m.integration * m.anomaly_history;
  p_fail_use = std::clamp(p_fail_use * k, 0.0, 1.0);
  e.p_success = 1.0 - p_fail_use;
  return e;
}

// --- Base --------------------------------------------------------------------
// La base REJETTE toute fiche sans provenance [GDD 12.3.1] et n'écrase jamais :
// toute mise à jour passe par revise(), qui archive l'état précédent.
class ReliabilityDatabase {
 public:
  // Renvoie faux (et n'insère pas) si la fiche est incomplète.
  bool add(const ReliabilityRecord& r) {
    if (!r.complete()) return false;
    if (find(r.id)) return false;              // pas d'écrasement silencieux
    records_.push_back(r);
    return true;
  }
  const ReliabilityRecord* find(const std::string& id) const {
    for (const auto& r : records_) if (r.id == id) return &r;
    return nullptr;
  }
  // Révision stricte [GDD 12.3.4] : archive l'ancienne valeur, applique la
  // nouvelle, ne supprime RIEN.
  bool revise(const std::string& id, const Revision& rev) {
    for (auto& r : records_) {
      if (r.id != id) continue;
      Revision old;
      old.date_iso = r.date_revised.empty() ? r.date_ref : r.date_revised;
      old.nominal = r.nominal; old.lo = r.lo; old.hi = r.hi;
      old.source = r.source; old.source_type = r.source_type;
      old.confidence = r.confidence; old.cause = "archive avant revision";
      r.history.push_back(old);
      r.nominal = rev.nominal; r.lo = rev.lo; r.hi = rev.hi;
      r.source = rev.source; r.source_type = rev.source_type;
      r.confidence = rev.confidence; r.date_revised = rev.date_iso;
      r.history.push_back(rev);
      return true;
    }
    return false;
  }
  const std::vector<ReliabilityRecord>& all() const { return records_; }

 private:
  std::vector<ReliabilityRecord> records_;
};

// --- Rollup 3 niveaux [GDD 12.3.5] -------------------------------------------
// PAS une somme. Blocs explicites série / parallèle / k-parmi-n. Golden :
// vérifié contre calcul main (redondance série/parallèle) [carte P3].
inline double rollup_series(const std::vector<double>& p) {
  double r = 1.0;
  for (double x : p) r *= x;
  return r;
}
inline double rollup_parallel(const std::vector<double>& p) {
  double q = 1.0;                              // toutes les branches tombent
  for (double x : p) q *= (1.0 - x);
  return 1.0 - q;
}
// k unités requises parmi n identiques de fiabilité p (binomiale exacte).
inline double rollup_k_of_n(int k, int n, double p) {
  double sum = 0.0;
  for (int i = k; i <= n; ++i) {
    double c = 1.0;
    for (int j = 0; j < i; ++j) c = c * double(n - j) / double(j + 1);
    sum += c * std::pow(p, i) * std::pow(1.0 - p, n - i);
  }
  return sum;
}

} // namespace fen::reliability

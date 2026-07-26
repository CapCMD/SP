// app/jeu.hpp - LE MODELE DU JEU : l'agence spatiale (couche vivante).
//
// Couche de calcul du jeu (UEBridge -> app -> mission -> astro_core). L'UI ne
// calcule RIEN : elle lit ces structures et appelle ces actions. Tout ce qui est
// physique sort du noyau (Ephemeris, Kepler).
//
// SCISSION (2026-07-26). Toute la mecanique de vol 2D HERITEE a ete retiree de ce
// modele : conception GEO / VAB / assistant, vol GEO temps reel, transferts
// interplanetaires, etude porkchop, marche de donnees, Monte-Carlo, installations
// et recherches maison. Elle n'etait plus ATTEIGNABLE depuis le passage en rendu
// total UE5 (aucun ecran ne l'armait : plus aucun appel a commit()/interp_commit()
// /vol_engager() dans le code vif) et elle est remplacee par la couche ARES (arbre
// techno, catalogue de missions, atelier d'assemblage) plus la boucle de mission
// cible (mission/MissionLoop.hpp). Ne restent ici que : l'AGENCE, le CATALOGUE de
// contrats affiche, la FLOTTE en service [GDD 8.3], l'ECONOMIE stricte, l'EPOQUE
// [GDD 14.1] et la PERSISTANCE.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "app/ares.hpp"
#include "fen/core/Constants.hpp"
#include "fen/core/Vec3.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/mission/Program.hpp"

namespace fen::app {

// ---------------------------------------------------------------------------
// L'AGENCE : argent, temps, confiance - et un MODE D'AIDE (accessibilite).
// Le mode ne change JAMAIS la physique : il change le prix de l'aide humaine.
// ---------------------------------------------------------------------------
enum class ModeAide { Normal, Pro };

struct LigneJournal { double mois; std::string texte; };

struct Agence {
  bool creee{false};
  std::string nom;
  ModeAide mode{ModeAide::Normal};
  double tresorerie{40.0};
  double mois{0.0};
  double confiance{0.70};
  int reussites{0}, echecs{0};
  std::uint64_t graine_agence{0x5DEECE66DULL};
  std::vector<LigneJournal> journal;

  void log(const std::string& s) { journal.push_back({mois, s}); }
  bool depenser(double musd, const std::string& motif);
  void encaisser(double musd, const std::string& motif);
};

// ---------------------------------------------------------------------------
// CONTRATS : le catalogue affiche par le poste PLANIFICATION. Les types au-dela
// de VolGeo restent NOMMES (l'ancien catalogue les decrivait) ; l'acceptation et
// le deroulement reels d'une mission passent desormais par la couche ARES.
// ---------------------------------------------------------------------------
enum class TypeContrat { VolGeo, EtudeMars, VolMars, VolComete, VolTitan };

struct Contrat {
  std::string id, titre, client, resume;
  TypeContrat type{TypeContrat::VolGeo};
  mission::Contract spec{};
  double cible_sma{42164170.0}, tol_sma{50e3}, tol_ecc{2e-3}, tol_inc_deg{0.25};
  double prime_succes{12.0}, penalite_echec{30.0};
  bool accepte{false}, termine{false}, reussi{false};
  double mois_signature{-1};        // ECHEANCE : le client n'attend pas indefiniment
};

// ---------------------------------------------------------------------------
// LA FLOTTE EN SERVICE [GDD 8.3] : chaque engin garde SON ephemeride propre.
// Modele DECLARE [GDD 6.8] : kepler 2 corps autour du corps de reference
// (relais -> Terre, orbiteur -> Mars, sonde -> Soleil), plan ecliptique.
// Les positions publiees sont des ESTIMATIONS de navigation - jamais une verite
// absolue [GDD 7.5].
// ---------------------------------------------------------------------------
struct EnginFlotte {
  enum Type { RelaisGeo = 0, OrbiteurMars = 1, SondeLointaine = 2 };
  int type{RelaisGeo};
  std::string nom;                  // id du contrat d'origine ("GEO-3", ...)
  double t0{0};                     // s TDB a la mise en service
  // relais / orbiteur : cercle de rayon sma_m (demi-grand axe REEL atteint),
  // phase0 = anomalie a t0, sens prograde.
  double sma_m{0}, phase0{0};
  // sonde : etat heliocentrique complet a t0 (propagation kepler universelle).
  Vec3 r0{}, v0{};
};

// ---------------------------------------------------------------------------
// LE JEU (couche vivante)
// ---------------------------------------------------------------------------
struct Jeu {
  Agence agence;
  std::vector<Contrat> contrats;
  int contrat_actif{-1};            // index dans contrats, -1 = aucun
  double donnees_gbit{0}, echantillons_kg{0};
  int relais_geo{0}, orbiteurs_mars{0}, sondes_lointaines{0};
  std::vector<EnginFlotte> flotte;  // ephemerides individuelles [GDD 8.3]
  // Couche ARES (GDD v1.2) : carriere, arbre 6 branches, catalogue verrouille,
  // Novellus, fiabilite, economie chiffree, boucle de mission.
  AresLayer ares;
  ephem::StandishEphemeris eph;
  // [GDD 14.1] WorldEpoch : etat du systeme solaire synchronise sur la date/heure
  // REELLE au seul moment de la creation de la partie ; ensuite le temps est
  // pilote par le jeu. 0 = partie d'avant la v0.7 (calendrier illustratif 2027).
  double epoch0_tdb{0};
  std::string erreur;
  // --- economie stricte : la faillite est un GAME OVER ---
  bool game_over{false};
  std::string raison_faillite;
  double cout_programme{0};         // depenses depuis la signature (marge par mission)

  Jeu();

  // --- agence / gestion ---
  void creer_agence(const std::string& nom, ModeAide mode);
  void passer_mois();               // LE TOUR : charges, revenus science, echeances
  double revenu_mensuel_gbit() const;   // pour la carte : ce que rapporte la flotte
  double epoch_courant() const;         // s TDB estime a partir du calendrier agence
  // --- flotte [GDD 8.3] : position ESTIMEE d'un engin a l'epoque t ---
  int flotte_parent(const EnginFlotte& e) const;               // fen::ephem::Body
  Vec3 flotte_position_rel(const EnginFlotte& e, double t) const;  // m, rel. parent
  // --- economie stricte ---
  bool payer(double musd, const std::string& motif);       // achat VOLONTAIRE : refuse si fonds insuffisants
  void depense_obligatoire(double musd, const std::string& motif);  // peut declencher la faillite
  void reinitialiser();                                     // nouvelle partie (apres game over)
  // --- contrats ---
  const Contrat* actif() const {
    return contrat_actif >= 0 ? &contrats[contrat_actif] : nullptr;
  }
  // --- persistance ---
  bool sauvegarder(const std::string& chemin) const;
  bool charger(const std::string& chemin);

 private:
  void generer_contrats();
  void flotte_reconstruire();       // sauvegardes anciennes : compteurs -> engins
};

} // namespace fen::app

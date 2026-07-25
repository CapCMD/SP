// app/jeu.hpp - LE MODELE DU JEU v0.5 : l'agence spatiale complete.
//
// Couche de calcul du jeu (ui -> app -> mission -> astro_core). L'UI ne calcule
// RIEN : elle lit ces structures et appelle ces actions. Tout ce qui est
// physique sort du noyau (Transfers, Vehicle, Session, Lambert, Kepler, BPlane).
// La ou un MODELE simplifie est utilise (dispersion interplanetaire), il est
// DECLARE : ce sont les valeurs MESUREES de m01_corridor, et le memo le dit.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fen/astro/Elements.hpp"
#include "fen/astro/Porkchop.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/core/Constants.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/flight/Session.hpp"
#include "fen/io/Fpl.hpp"
#include "fen/mission/Program.hpp"
#include "fen/vehicle/Vehicle.hpp"

namespace fen::app {

// ---------------------------------------------------------------------------
// L'AGENCE : argent, temps, confiance - et un MODE D'AIDE (accessibilite).
// Le mode ne change JAMAIS la physique : il change le prix de l'aide humaine.
// ---------------------------------------------------------------------------
// DIFFICULTE (2 modes, cf. ecran titre) : le mode ne change JAMAIS la physique,
// seulement le PRIX/la DISPONIBILITE de l'aide humaine.
//   Normal : acces a l'assistant (l'aide existe, gratuite ou peu chere).
//   Pro    : le joueur realise tous les calculs sans aide.
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
  std::uint64_t tirer_graine();
};

// ---------------------------------------------------------------------------
// GESTION : installations, recherche, ressources. Effets = des PRIX et des
// DELAIS (jamais la physique). Chaque effet est affiche et justifie.
// ---------------------------------------------------------------------------
struct Installation {
  std::string id, nom, effet, pourquoi;   // pourquoi = a quoi ca sert concretement
  double cout{}, entretien{};             // M$ / M$ par mois
  bool construite{false};
};

struct Recherche {
  std::string id, nom, desc, pourquoi, prereq;   // prereq = id d'une autre recherche
  double cout{};                       // M$ au lancement
  double mois_requis{};
  double avancement{0};
  bool faite{false}, active{false};
};

// ---------------------------------------------------------------------------
// CONTRATS
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
// LE MARCHE : donnees et echantillons se vendent a des ACHETEURS distincts,
// aux prix FLUCTUANTS (deterministes : fonction du mois, pas de rand cache)
// et a la DEMANDE mensuelle bornee. L'historique des ventes est conserve.
// ---------------------------------------------------------------------------
struct Acheteur {
  std::string nom, profil;
  double mult_donnees{1.0}, mult_echant{1.0};   // preference de prix
  double demande_gbit{20}, demande_kg{4};       // par mois
};
struct Vente { double mois; std::string acheteur, quoi; double qte, total; };

// ---------------------------------------------------------------------------
// LE VAB : assemblage modulaire du vaisseau (facon KSP, sans pilotage).
// Les masses sont reelles, le Delta-v sort de vehicle::Stage (Tsiolkovski).
// ---------------------------------------------------------------------------
struct Vab {
  int avionique{0};                 // 0 basique / 1 redondante / 2 miniaturisee
  int structure{1};                 // 0 legere / 1 standard / 2 renforcee
  double ergols{2600};              // kg charges dans le reservoir
  bool antenne{false};              // +40 kg, +4 M$ : +50 % de debit science
};

// ---------------------------------------------------------------------------
// CONCEPTION GEO (Boucle A) + ASSISTANT de derivation (accessibilite)
// ---------------------------------------------------------------------------
struct Derivees {
  double v_circ{}, v_gto_peri{}, dv_inj{}, v_gto_apo{}, v_geo{};
  double dv_sep{}, dv_comb{}, economie{};
  double rsw_s{}, rsw_w{}, tof_half{};
  double dv_total_sep{}, dv_total_comb{};
};
Derivees deriver_m00(double r_park, double i_park_rad, double r_geo);

struct NiveauPoursuite {
  const char* nom;
  double cout_musd, jours, p_physique_catalogue;
  std::vector<std::array<double,3>> passes;
  int revolutions_sup;
};
const NiveauPoursuite& niveau_poursuite(int lvl);

// une etape de l'assistant : question, formule, valeur juste (calculee)
struct EtapeWizard {
  std::string question, formule, indice;
  double valeur{};                  // la bonne reponse (m/s)
  double reponse{0};                // celle du joueur
  bool validee{false}, revelee{false};
};

struct Conception {
  double dv_inj_joueur{0}, dv_comb_joueur{0};
  bool derive_verifiee{false}, derive_ok{false}, corrige_demande{false};
  bool indice_separee{false};
  int moteur{0}, lanceur{-1};
  double heures_essai{0.0};
  bool revue{false};
  bool instrument{false};           // +150 kg, +8 M$ : le satellite devient un
                                    // RELAIS SCIENCE apres la mission (Gbit/mois)
  int niveau{5};
  double marge_dv{60.0};
  double p_physique{0.90};
  bool p_mesuree{false};
  Derivees d{};
  vehicle::SizingResult taille{};
  mission::Assessment bilan{};
  double perte_poussee_finie{0.0};
  bool matrice_achetee{false};
  std::vector<EtapeWizard> wizard;  // l'assistant pas-a-pas (rempli a l'acceptation)
  Vab vab{};                        // l'assemblage du vaisseau
  bool vab_auto{true};              // true = les ergols sont dimensionnes au point fixe
};

struct CaseMatrice { mission::Assessment a; int moteur, niveau; };

// ---------------------------------------------------------------------------
// VOL GEO (Boucle B) - TEMPS REEL : la salle de vol vit, le temps s'ecoule.
// ---------------------------------------------------------------------------
enum class EtapeVol {
  PreInjection, PretAMF, DeriveAMF2, PretAMF2, DeriveTRIM, PretTRIM,
  DeriveVerdict, Verdict
};

struct Evenement { double t_h; std::string nom; int type; };

struct PropositionAnalyse {
  bool valide{false};
  double t_burn{};
  Vec3 dv{};
  std::string note;
  std::string memo;                 // LE CALCUL DETAILLE (ouvrable dans l'UI)
};

struct Vol {
  bool commis{false};
  bool rehearsal{false};            // tutoriel : execution au nominal (Gates off)
  double retard_mois{0};            // livraison au-dela de l'echeance : penalite sur la prime
  std::uint64_t graine{0};
  io::FplDocument doc;
  double t0{0};
  std::unique_ptr<flight::Session> S;
  EtapeVol etape{EtapeVol::PreInjection};
  // --- temps reel ---
  bool tr_actif{true};              // le temps s'ecoule-t-il ?
  double tr_warp{600.0};            // secondes simulees / seconde reelle
  double tr_cible{0};               // jusqu'ou la phase courante avance
  bool tr_en_route{false};          // une derive est en cours vers tr_cible
  // --- navigation ---
  flight::Observation obs{};
  bool obs_valide{false};
  int nb_observations{0};
  // --- corrections ---
  double dv_corr{0.0}, cout_analyse{0.0};
  PropositionAnalyse prop{};
  // --- trace estimee (km ; t en s TDB) pour le schema 2D/3D et le marqueur ---
  std::vector<double> traj_x, traj_y, traj_z, traj_t;
  double sigma_pos_km{0};
  // --- ascension (transition de lancement) ---
  double ascension_t{-1};           // -1 = pas en cours ; 0..1 = montee du sol au parking
  // --- salle de vol ---
  std::vector<Evenement> chrono;
  std::vector<std::string> flux;    // TELEMETRIE defilante
  int stations{0};                  // bitmask passes actives 1|2|4 (G/M/C)
  // --- verdict ---
  bool fini{false}, ok{false};
  astro::Elements el_final{};
  bool a_ok{false}, e_ok{false}, i_ok{false};   // detail par critere
  std::string verdict, postmortem, pourquoi;    // pourquoi = explication en clair
  std::vector<double> verite_x, verite_y;
  bool perdu_avant_cible{false};
  std::string raison_perte;
};

// ---------------------------------------------------------------------------
// INTERPLANETAIRE (Mars / comete) - conception sur la carte, vol au corridor.
// Le MODELE de dispersion est celui MESURE par m01_corridor (echelle de TCM) ;
// tout le reste (Lambert, C3, Oberth, b<->r_p) est exact.
// ---------------------------------------------------------------------------
struct ConceptionInterp {
  bool carte_calculee{false};
  std::atomic<bool> calcul{false};  // non copiable : d'ou le reset() manuel
  int n_dep{41}, n_tof{41};
  std::vector<float> grille;        // vinf_dep + vinf_arr (km/s), oriente bas->haut
  double dep0{}, dep1{}, tof0{}, tof1{};
  bool choisie{false};
  double t_dep{}, tof{};
  double c3{}, vinf_dep{}, vinf_arr{};
  double dv_tmi{}, dv_insertion{}, dv_total{};
  int strategie_tcm{3};             // 0..3 : echelle mesuree (m01_corridor)
  int moteur{0};
  int n_etages{1};                  // 1..3 : empilement d'etages identiques (missions lourdes)
  double marge_dv{40.0};
  bool revue{true};
  bool collecteur{false};
  bool assistance{false};           // tour a assistances grav. : modele DECLARE
                                    // (reduction de dv_tmi mesuree sur t01_veega)
  vehicle::SizingResult taille{};
  mission::Assessment bilan{};

  ConceptionInterp() = default;
  void reset() {                    // remet a neuf sans toucher au type atomique
    carte_calculee = false; calcul.store(false);
    n_dep = 41; n_tof = 41; grille.clear();
    dep0 = dep1 = tof0 = tof1 = 0;
    choisie = false; t_dep = tof = 0;
    c3 = vinf_dep = vinf_arr = dv_tmi = dv_insertion = dv_total = 0;
    strategie_tcm = 3; moteur = 0; n_etages = 1; marge_dv = 40.0; revue = true; collecteur = false;
    assistance = false; taille = {}; bilan = {};
  }
};

struct VolInterp {
  bool commis{false}, fini{false}, ok{false};
  double retard_mois{0};            // livraison au-dela de l'echeance : penalite sur la prime
  std::uint64_t graine{0};
  TypeContrat type{TypeContrat::VolMars};
  double t_dep{}, t_arr{}, tof{};
  double t{};                       // s TDB courant
  bool tr_actif{true};
  double tr_warp{86400.0 * 2};      // 2 jours simules / s
  int phase{0};                     // 0->TCM1(dep+30j) 1->TCM2(arr-30j) 2->arrivee 3 verdict
  double t_tcm1{}, t_tcm2{};
  bool tcm1_faite{false}, tcm2_faite{false};
  int strategie_tcm{0};             // 0..3 : niveau de correction effectivement realise
  double ellipse_km{};              // 3-sigma courante (echelle mesuree)
  double dv_tcm{0};
  // arrivee (tiree de la graine, gelee au commit)
  double bt_arr{}, br_arr{}, b_arr{};
  double rp_km{}, dv_ins_reel{};
  double d_ca_km{};                 // comete : distance de plus proche approche
  double science_gbit{};
  // traces (UA, plan ecliptique) : orbites reelles + arc de croisiere
  std::vector<double> ter_x, ter_y, cib_x, cib_y, arc_x, arc_y, arc_t;
  std::vector<std::string> flux;
  bool corridor_ok{false}, marge_ok{false};   // detail du verdict (clarte)
  std::string verdict, postmortem, pourquoi;
};

// ---------------------------------------------------------------------------
// TUTORIEL : un personnage fictif guide la premiere mission GEO.
// ---------------------------------------------------------------------------
struct Tuto {
  bool actif{false};
  int etape{0};                     // progression scriptee
  int dernier_vu{-1};               // pour ne montrer chaque beat qu'une fois
};

// ---------------------------------------------------------------------------
// L'ETUDE MARS (inchangee)
// ---------------------------------------------------------------------------
struct EtudeMars {
  std::atomic<bool> calcul_en_cours{false};
  bool calculee{false};
  int n_dep{61}, n_tof{61};
  std::vector<float> grille_c3;
  double dep0{}, dep1{}, tof0{}, tof1{};
  double best_c3{0}, best_dep{0}, best_tof{0};
  std::string best_dep_iso;
  double cout_calcul() const { return 0.0004 * n_dep * n_tof; }
  int tcm_choix{1};
  bool corridor_vu{false};
  bool livree{false};
};

// ---------------------------------------------------------------------------
// LE JEU
// ---------------------------------------------------------------------------
struct Jeu {
  Agence agence;
  std::vector<Contrat> contrats;
  int contrat_actif{-1};            // contrat de vol en cours (tout type)
  Conception conception;
  Vol vol;
  ConceptionInterp cinterp;
  VolInterp vinterp;
  EtudeMars etude;
  std::vector<CaseMatrice> matrice;
  std::vector<Installation> installations;
  std::vector<Recherche> recherches;
  double donnees_gbit{0}, echantillons_kg{0};
  int relais_geo{0}, orbiteurs_mars{0}, sondes_lointaines{0};
  Tuto tuto;
  ephem::StandishEphemeris eph;
  std::string erreur;
  // --- economie stricte : la faillite est un GAME OVER ---
  bool game_over{false};
  std::string raison_faillite;
  double cout_programme{0};         // depenses depuis la signature (marge par mission)
  // --- marche ---
  std::vector<Acheteur> acheteurs;
  std::vector<Vente> historique_ventes;
  double demande_gbit[3]{}, demande_kg[3]{};   // demande RESTANTE ce mois-ci
  int mois_marche{-1};              // dernier mois ou la demande a ete regeneree

  std::atomic<bool> mc_en_cours{false};
  std::atomic<int> mc_fait{0};
  int mc_total{0};
  double mc_resultat{-1};
  std::unique_ptr<std::thread> worker;

  Jeu();
  ~Jeu();

  // --- agence / gestion ---
  void creer_agence(const std::string& nom, ModeAide mode);
  void passer_mois();               // LE TOUR : entretien, recherche, relais science
  void construire(int i);
  void lancer_recherche(int i);
  bool recherche_faite(const std::string& id) const;
  double revenu_mensuel_gbit() const;   // pour la carte : ce que rapporte la flotte
  double epoch_courant() const;         // s TDB estime a partir du calendrier agence
  static constexpr double PRIX_GBIT = 0.8, PRIX_ECHANT_KG = 0.25;
  // --- economie stricte ---
  bool payer(double musd, const std::string& motif);       // achat VOLONTAIRE : refuse si fonds insuffisants
  void depense_obligatoire(double musd, const std::string& motif);  // peut declencher la faillite
  void reinitialiser();                                     // nouvelle partie (apres game over)
  // --- marche ---
  void rafraichir_marche();             // regenere la demande mensuelle (deterministe)
  double prix_donnees(int acheteur) const;      // M$/Gbit, fluctuant
  double prix_echantillons(int acheteur) const; // M$/kg, fluctuant
  void vendre_a(int acheteur, bool echantillons, double qte);
  // effets des installations / du mode (prix et delais, jamais la physique)
  double m_essais() const, m_calcul() const, m_poursuite() const;
  double prix_analyse() const;
  double mois_integration_eff() const;
  bool p_catalogue_visible() const { return agence.mode != ModeAide::Pro; }
  // REPUTATION : la confiance module l'exigence P(succes) du client. Confiance
  // haute -> client plus tolerant ; en defaut -> plus exigeant (borne anti-spirale).
  double exigence_client(double base_min_p) const;

  // --- contrats ---
  void accepter_contrat(int idx);
  const Contrat* actif() const {
    return contrat_actif >= 0 ? &contrats[contrat_actif] : nullptr;
  }

  // --- conception GEO ---
  void verifier_derivations();
  void demander_corrige() { conception.corrige_demande = true; }
  void recalculer_conception();
  void mesurer_p_physique(int n_vols);
  void encaisser_mc();
  void acheter_matrice();
  // LE VAB : masses reelles, Delta-v Tsiolkovski (vehicle::Stage)
  double vab_masse_seche() const;   // hors ergols, hors charge utile
  double vab_m0() const;            // masse totale au decollage
  double vab_dv() const;            // Delta-v de l'etage assemble (exact)
  double vab_duree_injection() const;   // s : duree de l'arc d'injection (pertes si long)
  void vab_dimensionner();          // point fixe -> regle les ergols au besoin exact
  // UNE SEULE evaluation programme, partagee bilan/matrice (source unique de verite)
  mission::Assessment evaluer_geo(int moteur, int lanceur, int niveau,
                                  double heures, bool revue, double marge,
                                  double p_physique, bool instrument,
                                  const Vab* vab_joueur) const;
  // assistant : verifier / reveler une etape (reveler coute 0,1 M$ en mode Pro)
  void wizard_verifier(int etape);
  void wizard_reveler(int etape);

  // --- vol GEO ---
  bool commit();
  void tick(double dt_reel);        // A APPELER CHAQUE FRAME (temps reel)
  void vol_engager();               // engage la phase (lancement / derive)
  void vol_sauter();                // termine instantanement la derive en cours
  void vol_observer();
  void vol_analyser();
  void vol_bruler_proposition();
  void vol_bruler_manuel(double dt_s, double dv_r, double dv_s, double dv_w);
  void terminer_vol();
  Vec3 vol_position_estimee() const;   // km, interpolation de la trace au temps courant

  // --- interplanetaire ---
  void interp_calculer_carte();
  void interp_choisir(double t_dep, double tof);   // Lambert exact au point choisi
  void interp_recalculer();          // vehicule + bilan
  bool interp_commit();
  void interp_faire_tcm();
  void interp_passer_tcm();
  void interp_terminer();            // arrivee : tirage, corridor, insertion
  State etat_cible(double t) const;  // Mars (ephemeride) ou comete (Kepler exact)

  // --- etude ---
  void etude_calculer_porkchop();
  void etude_livrer();

  // --- memos techniques (les calculs detailles, ouvrables dans l'UI) ---
  std::string memo_derivations() const;
  std::string memo_vehicule() const;
  std::string memo_liaison() const;
  std::string memo_interp() const;
  std::string memo_titan() const;
  std::string memo_modele_disp() const;   // le modele de dispersion, declare

  // --- persistance ---
  bool sauvegarder(const std::string& chemin) const;
  bool charger(const std::string& chemin);

 private:
  void generer_contrats();
  void offrir_apres_mission(bool reussi);
  void ecrire_fpl(const std::string& chemin) const;
  void rafraichir_trace();
  void pousser_chrono(const std::string& nom, int type);
  void teletype(const std::string& ligne);          // vol GEO
  void teletype_i(const std::string& ligne);        // vol interp
  double periode_estimee() const;
  PropositionAnalyse calculer_manoeuvre(const char* apside_seul, double saut) const;
  void vol_apres_burn();
  void arrivee_phase();              // transition quand tr_cible est atteinte
  int stations_actives_a(double t) const;
  void construire_wizard();
  void appliquer_bonus(mission::Assessment& a) const;
};

} // namespace fen::app

// app/jeu.cpp - le modele du jeu v0.5. Tout chiffre affiche sort d'ici ; tout
// ce qui est physique sort du noyau. La ou un MODELE simplifie sert (dispersion
// interplanetaire = echelle de TCM mesuree par m01_corridor), c'est declare, et
// le memo memo_modele_disp() l'explique au joueur.
#include "app/jeu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "fen/astro/BPlane.hpp"
#include "fen/astro/Kepler.hpp"
#include "fen/astro/Lambert.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/force/Forces.hpp"
#include "fen/prop/Propagator.hpp"

namespace fen::app {
using namespace fen::cst;
static constexpr double R_GEO   = 42164170.0;
static constexpr double R_PARK  = 6378137.0 + 200e3;
static constexpr double I_PARK  = 28.5 * DEG;
static constexpr double STRUCT_KG = 150.0, RESIDU = 0.02;
static constexpr int    N_ALLUMAGES = 4;
static constexpr double PERTE_FINIE_REL = 0.0022;
static constexpr double PRIX_MC_PAR_VOL = 0.03;
static constexpr double PRIX_MATRICE = 0.4;

// ---------------------------------------------------------------------------
// Agence
// ---------------------------------------------------------------------------
bool Agence::depenser(double musd, const std::string& motif) {
  tresorerie -= musd;
  char b[160];
  std::snprintf(b, sizeof(b), "-%.2f M$ : %s (tresorerie %.1f M$)", musd, motif.c_str(), tresorerie);
  log(b);
  return true;
}
void Agence::encaisser(double musd, const std::string& motif) {
  tresorerie += musd;
  char b[160];
  std::snprintf(b, sizeof(b), "+%.2f M$ : %s (tresorerie %.1f M$)", musd, motif.c_str(), tresorerie);
  log(b);
}
std::uint64_t Agence::tirer_graine() {
  graine_agence += 0x9E3779B97f4A7C15ULL;
  std::uint64_t z = graine_agence;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return (z ^ (z >> 31)) % 100000ULL;
}

// ---------------------------------------------------------------------------
// ECONOMIE STRICTE : jamais de solde negatif silencieux.
//  - achat VOLONTAIRE : refuse si les fonds manquent (message clair) ;
//  - depense OBLIGATOIRE (entretien, salaires, penalite) : si elle met la
//    tresorerie a zero ou moins -> FAILLITE, game over motive.
// ---------------------------------------------------------------------------
bool Jeu::payer(double musd, const std::string& motif) {
  if (game_over) return false;
  if (musd > agence.tresorerie) {
    char b[200];
    std::snprintf(b, sizeof(b), "FONDS INSUFFISANTS : %s coute %.1f M$, il te reste %.1f M$. "
                  "Vends des donnees, livre une etude, ou attends tes revenus.",
                  motif.c_str(), musd, agence.tresorerie);
    erreur = b;
    return false;
  }
  agence.depenser(musd, motif);
  if (contrat_actif >= 0 && !contrats[contrat_actif].termine) cout_programme += musd;
  return true;
}
void Jeu::depense_obligatoire(double musd, const std::string& motif) {
  if (game_over) return;
  agence.depenser(musd, motif);
  if (contrat_actif >= 0 && !contrats[contrat_actif].termine) cout_programme += musd;
  if (agence.tresorerie <= 0.0) {
    game_over = true;
    char b[240];
    std::snprintf(b, sizeof(b), "La depense obligatoire \"%s\" (%.1f M$) a vide la caisse "
                  "(solde %.1f M$). Sans tresorerie, plus de salaires : l'agence est dissoute.",
                  motif.c_str(), musd, agence.tresorerie);
    raison_faillite = b;
    agence.log("*** FAILLITE : " + std::string(motif) + " ***");
  }
}
void Jeu::reinitialiser() {
  if (worker && worker->joinable()) worker->join();
  agence = Agence{};
  contrat_actif = -1;
  conception = Conception{};
  vol = Vol{};
  cinterp.reset();
  vinterp = VolInterp{};
  etude.calculee = false; etude.calcul_en_cours = false; etude.livree = false;
  etude.corridor_vu = false; etude.grille_c3.clear(); etude.tcm_choix = 1;
  matrice.clear();
  donnees_gbit = echantillons_kg = 0;
  relais_geo = orbiteurs_mars = sondes_lointaines = 0;
  tuto = Tuto{};
  game_over = false; raison_faillite.clear(); cout_programme = 0;
  historique_ventes.clear(); mois_marche = -1;
  erreur.clear();
  generer_contrats();
  Jeu neuf_catalogues;                       // catalogues remis a neuf
  installations = neuf_catalogues.installations;
  recherches = neuf_catalogues.recherches;
}

// ---------------------------------------------------------------------------
// LE MARCHE : prix fluctuants deterministes (sinusoides dephasees par acheteur,
// pas de RNG cache), demande mensuelle bornee, historique conserve.
// ---------------------------------------------------------------------------
static double fluctuation(double mois, int k, double amplitude) {
  return 1.0 + amplitude * std::sin(mois * (0.55 + 0.13 * k) + k * 2.1)
             + 0.5 * amplitude * std::sin(mois * 0.21 + k * 4.7);
}
void Jeu::rafraichir_marche() {
  const int m = (int)agence.mois;
  if (m == mois_marche) return;
  mois_marche = m;
  for (int k = 0; k < 3; ++k) {
    demande_gbit[k] = acheteurs[k].demande_gbit * std::max(0.2, fluctuation(agence.mois, k, 0.5));
    demande_kg[k]   = acheteurs[k].demande_kg   * std::max(0.2, fluctuation(agence.mois, k + 3, 0.5));
  }
}
double Jeu::prix_donnees(int k) const {
  return PRIX_GBIT * acheteurs[k].mult_donnees * std::max(0.25, fluctuation(agence.mois, k, 0.35));
}
double Jeu::prix_echantillons(int k) const {
  double p = PRIX_ECHANT_KG * acheteurs[k].mult_echant * std::max(0.25, fluctuation(agence.mois, k + 3, 0.35));
  for (const auto& i : installations) if (i.id == "labo" && i.construite) p *= 2.0;
  return p;
}
void Jeu::vendre_a(int k, bool echant, double qte) {
  if (k < 0 || k >= 3) return;
  rafraichir_marche();
  double& stock = echant ? echantillons_kg : donnees_gbit;
  double& dem = echant ? demande_kg[k] : demande_gbit[k];
  qte = std::min({qte, stock, dem});
  if (qte <= 0) { erreur = "Rien a vendre ici : stock vide ou demande epuisee ce mois-ci."; return; }
  const double prix = echant ? prix_echantillons(k) : prix_donnees(k);
  stock -= qte; dem -= qte;
  const double total = qte * prix;
  agence.encaisser(total, "vente a " + acheteurs[k].nom);
  historique_ventes.push_back({agence.mois, acheteurs[k].nom,
                               echant ? "echantillons (kg)" : "donnees (Gbit)", qte, total});
  if (historique_ventes.size() > 40) historique_ventes.erase(historique_ventes.begin());
}

// ---------------------------------------------------------------------------
// Physique de conception GEO
// ---------------------------------------------------------------------------
Derivees deriver_m00(double r_park, double i_park, double r_geo) {
  Derivees d;
  const double a_gto = 0.5 * (r_park + r_geo);
  d.v_circ     = astro::v_circular(r_park, MU_EARTH);
  d.v_gto_peri = astro::vis_viva(r_park, a_gto, MU_EARTH);
  d.dv_inj     = d.v_gto_peri - d.v_circ;
  d.v_gto_apo  = astro::vis_viva(r_geo, a_gto, MU_EARTH);
  d.v_geo      = astro::v_circular(r_geo, MU_EARTH);
  d.dv_sep     = std::fabs(d.v_geo - d.v_gto_apo) + astro::dv_plane_change(d.v_geo, i_park);
  d.dv_comb    = astro::dv_combined(d.v_gto_apo, d.v_geo, i_park);
  d.economie   = d.dv_sep - d.dv_comb;
  d.rsw_s      = d.v_geo * std::cos(i_park) - d.v_gto_apo;
  d.rsw_w      = d.v_geo * std::sin(i_park);
  d.tof_half   = PI * std::sqrt(std::pow(a_gto, 3) / MU_EARTH);
  d.dv_total_sep  = d.dv_inj + d.dv_sep;
  d.dv_total_comb = d.dv_inj + d.dv_comb;
  return d;
}

const NiveauPoursuite& niveau_poursuite(int lvl) {
  auto P = [](int s, double a, double b) { return std::array<double,3>{(double)s, a, b}; };
  static const std::vector<NiveauPoursuite> N = {
    {"AVEUGLE (aucune poursuite)", 0.0,  1.25, 0.06, {}, 0},
    {"30 min, 1 station",          0.07, 1.25, 0.50, {P(0,4000,5800)}, 0},
    {"3 h, 1 station",             0.47, 1.25, 0.40, {P(0,3600,15000)}, 0},
    {"1 arc apres chaque manoeuvre",1.68,1.25, 0.40,
      {P(0,3600,15000),P(2,21000,36000),P(2,64000,78000)}, 0},
    {"3 stations, arcs courts",    5.05, 1.25, 0.85,
      {P(0,3600,15000),P(1,3600,15000),P(2,3600,15000),
       P(0,21000,36000),P(1,21000,36000),P(2,21000,36000),
       P(0,64000,78000),P(1,64000,78000),P(2,64000,78000)}, 0},
    {"3 stations, arcs COMPLETS",  10.8, 1.25, 0.90,
      {P(0,3600,15000),P(1,3600,15000),P(2,3600,15000),
       P(0,21000,58000),P(1,21000,58000),P(2,21000,58000),
       P(0,65000,103000),P(1,65000,103000),P(2,65000,103000)}, 0},
    {"tout + 2 revolutions d'attente", 32.9, 4.3, 0.975,
      {P(0,3600,15000),P(1,3600,15000),P(2,3600,15000),
       P(0,21000,58000),P(1,21000,58000),P(2,21000,58000),
       P(0,65000,280000),P(1,65000,280000),P(2,65000,280000)}, 2},
  };
  return N[(lvl < 0) ? 0 : (lvl > 6 ? 6 : lvl)];
}

// ---------------------------------------------------------------------------
// La comete cible (contrat Rosetta-like) : orbite keplerienne heliocentrique
// FIXE, propagee exactement par kepler_propagate. Elements proches de 67P.
// ---------------------------------------------------------------------------
namespace {
astro::Elements comete_elements() {
  astro::Elements el{};
  el.a = 3.462 * AU; el.e = 0.641; el.i = 7.04 * DEG;
  el.raan = 50.1 * DEG; el.argp = 12.8 * DEG; el.nu = 0.0;   // au perihelie a t_ref
  return el;
}
constexpr double COMETE_TREF = 0.0;   // s TDB : nu=0 a J2000 (choix de scenario)

astro::KeplerResult comete_state_at(double t) {
  Vec3 r0, v0;
  astro::elements_to_rv(comete_elements(), MU_SUN, r0, v0);
  return astro::kepler_propagate(r0, v0, t - COMETE_TREF, MU_SUN);
}
} // namespace

// ---------------------------------------------------------------------------
// Jeu : construction, catalogues de gestion
// ---------------------------------------------------------------------------
Jeu::Jeu() {
  generer_contrats();
  installations = {
    {"dsn",  "Antenne DSN dediee",  "poursuite -20 %",
      "Tu possedes ton antenne au lieu de louer du reseau : chaque campagne de\n"
      "poursuite (la NAVIGATION, ce qui fait passer une mission de 6 % a 97 %)\n"
      "coute 20 % de moins. Rentable des la 3e mission.", 12.0, 0.15, false},
    {"banc", "Banc d'essai a feu",  "essais moteur -35 %",
      "Tester un moteur achete de la FIABILITE (courbe R(h)). Avec ton banc,\n"
      "l'heure d'essai coute 35 % de moins : tu montes la P(succes) pour moins cher.", 9.0, 0.10, false},
    {"calc", "Grappe de calcul",    "temps de calcul -50 %",
      "La FIDELITE se paie en calcul (porkchop, Monte-Carlo, matrice). Ta grappe\n"
      "divise ce cout par deux : tu peux MESURER au lieu de supposer.", 7.0, 0.08, false},
    {"integ","Hall d'integration",  "integration -1 mois",
      "Assembler le vaisseau prend du temps. Ton hall gagne 1 mois sur CHAQUE\n"
      "programme : precieux quand un client impose un delai serre.", 15.0, 0.12, false},
    {"labo", "Laboratoire d'echantillons", "x2 valeur des echantillons",
      "Analyser les echantillons cometaires sur place double leur valeur a la\n"
      "revente. Utile seulement si tu ramenes des echantillons.", 6.0, 0.05, false},
  };
  recherches = {
    {"nav2", "Navigation avancee",
      "Filtre a etats augmentes : la poursuite achete plus d'observabilite.",
      "EFFET : +4 points de P(physique) au catalogue, a poursuite egale. Concretement\n"
      "tu reussis plus souvent sans payer un niveau de poursuite de plus.", "", 10.0, 3.0, 0, false, false},
    {"isru", "Prospection ISRU",
      "Prospection in-situ : debloque et valorise le retour d'echantillons.",
      "EFFET : double la valeur scientifique des missions de retour. Prerequis de\n"
      "l'aerocapture.", "", 14.0, 4.0, 0, false, false},
    {" deep","Espace profond",
      "Antennes grand gain : le budget de liaison s'ameliore avec la distance.",
      "EFFET : double le retour de donnees (Gbit/mois) de tes sondes lointaines.\n"
      "C'est de la physique (gain d'antenne G_r dans l'equation de liaison).", "nav2", 18.0, 5.0, 0, false, false},
    {"aero", "Aerocapture",
      "Freinage atmospherique a l'arrivee planetaire (modele DECLARE).",
      "EFFET : -20 % sur le Delta-v d'insertion a Mars. L'atmosphere freine ; en\n"
      "echange le corridor d'entree est plus etroit (la marge le paie).", "isru", 22.0, 6.0, 0, false, false},
    {"proc", "Procedes de fabrication",
      "Chaines d'integration matures : les moteurs coutent moins et arrivent plus vite.",
      "EFFET : -20 % sur le prix des moteurs et -2 mois sur leur delai de livraison.\n"
      "Du prix et du delai, jamais la physique.", "", 12.0, 4.0, 0, false, false},
    {"extr", "Extraction amelioree",
      "Collecteurs a haut rendement pour la poussiere cometaire et les aerosols.",
      "EFFET : +50 % d'echantillons ramenes par les collecteurs. Se cumule avec le\n"
      "laboratoire (qui double le PRIX de vente).", "isru", 10.0, 3.0, 0, false, false},
  };
  acheteurs = {
    {"Agence continentale", "institutionnel : achete tout, prix moyens", 1.0, 1.0, 30, 3},
    {"LabCorp Exo",         "laboratoire : paie cher les echantillons",  0.7, 2.2, 10, 8},
    {"OrbitalMedia",        "entreprise : paie cher les donnees",        1.6, 0.5, 22, 1.5},
  };
  rafraichir_marche();
}
Jeu::~Jeu() { if (worker && worker->joinable()) worker->join(); }

bool Jeu::recherche_faite(const std::string& id) const {
  for (const auto& r : recherches) if (r.id == id && r.faite) return true;
  return false;
}

// --- effets (prix/delais seulement, jamais la physique) ---
double Jeu::m_poursuite() const {
  double f = 1.0;
  for (const auto& i : installations) if (i.id == "dsn" && i.construite) f *= 0.80;
  return f;
}
double Jeu::m_essais() const {
  double f = 1.0;
  for (const auto& i : installations) if (i.id == "banc" && i.construite) f *= 0.65;
  return f;
}
double Jeu::m_calcul() const {
  double f = 1.0;
  for (const auto& i : installations) if (i.id == "calc" && i.construite) f *= 0.50;
  return f;
}
double Jeu::mois_integration_eff() const {
  double m = 4.0;
  for (const auto& i : installations) if (i.id == "integ" && i.construite) m -= 1.0;
  return std::max(2.0, m);
}
// REPUTATION : reference = confiance 0,70 (neutre, valeur de depart). Au-dessus, le
// client accorde un peu de mou ; en dessous, il durcit. Ajustement borne a
// [-0,05 ; +0,10] et exigence finale plafonnee a 0,985 -> pas de spirale infaisable.
double Jeu::exigence_client(double base_min_p) const {
  const double adj = std::min(0.10, std::max(-0.05, (0.70 - agence.confiance) * 0.15));
  return std::min(0.985, std::max(0.50, base_min_p + adj));
}
// le mode d'aide fixe le prix de la DIVISION ANALYSE (aide humaine), jamais la physique
double Jeu::prix_analyse() const {
  const double base = 0.6 * m_calcul();
  switch (agence.mode) {
    case ModeAide::Normal: return base;         // l'aide existe : au prix de base
    case ModeAide::Pro:    return base * 1.5;   // sans aide : l'analyse coute plus cher
  }
  return base;
}

// ---------------------------------------------------------------------------
// Contrats
// ---------------------------------------------------------------------------
void Jeu::generer_contrats() {
  contrats.clear();
  { Contrat c; c.id="M00"; c.titre="GEO-SAT 1 : la manoeuvre qui ferme le bilan";
    c.client="OpSat Communications";
    c.resume="Livrer 1200 kg en orbite geostationnaire. Le cahier des charges ne "
      "donne AUCUN Delta-v : a l'agence de les deriver. Depart en parking 200 km / "
      "28,5 deg, au noeud ascendant.";
    c.spec={1200.0,115.0,18.0,0.85}; c.prime_succes=12.0; c.penalite_echec=30.0;
    contrats.push_back(c); }
  { Contrat c; c.id="M00b"; c.titre="GEO-SAT 2 : le client presse";
    c.client="TelStar Group";
    c.resume="1000 kg en GEO, 95 M$, mais 14 mois seulement. Le moteur a haut Isp "
      "n'arrivera jamais a temps : il va falloir choisir ce qu'on sacrifie.";
    c.spec={1000.0,95.0,14.0,0.80}; c.prime_succes=10.0; c.penalite_echec=25.0;
    contrats.push_back(c); }
  { Contrat c; c.id="E-M01"; c.titre="Etude : la fenetre et le corridor de Mars";
    c.client="Agence d'exploration"; c.type=TypeContrat::EtudeMars;
    c.resume="CONTRAT D'ANALYSE (pas de lancement, pas de vaisseau, AUCUN risque de "
      "perdre une sonde) : un client te paie pour un travail de bureau d'etudes. Tu "
      "calcules et tu LIVRES deux documents : (1) la carte porkchop Terre->Mars (les "
      "meilleures dates/durees de depart) et (2) le corridor du plan-B (la marge a "
      "l'arrivee). Le calcul se paie en temps de calcul ; a la livraison tu encaisses "
      "9 M$. C'est le contrat ideal pour renflouer l'agence sans rien risquer - et pour "
      "apprendre a lire une porkchop avant de voler vers Mars.";
    c.spec={0.0,9.0,6.0,0.0}; c.prime_succes=0.0; c.penalite_echec=0.0;
    contrats.push_back(c); }
  { Contrat c; c.id="V-M02"; c.titre="Orbiteur de Mars : franchir le corridor";
    c.client="Consortium areologique"; c.type=TypeContrat::VolMars;
    c.resume="Placer une sonde en orbite de Mars. Tu concois la trajectoire sur la "
      "carte (Lambert exact), tu voles, et l'insertion se joue dans le CORRIDOR du "
      "plan-B : ton ellipse 3-sigma doit tenir dans l'anneau, sinon Oberth te punit.";
    c.spec={900.0,180.0,40.0,0.75}; c.prime_succes=45.0; c.penalite_echec=60.0;
    contrats.push_back(c); }
  { Contrat c; c.id="V-C01"; c.titre="Rendez-vous cometaire (type Rosetta)";
    c.client="Institut des origines"; c.type=TypeContrat::VolComete;
    c.resume="Atteindre une comete (a=3,46 UA, e=0,64) et passer sous 1000 km au plus "
      "pres. C3 eleve : la fenetre et le choix de transit decident tout. Un collecteur "
      "de poussiere (optionnel) rapporte des echantillons.";
    c.spec={450.0,240.0,60.0,0.60}; c.prime_succes=80.0; c.penalite_echec=40.0;
    contrats.push_back(c); }
  { Contrat c; c.id="T-01"; c.titre="TITAN : la mission exobiologique";
    c.client="Programme des mondes oceaniques"; c.type=TypeContrat::VolTitan;
    c.resume="Le graal : atteindre le systeme de Saturne (9,5 UA) et survoler Titan sous "
      "2000 km pour echantillonner son atmosphere. Le C3 direct est enorme (>100 km2/s2) : "
      "sans assistances gravitationnelles il faut un lanceur lourd et un budget colossal. "
      "Avec, ca devient raisonnable. C'est LA mission qui recompense la maitrise du "
      "calendrier et des survols.";
    c.spec={350.0,300.0,120.0,0.55}; c.prime_succes=140.0; c.penalite_echec=50.0;
    contrats.push_back(c); }
}

double Jeu::revenu_mensuel_gbit() const {
  const double f = recherche_faite(" deep") ? 2.0 : 1.0;
  return relais_geo * 1.2 * f + orbiteurs_mars * 0.8 * f + sondes_lointaines * 0.5 * f;
}
double Jeu::epoch_courant() const {
  // calendrier illustratif : la carte n'est pas la verite, elle situe les corps
  return epoch_from_iso("2027-03-14T00:00:00").tdb + agence.mois * 30.44 * DAY;
}

void Jeu::offrir_apres_mission(bool reussi) {
  const int no = 6 + agence.reussites + agence.echecs;
  Contrat n;
  n.id = "GEO-" + std::to_string(no);
  n.titre = "GEO-SAT " + std::to_string(no) + " : nouvelle offre";
  n.client = reussi ? "OpSat (client renouvele)" : "NovaCom (ils tentent leur chance)";
  n.resume = "Le marche GEO continue : meme physique, nouveau contrat. Budget et "
             "calendrier varient - ils ne pardonnent toujours pas.";
  n.spec = {1000.0 + 100.0 * ((no * 37) % 5), 105.0 + 4.0 * ((no * 13) % 4), 17.0, 0.85};
  n.prime_succes = 11.0; n.penalite_echec = 28.0;
  contrats.push_back(n);
  agence.log("Nouvel appel d'offres GEO : " + n.id + ".");
}

void Jeu::creer_agence(const std::string& nom, ModeAide mode) {
  agence = Agence{};
  agence.creee = true;
  agence.mode = mode;
  agence.nom = nom.empty() ? "AGENCE SANS NOM" : nom;
  agence.graine_agence = 0x5DEECE66DULL;
  for (char ch : agence.nom) agence.graine_agence = agence.graine_agence * 131 + (unsigned char)ch;
  agence.tresorerie = (mode == ModeAide::Pro) ? 32.0 : 45.0;
  agence.log("Agence fondee. Dotation : " + std::to_string((int)agence.tresorerie) + " M$.");
  agence.log(mode == ModeAide::Pro
    ? "Mode PRO : aucune aide. Tu realises tous les calculs toi-meme."
    : "Mode NORMAL : l'assistant est disponible pour te guider dans les calculs.");
}

// ---------------------------------------------------------------------------
// Gestion : le TOUR mensuel, construction, recherche, ventes
// ---------------------------------------------------------------------------
void Jeu::passer_mois() {
  if (game_over) return;
  agence.mois += 1.0;
  // --- COUTS RECURRENTS : salaires, entretien, maintenance de la flotte,
  //     location du pas de tir pendant un vol commis. C'est la pression du temps.
  double fixe = 0.6;                                   // salaires de l'equipe
  for (const auto& i : installations) if (i.construite) fixe += i.entretien;
  fixe += relais_geo * 0.05 + orbiteurs_mars * 0.12 + sondes_lointaines * 0.20;   // operations flotte
  if ((vol.commis && !vol.fini) || (vinterp.commis && !vinterp.fini)) fixe += 0.25;  // pas de tir / ops vol
  depense_obligatoire(fixe, "charges du mois (salaires, entretien, flotte)");
  if (game_over) return;
  // --- revenus science ---
  double gbit = revenu_mensuel_gbit();
  if (gbit > 0) { donnees_gbit += gbit;
    char b[96]; std::snprintf(b, sizeof(b), "+%.1f Gbit de donnees (flotte en orbite)", gbit);
    agence.log(b); }
  // --- recherche ---
  for (auto& r : recherches) if (r.active && !r.faite) {
    r.avancement += 1.0;
    if (r.avancement >= r.mois_requis) {
      r.faite = true; r.active = false;
      agence.log("Recherche TERMINEE : " + r.nom + ".");
    }
  }
  // --- ECHEANCES : un contrat signe et non livre a une date limite ---
  for (auto& c : contrats) {
    if (!c.accepte || c.termine || c.mois_signature < 0) continue;
    const bool en_vol = (contrat_actif >= 0 && &c == &contrats[contrat_actif]) &&
                        ((vol.commis && !vol.fini) || (vinterp.commis && !vinterp.fini));
    if (en_vol) continue;   // le vol est parti : le client attend le verdict
    if (agence.mois - c.mois_signature > c.spec.deadline_months) {
      c.termine = true; c.reussi = false;
      depense_obligatoire(c.penalite_echec * 0.5, "resiliation " + c.id + " (echeance depassee)");
      agence.confiance = std::max(0.0, agence.confiance - 0.08);
      agence.log("Le client de " + c.id + " a RESILIE : echeance depassee.");
      if (contrat_actif >= 0 && &c == &contrats[contrat_actif]) contrat_actif = -1;
    }
  }
  rafraichir_marche();
}
void Jeu::construire(int i) {
  if (i < 0 || i >= (int)installations.size()) return;
  auto& b = installations[i];
  if (b.construite) return;
  if (!payer(b.cout, "construction : " + b.nom)) return;
  agence.mois += 1.0;
  b.construite = true;
  agence.log(b.nom + " operationnelle : " + b.effet + ".");
  if (contrat_actif >= 0 && !contrats[contrat_actif].termine) recalculer_conception();
}
void Jeu::lancer_recherche(int i) {
  if (i < 0 || i >= (int)recherches.size()) return;
  auto& r = recherches[i];
  if (r.faite || r.active) return;
  if (!r.prereq.empty() && !recherche_faite(r.prereq)) {
    erreur = "Prerequis manquant : " + r.prereq; return;
  }
  for (auto& o : recherches) if (o.active) { erreur = "Une recherche est deja en cours."; return; }
  if (!payer(r.cout, "lancement recherche : " + r.nom)) return;
  r.active = true;
  agence.log("Recherche lancee : " + r.nom + " (" + std::to_string((int)r.mois_requis) + " mois).");
}

// ---------------------------------------------------------------------------
// Accepter un contrat + construire l'assistant de derivation
// ---------------------------------------------------------------------------
void Jeu::construire_wizard() {
  auto& w = conception.wizard;
  w.clear();
  const auto d = deriver_m00(R_PARK, I_PARK, R_GEO);
  w.push_back({"1. Vitesse circulaire sur le parking (r = 6578,1 km) ?",
               "v = sqrt(mu / r)", "mu_Terre = 398600,4 km3/s2", d.v_circ, 0, false, false});
  w.push_back({"2. Vitesse au perigee du GTO (a = (r_park+r_geo)/2) ?",
               "v = sqrt(mu (2/r - 1/a))", "c'est vis-viva au perigee", d.v_gto_peri, 0, false, false});
  w.push_back({"3. Delta-v d'injection ?",
               "dv_inj = v_gto_peri - v_circ", "la difference des deux precedentes", d.dv_inj, 0, false, false});
  w.push_back({"4. Vitesse a l'apogee du GTO (r = r_geo) ?",
               "v = sqrt(mu (2/r - 1/a))", "vis-viva a l'apogee", d.v_gto_apo, 0, false, false});
  w.push_back({"5. Vitesse GEO circulaire ?",
               "v = sqrt(mu / r_geo)", "circulaire a 42164 km", d.v_geo, 0, false, false});
  w.push_back({"6. Insertion + plan COMBINES en une impulsion a l'apogee ?",
               "dv = sqrt(v_apo^2 + v_geo^2 - 2 v_apo v_geo cos i)",
               "LOI DES COSINUS, pas une addition. i = 28,5 deg.", d.dv_comb, 0, false, false});
  conception.dv_inj_joueur = 0; conception.dv_comb_joueur = 0;
}

void Jeu::accepter_contrat(int idx) {
  if (idx < 0 || idx >= (int)contrats.size()) return;
  Contrat& c = contrats[idx];
  if (c.accepte || c.termine) return;
  if (c.type == TypeContrat::EtudeMars) {
    c.accepte = true;
    c.mois_signature = agence.mois;
    agence.log("Etude " + c.id + " acceptee : payee a la LIVRAISON (echeance " +
               std::to_string((int)c.spec.deadline_months) + " mois).");
    return;
  }
  if (contrat_actif >= 0 && !contrats[contrat_actif].termine) {
    erreur = "Un programme est deja en cours : livre-le d'abord."; return;
  }
  contrat_actif = idx;
  c.accepte = true;
  c.mois_signature = agence.mois;
  cout_programme = 0;               // la MARGE de la mission se mesure d'ici
  agence.encaisser(c.spec.budget_musd, "budget du contrat " + c.id + " (a la signature)");
  char b[128]; std::snprintf(b, sizeof(b), "Echeance du contrat %s : T+%.0f mois. Le client n'attendra pas.",
                             c.id.c_str(), agence.mois + c.spec.deadline_months);
  agence.log(b);
  if (c.type == TypeContrat::VolGeo) {
    conception = Conception{};
    vol = Vol{};
    construire_wizard();
    recalculer_conception();
  } else {
    cinterp.reset();
    vinterp = VolInterp{};
    cinterp.moteur = 0;
    interp_recalculer();
  }
}

// ---------------------------------------------------------------------------
// Conception GEO
// ---------------------------------------------------------------------------
void Jeu::verifier_derivations() {
  Conception& k = conception;
  k.d = deriver_m00(R_PARK, I_PARK, R_GEO);
  k.derive_verifiee = true;
  const bool inj_ok  = std::fabs(k.dv_inj_joueur  - k.d.dv_inj)  <= 1e-3 * k.d.dv_inj;
  const bool comb_ok = std::fabs(k.dv_comb_joueur - k.d.dv_comb) <= 1e-3 * k.d.dv_comb;
  k.indice_separee = !comb_ok && std::fabs(k.dv_comb_joueur - k.d.dv_sep) < 1.0;
  k.derive_ok = inj_ok && comb_ok;
  if (k.derive_ok) agence.log("Derivations validees : la conception est ouverte.");
}
void Jeu::wizard_verifier(int e) {
  if (e < 0 || e >= (int)conception.wizard.size()) return;
  auto& w = conception.wizard[e];
  w.validee = std::fabs(w.reponse - w.valeur) <= 1e-3 * std::fabs(w.valeur);
  if (w.validee && e == 2) conception.dv_inj_joueur = w.valeur;   // injection
  if (w.validee && e == 5) conception.dv_comb_joueur = w.valeur;  // combinee
  // les deux cles justes -> derivation ok
  if (conception.wizard[2].validee && conception.wizard[5].validee) {
    conception.dv_inj_joueur = conception.wizard[2].valeur;
    conception.dv_comb_joueur = conception.wizard[5].valeur;
    verifier_derivations();
  }
}
void Jeu::wizard_reveler(int e) {
  if (e < 0 || e >= (int)conception.wizard.size()) return;
  if (agence.mode == ModeAide::Pro && !payer(0.1, "aide : etape revelee")) return;
  auto& w = conception.wizard[e];
  w.revelee = true; w.reponse = w.valeur; w.validee = true;
  if (e == 2) conception.dv_inj_joueur = w.valeur;
  if (e == 5) conception.dv_comb_joueur = w.valeur;
  if (conception.wizard[2].validee && conception.wizard[5].validee) {
    conception.dv_inj_joueur = conception.wizard[2].valeur;
    conception.dv_comb_joueur = conception.wizard[5].valeur;
    verifier_derivations();
  }
}

void Jeu::appliquer_bonus(mission::Assessment& a) const {
  // les installations changent des PRIX, pas la physique (deja pris en compte
  // via les multiplicateurs a l'appel). Rien a faire ici pour l'instant.
  (void)a;
}

// ---------------------------------------------------------------------------
// LE VAB : masses reelles (vehicle::Stage), Delta-v Tsiolkovski exact.
// ---------------------------------------------------------------------------
static const double AVIO_KG[3]  = {45, 70, 22};    // basique / redondante / miniaturisee
static const double AVIO_MUSD[3]= {2, 5, 9};
static const double STRU_KG[3]  = {95, 150, 230};  // legere / standard / renforcee
static const double STRU_MUSD[3]= {1.5, 2.5, 4.5};

static vehicle::Stage vab_etage(const Conception& k, double payload) {
  const auto& E = mission::engines()[k.moteur];
  vehicle::Stage s;
  s.engine = E.eng;
  s.tank.propellant_mass = k.vab.ergols;
  s.tank.dry_fraction = E.tank_dry_fraction;
  s.tank.residual_fraction = RESIDU;
  s.structure_mass = STRU_KG[k.vab.structure] + AVIO_KG[k.vab.avionique];
  (void)payload;
  return s;
}
static double vab_payload(const Jeu& j, const Conception& k) {
  const Contrat* c = j.actif();
  return (c ? c->spec.payload_kg : 1200.0) + (k.instrument ? 150.0 : 0.0) + (k.vab.antenne ? 40.0 : 0.0);
}
double Jeu::vab_masse_seche() const { return vab_etage(conception, 0).dry_mass(); }
double Jeu::vab_m0() const {
  vehicle::Vehicle v; v.payload_dry = vab_payload(*this, conception);
  v.stages.push_back(vab_etage(conception, v.payload_dry));
  return v.total_mass();
}
double Jeu::vab_dv() const {
  vehicle::Vehicle v; v.payload_dry = vab_payload(*this, conception);
  v.stages.push_back(vab_etage(conception, v.payload_dry));
  return v.stage_dv(0);
}
double Jeu::vab_duree_injection() const {
  // arc d'injection : dt ~ m0 * dv_inj / F (approximation a poussee constante).
  const auto& E = mission::engines()[conception.moteur];
  return vab_m0() * conception.d.dv_inj / std::max(1.0, E.eng.thrust_vac);
}
void Jeu::vab_dimensionner() {
  Conception& k = conception;
  const auto& E = mission::engines()[k.moteur];
  const double dv_design = k.d.dv_total_comb + k.perte_poussee_finie + k.marge_dv;
  auto sz = vehicle::size_stage_for_dv(dv_design, vab_payload(*this, k), E.eng,
      E.tank_dry_fraction, STRU_KG[k.vab.structure] + AVIO_KG[k.vab.avionique], RESIDU);
  if (sz.converged) k.vab.ergols = sz.propellant;
}

// ---------------------------------------------------------------------------
// UNE SEULE EVALUATION DE PROGRAMME, partagee par le bilan ET la matrice.
// (Le bug historique : la matrice n'appliquait pas les memes ajustements que
//  le bilan - hall d'integration, banc d'essai, nav2 - et rendait des verdicts
//  incoherents. Une seule fonction = plus de divergence possible.)
// ---------------------------------------------------------------------------
mission::Assessment Jeu::evaluer_geo(int moteur, int lanceur, int niveau,
                                     double heures, bool revue, double marge,
                                     double p_physique, bool instrument,
                                     const Vab* vab_joueur) const {
  using namespace mission;
  const Contrat& c = contrats[contrat_actif];
  const auto& E = engines()[moteur];
  const auto& niv = niveau_poursuite(niveau);
  const double dv_nominal = conception.d.dv_total_comb;
  const double perte = PERTE_FINIE_REL * dv_nominal;
  Assessment a;
  a.dv_design = dv_nominal + perte + marge;
  const double payload = c.spec.payload_kg + (instrument ? 150.0 : 0.0)
                       + (vab_joueur && vab_joueur->antenne ? 40.0 : 0.0);
  // masses : celles du VAB du joueur si fournies, sinon point fixe
  if (vab_joueur) {
    vehicle::Stage s; s.engine = E.eng;
    s.tank.propellant_mass = vab_joueur->ergols;
    s.tank.dry_fraction = E.tank_dry_fraction; s.tank.residual_fraction = RESIDU;
    s.structure_mass = STRU_KG[vab_joueur->structure] + AVIO_KG[vab_joueur->avionique];
    a.propellant_kg = vab_joueur->ergols;
    a.dry_kg = s.dry_mass();
    a.m0_kg = payload + s.wet_mass();
  } else {
    auto sz = vehicle::size_stage_for_dv(a.dv_design, payload, E.eng,
                                         E.tank_dry_fraction, 150.0, RESIDU);
    a.propellant_kg = sz.propellant; a.dry_kg = sz.stage_dry; a.m0_kg = sz.m0;
    if (!sz.converged) { a.why = "DELTA-V INFAISABLE EN UN ETAGE"; return a; }
  }
  // lanceur
  if (lanceur >= 0) {
    if (launchers()[lanceur].payload_leo < a.m0_kg) { a.why = "LE LANCEUR CHOISI NE SOULEVE PAS"; return a; }
    a.launcher_index = lanceur;
  } else {
    double best = 1e300;
    for (std::size_t i = 0; i < launchers().size(); ++i) {
      const auto& L = launchers()[i];
      if (L.payload_leo >= a.m0_kg && L.cost_musd < best) { best = L.cost_musd; a.launcher_index = (int)i; }
    }
  }
  if (a.launcher_index < 0) { a.why = "AUCUN LANCEUR NE SOULEVE"; return a; }
  a.fits_mass = true;
  const auto& L = launchers()[a.launcher_index];
  // argent (avec les effets des installations et de la recherche "procedes")
  const double m_moteur = recherche_faite("proc") ? 0.8 : 1.0;
  a.cost_launcher = L.cost_musd;
  a.cost_engine   = (E.unit_cost_musd + E.dev_cost_musd) * m_moteur;
  a.cost_stage    = COST_STAGE_FIXED + COST_PER_KG_DRY * a.dry_kg
                  + AVIO_MUSD[vab_joueur ? vab_joueur->avionique : 0]
                  + STRU_MUSD[vab_joueur ? vab_joueur->structure : 1]
                  + (vab_joueur && vab_joueur->antenne ? 4.0 : 0.0);
  a.cost_tracking = niv.cout_musd * m_poursuite();
  a.cost_tests    = COST_TEST_PER_H * heures * m_essais();
  a.cost_review   = revue ? COST_REVIEW : 0.0;
  a.cost_compute  = 0.0;
  // temps (avec hall d'integration et delai moteur reduit par "procedes")
  const double lead_moteur = E.lead_months - (recherche_faite("proc") ? 2.0 : 0.0);
  a.schedule_months = std::max(lead_moteur, L.lead_months) + mois_integration_eff() + niv.jours / 30.44;
  a.cost_ops = COST_OPS_PER_MONTH * a.schedule_months;
  a.cost_total = a.cost_launcher + a.cost_engine + a.cost_stage + a.cost_tracking
               + a.cost_compute + a.cost_tests + a.cost_review + a.cost_ops
               + (instrument ? 8.0 : 0.0);
  a.fits_budget   = a.cost_total <= c.spec.budget_musd;
  a.fits_schedule = a.schedule_months <= c.spec.deadline_months;
  // risque
  a.p_launcher = L.reliability;
  a.p_engine   = std::pow(engine_reliability(E, heures), N_ALLUMAGES);
  a.p_blunder  = revue ? P_BLUNDER_REVIEW : P_BLUNDER_NO_REVIEW;
  mission::Contract spec_eff = c.spec;               // exigence modulee par la reputation
  spec_eff.min_success_prob = exigence_client(c.spec.min_success_prob);
  finalize(a, spec_eff, p_physique);
  return a;
}

void Jeu::recalculer_conception() {
  Conception& k = conception;
  if (contrat_actif < 0) return;
  const Contrat& c = contrats[contrat_actif];
  if (c.type != TypeContrat::VolGeo) return;
  k.d = deriver_m00(R_PARK, I_PARK, R_GEO);
  const auto& niv = niveau_poursuite(k.niveau);
  double p_cat = niv.p_physique_catalogue;
  if (recherche_faite("nav2")) p_cat = std::min(0.99, p_cat + 0.04);
  if (c.id == "M00") p_cat = std::max(p_cat, 0.99);  // vol de REPETITION : execution nominale
  if (!k.p_mesuree) k.p_physique = p_cat;
  k.perte_poussee_finie = PERTE_FINIE_REL * k.d.dv_total_comb;
  if (k.vab_auto) vab_dimensionner();          // point fixe -> ergols exacts
  k.bilan = evaluer_geo(k.moteur, k.lanceur, k.niveau, k.heures_essai, k.revue,
                        k.marge_dv, k.p_physique, k.instrument, &k.vab);
  // contrainte VAB : le Delta-v de l'etage assemble doit couvrir le besoin
  // (tolerance 0,5 m/s : le point fixe converge a l'epsilon pres)
  if (k.bilan.fits_mass && vab_dv() < k.bilan.dv_design - 0.5) {
    k.bilan.fits_risk = false; k.bilan.ok = false;
    char b[120]; std::snprintf(b, sizeof(b), "DELTA-V INSUFFISANT : l'etage donne %.0f m/s, il en faut %.0f",
                               vab_dv(), k.bilan.dv_design);
    k.bilan.why = b;
  }
  k.taille.propellant = k.bilan.propellant_kg;
  k.taille.stage_dry = k.bilan.dry_kg;
  k.taille.m0 = k.bilan.m0_kg;
  k.taille.converged = true;
}

void Jeu::acheter_matrice() {
  if (conception.matrice_achetee) return;
  if (!payer(PRIX_MATRICE * m_calcul(), "division analyse : matrice de programme")) return;
  agence.mois += 0.1;
  conception.matrice_achetee = true;
  matrice.clear();
  if (contrat_actif < 0) return;
  // MEME evaluation que le bilan (evaluer_geo), en mode point-fixe (vab=nullptr) :
  // la matrice compare des CONFIGURATIONS, pas ton reservoir du moment.
  for (int m = 0; m < (int)mission::engines().size(); ++m)
    for (int l = 0; l <= 6; ++l) {
      double p_cat = niveau_poursuite(l).p_physique_catalogue;
      if (recherche_faite("nav2")) p_cat = std::min(0.99, p_cat + 0.04);
      auto a = evaluer_geo(m, -1, l, conception.heures_essai, conception.revue,
                           conception.marge_dv, p_cat, conception.instrument, nullptr);
      matrice.push_back({a, m, l});
    }
  recalculer_conception();
}

// ---------------------------------------------------------------------------
// Monte-Carlo : voler une fois, boucle fermee (transpose verifie de m00_play)
// ---------------------------------------------------------------------------
namespace {
struct ResVol { bool valide{false}, ok{false}; double a{}; };
force::ForceStack pile_terre(const ephem::IEphemeris& eph) {
  force::ForceStack G;
  G.add(std::make_shared<force::CentralGravity>(MU_EARTH));
  G.add(std::make_shared<force::ThirdBodyGravity>(&eph, ephem::Body::Sun, ephem::Body::EarthBary));
  G.add(std::make_shared<force::ThirdBodyGravity>(&eph, ephem::Body::Moon, ephem::Body::EarthBary));
  return G;
}
struct Prediction { double t{}; StateN y{}; bool ok{false}; };
Prediction predire(const force::ForceStack& G, const flight::Observation& o,
                   const char* seul, double saut) {
  const auto el = astro::rv_to_elements(o.state.r, o.state.v, MU_EARTH);
  const double T = astro::orbital_period(el.a, MU_EARTH);
  if (!(T > 0)) return {};
  StateN y{o.state.r.x, o.state.r.y, o.state.r.z, o.state.v.x, o.state.v.y, o.state.v.z, o.state.m};
  prop::PropOptions po; po.step.rtol = 1e-11; po.sample_dt = 0;
  auto r0 = prop::propagate(G, o.t, y, o.t + saut * T, {}, po);
  auto r = prop::propagate(G, o.t + saut * T, r0.y_final, o.t + saut * T + 1.1 * T,
                           {prop::event_periapsis(MU_EARTH), prop::event_apoapsis(MU_EARTH)}, po);
  for (auto& ev : r.events) if (!seul[0] || ev.name == seul) return {ev.t, ev.y, true};
  return {};
}
// Comme predire, mais renvoie l'apside dont le RAYON est le plus proche de R_GEO
// (et non le premier rencontre). La circularisation finale doit viser l'apside QUI
// EST a R_GEO : sinon on circularise au mauvais rayon et `a` sort de tolerance.
// (Bug corrige le 16/07/2026 : mesure sur m00_probe, cf. m00-tutoriel-non-gagnable.)
Prediction predire_rgeo(const force::ForceStack& G, const flight::Observation& o, double saut) {
  const auto el = astro::rv_to_elements(o.state.r, o.state.v, MU_EARTH);
  const double T = astro::orbital_period(el.a, MU_EARTH);
  if (!(T > 0)) return {};
  StateN y{o.state.r.x, o.state.r.y, o.state.r.z, o.state.v.x, o.state.v.y, o.state.v.z, o.state.m};
  prop::PropOptions po; po.step.rtol = 1e-11; po.sample_dt = 0;
  auto r0 = prop::propagate(G, o.t, y, o.t + saut * T, {}, po);
  auto r = prop::propagate(G, o.t + saut * T, r0.y_final, o.t + saut * T + 1.15 * T,
                           {prop::event_periapsis(MU_EARTH), prop::event_apoapsis(MU_EARTH)}, po);
  Prediction best; double best_d = 1e30;
  for (auto& ev : r.events) {
    const double d = std::fabs(norm(Vec3{ev.y[0], ev.y[1], ev.y[2]}) - R_GEO);
    if (d < best_d) { best_d = d; best = {ev.t, ev.y, true}; }
  }
  return best;
}
Vec3 v_cible_gto(const Vec3& r) {
  const double a = 0.5 * (norm(r) + R_GEO);
  return unit(cross(Vec3{0,0,1}, r)) * astro::vis_viva(norm(r), a, MU_EARTH);
}
Vec3 v_cible_circ(const Vec3& r) {
  return unit(cross(Vec3{0,0,1}, r)) * astro::v_circular(norm(r), MU_EARTH);
}
ResVol vol_mc(const io::FplDocument& doc, const ephem::IEphemeris& eph,
              std::uint64_t graine, const NiveauPoursuite& N, bool rehearsal = false) {
  prop::PropOptions opt; opt.step.rtol = 1e-11; opt.sample_dt = 0.0;
  flight::Session S(doc.plan, eph, graine, opt);
  if (rehearsal) S.set_gates_enabled(false);   // repetition tutoriel : execution nominale
  const double t0 = doc.plan.epoch0;
  for (auto& p : N.passes) { nav::Pass q; q.station=(int)p[0]; q.t_start=t0+p[1]; q.t_end=t0+p[2]; q.sample_dt=60; S.schedule_pass(q); }
  auto G = pile_terre(eph);
  ResVol v;
  auto bruler = [&](const Prediction& p, const Vec3& vt) {
    flight::BurnCmd b; b.id="MC"; b.t=p.t; b.frame=flight::DvFrame::Inertial;
    b.hold=force::ThrustFrame::InertialFixed; b.stage=0; b.dv=vt-vel(p.y); S.commit_burn(b);
  };
  S.commit_burn(doc.plan.burns[0]); if (!S.alive()) return v;
  S.advance_to(t0 + 15200);
  auto o = S.observe();
  auto p1 = predire(G, o, "APOAPSIS", 0.0); if (!p1.ok) return v;
  bruler(p1, v_cible_gto(pos(p1.y))); if (!S.alive()) return v;
  auto e2 = astro::rv_to_elements(S.observe().state.r, S.observe().state.v, MU_EARTH);
  if (!(e2.a > 0)) return v;
  S.advance_to(S.t() + 0.38 * astro::orbital_period(e2.a, MU_EARTH));
  o = S.observe();
  auto p2 = predire(G, o, "", 0.02); if (!p2.ok) return v;
  bruler(p2, v_cible_gto(pos(p2.y))); if (!S.alive()) return v;
  auto e3 = astro::rv_to_elements(S.observe().state.r, S.observe().state.v, MU_EARTH);
  if (!(e3.a > 0)) return v;
  S.advance_to(S.t() + (0.38 + N.revolutions_sup) * astro::orbital_period(e3.a, MU_EARTH));
  o = S.observe();
  auto p3 = predire_rgeo(G, o, 0.02); if (!p3.ok) return v;   // circularise a l'apside a R_GEO
  bruler(p3, v_cible_circ(pos(p3.y))); if (!S.alive()) return v;
  S.advance_to(S.t() + 2000);
  const auto tr = S.truth_state();
  const auto el = astro::rv_to_elements(tr.r, tr.v, MU_EARTH);
  v.valide = true; v.a = el.a;
  v.ok = std::fabs(el.a - R_GEO) < 50e3 && el.e < 2e-3 && el.i / DEG < 0.25;
  return v;
}
} // namespace

void Jeu::mesurer_p_physique(int n_vols) {
  if (mc_en_cours || contrat_actif < 0) return;
  if (!payer(PRIX_MC_PAR_VOL * n_vols * m_calcul(),
             "division analyse : Monte-Carlo (" + std::to_string(n_vols) + " vols)")) return;
  agence.mois += 0.002 * n_vols;
  ecrire_fpl("plan_agence.fpl");
  auto doc = std::make_shared<io::FplDocument>(io::parse_fpl("plan_agence.fpl"));
  mc_total = n_vols; mc_fait = 0; mc_resultat = -1;
  const int niveau = conception.niveau;
  const bool rehe = contrats[contrat_actif].id == "M00";   // tutoriel : vol de repetition
  if (worker && worker->joinable()) worker->join();
  mc_en_cours = true;
  worker = std::make_unique<std::thread>([this, doc, n_vols, niveau, rehe] {
    const auto& N = niveau_poursuite(niveau);
    int ok = 0;
    for (int k = 0; k < n_vols; ++k) { auto r = vol_mc(*doc, eph, 900 + k, N, rehe); if (r.valide && r.ok) ++ok; mc_fait = k + 1; }
    mc_resultat = (n_vols > 0) ? double(ok) / n_vols : 0.0;
    mc_en_cours = false;
  });
}
void Jeu::encaisser_mc() {
  if (mc_en_cours || mc_resultat < 0) return;
  conception.p_physique = mc_resultat; conception.p_mesuree = true;
  char b[128]; std::snprintf(b, sizeof(b), "P(physique) MESUREE : %.0f %% (n=%d)", 100.0 * mc_resultat, mc_total);
  agence.log(b); mc_resultat = -1; recalculer_conception();
}

// ---------------------------------------------------------------------------
// .fpl
// ---------------------------------------------------------------------------
void Jeu::ecrire_fpl(const std::string& chemin) const {
  const Conception& k = conception;
  const Contrat& c = contrats[contrat_actif];
  const auto& E = mission::engines()[k.moteur];
  const double dv_inj_cmd = k.d.dv_inj * (1.0 + PERTE_FINIE_REL);
  FILE* f = std::fopen(chemin.c_str(), "w"); if (!f) return;
  std::fprintf(f, "# genere par l'agence %s\n", agence.nom.c_str());
  std::fprintf(f, "MISSION      %s\nCENTER EARTH\nPERTURBERS SUN MOON\n", c.id.c_str());
  std::fprintf(f, "EPOCH        %s\n", epoch_to_iso(Epoch{epoch_courant()}).c_str());
  std::fprintf(f, "ENGINE       id=MOTEUR thrust=%.1fkN isp=%.1fs mass=%.0fkg mr=%.2f-\n",
               E.eng.thrust_vac/1000, E.eng.isp_vac, E.eng.mass, E.eng.mixture_ratio);
  std::fprintf(f, "STAGE        id=ETAGE engine=MOTEUR propellant=%.0fkg tank_dry_frac=%.2f- \\\n",
               k.bilan.propellant_kg, E.tank_dry_fraction);
  std::fprintf(f, "             structure=%.0fkg residual=%.2f-\n", STRUCT_KG, RESIDU);
  std::fprintf(f, "PAYLOAD      %.0fkg\n", c.spec.payload_kg + (k.instrument ? 150.0 : 0.0));
  std::fprintf(f, "ELEMENTS     sma=%.3fkm ecc=0- inc=%.1fdeg raan=0deg argp=0deg ta=0deg\n",
               R_PARK/1000, I_PARK/DEG);
  std::fprintf(f, "BURN         id=GTO t=0s frame=RSW hold=INERTIAL dv=[0,%.1f,0]m/s stage=0\n", dv_inj_cmd);
  std::fprintf(f, "GOAL sma=%.2fkm tol=%.0fkm\nGOAL ecc<%.0e-\nGOAL inc<%.2fdeg\nSTOP t=500000s\n",
               c.cible_sma/1000, c.tol_sma/1000, c.tol_ecc, c.tol_inc_deg);
  std::fclose(f);
}

// ---------------------------------------------------------------------------
// COMMIT GEO
// ---------------------------------------------------------------------------
bool Jeu::commit() {
  if (contrat_actif < 0) { erreur = "Aucun contrat."; return false; }
  recalculer_conception();
  Conception& k = conception;
  const Contrat& c = contrats[contrat_actif];
  if (!k.bilan.fits_mass) { erreur = "Aucun lanceur ne souleve ce vehicule."; return false; }
  if (!k.derive_ok) { erreur = "Derive d'abord tes Delta-v (ou utilise l'assistant)."; return false; }
  if (k.bilan.p_success < exigence_client(c.spec.min_success_prob)) {
    agence.confiance = std::max(0.0, agence.confiance - 0.025);
    agence.log("COMMIT sous l'exigence du client : concession de confiance (-2,5 pts).");
  }
  if (!payer(k.bilan.cost_total, "COMMIT du programme " + c.id)) return false;
  // TEMPORALITE : la livraison (mois courant + delai de programme) contre l'echeance
  // du client. Un retard n'annule pas la mission mais le client retient une part de la
  // prime (clause de retard). Le tutoriel (repetition) en est exempte.
  const double retard = (agence.mois + k.bilan.schedule_months)
                      - (c.mois_signature + c.spec.deadline_months);
  const double retard_mois = (c.id == "M00") ? 0.0 : std::max(0.0, retard);
  if (retard_mois > 0.05) {
    char b[140]; std::snprintf(b, sizeof(b),
        "LIVRAISON EN RETARD de %.1f mois : le client appliquera une penalite sur la prime.", retard_mois);
    agence.log(b);
  }
  agence.mois += k.bilan.schedule_months;
  vol = Vol{};
  vol.retard_mois = retard_mois;
  vol.graine = agence.tirer_graine();
  ecrire_fpl("plan_agence.fpl");
  vol.doc = io::parse_fpl("plan_agence.fpl");
  vol.t0 = vol.doc.plan.epoch0;
  // TUTORIEL : garantir un vol de REPETITION gagnant si bien pilote. Comme
  // l'execution est au nominal, la boucle de reference (vol_mc rehearsal) est
  // predictive du vol interactif ; on re-tire la graine (borne 8) jusqu'a ce
  // qu'elle boucle. La physique tranche toujours : un plan FAUX echoue quand meme.
  if (c.id == "M00") {
    const auto& Nv = niveau_poursuite(k.niveau);
    for (int t = 0; t < 8; ++t) {
      auto r = vol_mc(vol.doc, eph, vol.graine, Nv, /*rehearsal=*/true);
      if (r.valide && r.ok) break;
      vol.graine = agence.tirer_graine();
    }
  }
  prop::PropOptions opt; opt.step.rtol = 1e-11; opt.sample_dt = 0.0;
  vol.S = std::make_unique<flight::Session>(vol.doc.plan, eph, vol.graine, opt);
  // TUTORIEL = VOL DE REPETITION : l'erreur d'execution (Gates) est a son nominal,
  // comme une repetition en simulateur haute-fidelite avant un vrai tir. Un vol
  // CORRECT (bonnes derivations + bonne poursuite + bonne geometrie de manoeuvre)
  // boucle a coup sur : la physique tranche toujours, mais pas le tirage de la
  // queue statistique. Le vrai vol stochastique commence a M00b (Gates actif).
  vol.rehearsal = c.id == "M00";
  if (vol.rehearsal) vol.S->set_gates_enabled(false);
  const auto& N = niveau_poursuite(k.niveau);
  for (auto& p : N.passes) { nav::Pass q; q.station=(int)p[0]; q.t_start=vol.t0+p[1]; q.t_end=vol.t0+p[2]; q.sample_dt=60; vol.S->schedule_pass(q); }
  vol.commis = true; vol.etape = EtapeVol::PreInjection;
  if (vol.rehearsal) teletype("VOL DE REPETITION (tutoriel) : execution au nominal. Ton travail decide, pas le hasard.");
  teletype("COMMIT. Graine tiree et GELEE. Revelee au post-mortem seulement.");
  teletype("Vehicule sur le pas de tir orbital. En attente de LANCEMENT.");
  agence.log("COMMIT du vol " + c.id + ".");
  return true;
}

// ---------------------------------------------------------------------------
// Salle de vol GEO : temps reel
// ---------------------------------------------------------------------------
void Jeu::teletype(const std::string& l) {
  vol.flux.push_back(l);
  if (vol.flux.size() > 16) vol.flux.erase(vol.flux.begin());
}
void Jeu::pousser_chrono(const std::string& nom, int type) {
  vol.chrono.push_back({(vol.S->t() - vol.t0) / 3600.0, nom, type});
}
int Jeu::stations_actives_a(double t) const {
  if (!vol.commis) return 0;
  const double rel = t - vol.t0; int m = 0;
  const auto& N = niveau_poursuite(conception.niveau);
  for (auto& p : N.passes) if (rel >= p[1] && rel <= p[2]) m |= (1 << (int)p[0]);
  return m;
}
double Jeu::periode_estimee() const {
  if (!vol.S) return -1;
  auto o = vol.S->observe();
  const auto el = astro::rv_to_elements(o.state.r, o.state.v, MU_EARTH);
  if (!(el.a > 0)) return -1;
  return astro::orbital_period(el.a, MU_EARTH);
}

// belief trace : l'orbite que le joueur CROIT suivre. Avant toute observation,
// c'est le GTO nominal (injection commandee, sans Gates). Apres, c'est l'estime.
void Jeu::rafraichir_trace() {
  vol.traj_x.clear(); vol.traj_y.clear(); vol.traj_z.clear(); vol.traj_t.clear();
  Vec3 r, v; double t0;
  if (vol.obs_valide) { r = vol.obs.state.r; v = vol.obs.state.v; t0 = vol.obs.t; }
  else {
    // GTO nominal : parking + injection commandee (prograde RSW)
    const State s0 = vol.doc.plan.initial;
    const auto b = rsw_basis(s0.r, s0.v);
    const Vec3 dv_in = rsw_to_inertial(b, vol.doc.plan.burns[0].dv);
    r = s0.r; v = s0.v + dv_in; t0 = vol.t0;
  }
  const auto el = astro::rv_to_elements(r, v, MU_EARTH);
  const double T = (el.a > 0) ? astro::orbital_period(el.a, MU_EARTH) : 20000.0;
  const int N = 220;
  for (int i = 0; i <= N; ++i) {
    const double dt = 1.02 * T * i / N;
    auto k = astro::kepler_propagate(r, v, dt, MU_EARTH);
    vol.traj_x.push_back(k.r.x / 1000.0);
    vol.traj_y.push_back(k.r.y / 1000.0);
    vol.traj_z.push_back(k.r.z / 1000.0);
    vol.traj_t.push_back(t0 + dt);
  }
  vol.sigma_pos_km = vol.obs_valide ? vol.obs.sigma_pos / 1000.0 : 0.0;
}
Vec3 Jeu::vol_position_estimee() const {
  if (vol.traj_t.size() < 2) return {};
  double t = vol.S ? vol.S->t() : vol.traj_t.front();
  const double t0 = vol.traj_t.front(), t1 = vol.traj_t.back(), per = t1 - t0;
  if (per <= 0) return {vol.traj_x[0], vol.traj_y[0], vol.traj_z[0]};
  double u = std::fmod(t - t0, per); if (u < 0) u += per;
  const double tt = t0 + u;
  // recherche lineaire (la trace est courte)
  for (size_t i = 1; i < vol.traj_t.size(); ++i)
    if (vol.traj_t[i] >= tt) {
      const double f = (tt - vol.traj_t[i-1]) / std::max(1e-9, vol.traj_t[i] - vol.traj_t[i-1]);
      return { vol.traj_x[i-1] + f * (vol.traj_x[i]-vol.traj_x[i-1]),
               vol.traj_y[i-1] + f * (vol.traj_y[i]-vol.traj_y[i-1]),
               vol.traj_z[i-1] + f * (vol.traj_z[i]-vol.traj_z[i-1]) };
    }
  return {vol.traj_x.back(), vol.traj_y.back(), vol.traj_z.back()};
}

void Jeu::vol_engager() {
  if (!vol.S || vol.fini || vol.tr_en_route) return;
  switch (vol.etape) {
    case EtapeVol::PreInjection:
      // TRANSITION DE LANCEMENT : une ascension animee (compte a rebours + montee
      // du sol au parking) avant l'injection. La physique de l'injection reste
      // identique : l'ascension est une mise en scene du temps reel, pas un calcul.
      if (vol.ascension_t < 0) {
        vol.ascension_t = 0.0;
        teletype("=== SEQUENCE DE LANCEMENT ===");
        teletype("T-10 s. Bras retires. Sequence d'allumage armee.");
        return;   // le tick fait monter ascension_t ; l'injection se fait a la fin
      }
      break;
    case EtapeVol::DeriveAMF2: {
      const double T = periode_estimee();
      if (T <= 0) { vol.perdu_avant_cible = true; vol.raison_perte = "estime diverge"; terminer_vol(); return; }
      vol.tr_cible = vol.S->t() + 0.38 * T; vol.tr_en_route = true;
      teletype("Derive vers l'apside opposee (0,38 periode).");
      break;
    }
    case EtapeVol::DeriveTRIM: {
      const double T = periode_estimee();
      if (T <= 0) { vol.perdu_avant_cible = true; vol.raison_perte = "estime diverge"; terminer_vol(); return; }
      const auto& N = niveau_poursuite(conception.niveau);
      vol.tr_cible = vol.S->t() + (0.38 + N.revolutions_sup) * T; vol.tr_en_route = true;
      teletype(N.revolutions_sup ? "Derive + revolutions d'attente payees." : "Derive vers l'apside a R_GEO.");
      break;
    }
    case EtapeVol::DeriveVerdict:
      vol.tr_cible = vol.S->t() + 2000; vol.tr_en_route = true;
      teletype("Stabilisation. La verite va etre revelee.");
      break;
    default: break;
  }
}
void Jeu::vol_sauter() {
  if (!vol.S) return;
  if (vol.ascension_t >= 0.0) {   // sauter l'ascension : injecter tout de suite
    vol.ascension_t = -1.0;
    vol.S->commit_burn(vol.doc.plan.burns[0]);
    pousser_chrono("injection GTO", 1);
    if (!vol.S->alive()) { vol.perdu_avant_cible = true; vol.raison_perte = "reservoir vide a l'injection"; terminer_vol(); return; }
    rafraichir_trace();
    vol.tr_cible = vol.t0 + 15200; vol.tr_en_route = true;
    return;
  }
  if (!vol.tr_en_route) return;
  vol.S->advance_to(vol.tr_cible);
  vol.tr_en_route = false;
  arrivee_phase();
}
void Jeu::arrivee_phase() {
  switch (vol.etape) {
    case EtapeVol::PreInjection: vol.etape = EtapeVol::PretAMF;
      teletype("Apogee atteint. OBSERVE, puis calcule l'AMF."); break;
    case EtapeVol::DeriveAMF2: vol.etape = EtapeVol::PretAMF2;
      teletype("Fenetre AMF2 ouverte. OBSERVE."); break;
    case EtapeVol::DeriveTRIM: vol.etape = EtapeVol::PretTRIM;
      teletype("Fenetre TRIM ouverte. OBSERVE."); break;
    case EtapeVol::DeriveVerdict: terminer_vol(); break;
    default: break;
  }
  vol.obs_valide = false;
}

void Jeu::tick(double dt_reel) {
  // ASCENSION : la transition de lancement (compte a rebours + montee), ~9 s.
  if (vol.commis && !vol.fini && vol.ascension_t >= 0.0 && vol.tr_actif && vol.S) {
    const double avant = vol.ascension_t;
    vol.ascension_t += dt_reel / 9.0;
    // egrene le compte a rebours dans la telemetrie
    for (int s = 9; s >= 1; --s) {
      const double seuil = (10.0 - s) / 10.0 * 0.5;   // T-10..T-1 pendant la 1re moitie
      if (avant < seuil && vol.ascension_t >= seuil) { char b[24]; std::snprintf(b, sizeof(b), "T-%d s", s); teletype(b); }
    }
    if (avant < 0.5 && vol.ascension_t >= 0.5) teletype(">>> ALLUMAGE. Decollage.");
    if (vol.ascension_t >= 1.0) {
      vol.ascension_t = -1.0;
      vol.S->commit_burn(vol.doc.plan.burns[0]);
      pousser_chrono("injection GTO", 1);
      teletype(">>> INJECTION GTO : etage superieur allume.");
      if (!vol.S->alive()) { vol.perdu_avant_cible = true; vol.raison_perte = "reservoir vide a l'injection"; terminer_vol(); return; }
      rafraichir_trace();
      vol.tr_cible = vol.t0 + 15200; vol.tr_en_route = true;
      teletype("Montee vers l'apogee. Surveille les stations de poursuite.");
    }
    return;
  }
  // vol GEO en temps reel
  if (vol.commis && !vol.fini && vol.tr_en_route && vol.tr_actif && vol.S) {
    double t = vol.S->t();
    const double reste = vol.tr_cible - t;
    double pas = dt_reel * vol.tr_warp;
    if (pas >= reste) { vol.S->advance_to(vol.tr_cible); vol.tr_en_route = false; vol.stations = 0; arrivee_phase(); }
    else {
      vol.S->advance_to(t + pas);
      vol.stations = stations_actives_a(vol.S->t());
    }
  }
  // vol interplanetaire en temps reel
  if (vinterp.commis && !vinterp.fini && vinterp.tr_actif) {
    double pas = dt_reel * vinterp.tr_warp;
    double cible = (vinterp.phase == 0) ? vinterp.t_tcm1
                 : (vinterp.phase == 1) ? vinterp.t_tcm2 : vinterp.t_arr;
    if (vinterp.t + pas >= cible) {
      vinterp.t = cible;
      if (vinterp.phase == 0) { vinterp.phase = 1; teletype_i("Fenetre TCM-1 (depart +30 j)."); }
      else if (vinterp.phase == 1) { vinterp.phase = 2; teletype_i("Fenetre TCM-2 (arrivee -30 j)."); }
      else { interp_terminer(); }
    } else vinterp.t += pas;
  }
}

void Jeu::vol_observer() {
  if (!vol.S || vol.fini) return;
  vol.obs = vol.S->observe();
  vol.obs_valide = true; ++vol.nb_observations;
  pousser_chrono("OBS", 3);
  char b[96]; std::snprintf(b, sizeof(b), "[OBS %d] %s | sigma_pos %.1f m",
                            vol.nb_observations, vol.obs.source.c_str(), vol.obs.sigma_pos);
  teletype(b);
  rafraichir_trace();
}

PropositionAnalyse Jeu::calculer_manoeuvre(const char* apside, double saut) const {
  PropositionAnalyse pa;
  if (!vol.obs_valide) { pa.note = "Observe d'abord."; return pa; }
  auto G = pile_terre(eph);
  // TRIM : circulariser a l'apside QUI EST a R_GEO (pas la premiere rencontree),
  // sinon on fige `a` au mauvais rayon.
  auto p = (vol.etape == EtapeVol::PretTRIM) ? predire_rgeo(G, vol.obs, saut)
                                             : predire(G, vol.obs, apside, saut);
  if (!p.ok) { pa.note = "Estime insuffisant : pas de prediction d'apside."; return pa; }
  const Vec3 vt = (vol.etape == EtapeVol::PretTRIM) ? v_cible_circ(pos(p.y)) : v_cible_gto(pos(p.y));
  pa.valide = true; pa.t_burn = p.t; pa.dv = vt - vel(p.y);
  char b[160]; std::snprintf(b, sizeof(b), "manoeuvre a t0+%.0f s (dans %.0f s), |dv| = %.2f m/s",
                             p.t - vol.t0, p.t - vol.S->t(), norm(pa.dv));
  pa.note = b;
  char m[900]; std::snprintf(m, sizeof(m),
    "CALCUL DE LA MANOEUVRE (division analyse)\n"
    "1. determination d'orbite sur tes passes -> estime (r,v)\n"
    "2. propagation N-corps de l'estime jusqu'a l'apside vise\n"
    "3. vitesse requise a l'apside : v_cible = %s\n"
    "4. dv = v_cible - v_predite = [%.2f, %.2f, %.2f] m/s (inertiel)\n"
    "   |dv| = %.2f m/s\n"
    "NB : ce calcul part de TON estime, pas de la verite. Une mauvaise\n"
    "navigation donne un bon calcul sur une mauvaise orbite.",
    vol.etape == EtapeVol::PretTRIM ? "sqrt(mu/r) (circulaire)" : "vis-viva vers a=(r+R_GEO)/2",
    pa.dv.x, pa.dv.y, pa.dv.z, norm(pa.dv));
  pa.memo = m;
  return pa;
}
void Jeu::vol_analyser() {
  if (!vol.S || vol.fini) return;
  if (vol.etape != EtapeVol::PretAMF && vol.etape != EtapeVol::PretAMF2 && vol.etape != EtapeVol::PretTRIM)
    { erreur = "Rien a calculer ici."; return; }
  if (!payer(prix_analyse(), "division analyse : manoeuvre")) return;
  vol.cout_analyse += prix_analyse();
  vol.prop = calculer_manoeuvre(vol.etape == EtapeVol::PretAMF ? "APOAPSIS" : "",
                                vol.etape == EtapeVol::PretAMF ? 0.0 : 0.02);
  if (vol.prop.valide) teletype("Analyse : " + vol.prop.note);
}
static const char* nom_burn(EtapeVol e) {
  return e == EtapeVol::PretAMF ? "AMF" : (e == EtapeVol::PretAMF2 ? "AMF2" : "TRIM");
}
void Jeu::vol_apres_burn() {
  vol.prop = PropositionAnalyse{}; vol.obs_valide = false;
  if (!vol.S->alive()) { vol.perdu_avant_cible = true; vol.raison_perte = "reservoir vide en vol"; terminer_vol(); return; }
  vol.etape = (vol.etape == EtapeVol::PretAMF)  ? EtapeVol::DeriveAMF2
            : (vol.etape == EtapeVol::PretAMF2) ? EtapeVol::DeriveTRIM : EtapeVol::DeriveVerdict;
  rafraichir_trace();
}
void Jeu::vol_bruler_proposition() {
  if (!vol.S || !vol.prop.valide || vol.fini) return;
  flight::BurnCmd b; b.id = nom_burn(vol.etape); b.t = vol.prop.t_burn;
  b.frame = flight::DvFrame::Inertial; b.hold = force::ThrustFrame::InertialFixed; b.stage = 0; b.dv = vol.prop.dv;
  vol.S->commit_burn(b); pousser_chrono(b.id, 1);
  teletype(">>> " + std::string(b.id) + " : moteur allume.");
  if (vol.etape != EtapeVol::PretAMF) vol.dv_corr += norm(vol.prop.dv);
  vol_apres_burn();
}
void Jeu::vol_bruler_manuel(double dt_s, double dv_r, double dv_s, double dv_w) {
  if (!vol.S || vol.fini) return;
  if (vol.etape != EtapeVol::PretAMF && vol.etape != EtapeVol::PretAMF2 && vol.etape != EtapeVol::PretTRIM)
    { erreur = "Pas de manoeuvre ici."; return; }
  flight::BurnCmd b; b.id = nom_burn(vol.etape); b.t = vol.S->t() + std::max(1.0, dt_s);
  b.frame = flight::DvFrame::RSW; b.hold = force::ThrustFrame::InertialFixed; b.stage = 0;
  b.dv = Vec3{dv_r, dv_s, dv_w};
  vol.S->commit_burn(b); pousser_chrono(std::string(b.id) + " (manuel)", 1);
  teletype(">>> " + std::string(b.id) + " (manuel) : moteur allume.");
  if (vol.etape != EtapeVol::PretAMF) vol.dv_corr += norm(b.dv);
  vol_apres_burn();
}

void Jeu::terminer_vol() {
  Contrat& c = contrats[contrat_actif];
  vol.fini = true; vol.etape = EtapeVol::Verdict; c.termine = true;
  const auto tr = vol.S->truth_state();
  vol.el_final = astro::rv_to_elements(tr.r, tr.v, MU_EARTH);
  vol.a_ok = std::fabs(vol.el_final.a - c.cible_sma) < c.tol_sma;
  vol.e_ok = vol.el_final.e < c.tol_ecc;
  vol.i_ok = vol.el_final.i / DEG < c.tol_inc_deg;
  const bool cible = !vol.perdu_avant_cible && vol.a_ok && vol.e_ok && vol.i_ok;
  vol.ok = cible; c.reussi = cible;
  // explication EN CLAIR : quel(s) critere(s) a cede, et de combien
  if (vol.perdu_avant_cible) vol.pourquoi = vol.raison_perte + ".";
  else if (cible) vol.pourquoi = "Les TROIS criteres sont dans la tolerance : altitude (a), "
      "forme de l'orbite (e) et inclinaison (i). Ta navigation a suffi.";
  else {
    vol.pourquoi = "Tu as rate parce que ";
    std::vector<std::string> f;
    if (!vol.a_ok) { char b[80]; std::snprintf(b,sizeof(b),"l'altitude a devie de %.0f km (max %.0f)", std::fabs(vol.el_final.a-c.cible_sma)/1000, c.tol_sma/1000); f.push_back(b); }
    if (!vol.e_ok) { char b[80]; std::snprintf(b,sizeof(b),"l'orbite est trop elliptique (e=%.4f, max %.4f)", vol.el_final.e, c.tol_ecc); f.push_back(b); }
    if (!vol.i_ok) { char b[80]; std::snprintf(b,sizeof(b),"le plan est trop incline (i=%.3f deg, max %.2f)", vol.el_final.i/DEG, c.tol_inc_deg); f.push_back(b); }
    for (size_t j=0;j<f.size();++j) vol.pourquoi += (j? " ET " : "") + f[j];
    vol.pourquoi += ". C'est presque toujours l'INCLINAISON qui cede : elle demande une "
                    "manoeuvre hors-plan couteuse, tres sensible aux erreurs d'execution.";
  }
  { auto G = pile_terre(eph);
    const double T = (vol.el_final.a > 0) ? astro::orbital_period(vol.el_final.a, MU_EARTH) : 20000.0;
    StateN y = vol.S->y();
    prop::PropOptions po; po.step.rtol = 1e-9; po.sample_dt = std::max(60.0, T / 240.0);
    auto r = prop::propagate(G, vol.S->t(), y, vol.S->t() + 1.03 * T, {}, po);
    vol.verite_x.clear(); vol.verite_y.clear();
    for (const auto& s : r.samples) { vol.verite_x.push_back(s.y[0]/1000.0); vol.verite_y.push_back(s.y[1]/1000.0); }
  }
  char b[512];
  if (vol.perdu_avant_cible) std::snprintf(b, sizeof(b), "MISSION PERDUE : %s.", vol.raison_perte.c_str());
  else std::snprintf(b, sizeof(b),
      "a = %.1f km (cible %.0f, ecart %+.1f, tol +/-%.0f)\ne = %.6f (tol < %.0e)\n"
      "i = %.4f deg (tol < %.2f)\ncorrection payee : %.1f m/s | analyse : %.1f M$",
      vol.el_final.a/1000, c.cible_sma/1000, (vol.el_final.a-c.cible_sma)/1000, c.tol_sma/1000,
      vol.el_final.e, c.tol_ecc, vol.el_final.i/DEG, c.tol_inc_deg, vol.dv_corr, vol.cout_analyse);
  vol.verdict = b;
  agence.mois += 0.1;
  double prime_versee = 0.0;
  if (vol.ok) {
    const double fac = std::max(0.3, 1.0 - 0.06 * vol.retard_mois);   // clause de retard (liquidated damages)
    prime_versee = c.prime_succes * fac;
    agence.encaisser(prime_versee, "prime de succes " + c.id + (vol.retard_mois > 0.05 ? " (reduite : retard)" : ""));
    if (vol.retard_mois > 0.05) { char rb[128]; std::snprintf(rb, sizeof(rb),
        "Retard de %.1f mois : prime reduite a %.0f %% (%.1f au lieu de %.1f M$).",
        vol.retard_mois, 100*fac, prime_versee, c.prime_succes); agence.log(rb); }
    agence.confiance = std::min(0.98, agence.confiance + 0.08); ++agence.reussites;
    if (conception.instrument) { ++relais_geo; agence.log("Satellite operationnel : +1 relais science en GEO."); }
    agence.log("MISSION REUSSIE.");
  } else {
    depense_obligatoire(c.penalite_echec, "penalite d'echec " + c.id);
    agence.confiance = std::max(0.0, agence.confiance - 0.12); ++agence.echecs;
    agence.log("MISSION PERDUE.");
  }
  { // LA MARGE de la mission : recettes - depenses depuis la signature
    const double recettes = c.spec.budget_musd + (vol.ok ? prime_versee : -c.penalite_echec);
    char b[140]; std::snprintf(b, sizeof(b), "MARGE de %s : %+.1f M$ (recettes %.1f - depenses %.1f)",
                               c.id.c_str(), recettes - cout_programme, recettes, cout_programme);
    agence.log(b);
  }
  offrir_apres_mission(vol.ok);

  vol.postmortem = "Monte-Carlo de decomposition en cours (30 vols)...";
  if (worker && worker->joinable()) worker->join();
  auto doc = std::make_shared<io::FplDocument>(vol.doc);
  const int niveau = conception.niveau; const double a_final = vol.el_final.a; const double gr = (double)vol.graine;
  const bool rehe = vol.rehearsal;
  mc_en_cours = true; mc_fait = 0; mc_total = 30;
  worker = std::make_unique<std::thread>([this, doc, niveau, a_final, gr, rehe] {
    const auto& N = niveau_poursuite(niveau);
    int ok = 0, val = 0; double sa = 0, s2 = 0;
    for (int k = 0; k < 30; ++k) { auto r = vol_mc(*doc, eph, 900 + k, N, rehe);
      if (r.valide) { ++val; if (r.ok) ++ok; const double e=(r.a-R_GEO)/1000; sa+=e; s2+=e*e; } mc_fait = k + 1; }
    const double moy = val ? sa/val : 0, sig = val ? std::sqrt(std::max(0.0, s2/val - moy*moy)) : 0;
    const double ec = (a_final - R_GEO)/1000;
    char t[760]; std::snprintf(t, sizeof(t),
      "GRAINE REVELEE : %.0f (gelee au COMMIT, comme promis).\n\n"
      "P(objectifs tenus) MESUREE a ce niveau : %d %% (n=30).\n"
      "Dispersion sigma sur a ~ %.0f km. Ton ecart : %+.0f km.\n\n%s\n\n"
      "Le jeu ne dit jamais \"rate\" : il DECOMPOSE. La source dominante ici est\n"
      "la NAVIGATION. La tolerance la plus serree est souvent l'inclinaison (0,25).\n"
      "REMEDE : monte d'un niveau de poursuite, ou provisionne plus de marge.",
      gr, val ? 100*ok/val : 0, sig, ec,
      (std::fabs(ec) > 2*sig && sig > 0) ? "Ton vol est dans la QUEUE : malchance de tirage."
                                         : "Ton vol est TYPIQUE : c'est le niveau de nav achete, pas la malchance.");
    vol.postmortem = t; mc_en_cours = false;
  });
}

// ---------------------------------------------------------------------------
// INTERPLANETAIRE : Mars / comete. Lambert exact, Oberth exact, b<->rp exact.
// Le seul modele simplifie : l'ELLIPSE de dispersion (echelle de m01_corridor).
// ---------------------------------------------------------------------------
State Jeu::etat_cible(double t) const {
  const auto ty = contrat_actif >= 0 ? contrats[contrat_actif].type : TypeContrat::VolMars;
  if (ty == TypeContrat::VolComete) { auto k = comete_state_at(t); return State{k.r, k.v, 0}; }
  const ephem::Body corps = (ty == TypeContrat::VolTitan) ? ephem::Body::Saturn : ephem::Body::Mars;
  auto pv = eph.state(corps, ephem::Body::Sun, Epoch{t});
  return State{pv.r, pv.v, 0};
}
// un survol (comete / Titan) vise a annuler le v_inf : l'insertion "coute" v_inf,
// mais le survol scientifique ne demande qu'une approche serree.
static bool est_survol(TypeContrat t) { return t == TypeContrat::VolComete || t == TypeContrat::VolTitan; }

void Jeu::interp_calculer_carte() {
  if (cinterp.calcul || cinterp.carte_calculee || contrat_actif < 0) return;
  const auto ty = contrats[contrat_actif].type;
  const bool comete = ty == TypeContrat::VolComete;
  const bool titan = ty == TypeContrat::VolTitan;
  if (!payer(0.0006 * cinterp.n_dep * cinterp.n_tof * m_calcul(),
             "temps de calcul : carte de transfert")) return;
  agence.mois += 0.3;
  // COHERENCE DE DATE : on ne planifie pas un depart AVANT aujourd'hui. La fenetre
  // commence au max(borne du scenario, date courante de l'agence) et garde >= 2 ans
  // de large pour contenir au moins une fenetre synodique.
  const double aujourdhui = epoch_courant();
  cinterp.dep0 = std::max(epoch_from_iso("2026-06-01T00:00:00").tdb, aujourdhui);
  cinterp.dep1 = std::max(epoch_from_iso(titan ? "2032-12-31T00:00:00" : comete ? "2030-12-31T00:00:00" : "2029-08-31T00:00:00").tdb,
                          cinterp.dep0 + 2.0 * 365.25 * DAY);
  cinterp.tof0 = (titan ? 900.0 : comete ? 250.0 : 128.0) * DAY;
  cinterp.tof1 = (titan ? 3200.0 : comete ? 1500.0 : 420.0) * DAY;
  if (worker && worker->joinable()) worker->join();
  cinterp.calcul = true;
  worker = std::make_unique<std::thread>([this, comete] {
    const int nd = cinterp.n_dep, nt = cinterp.n_tof;
    cinterp.grille.assign((size_t)nd * nt, 60.0f);
    for (int i = 0; i < nd; ++i) {
      const double td = cinterp.dep0 + (cinterp.dep1 - cinterp.dep0) * i / std::max(1, nd - 1);
      const auto E = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{td});
      for (int j = 0; j < nt; ++j) {
        const double tof = cinterp.tof0 + (cinterp.tof1 - cinterp.tof0) * j / std::max(1, nt - 1);
        const State C = etat_cible(td + tof);
        auto L = astro::lambert(E.r, C.r, tof, MU_SUN, true, 0);
        if (!L.ok) continue;
        const double vd = norm(L.solutions[0].v1 - E.v);
        const double va = norm(L.solutions[0].v2 - C.v);
        cinterp.grille[(size_t)(nd - 1 - i) * nt + j] = (float)std::min(60.0, (vd + va) / 1000.0);
      }
    }
    cinterp.carte_calculee = true; cinterp.calcul = false;
  });
}

void Jeu::interp_choisir(double t_dep, double tof) {
  if (contrat_actif < 0) return;
  cinterp.t_dep = t_dep; cinterp.tof = tof;
  const auto E = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{t_dep});
  const State C = etat_cible(t_dep + tof);
  auto L = astro::lambert(E.r, C.r, tof, MU_SUN, true, 0);
  if (!L.ok) { erreur = "Lambert n'a pas converge a ce point."; return; }
  const auto ty = contrats[contrat_actif].type;
  cinterp.vinf_dep = norm(L.solutions[0].v1 - E.v);
  cinterp.vinf_arr = norm(L.solutions[0].v2 - C.v);
  cinterp.c3 = cinterp.vinf_dep * cinterp.vinf_dep / 1e6;
  // TMI depuis un parking 300 km : v_p hyperbolique - v_circ
  const double rp = R_EARTH + 300e3;
  const double vp = std::sqrt(cinterp.vinf_dep * cinterp.vinf_dep + 2 * MU_EARTH / rp);
  cinterp.dv_tmi = vp - astro::v_circular(rp, MU_EARTH);
  // ASSISTANCE GRAVITATIONNELLE (modele DECLARE) : un tour a survols (type V-E-E-J-S)
  // ne fournit pas la meme energie depuis le lanceur. La reduction retenue (0,53)
  // est celle MESUREE sur t01_veega (24310 -> 12763 m/s). En echange : +2 a +3 ans.
  if (cinterp.assistance) cinterp.dv_tmi *= 0.53;
  // insertion : un SURVOL (comete/Titan) ne demande PAS d'annuler le v_inf - on passe
  // au plus pres, on ne se met pas en orbite. Le cout est le TMI + le ciblage (TCM).
  // Une CAPTURE (Mars) exige au contraire de freiner (Oberth au periastre).
  if (est_survol(ty)) {
    cinterp.dv_insertion = 0.0;   // flyby : pas de mise en orbite
  } else {
    const double rpm = R_MARS + 500e3;
    const double vpm = std::sqrt(cinterp.vinf_arr * cinterp.vinf_arr + 2 * MU_MARS / rpm);
    double dv_capture = vpm - std::sqrt(MU_MARS / rpm);
    if (recherche_faite("aero")) dv_capture *= 0.80;   // aerocapture (modele declare)
    cinterp.dv_insertion = dv_capture;
  }
  cinterp.dv_total = cinterp.dv_tmi + cinterp.dv_insertion;
  cinterp.choisie = true;
  interp_recalculer();
  agence.log("Point de transfert fige (Lambert exact) : C3 " +
             std::to_string((int)cinterp.c3) + " km2/s2.");
}

void Jeu::interp_recalculer() {
  if (contrat_actif < 0 || !cinterp.choisie) return;
  const Contrat& c = contrats[contrat_actif];
  const auto& E = mission::engines()[cinterp.moteur];
  const double payload = c.spec.payload_kg + (cinterp.collecteur ? 80.0 : 0.0);
  const double perte = PERTE_FINIE_REL * cinterp.dv_total;
  mission::Program pr; pr.engine_index = cinterp.moteur; pr.launcher_index = -1;
  pr.dv_margin = cinterp.marge_dv; pr.review = cinterp.revue; pr.test_hours = 200;
  pr.tracking_musd = 6.0 * m_poursuite(); pr.tracking_days = cinterp.tof / DAY;
  mission::Contract spec = c.spec; spec.payload_kg = payload;
  // VEHICULE MULTI-ETAGES : le joueur empile 1..3 etages identiques pour franchir le
  // mur du mono-etage chimique (dv <~ ve*ln(1/frac_seche)). Le partage EGAL du dv est
  // l'optimum pour des etages identiques -> physique exacte, pas un choix du jeu.
  const int ne = std::max(1, std::min(3, cinterp.n_etages));
  cinterp.bilan = mission::assess_multistage(spec, pr, 4, cinterp.dv_total, perte, ne);
  // P(physique) interplanetaire : l'echelle de TCM mesuree (m01_corridor)
  static const double P_TCM[4] = {0.00, 0.29, 0.82, 1.00};
  double p = P_TCM[cinterp.strategie_tcm];
  if (recherche_faite("nav2")) p = std::min(0.99, p + 0.03);
  if (!cinterp.bilan.fits_mass) {   // assess_multistage a pose un why explicite (ajoute un etage / trop lourd)
    cinterp.bilan.ok = false;
    cinterp.taille.m0 = 0; cinterp.taille.propellant = 0; cinterp.taille.converged = false;
    return;
  }
  // TEMPS interplanetaire (fenetre + croisiere + assistance) : remplace le schedule de base
  const auto& L = mission::launchers()[cinterp.bilan.launcher_index];
  cinterp.bilan.schedule_months = std::max(E.lead_months, L.lead_months)
      + mois_integration_eff() + cinterp.tof / DAY / 30.44
      + (cinterp.assistance ? 30.0 : 0.0);   // le tour a survols ajoute ~2,5 ans
  cinterp.bilan.cost_ops = 0.45 * cinterp.bilan.schedule_months;
  // cout total COHERENT : le vrai cost_ops (croisiere pluriannuelle) + le collecteur
  // (le moteur x N etages est deja dans cost_engine, l'etage x N dans cost_stage).
  cinterp.bilan.cost_total = cinterp.bilan.cost_launcher + cinterp.bilan.cost_engine
      + cinterp.bilan.cost_stage + cinterp.bilan.cost_tracking + cinterp.bilan.cost_compute
      + cinterp.bilan.cost_tests + cinterp.bilan.cost_review + cinterp.bilan.cost_ops
      + (cinterp.collecteur ? 6.0 : 0.0);
  cinterp.bilan.fits_budget   = cinterp.bilan.cost_total <= spec.budget_musd;
  cinterp.bilan.fits_schedule = cinterp.bilan.schedule_months <= spec.deadline_months;
  spec.min_success_prob = exigence_client(c.spec.min_success_prob);   // exigence modulee par la reputation
  mission::finalize(cinterp.bilan, spec, std::max(0.05, p));
  cinterp.taille.m0 = cinterp.bilan.m0_kg;
  cinterp.taille.propellant = cinterp.bilan.propellant_kg;
  cinterp.taille.stage_dry = cinterp.bilan.dry_kg;
  cinterp.taille.converged = true;
}

bool Jeu::interp_commit() {
  if (!cinterp.choisie) { erreur = "Choisis un point sur la carte d'abord."; return false; }
  interp_recalculer();
  const Contrat& c = contrats[contrat_actif];
  if (!cinterp.bilan.fits_mass) { erreur = "Aucun lanceur ne souleve ce vehicule."; return false; }
  if (!payer(cinterp.bilan.cost_total, "COMMIT interplanetaire " + c.id)) return false;
  // TEMPORALITE : livraison (mois + delai, croisiere comprise) contre l'echeance client.
  const double retard = (agence.mois + cinterp.bilan.schedule_months)
                      - (c.mois_signature + c.spec.deadline_months);
  const double retard_mois = std::max(0.0, retard);
  if (retard_mois > 0.05) {
    char b[140]; std::snprintf(b, sizeof(b),
        "LIVRAISON EN RETARD de %.1f mois : penalite sur la prime (transit trop lent ?).", retard_mois);
    agence.log(b);
  }
  agence.mois += cinterp.bilan.schedule_months;
  vinterp = VolInterp{};
  vinterp.retard_mois = retard_mois;
  vinterp.type = c.type;
  vinterp.graine = agence.tirer_graine();
  vinterp.t_dep = cinterp.t_dep; vinterp.tof = cinterp.tof; vinterp.t_arr = cinterp.t_dep + cinterp.tof;
  vinterp.t = cinterp.t_dep;
  vinterp.t_tcm1 = cinterp.t_dep + 30 * DAY;
  vinterp.t_tcm2 = cinterp.t_dep + cinterp.tof - 30 * DAY;
  vinterp.strategie_tcm = 0;  // pas encore corrige
  // ellipse de dispersion 3-sigma initiale = "aucune correction" (echelle mesuree)
  static const double ELL[4] = {87778.0, 5195.0, 539.0, 121.0};
  vinterp.ellipse_km = ELL[0];
  // arrivee : tirage du b-plane (gele). Le b vise = corridor central.
  const double mu = est_survol(c.type) ? 0.0 : MU_MARS;
  const double vinf = cinterp.vinf_arr;
  // dispersion selon la strategie CHOISIE en conception (pré-provisionnée)
  const double ell_prevue = ELL[cinterp.strategie_tcm];
  // tir pseudo-aleatoire deterministe (graine)
  std::uint64_t s = vinterp.graine * 2654435761u + 12345u;
  auto rnd = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return ((s >> 11) * (1.0/9007199254740992.0)) * 2 - 1; };
  const double bt = ell_prevue * 0.9 * rnd(), br = ell_prevue * 0.9 * rnd();
  if (est_survol(c.type)) {
    vinterp.d_ca_km = std::sqrt(bt*bt + br*br) * 0.02 + 200.0;   // approche : dispersion -> distance
  } else {
    const double rp_vise = R_MARS + 500e3;
    const double b_vise = astro::b_from_rp(rp_vise, vinf, mu);
    vinterp.b_arr = b_vise + std::sqrt(bt*bt + br*br) * (rnd() > 0 ? 1 : -1) * 0.5;
    vinterp.bt_arr = bt; vinterp.br_arr = br;
    vinterp.rp_km = astro::rp_from_b(std::max(1.0, vinterp.b_arr), vinf, mu) / 1000.0;
  }
  // traces heliocentriques (UA)
  const int NP = 200;
  for (int i = 0; i <= NP; ++i) {
    const double f = (double)i / NP;
    auto e = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{vinterp.t_dep + f * 365.25 * DAY});
    vinterp.ter_x.push_back(e.r.x / AU); vinterp.ter_y.push_back(e.r.y / AU);
    const double per_j = c.type==TypeContrat::VolTitan? 10759.0 : c.type==TypeContrat::VolComete? 2400.0 : 687.0;
    const State cc = etat_cible(vinterp.t_dep + f * per_j * DAY);
    vinterp.cib_x.push_back(cc.r.x / AU); vinterp.cib_y.push_back(cc.r.y / AU);
  }
  // arc de croisiere : Lambert propage
  const auto E0 = eph.state(ephem::Body::EarthBary, ephem::Body::Sun, Epoch{vinterp.t_dep});
  const State C1 = etat_cible(vinterp.t_arr);
  auto L = astro::lambert(E0.r, C1.r, vinterp.tof, MU_SUN, true, 0);
  if (L.ok) {
    for (int i = 0; i <= NP; ++i) {
      auto k = astro::kepler_propagate(E0.r, L.solutions[0].v1, vinterp.tof * i / NP, MU_SUN);
      vinterp.arc_x.push_back(k.r.x / AU); vinterp.arc_y.push_back(k.r.y / AU);
      vinterp.arc_t.push_back(vinterp.t_dep + vinterp.tof * i / NP);
    }
  }
  vinterp.commis = true; vinterp.phase = 0;
  teletype_i("COMMIT. Injection trans-cible executee. Croisiere en cours.");
  teletype_i("Deux corrections (TCM) t'attendent : depart+30 j, arrivee-30 j.");
  agence.log("COMMIT du vol interplanetaire " + c.id + ".");
  return true;
}

void Jeu::teletype_i(const std::string& l) {
  vinterp.flux.push_back(l);
  if (vinterp.flux.size() > 16) vinterp.flux.erase(vinterp.flux.begin());
}
void Jeu::interp_faire_tcm() {
  if (!vinterp.commis || vinterp.fini) return;
  static const double ELL[4] = {87778.0, 5195.0, 539.0, 121.0};
  if (vinterp.phase == 0 && !vinterp.tcm1_faite) {
    vinterp.tcm1_faite = true; vinterp.strategie_tcm = std::max(vinterp.strategie_tcm, 1);
    vinterp.ellipse_km = ELL[1]; vinterp.dv_tcm += 12.0;
    teletype_i("TCM-1 executee : ellipse ramenee a ~5195 km. -12 m/s.");
  } else if (vinterp.phase == 1 && !vinterp.tcm2_faite) {
    vinterp.tcm2_faite = true;
    vinterp.strategie_tcm = (vinterp.tcm1_faite) ? 3 : 2;
    vinterp.ellipse_km = ELL[vinterp.strategie_tcm]; vinterp.dv_tcm += 12.0;
    teletype_i("TCM-2 executee : ellipse ramenee a ~" +
               std::to_string((int)vinterp.ellipse_km) + " km. -12 m/s.");
  } else erreur = "Pas de TCM disponible a cette phase.";
}
void Jeu::interp_passer_tcm() {
  if (!vinterp.commis || vinterp.fini) return;
  if (vinterp.phase == 0) { vinterp.phase = 1; vinterp.t = vinterp.t_tcm2;
    teletype_i("TCM-1 sautee. Fenetre TCM-2 (arrivee-30 j)."); }
  else if (vinterp.phase == 1) { vinterp.phase = 2; vinterp.t = vinterp.t_arr; interp_terminer(); }
}

void Jeu::interp_terminer() {
  if (vinterp.fini) return;
  vinterp.fini = true; vinterp.phase = 3;
  Contrat& c = contrats[contrat_actif];
  c.termine = true;
  const bool survol = est_survol(c.type);
  const bool titan = (c.type == TypeContrat::VolTitan);
  // recalculer l'arrivee reelle a partir de la strategie effectivement realisee
  static const double ELL[4] = {87778.0, 5195.0, 539.0, 121.0};
  const double ell = ELL[vinterp.strategie_tcm];
  std::uint64_t s = vinterp.graine * 6364136223846793005ULL + 1;
  auto rnd = [&]() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return ((s >> 11) * (1.0/9007199254740992.0)) * 2 - 1; };
  const double bt = ell * 0.85 * rnd(), br = ell * 0.85 * rnd();
  const double dispersion = std::sqrt(bt*bt + br*br);
  char b[600];
  if (survol) {
    const double seuil = titan ? 2000.0 : 1000.0;   // km au plus pres exige
    vinterp.d_ca_km = dispersion * 0.05 + (titan ? 300.0 : 120.0);
    vinterp.ok = vinterp.d_ca_km < seuil;
    vinterp.corridor_ok = vinterp.ok; vinterp.marge_ok = vinterp.ok;
    if (cinterp.collecteur && vinterp.ok) {
      double kg = titan ? 1.5 : 3.5;
      if (recherche_faite("extr")) kg *= 1.5;   // extraction amelioree
      echantillons_kg += kg;
    }
    vinterp.science_gbit = vinterp.ok ? (titan ? 60.0 : 40.0) : 5.0; donnees_gbit += vinterp.science_gbit;
    if (vinterp.ok) ++sondes_lointaines;
    vinterp.pourquoi = vinterp.ok
      ? "Approche dans le seuil : tes TCM ont resserre la visee assez tot. Le survol "
        "scientifique est reussi, les instruments ont collecte."
      : "Approche trop lointaine : la dispersion residuelle depasse le seuil. Il "
        "fallait corriger plus tot (TCM precoce) - a cette distance, chaque km d'erreur "
        "de visee devient des milliers de km a l'arrivee.";
    std::snprintf(b, sizeof(b),
      "%s : approche la plus proche %.0f km (objectif < %.0f km).\n"
      "TCM cumulees : %.0f m/s. Dispersion resid. 3-sigma : %.0f km.\n"
      "Retour scientifique : %.0f Gbit%s.",
      titan ? "TITAN" : "COMETE", vinterp.d_ca_km, seuil, vinterp.dv_tcm, ell,
      vinterp.science_gbit, (cinterp.collecteur && vinterp.ok) ? " + echantillons" : "");
  } else {
    const double vinf = cinterp.vinf_arr;
    const double rp_vise = R_MARS + 500e3;
    const double b_vise = astro::b_from_rp(rp_vise, vinf, MU_MARS);
    const double b_reel = b_vise + dispersion * (rnd() > 0 ? 1 : -1) * 0.5;
    vinterp.rp_km = astro::rp_from_b(std::max(1.0, b_reel), vinf, MU_MARS) / 1000.0;
    // insertion reelle a ce rp (Oberth exact) : v_p = sqrt(vinf^2 + 2mu/rp)
    const double rp_m = vinterp.rp_km * 1000.0;
    const double vp = std::sqrt(vinf*vinf + 2*MU_MARS/rp_m);
    const double v_capture_cible = std::sqrt(MU_MARS / rp_m);  // circulaire au rp atteint
    vinterp.dv_ins_reel = vp - v_capture_cible;
    // le corridor : r_p doit rester au-dessus de l'atmosphere ET sous le budget
    const double rp_min = R_MARS + 120e3, rp_max = R_MARS + 900e3;
    const bool dans_corridor = rp_m > rp_min && rp_m < rp_max;
    // et la marge d'insertion doit couvrir le surcout Oberth
    const double dv_nominal = std::sqrt(vinf*vinf + 2*MU_MARS/rp_vise) - std::sqrt(MU_MARS/rp_vise);
    const bool marge_ok = (vinterp.dv_ins_reel - dv_nominal) < cinterp.marge_dv;
    vinterp.corridor_ok = dans_corridor; vinterp.marge_ok = marge_ok;
    vinterp.ok = dans_corridor && marge_ok;
    if (vinterp.ok) { ++orbiteurs_mars; }
    vinterp.pourquoi = vinterp.ok
      ? "Le periastre est tombe dans le corridor ET la marge a couvert le surcout "
        "Oberth : insertion reussie."
      : !dans_corridor
        ? "Le periastre est tombe HORS corridor : trop bas tu brules dans l'atmosphere, "
          "trop haut tu ne captures pas. Tes TCM n'ont pas assez resserre la visee."
        : "Dans le corridor, mais l'erreur de visee a gonfle le cout d'insertion "
          "au-dela de ta marge (effet Oberth). Il fallait plus de marge ou une meilleure TCM.";
    std::snprintf(b, sizeof(b),
      "Rayon periastre atteint : %.0f km (corridor %.0f - %.0f km).\n"
      "Surcout Oberth d'insertion : %+.0f m/s vs nominal (marge %.0f m/s).\n"
      "TCM cumulees : %.0f m/s. Dispersion resid. 3-sigma : %.0f km.\n"
      "%s",
      vinterp.rp_km, rp_min/1000, rp_max/1000,
      vinterp.dv_ins_reel - dv_nominal, cinterp.marge_dv, vinterp.dv_tcm, ell,
      dans_corridor ? (marge_ok ? "Insertion reussie." : "Reservoir insuffisant : Oberth a mange la marge.")
                    : "Hors corridor : atmosphere ou survol.");
  }
  vinterp.verdict = b;
  vinterp.postmortem =
    "Le corridor du plan-B n'est pas une punition de jeu : c'est Oberth.\n"
    "Corriger tot coute des dizaines de m/s ; ne pas corriger en coute des\n"
    "milliers, parce que l'erreur de visee devient une erreur de vitesse a\n"
    "l'insertion, multipliee par l'effet de fronde au periastre.\n"
    "Ouvre le memo 'modele de dispersion' pour voir ce qui est mesure et ce\n"
    "qui est un modele declare.";
  agence.mois += 0.2;
  double prime_versee = 0.0;
  if (vinterp.ok) {
    const double fac = std::max(0.3, 1.0 - 0.06 * vinterp.retard_mois);   // clause de retard
    prime_versee = c.prime_succes * fac;
    agence.encaisser(prime_versee, "prime " + c.id + (vinterp.retard_mois > 0.05 ? " (reduite : retard)" : ""));
    if (vinterp.retard_mois > 0.05) { char rb[128]; std::snprintf(rb, sizeof(rb),
        "Retard de %.1f mois : prime reduite a %.0f %% (%.1f au lieu de %.1f M$).",
        vinterp.retard_mois, 100*fac, prime_versee, c.prime_succes); agence.log(rb); }
    c.reussi = true;
    agence.confiance = std::min(0.98, agence.confiance + 0.10); ++agence.reussites;
    agence.log("MISSION INTERPLANETAIRE REUSSIE : " + c.id + "."); }
  else { depense_obligatoire(c.penalite_echec, "penalite " + c.id); c.reussi = false;
    agence.confiance = std::max(0.0, agence.confiance - 0.12); ++agence.echecs;
    agence.log("MISSION INTERPLANETAIRE PERDUE : " + c.id + "."); }
  { const double recettes = c.spec.budget_musd + (vinterp.ok ? prime_versee : -c.penalite_echec);
    char b[140]; std::snprintf(b, sizeof(b), "MARGE de %s : %+.1f M$ (recettes %.1f - depenses %.1f)",
                               c.id.c_str(), recettes - cout_programme, recettes, cout_programme);
    agence.log(b); }
  teletype_i(vinterp.ok ? ">>> ARRIVEE : objectif tenu." : ">>> ARRIVEE : objectif manque.");
}

// ---------------------------------------------------------------------------
// Etude Mars (inchangee sur le fond)
// ---------------------------------------------------------------------------
void Jeu::etude_calculer_porkchop() {
  if (etude.calcul_en_cours || etude.calculee) return;
  if (!payer(etude.cout_calcul() * m_calcul(), "temps de calcul : porkchop Terre->Mars")) return;
  agence.mois += 0.25;
  etude.dep0 = epoch_from_iso("2026-06-01T00:00:00").tdb;
  etude.dep1 = epoch_from_iso("2029-08-31T00:00:00").tdb;
  etude.tof0 = 128.0 * DAY; etude.tof1 = 420.0 * DAY;
  if (worker && worker->joinable()) worker->join();
  etude.calcul_en_cours = true;
  worker = std::make_unique<std::thread>([this] {
    auto pc = astro::porkchop(eph, ephem::Body::EarthBary, ephem::Body::Mars,
                              etude.dep0, etude.dep1, etude.n_dep, etude.tof0, etude.tof1, etude.n_tof);
    etude.grille_c3.assign((size_t)etude.n_dep * etude.n_tof, 200.0f);
    for (int i = 0; i < etude.n_dep; ++i) for (int j = 0; j < etude.n_tof; ++j) {
      const auto& p = pc.at(i, j);
      if (p.ok) etude.grille_c3[(size_t)(etude.n_dep-1-i) * etude.n_tof + j] = (float)std::min(200.0, p.c3/1e6);
    }
    etude.best_c3 = pc.best_c3.c3/1e6; etude.best_dep = pc.best_c3.t_dep; etude.best_tof = pc.best_c3.tof;
    etude.best_dep_iso = epoch_to_iso(Epoch{pc.best_c3.t_dep}).substr(0, 10);
    etude.calculee = true; etude.calcul_en_cours = false;
  });
}
void Jeu::etude_livrer() {
  for (auto& c : contrats) {
    if (c.type != TypeContrat::EtudeMars || !c.accepte || c.termine) continue;
    if (!etude.calculee || !etude.corridor_vu) { erreur = "Livrables incomplets."; return; }
    c.termine = true; c.reussi = true; etude.livree = true;
    agence.mois += 0.5;
    agence.encaisser(c.spec.budget_musd, "livraison de l'etude " + c.id);
    agence.confiance = std::min(0.98, agence.confiance + 0.04); ++agence.reussites;
    agence.log("Etude livree. Fenetre suivante a +780 j (periode synodique).");
  }
}

// ---------------------------------------------------------------------------
// MEMOS TECHNIQUES : les calculs detailles, ouvrables dans l'UI
// ---------------------------------------------------------------------------
std::string Jeu::memo_derivations() const {
  const auto d = deriver_m00(R_PARK, I_PARK, R_GEO);
  char b[2600]; std::snprintf(b, sizeof(b),
    "POURQUOI CES CALCULS (la manoeuvre qui ferme le bilan)\n"
    "=====================================================\n\n"
    "Constantes : mu_Terre = 398600,4418 km3/s2 ; r_park = 6578,137 km ;\n"
    "r_geo = 42164,170 km ; i = 28,5 deg (inclinaison du parking).\n\n"
    "1) VITESSE CIRCULAIRE. Sur une orbite circulaire, la gravite fournit\n"
    "   exactement la force centripete : mu/r^2 = v^2/r  =>  v = sqrt(mu/r).\n"
    "   v_park = %.1f m/s.\n\n"
    "2) LE GTO. Pour monter, on passe sur une ellipse dont le perigee touche\n"
    "   le parking et l'apogee touche la GEO : a = (r_park + r_geo)/2.\n"
    "   VIS-VIVA donne la vitesse partout : v = sqrt(mu (2/r - 1/a)).\n"
    "   Au perigee : %.1f m/s. A l'apogee : %.1f m/s.\n\n"
    "3) INJECTION = %.1f m/s (on accelere du circulaire au perigee du GTO).\n\n"
    "4) LA CLE. A l'apogee il faut DEUX choses : passer de v_apo a v_geo (le\n"
    "   module) ET tourner le plan de 28,5 deg. Les faire SEPAREMENT coute\n"
    "   %.1f m/s. Les COMBINER en une seule impulsion, c'est la loi des\n"
    "   cosinus (un triangle de vitesses) :\n"
    "   dv = sqrt(v_apo^2 + v_geo^2 - 2 v_apo v_geo cos i) = %.1f m/s.\n\n"
    "   ECONOMIE = %.1f m/s. Ce n'est pas un detail : via Tsiolkovski, ces\n"
    "   ~1150 m/s valent ~54 %% de masse au decollage. Le joueur qui ne voit\n"
    "   pas (4) ne peut simplement pas payer son lanceur.\n\n"
    "GEOMETRIE CACHEE : le parking est livre AU NOEUD ASCENDANT pour que\n"
    "l'apogee du GTO tombe sur l'autre noeud - le seul endroit ou tourner\n"
    "le plan est possible sans gaspiller.",
    d.v_circ, d.v_gto_peri, d.v_gto_apo, d.dv_inj, d.dv_sep, d.dv_comb, d.economie);
  return b;
}
std::string Jeu::memo_vehicule() const {
  return
    "L'EQUATION DE LA FUSEE, A L'ENVERS (le point fixe)\n"
    "=================================================\n\n"
    "Tsiolkovski : dv = v_e ln(m0/mf), avec v_e = Isp g0.\n"
    "On CONNAIT dv (le budget) et on CHERCHE la masse d'ergols. Mais mf\n"
    "contient les reservoirs, qui pesent proportionnellement aux ergols :\n"
    "  mf = charge_utile + moteur + structure + (frac_reservoir+residu)*mp\n"
    "  m0 = mf exp(dv/v_e)\n"
    "  mp = m0 - mf\n"
    "mp apparait des DEUX cotes : c'est un POINT FIXE. size_stage_for_dv()\n"
    "l'itere jusqu'a convergence (~1e-9). Ce n'est pas une decision de\n"
    "conception, juste une iteration mecanique - donc le jeu la fait pour\n"
    "toi. Ce qu'il ne fait pas pour toi, c'est CHOISIR le dv (ca, c'est (4)).\n\n"
    "Consequence : un mauvais dv (plan separe) gonfle mp, donc les reservoirs,\n"
    "donc m0, donc le lanceur, donc le prix. La sanction est une masse.";
}
std::string Jeu::memo_liaison() const {
  return
    "LE BUDGET DE LIAISON (retour de donnees)\n"
    "========================================\n\n"
    "Equation de la liaison : P_r = P_t G_t G_r (lambda / 4 pi d)^2.\n"
    "Le debit s'effondre en 1/d^2 : a 1,5 UA de Mars, une antenne recoit\n"
    "~(1 UA / 1,5 UA)^2 = 44 % de ce qu'elle recevrait a 1 UA, et bien\n"
    "moins encore pour une comete a 3,5 UA. C'est pour ca que le retour\n"
    "scientifique est un BUDGET DE BITS, pas des 'points' : chaque Gbit se\n"
    "gagne contre la distance. La recherche 'Espace profond' achete du gain\n"
    "d'antenne (G_r), donc double le debit - de la vraie physique, pas un\n"
    "bonus arbitraire.";
}
std::string Jeu::memo_interp() const {
  char b[2200]; std::snprintf(b, sizeof(b),
    "CONCEVOIR UN TRANSFERT INTERPLANETAIRE\n"
    "======================================\n\n"
    "1) LAMBERT. Etant donne la position de la Terre au depart et celle de\n"
    "   la cible a l'arrivee, plus la duree de transit, le probleme de\n"
    "   Lambert donne l'UNIQUE ellipse heliocentrique qui relie les deux.\n"
    "   Il rend v1 (vitesse requise au depart) et v2 (a l'arrivee).\n\n"
    "2) C3 et v_inf. v_inf_depart = |v1 - v_Terre|. Le C3 = v_inf^2 est ce\n"
    "   que le lanceur doit fournir EN PLUS de l'echappement. C3 actuel :\n"
    "   %.1f km2/s2 (le plancher de Hohmann Terre-Mars est ~8,67).\n\n"
    "3) TMI depuis un parking : on part d'une orbite circulaire 300 km et\n"
    "   on accelere sur une hyperbole d'echappement de la bonne energie :\n"
    "   v_p = sqrt(v_inf^2 + 2 mu/r_p),  dv = v_p - sqrt(mu/r_p) = %.0f m/s.\n\n"
    "4) ARRIVEE = LE CORRIDOR. On ne vise pas 'une orbite', on vise un point\n"
    "   (B.T, B.R) dans le plan-B. La relation b <-> r_p est EXACTE :\n"
    "   b^2 = r_p^2 + 2 mu r_p / v_inf^2. Une erreur de visee b devient une\n"
    "   erreur de r_p, donc de vitesse d'insertion - amplifiee par Oberth.\n"
    "   D'ou l'echelle de TCM : corriger tot coute peu, tard coute cher,\n"
    "   pas du tout coute la mission.",
    cinterp.c3, cinterp.dv_tmi);
  return b;
}
std::string Jeu::memo_titan() const {
  return
    "TITAN : POURQUOI C'EST LE GRAAL\n"
    "===============================\n\n"
    "Saturne est a ~9,5 UA. Un transfert de Hohmann direct depuis la Terre\n"
    "exige un v_inf de depart d'environ 10-11 km/s, soit un C3 > 100 km2/s2.\n"
    "AUCUN lanceur ne vend ca : c'est LE MUR DU C3, et c'est reel (Cassini a\n"
    "du le contourner).\n\n"
    "LA SOLUTION : les assistances gravitationnelles. En passant pres de Venus,\n"
    "la Terre puis Jupiter (un tour V-E-E-J-S), on VOLE de l'energie aux planetes\n"
    "sans depenser d'ergols. L'outil t01_veega du noyau l'a MESURE : le meme tour\n"
    "passe de 24310 a 12763 m/s de Delta-v embarque - mieux que doubler le budget\n"
    "de calcul. C'est pour ca que l'option 'assistance' existe sur la carte.\n\n"
    "LE PRIX : le temps. Un tour a survols prend 6 a 8 ans au lieu de 3. Le\n"
    "calendrier devient l'ennemi, et le budget de liaison (Titan est loin, le\n"
    "debit s'effondre en 1/d^2) rend chaque bit precieux. C'est la mission qui\n"
    "recompense TOUT ce que le jeu enseigne.";
}
std::string Jeu::memo_modele_disp() const {
  return
    "CE QUI EST EXACT, CE QUI EST UN MODELE DECLARE\n"
    "==============================================\n\n"
    "EXACT (calcule par le noyau, verifie par les oracles) :\n"
    "  - Lambert (Izzo 2014), C3, v_inf a 1e-12 pres\n"
    "  - la relation b <-> r_p (conservation de h et de l'energie)\n"
    "  - le surcout Oberth a l'insertion : v_p = sqrt(v_inf^2 + 2mu/r_p)\n"
    "  - les orbites (Kepler variables universelles ; ephemeride Standish)\n\n"
    "MODELE DECLARE (par honnetete, pas par paresse) :\n"
    "  - L'ELLIPSE DE DISPERSION 3-sigma a l'arrivee n'est pas ici propagee\n"
    "    par un Monte-Carlo N-corps complet (trop lourd pour du temps reel).\n"
    "    On utilise l'ECHELLE MESUREE par l'outil m01_corridor sur la vraie\n"
    "    physique : aucune correction -> 87778 km ; TCM precoce -> 5195 km ;\n"
    "    TCM tardive -> 539 km ; les deux -> 121 km. Ce sont des chiffres\n"
    "    MESURES, pas inventes - mais figes, pas recalcules par tir.\n"
    "  - L'aerocapture (-20 % d'insertion) est un modele : l'atmosphere\n"
    "    freine, on ne simule pas la rentree. C'est signale la ou il agit.\n"
    "  - L'ASSISTANCE GRAVITATIONNELLE (missions Titan/comete) applique une\n"
    "    reduction de 47 % au dv de depart. Ce n'est pas invente : c'est\n"
    "    exactement le gain MESURE par l'outil t01_veega sur un tour V-E-E-J-S\n"
    "    (24310 -> 12763 m/s). En echange, +2 a 3 ans de croisiere.\n\n"
    "Regle du jeu (GDD axiome 2) : un modele simplifie est LEGITIME s'il est\n"
    "declare et borne. Il ne l'est pas s'il est cache. Voila, il est declare.";
}

// ---------------------------------------------------------------------------
// Persistance
// ---------------------------------------------------------------------------
bool Jeu::sauvegarder(const std::string& chemin) const {
  std::ofstream f(chemin);
  if (!f) return false;
  f << "FENETRE_SAUVEGARDE 2\n";
  f << "nom=" << agence.nom << "\nmode=" << (int)agence.mode << "\n";
  f << "tresorerie=" << agence.tresorerie << "\nmois=" << agence.mois << "\n";
  f << "confiance=" << agence.confiance << "\nreussites=" << agence.reussites
    << "\nechecs=" << agence.echecs << "\ngraine=" << agence.graine_agence << "\n";
  f << "donnees=" << donnees_gbit << "\nechant=" << echantillons_kg << "\n";
  f << "relais=" << relais_geo << "\norbmars=" << orbiteurs_mars << "\nsondes=" << sondes_lointaines << "\n";
  for (size_t i = 0; i < installations.size(); ++i) f << "inst" << i << "=" << installations[i].construite << "\n";
  for (size_t i = 0; i < recherches.size(); ++i)
    f << "rech" << i << "=" << recherches[i].faite << " " << recherches[i].active << " " << recherches[i].avancement << "\n";
  for (size_t i = 0; i < contrats.size() && i < 6; ++i)
    f << "contrat" << i << "=" << contrats[i].accepte << " " << contrats[i].termine << " " << contrats[i].reussi << "\n";
  for (const auto& l : agence.journal) f << "J " << l.mois << "|" << l.texte << "\n";
  return true;
}
bool Jeu::charger(const std::string& chemin) {
  std::ifstream f(chemin);
  if (!f) return false;
  std::string ligne;
  if (!std::getline(f, ligne) || ligne.rfind("FENETRE_SAUVEGARDE", 0) != 0) return false;
  generer_contrats();
  Jeu neuf; installations = neuf.installations; recherches = neuf.recherches;
  agence = Agence{}; agence.creee = true;
  contrat_actif = -1; donnees_gbit = echantillons_kg = 0; relais_geo = orbiteurs_mars = 0;
  while (std::getline(f, ligne)) {
    if (ligne.rfind("J ", 0) == 0) { auto bar = ligne.find('|');
      if (bar != std::string::npos) agence.journal.push_back({std::atof(ligne.substr(2, bar-2).c_str()), ligne.substr(bar+1)});
      continue; }
    auto eq = ligne.find('='); if (eq == std::string::npos) continue;
    const std::string k = ligne.substr(0, eq), v = ligne.substr(eq+1);
    if (k == "nom") agence.nom = v;
    else if (k == "mode") agence.mode = (ModeAide)std::atoi(v.c_str());
    else if (k == "tresorerie") agence.tresorerie = std::atof(v.c_str());
    else if (k == "mois") agence.mois = std::atof(v.c_str());
    else if (k == "confiance") agence.confiance = std::atof(v.c_str());
    else if (k == "reussites") agence.reussites = std::atoi(v.c_str());
    else if (k == "echecs") agence.echecs = std::atoi(v.c_str());
    else if (k == "graine") agence.graine_agence = std::strtoull(v.c_str(), nullptr, 10);
    else if (k == "donnees") donnees_gbit = std::atof(v.c_str());
    else if (k == "echant") echantillons_kg = std::atof(v.c_str());
    else if (k == "relais") relais_geo = std::atoi(v.c_str());
    else if (k == "orbmars") orbiteurs_mars = std::atoi(v.c_str());
    else if (k == "sondes") sondes_lointaines = std::atoi(v.c_str());
    else if (k.rfind("inst", 0) == 0) { int i = std::atoi(k.c_str()+4);
      if (i >= 0 && i < (int)installations.size()) installations[i].construite = std::atoi(v.c_str()); }
    else if (k.rfind("rech", 0) == 0) { int i = std::atoi(k.c_str()+4);
      if (i >= 0 && i < (int)recherches.size()) { std::istringstream ss(v); ss >> recherches[i].faite >> recherches[i].active >> recherches[i].avancement; } }
    else if (k.rfind("contrat", 0) == 0) { int i = std::atoi(k.c_str()+7);
      if (i >= 0 && i < (int)contrats.size()) { std::istringstream ss(v); ss >> contrats[i].accepte >> contrats[i].termine >> contrats[i].reussi; } }
  }
  return true;
}

} // namespace fen::app

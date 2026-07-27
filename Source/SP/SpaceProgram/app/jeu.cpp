// app/jeu.cpp - le modele du jeu : l'agence spatiale (couche vivante). Tout
// chiffre affiche sort d'ici ; tout ce qui est physique sort du noyau.
//
// Voir jeu.hpp pour la note de SCISSION (2026-07-26) : la mecanique de vol 2D
// heritee (conception/VAB, vol GEO, interplanetaire, etude, marche, Monte-Carlo,
// installations, recherches maison) a ete retiree car plus atteignable depuis le
// rendu total UE5. Ne subsistent que l'agence, le catalogue de contrats, la
// flotte [GDD 8.3], l'economie stricte, l'epoque [GDD 14.1] et la persistance.
#include "app/jeu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include "fen/astro/Kepler.hpp"
#include "fen/core/Epoch.hpp"

namespace fen::app {
using namespace fen::cst;
static constexpr double R_GEO = 42164170.0;   // orbite de reference des relais

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

// ---------------------------------------------------------------------------
// ECONOMIE STRICTE : jamais de solde negatif silencieux.
//  - achat VOLONTAIRE : refuse si les fonds manquent (message clair) ;
//  - depense OBLIGATOIRE (salaires, penalite) : si elle met la tresorerie a
//    zero ou moins -> FAILLITE, game over motive.
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

// TOUTE PARTIE DEMARRE EN PAUSE [GDD 14.2]. Le temps est une DEPENSE (charges
// fixes superieures aux recettes garanties) : il ne doit jamais se mettre a couler
// sans que le joueur l'ait demande - ni a la fondation, ni au chargement, ni en
// heritant la cadence de la partie precedente.
void Jeu::remettre_horloge_en_pause() {
  cadence = game::TimeRate::Paused;
  accu_jours = 0.0;
}

// ---------------------------------------------------------------------------
// LE RYTHME DU TEMPS EN MISSION [GDD 14.3]. La loi vit dans le coeur pur
// (mission/MissionTempo.hpp) ; ici on la branche sur la partie en cours.
// ---------------------------------------------------------------------------
mission::TempoLimit Jeu::plafond_temps() const {
  if (!ares.initialisee()) return {};          // pas de mission : aucun plafond
  const auto& G = *ares.etat;
  return mission::tempo_limit(G.missions, G.clock.now_days());
}

// La porte d'entree du joueur. Renvoie FAUX si la demande a du etre bornee :
// l'appelant (bandeau du temps, poste AGENCE, touches) sait donc qu'il doit
// MONTRER un refus, et non poser un cran qui ne s'appliquera pas.
bool Jeu::regler_cadence(game::TimeRate r) {
  const game::TimeRate plafond = plafond_temps().max_rate;
  const bool borne = static_cast<int>(r) > static_cast<int>(plafond);
  cadence = borne ? plafond : r;
  return !borne;
}

// RAMENE la cadence sous le plafond. C'est le verbe du GDD : ce n'est pas au
// joueur de ralentir quand une manoeuvre fine commence, c'est la manoeuvre qui
// ralentit le monde. Renvoie vrai si elle a effectivement freine.
bool Jeu::appliquer_plafond() {
  const game::TimeRate plafond = plafond_temps().max_rate;
  if (static_cast<int>(cadence) <= static_cast<int>(plafond)) return false;
  cadence = plafond;
  accu_jours = 0.0;   // le reste accumule appartenait a l'ancienne cadence
  return true;
}

void Jeu::reinitialiser() {
  agence = Agence{};
  remettre_horloge_en_pause();
  contrat_actif = -1;
  donnees_gbit = echantillons_kg = 0;
  relais_geo = orbiteurs_mars = sondes_lointaines = 0;
  flotte.clear();
  epoch0_tdb = 0;
  game_over = false; raison_faillite.clear(); cout_programme = 0;
  erreur.clear();
  generer_contrats();
}

Jeu::Jeu() {
  generer_contrats();
}

// ---------------------------------------------------------------------------
// CATALOGUE de contrats : affiche par le poste PLANIFICATION.
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
  return relais_geo * 1.2 + orbiteurs_mars * 0.8 + sondes_lointaines * 0.5;
}
double Jeu::epoch_courant() const {
  // [GDD 14.1] : base = l'instant REEL fige a la creation (epoch0) ; le temps de
  // jeu avance ensuite avec le calendrier de l'agence. Les parties d'avant la
  // v0.7 (epoch0 absent de la save) gardent leur calendrier illustratif 2027.
  const double base = (epoch0_tdb != 0.0) ? epoch0_tdb
                                          : epoch_from_iso("2027-03-14T00:00:00").tdb;
  return base + agence.mois * ARES_MONTH_S;
}

// ---------------------------------------------------------------------------
// LE TEMPS QUI COULE [GDD 14.2]. Voir jeu.hpp pour la doctrine (calendrier
// continu, comptabilite mensuelle, sous-pas fixes).
// ---------------------------------------------------------------------------
double Jeu::faire_couler_le_temps(double dt_reel_s) {
  if (!agence.creee || game_over) { accu_jours = 0.0; return 0.0; }
  // [GDD 14.3] LE PLAFOND D'ABORD : une phase critique qui s'ouvre freine le
  // monde DANS LA FRAME OU ELLE S'OUVRE. Le rappeler ici plutot qu'au seul
  // reglage rend la faute impossible - qui que ce soit qui ait pose `cadence`,
  // pas une seconde de jeu ne se convertit au-dessus du plafond.
  appliquer_plafond();
  const double s_par_s = game::rate_seconds_per_second(cadence);
  if (s_par_s <= 0.0 || !(dt_reel_s > 0.0)) return 0.0;
  // GARDE-FOU : une frame anormalement longue (compilation de shaders, fenetre
  // deplacee, point d'arret) ne doit pas TELEPORTER le calendrier de plusieurs
  // mois - ce serait une avance que le joueur n'a pas demandee, avec ses charges.
  const double dt = (dt_reel_s > 0.25) ? 0.25 : dt_reel_s;
  accu_jours += dt * s_par_s / DAY;
  const double pas = std::floor(accu_jours / PAS_JOURS);
  if (pas < 1.0) return 0.0;
  const double jours = pas * PAS_JOURS;
  accu_jours -= jours;
  avancer_temps(jours);
  return jours;
}

void Jeu::avancer_temps(double jours) {
  if (!agence.creee || game_over || !(jours > 0.0)) return;
  const double cible = agence.mois + jours * DAY / ARES_MONTH_S;
  // Solder chaque mois ENTIER franchi. passer_mois() porte la comptabilite ET
  // incremente `mois` de 1 : en le repositionnant d'abord sur la frontiere, on
  // avance donc de frontiere en frontiere, sans jamais en sauter ni en compter
  // deux fois. La boucle termine : chaque tour augmente floor(mois) de 1.
  while (std::floor(cible) > std::floor(agence.mois)) {
    agence.mois = std::floor(agence.mois);
    passer_mois();
    if (game_over) return;      // faillite : le calendrier s'arrete la
  }
  agence.mois = cible;
}

// ---------------------------------------------------------------------------
// FLOTTE [GDD 8.3] : l'ephemeride ENTRETENUE de chaque engin en service.
// Kepler 2 corps, plan ecliptique - modele DECLARE [GDD 6.8], publie comme
// ESTIMATION de navigation [GDD 7.5], jamais comme une verite.
// ---------------------------------------------------------------------------
int Jeu::flotte_parent(const EnginFlotte& e) const {
  switch (e.type) {
    case EnginFlotte::RelaisGeo:    return (int)ephem::Body::EarthBary;
    case EnginFlotte::OrbiteurMars: return (int)ephem::Body::Mars;
    default:                        return (int)ephem::Body::Sun;
  }
}
Vec3 Jeu::flotte_position_rel(const EnginFlotte& e, double t) const {
  if (e.type == EnginFlotte::SondeLointaine) {
    const auto k = astro::kepler_propagate(e.r0, e.v0, t - e.t0, MU_SUN);
    return k.converged ? k.r : e.r0;
  }
  const double mu = (e.type == EnginFlotte::RelaisGeo) ? MU_EARTH : MU_MARS;
  const double sma = std::max(1.0, e.sma_m);
  const double n = std::sqrt(mu / (sma * sma * sma));   // moyen mouvement circulaire
  const double a = e.phase0 + n * (t - e.t0);
  return Vec3{sma * std::cos(a), sma * std::sin(a), 0.0};
}

// Sauvegardes anterieures a la v0.7 : les compteurs existent sans ephemerides.
// RECONSTRUCTION DECLAREE : orbites de reference, phases regulieres. Les
// prochains engins mis en service porteront leurs vrais elements.
void Jeu::flotte_reconstruire() {
  auto compte = [this](int ty) {
    int n = 0;
    for (const auto& e : flotte) if (e.type == ty) ++n;
    return n;
  };
  const double t = epoch_courant();
  for (int k = compte(EnginFlotte::RelaisGeo); k < relais_geo; ++k) {
    EnginFlotte e; e.type = EnginFlotte::RelaisGeo;
    e.nom = "RELAIS-" + std::to_string(k + 1);
    e.t0 = t; e.sma_m = R_GEO;
    e.phase0 = 0.6 + TWO_PI * k / std::max(1, relais_geo);
    flotte.push_back(e);
  }
  for (int k = compte(EnginFlotte::OrbiteurMars); k < orbiteurs_mars; ++k) {
    EnginFlotte e; e.type = EnginFlotte::OrbiteurMars;
    e.nom = "ORBITEUR-" + std::to_string(k + 1);
    e.t0 = t; e.sma_m = R_MARS + 500e3;   // orbite de reference du contrat Mars
    e.phase0 = 1.1 + TWO_PI * k / std::max(1, orbiteurs_mars);
    flotte.push_back(e);
  }
  for (int k = compte(EnginFlotte::SondeLointaine); k < sondes_lointaines; ++k) {
    EnginFlotte e; e.type = EnginFlotte::SondeLointaine;
    e.nom = "SONDE-" + std::to_string(k + 1);
    e.t0 = t;
    const double ang = 0.5 + 0.9 * k, r = (20.0 + 6.0 * k) * AU;
    e.r0 = Vec3{r * std::cos(ang), r * std::sin(ang), 0.0};
    const double vc = std::sqrt(MU_SUN / r);            // orbite circulaire declaree
    e.v0 = Vec3{-vc * std::sin(ang), vc * std::cos(ang), 0.0};
    flotte.push_back(e);
  }
}

// [GDD 14.1] La date/heure REELLE (UTC systeme), en s TDB depuis J2000 — lue UNE
// SEULE fois, a la creation de la partie. Ensuite epoch0 est un etat sauvegarde :
// le determinisme (save/load/rejeu) est preserve.
static double epoch_reelle_tdb() {
  const std::time_t tt = std::time(nullptr);
  std::tm g{};
#if defined(_WIN32)
  gmtime_s(&g, &tt);
#else
  gmtime_r(&tt, &g);
#endif
  char iso[40];
  std::snprintf(iso, sizeof iso, "%04d-%02d-%02dT%02d:%02d:%02d",
                g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
  return epoch_from_iso(iso).tdb;
}

void Jeu::creer_agence(const std::string& nom, ModeAide mode) {
  agence = Agence{};
  remettre_horloge_en_pause();
  agence.creee = true;
  agence.mode = mode;
  agence.nom = nom.empty() ? "AGENCE SANS NOM" : nom;
  agence.graine_agence = 0x5DEECE66DULL;
  for (char ch : agence.nom) agence.graine_agence = agence.graine_agence * 131 + (unsigned char)ch;
  agence.tresorerie = (mode == ModeAide::Pro) ? 32.0 : 45.0;
  epoch0_tdb = epoch_reelle_tdb();   // [GDD 14.1] monde fige sur l'instant reel
  agence.log("Systeme solaire synchronise sur l'instant reel de la fondation.");
  agence.log("Agence fondee. Dotation : " + std::to_string((int)agence.tresorerie) + " M$.");
  agence.log(mode == ModeAide::Pro
    ? "Mode PRO : aucune aide. Tu realises tous les calculs toi-meme."
    : "Mode NORMAL : l'assistant est disponible pour te guider dans les calculs.");
}

// ---------------------------------------------------------------------------
// LE TOUR mensuel : charges recurrentes, revenus science, echeances de contrat.
// ---------------------------------------------------------------------------
void Jeu::passer_mois() {
  if (game_over) return;
  agence.mois += 1.0;
  // --- COUTS RECURRENTS : salaires de l'equipe + operations de la flotte. ---
  double fixe = 0.6;                                   // salaires
  fixe += relais_geo * 0.05 + orbiteurs_mars * 0.12 + sondes_lointaines * 0.20;   // ops flotte
  depense_obligatoire(fixe, "charges du mois (salaires, flotte)");
  if (game_over) return;
  // --- revenus science de la flotte en orbite ---
  double gbit = revenu_mensuel_gbit();
  if (gbit > 0) { donnees_gbit += gbit;
    char b[96]; std::snprintf(b, sizeof(b), "+%.1f Gbit de donnees (flotte en orbite)", gbit);
    agence.log(b); }
  // --- ECHEANCES : un contrat signe et non livre a une date limite ---
  for (auto& c : contrats) {
    if (!c.accepte || c.termine || c.mois_signature < 0) continue;
    if (agence.mois - c.mois_signature > c.spec.deadline_months) {
      c.termine = true; c.reussi = false;
      depense_obligatoire(c.penalite_echec * 0.5, "resiliation " + c.id + " (echeance depassee)");
      agence.confiance = std::max(0.0, agence.confiance - 0.08);
      agence.log("Le client de " + c.id + " a RESILIE : echeance depassee.");
      if (contrat_actif >= 0 && &c == &contrats[contrat_actif]) contrat_actif = -1;
    }
  }
}

// ---------------------------------------------------------------------------
// PERSISTANCE
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
  { char b[48]; std::snprintf(b, sizeof b, "epoch0=%.17g\n", epoch0_tdb); f << b; }   // [GDD 14.1]
  f << "relais=" << relais_geo << "\norbmars=" << orbiteurs_mars << "\nsondes=" << sondes_lointaines << "\n";
  for (const auto& e : flotte) {   // ephemerides individuelles [GDD 8.3]
    char b[420];                   // %.17g : epoques TDB et etats en m / m/s
    std::snprintf(b, sizeof(b),
        "engin=%d %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %s\n",
        e.type, e.t0, e.sma_m, e.phase0, e.r0.x, e.r0.y, e.r0.z,
        e.v0.x, e.v0.y, e.v0.z, e.nom.c_str());
    f << b;
  }
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
  agence = Agence{}; agence.creee = true;
  remettre_horloge_en_pause();     // on ne charge JAMAIS dans une partie qui defile
  contrat_actif = -1; donnees_gbit = echantillons_kg = 0;
  relais_geo = orbiteurs_mars = sondes_lointaines = 0; flotte.clear();
  epoch0_tdb = 0;   // saves d'avant v0.7 : reste 0 -> calendrier illustratif
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
    else if (k == "epoch0") epoch0_tdb = std::atof(v.c_str());
    else if (k == "relais") relais_geo = std::atoi(v.c_str());
    else if (k == "orbmars") orbiteurs_mars = std::atoi(v.c_str());
    else if (k == "sondes") sondes_lointaines = std::atoi(v.c_str());
    else if (k == "engin") {
      EnginFlotte e; std::istringstream ss(v);
      ss >> e.type >> e.t0 >> e.sma_m >> e.phase0 >> e.r0.x >> e.r0.y >> e.r0.z
         >> e.v0.x >> e.v0.y >> e.v0.z;
      if (ss) {
        std::getline(ss, e.nom);
        const auto deb = e.nom.find_first_not_of(' ');
        e.nom = (deb == std::string::npos) ? "" : e.nom.substr(deb);
        flotte.push_back(e);
      }
    }
    else if (k.rfind("contrat", 0) == 0) { int i = std::atoi(k.c_str()+7);
      if (i >= 0 && i < (int)contrats.size()) { std::istringstream ss(v); ss >> contrats[i].accepte >> contrats[i].termine >> contrats[i].reussi; } }
    // NB : les cles heritees "inst*"/"rech*" (installations/recherches maison,
    // systeme retire a la scission) sont simplement ignorees si presentes dans
    // une vieille sauvegarde.
  }
  // sauvegardes anterieures a la v0.7 (compteurs seuls) : reconstruction declaree
  flotte_reconstruire();
  return true;
}

} // namespace fen::app

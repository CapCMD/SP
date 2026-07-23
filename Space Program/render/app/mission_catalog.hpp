// render/app/mission_catalog.hpp
//
// CATALOGUE DE MISSIONS HISTORIQUES + ARBRE DE COMPETENCES (contenu du jeu 3D).
// Donnee PURE (chaines litterales statiques, aucune allocation) : l'app la traduit
// en presentation (spr::PanelList / spr::PanelTree) pour les postes de l'ISS. Ne
// touche NI la physique NI astro_core : c'est du CONTENU deverrouille selon le
// NIVEAU de l'agence (derive de sa progression reelle).
//
// Chaque mission decrit du MATERIEL REEL : lanceur, vaisseau/sonde, instruments,
// cible, exploit, difficulte (1..5) et palier de deverrouillage (tier 0..6).
#pragma once
#include <cstddef>
#include "fen/ephem/Ephemeris.hpp"   // fen::ephem::Body (cible reelle des missions)

namespace spgame {

// --- paliers d'agence (niveau) : gouvernent le deverrouillage -----------------
enum Tier { T_STARTUP = 0, T_LEO, T_CREWED, T_MOON, T_PLANETS, T_OUTER, T_FRONTIER, TIER_COUNT };
inline const char* tier_name(int t) {
  static const char* N[TIER_COUNT] = {
    "STARTUP", "ORBITE BASSE", "VOL HABITE", "LUNE", "PLANETES", "SYSTEME EXTERNE", "FRONTIERE"
  };
  return (t >= 0 && t < TIER_COUNT) ? N[t] : "?";
}

// NIVEAU de l'agence, derive de sa progression REELLE (reussites + flotte). Le
// modele de jeu n'a pas de "niveau" explicite : on en calcule un, sans rien changer
// au modele. Plus l'agence accomplit, plus haut le palier -> plus de missions
// deverrouillees. (Barème volontairement lisible.)
inline int agency_level(int reussites, int relais_geo, int orbiteurs_mars,
                        int sondes_lointaines, double tresorerie) {
  int prog = reussites * 2 + relais_geo + orbiteurs_mars * 2 + sondes_lointaines * 3;
  if (tresorerie >= 120.0) prog += 1;
  if (tresorerie >= 250.0) prog += 2;
  int lvl = 0;
  const int seuils[TIER_COUNT] = {0, 1, 4, 8, 14, 22, 32};
  for (int t = 0; t < TIER_COUNT; ++t) if (prog >= seuils[t]) lvl = t;
  return lvl;
}

// --- une mission type (materiel reel) ----------------------------------------
struct MissionDef {
  const char* name;      // designation
  int         year;      // annee de lancement
  const char* agency;    // NASA / URSS / ESA / JAXA / CNSA / SpaceX...
  const char* vehicle;   // LANCEUR reel
  const char* craft;     // vaisseau / sonde reel
  const char* payload;   // instruments / pieces clefs reelles
  const char* target;    // cible
  const char* feat;      // exploit / premiere
  int         difficulty;// 1..5
  int         tier;      // palier de deverrouillage (Tier)
};

// ~48 missions, des premiers satellites a la frontiere du systeme solaire.
inline constexpr MissionDef MISSIONS[] = {
  // --- T_STARTUP : premiers satellites ---------------------------------------
  {"Spoutnik 1",        1957, "URSS", "R-7 Semiorka",         "Spoutnik PS-1",         "emetteur radio 20/40 MHz",        "Orbite basse", "Premier satellite artificiel",              2, T_STARTUP},
  {"Explorer 1",        1958, "USA",  "Juno I (Jupiter-C)",   "Explorer 1",            "compteur Geiger (Van Allen)",     "Orbite basse", "Decouverte des ceintures de Van Allen",     2, T_STARTUP},
  {"Vanguard 1",        1958, "USA",  "Vanguard",             "Vanguard 1",            "cellules solaires",               "Orbite basse", "Premier satellite a energie solaire",       2, T_STARTUP},
  {"Telstar 1",         1962, "USA",  "Thor-Delta",           "Telstar",               "transpondeur actif",              "Orbite basse", "Premier relais TV transatlantique",         2, T_STARTUP},
  // --- T_LEO : premiers vols habites orbitaux, reconnaissance -----------------
  {"Vostok 1",          1961, "URSS", "Vostok-K (R-7)",       "Vostok 3KA",            "capsule, siege ejectable",        "Orbite basse", "Premier humain en orbite (Gagarine)",       3, T_LEO},
  {"Mercury-Atlas 6",   1962, "USA",  "Atlas LV-3B",          "Mercury 'Friendship 7'","retrofusees, bouclier ablatif",   "Orbite basse", "Premier Americain en orbite (Glenn)",       3, T_LEO},
  {"Vostok 6",          1963, "URSS", "Vostok-K (R-7)",       "Vostok 3KA",            "capsule",                         "Orbite basse", "Premiere femme dans l'espace (Terechkova)", 3, T_LEO},
  {"Corona KH-4",       1962, "USA",  "Thor-Agena",           "Corona",                "capsule de film recuperable",     "Orbite basse", "Reconnaissance photographique",             3, T_LEO},
  // --- T_CREWED : EVA, rendez-vous, premiers survols planetaires --------------
  {"Voskhod 2",         1965, "URSS", "Voskhod (R-7)",        "Voskhod 3KD",           "sas gonflable Volga",             "Orbite basse", "Premiere sortie extravehiculaire (Leonov)", 3, T_CREWED},
  {"Gemini 4",          1965, "USA",  "Titan II GLV",         "Gemini",                "pistolet de manoeuvre a main",    "Orbite basse", "Premiere EVA americaine (White)",           3, T_CREWED},
  {"Gemini 8",          1966, "USA",  "Titan II GLV",         "Gemini + cible Agena",  "systeme d'amarrage",              "Orbite basse", "Premier amarrage orbital (Armstrong)",      4, T_CREWED},
  {"Mariner 2",         1962, "USA",  "Atlas-Agena B",        "Mariner",               "radiometre micro-ondes",          "Venus",        "Premier survol reussi d'une planete",       3, T_CREWED},
  {"Mariner 4",         1964, "USA",  "Atlas-Agena D",        "Mariner",               "camera TV a balayage",            "Mars",         "Premieres photos rapprochees de Mars",      3, T_CREWED},
  // --- T_MOON : atterrisseurs, orbiteurs, vols habites lunaires ---------------
  {"Luna 9",            1966, "URSS", "Molniya-M",            "Luna E-6",              "camera panoramique",              "Lune",         "Premier atterrissage en douceur",           4, T_MOON},
  {"Surveyor 1",        1966, "USA",  "Atlas-Centaur",        "Surveyor",              "camera, patins amortisseurs",     "Lune",         "Alunissage doux (preparation Apollo)",      4, T_MOON},
  {"Lunar Orbiter 1",   1966, "USA",  "Atlas-Agena D",        "Lunar Orbiter",         "camera Kodak, film developpe bord","Lune",        "Cartographie des sites d'alunissage",       3, T_MOON},
  {"Apollo 8",          1968, "USA",  "Saturn V",             "CSM Apollo",            "moteur SPS, calculateur AGC",     "Lune",         "Premier vol habite autour de la Lune",      5, T_MOON},
  {"Apollo 11",         1969, "USA",  "Saturn V",             "CSM Columbia + LEM Eagle","EASEP, scaphandre A7L, PLSS",   "Lune",         "Premier alunissage habite",                 5, T_MOON},
  {"Luna 16",           1970, "URSS", "Proton-K / Bloc D",    "Luna E-8-5",            "foreuse, capsule de retour",      "Lune",         "Retour d'echantillon robotique",            5, T_MOON},
  {"Lunokhod 1",        1970, "URSS", "Proton-K / Bloc D",    "Lunokhod",              "rover telecommande 8 roues",      "Lune",         "Premier rover extraterrestre",              4, T_MOON},
  {"Apollo 15",         1971, "USA",  "Saturn V",             "CSM + LEM + LRV",       "Rover lunaire (LRV), ALSEP",      "Lune",         "Premiere mission J (rover lunaire)",        5, T_MOON},
  // --- T_PLANETS : Venus/Mars/Mercure, assistances gravitationnelles ----------
  {"Venera 7",          1970, "URSS", "Molniya-M",            "Venera",                "capsule blindee (90 bar)",        "Venus",        "Premier atterrissage sur une autre planete",5, T_PLANETS},
  {"Mariner 9",         1971, "USA",  "Atlas-Centaur",        "Mariner",               "cameras, UVS, IRIS",              "Mars",         "Premier orbiteur d'une autre planete",      4, T_PLANETS},
  {"Mariner 10",        1973, "USA",  "Atlas-Centaur",        "Mariner",               "cameras, magnetometre",           "Mercure/Venus","Premiere assistance gravitationnelle",      4, T_PLANETS},
  {"Viking 1",          1975, "USA",  "Titan IIIE-Centaur",   "Viking orbiteur+lander","bras, GCMS, biologie",            "Mars",         "Premiere recherche de vie in situ",         5, T_PLANETS},
  {"Venera 9",          1975, "URSS", "Proton-K",             "Venera",                "camera de surface, bouclier",     "Venus",        "Premieres images du sol de Venus",          5, T_PLANETS},
  {"Magellan",          1989, "USA",  "Navette STS-30 / IUS", "Magellan",              "radar a synthese d'ouverture",    "Venus",        "Cartographie radar globale de Venus",       4, T_PLANETS},
  {"Mars Pathfinder",   1996, "USA",  "Delta II",             "lander + rover Sojourner","airbags, APXS",                 "Mars",         "Premier rover martien (demonstration)",     4, T_PLANETS},
  {"MER Spirit/Oppy",   2003, "USA",  "Delta II",             "Mars Exploration Rover","Pancam, Mini-TES, RAT",           "Mars",         "Exploration prolongee par rovers",          5, T_PLANETS},
  {"MESSENGER",         2004, "USA",  "Delta II",             "MESSENGER",             "spectrometres, bouclier thermique","Mercure",     "Premier orbiteur de Mercure",               5, T_PLANETS},
  {"Curiosity (MSL)",   2011, "USA",  "Atlas V 541",          "Curiosity",             "sky crane, RTG, ChemCam, SAM",    "Mars",         "Rover nucleaire pose par sky crane",        5, T_PLANETS},
  {"Perseverance",      2020, "USA",  "Atlas V 541",          "Perseverance + Ingenuity","cache d'echantillons, MOXIE",   "Mars",         "Premier vol motorise sur Mars",             5, T_PLANETS},
  {"Parker Solar Probe",2018, "USA",  "Delta IV Heavy",       "Parker",                "bouclier carbone-carbone, FIELDS","Soleil",       "Plongee dans la couronne solaire",          5, T_PLANETS},
  // --- T_OUTER : geantes gazeuses, grands observatoires -----------------------
  {"Pioneer 10",        1972, "USA",  "Atlas-Centaur",        "Pioneer",               "RTG SNAP-19, photopolarimetre",   "Jupiter",      "Premier survol de Jupiter",                 4, T_OUTER},
  {"Voyager 1",         1977, "USA",  "Titan IIIE-Centaur",   "Voyager",               "cameras ISS, RTG, disque d'or",   "Jupiter/Saturne","Grand Tour ; objet humain le plus lointain",5, T_OUTER},
  {"Voyager 2",         1977, "USA",  "Titan IIIE-Centaur",   "Voyager",               "cameras, PLS, magnetometre",      "Uranus/Neptune","Seule visite d'Uranus et Neptune",          5, T_OUTER},
  {"Galileo",           1989, "USA",  "Navette STS-34 / IUS", "Galileo + sonde atmo",  "NIMS, sonde de descente",         "Jupiter",      "Premier orbiteur de Jupiter + sonde",       5, T_OUTER},
  {"Hubble (HST)",      1990, "USA",  "Navette STS-31",       "Hubble",                "miroir 2,4 m, WFPC, COSTAR",      "Orbite basse", "Grand observatoire spatial reparable",      5, T_OUTER},
  {"Dawn",              2007, "USA",  "Delta II",             "Dawn",                  "propulsion ionique (xenon)",      "Vesta/Ceres",  "Orbite autour de deux corps (ion)",         5, T_OUTER},
  {"New Horizons",      2006, "USA",  "Atlas V 551",          "New Horizons",          "LORRI, RTG, Ralph/Alice",         "Pluton",       "Premier survol de Pluton",                  5, T_OUTER},
  {"Juno",              2011, "USA",  "Atlas V 551",          "Juno",                  "grands panneaux solaires, JIRAM", "Jupiter",      "Orbiteur polaire solaire de Jupiter",       5, T_OUTER},
  // --- T_FRONTIER : retours d'echantillons, deviation, JWST, SLS --------------
  {"Cassini-Huygens",   1997, "USA/ESA","Titan IVB-Centaur",  "Cassini + Huygens",     "RTG, radar, atterrisseur Huygens","Saturne/Titan","Orbiteur Saturne + pose sur Titan",         5, T_FRONTIER},
  {"Rosetta / Philae",  2004, "ESA",  "Ariane 5 G+",          "Rosetta + Philae",      "harpons, foreuse SD2, panneaux",  "Comete 67P",   "Premier orbiteur + atterrisseur cometaire", 5, T_FRONTIER},
  {"Stardust",          1999, "USA",  "Delta II",             "Stardust",              "collecteur d'aerogel",            "Comete Wild 2","Retour d'echantillon cometaire",            5, T_FRONTIER},
  {"Hayabusa2",         2014, "JAXA", "H-IIA",                "Hayabusa2",             "impacteur SCI, capsule de retour","Asteroide Ryugu","Retour d'echantillon d'asteroide",         5, T_FRONTIER},
  {"OSIRIS-REx",        2016, "USA",  "Atlas V 411",          "OSIRIS-REx",            "bras TAGSAM, capsule SRC",        "Asteroide Bennu","Prelevement et retour (Bennu)",            5, T_FRONTIER},
  {"JWST",              2021, "USA/ESA","Ariane 5 ECA",       "James Webb",            "miroir 6,5 m, pare-soleil, NIRCam","Point L2",     "Grand observatoire infrarouge",             5, T_FRONTIER},
  {"DART",              2021, "USA",  "Falcon 9",             "DART",                  "impacteur cinetique, DRACO",      "Asteroide Dimorphos","Premiere deviation d'asteroide",         4, T_FRONTIER},
  {"Artemis I",         2022, "USA",  "SLS Block 1",          "Orion",                 "bouclier Orion, ESM (ESA)",       "Lune",         "Retour vers la Lune (non habite)",          5, T_FRONTIER},
};
inline constexpr int MISSION_COUNT = static_cast<int>(sizeof(MISSIONS) / sizeof(MISSIONS[0]));

// TECHNOLOGIES REQUISES par mission (ids de TECH_NODES ; nullptr = fin). Une mission
// n'est DEVERROUILLEE que si son palier est atteint ET toutes ses techs sont
// recherchees -> l'arbre de competences GATE reellement le catalogue. Tableau
// PARALLELE a MISSIONS (meme ordre). Chaque tech reference une piece reelle du vol.
struct MissionReq { const char* tech[4]; };
inline constexpr MissionReq MISSION_REQS[] = {
  /*Spoutnik 1*/       {{"l_med", "com_s", nullptr, nullptr}},
  /*Explorer 1*/       {{"l_small", "com_s", nullptr, nullptr}},
  /*Vanguard 1*/       {{"l_small", "nrg_solar", nullptr, nullptr}},
  /*Telstar 1*/        {{"l_med", "nrg_solar", "com_x", nullptr}},
  /*Vostok 1*/         {{"hab_capsule", "s_ablat", "l_med", nullptr}},
  /*Mercury-Atlas 6*/  {{"hab_capsule", "s_ablat", "a_inert", nullptr}},
  /*Vostok 6*/         {{"hab_capsule", "s_ablat", nullptr, nullptr}},
  /*Corona KH-4*/      {{"sc_cam", "s_ablat", nullptr, nullptr}},
  /*Voskhod 2*/        {{"hab_capsule", "hab_eva", nullptr, nullptr}},
  /*Gemini 4*/         {{"hab_capsule", "hab_eva", nullptr, nullptr}},
  /*Gemini 8*/         {{"hab_dock", "a_inert", nullptr, nullptr}},
  /*Mariner 2*/        {{"l_med", "com_s", "sc_spec", nullptr}},
  /*Mariner 4*/        {{"sc_cam", "com_s", "a_star", nullptr}},
  /*Luna 9*/           {{"p_hyper", "a_seq", "sc_cam", nullptr}},
  /*Surveyor 1*/       {{"p_hyper", "p_cryo", "sc_cam", nullptr}},
  /*Lunar Orbiter 1*/  {{"sc_cam", "com_s", "a_star", nullptr}},
  /*Apollo 8*/         {{"p_cryo", "hab_capsule", "a_digital", "l_super"}},
  /*Apollo 11*/        {{"p_cryo", "hab_eva", "a_digital", "l_super"}},
  /*Luna 16*/          {{"p_hyper", "sc_drill", "a_digital", "l_heavy"}},
  /*Lunokhod 1*/       {{"rob_rover", "nrg_solar", "com_s", nullptr}},
  /*Apollo 15*/        {{"p_cryo", "hab_long", "rob_rover", "l_super"}},
  /*Venera 7*/         {{"s_pica", "com_s", "a_seq", nullptr}},
  /*Mariner 9*/        {{"a_star", "com_x", "sc_cam", nullptr}},
  /*Mariner 10*/       {{"a_star", "com_x", "nrg_solar", nullptr}},
  /*Viking 1*/         {{"nrg_rtg", "sc_lab", "com_x", nullptr}},
  /*Venera 9*/         {{"s_pica", "sc_cam", "com_x", nullptr}},
  /*Magellan*/         {{"sc_radar", "com_x", "nrg_deploy", nullptr}},
  /*Mars Pathfinder*/  {{"a_edl", "sc_cam", "rob_rover", nullptr}},
  /*MER Spirit/Oppy*/  {{"a_edl", "rob_auto", "sc_spec", nullptr}},
  /*MESSENGER*/        {{"p_hyper", "a_dsn", "sc_spec", nullptr}},
  /*Curiosity (MSL)*/  {{"a_edl", "nrg_rtg", "sc_lab", "rob_auto"}},
  /*Perseverance*/     {{"a_edl", "rob_auto", "sc_drill", "rob_heli"}},
  /*Parker Solar Probe*/{{"s_pica", "a_star", "com_ka", nullptr}},
  /*Pioneer 10*/       {{"nrg_rtg", "com_x", "a_dsn", nullptr}},
  /*Voyager 1*/        {{"nrg_rtg", "a_dsn", "com_x", "sc_cam"}},
  /*Voyager 2*/        {{"nrg_rtg", "a_dsn", "com_x", nullptr}},
  /*Galileo*/          {{"nrg_rtg", "a_dsn", "sc_spec", nullptr}},
  /*Hubble (HST)*/     {{"sc_cam", "nrg_deploy", "hab_dock", nullptr}},
  /*Dawn*/             {{"p_ion", "nrg_array", "a_dsn", nullptr}},
  /*New Horizons*/     {{"nrg_rtg", "com_ka", "a_dsn", nullptr}},
  /*Juno*/             {{"nrg_array", "a_dsn", "com_ka", nullptr}},
  /*Cassini-Huygens*/  {{"nrg_rtg", "a_dsn", "sc_lab", "com_x"}},
  /*Rosetta / Philae*/ {{"nrg_deploy", "sc_drill", "a_dsn", nullptr}},
  /*Stardust*/         {{"s_pica", "sc_return", "com_x", nullptr}},
  /*Hayabusa2*/        {{"p_ion", "sc_return", "a_edl", nullptr}},
  /*OSIRIS-REx*/       {{"rob_arm", "sc_return", "a_dsn", nullptr}},
  /*JWST*/             {{"nrg_deploy", "com_ka", "sc_lab", nullptr}},
  /*DART*/             {{"p_ion", "a_ai", "com_x", nullptr}},
  /*Artemis I*/        {{"p_cryo", "hab_capsule", "l_super", "s_metal"}},
};
static_assert(sizeof(MISSION_REQS) / sizeof(MISSION_REQS[0]) == MISSION_COUNT,
              "MISSION_REQS doit avoir exactement MISSION_COUNT lignes");

// PROFIL DE VOL REEL de chaque mission : sert au PLANIFICATEUR DETERMINISTE (Δv
// patched-conic depuis les vraies positions des corps a l'epoque de lancement ;
// AUCUN RNG). `kind` = type de trajectoire ; `body` = corps cible tabule par
// l'ephemeride (sinon Body::COUNT) ; `helio_au` = demi-grand axe helio (UA) pour
// les cibles non tabulees (cometes, asteroides, L2) ou le perihelie (Parker).
enum TargetKind {
  TK_LEO,          // mise en orbite basse terrestre
  TK_MOON_ORBIT,   // orbite lunaire
  TK_MOON_LAND,    // atterrissage lunaire
  TK_MOON_RETURN,  // atterrissage + retour d'echantillon lunaire
  TK_FLYBY,        // survol heliocentrique (pas de capture)
  TK_ORBIT,        // mise en orbite autour de la cible (capture)
  TK_LAND,         // atterrissage sur la cible (capture + descente)
  TK_RETURN,       // rendez-vous + retour d'echantillon (petit corps)
  TK_SUN,          // plongee solaire (perihelie bas)
};
struct MissionProfile { int kind; fen::ephem::Body body; double helio_au; };
inline constexpr fen::ephem::Body B_NONE = fen::ephem::Body::COUNT;
inline constexpr MissionProfile MISSION_PROFILE[] = {
  /*Spoutnik 1*/       {TK_LEO,         B_NONE,                    0.0},
  /*Explorer 1*/       {TK_LEO,         B_NONE,                    0.0},
  /*Vanguard 1*/       {TK_LEO,         B_NONE,                    0.0},
  /*Telstar 1*/        {TK_LEO,         B_NONE,                    0.0},
  /*Vostok 1*/         {TK_LEO,         B_NONE,                    0.0},
  /*Mercury-Atlas 6*/  {TK_LEO,         B_NONE,                    0.0},
  /*Vostok 6*/         {TK_LEO,         B_NONE,                    0.0},
  /*Corona KH-4*/      {TK_LEO,         B_NONE,                    0.0},
  /*Voskhod 2*/        {TK_LEO,         B_NONE,                    0.0},
  /*Gemini 4*/         {TK_LEO,         B_NONE,                    0.0},
  /*Gemini 8*/         {TK_LEO,         B_NONE,                    0.0},
  /*Mariner 2*/        {TK_FLYBY,       fen::ephem::Body::Venus,   0.0},
  /*Mariner 4*/        {TK_FLYBY,       fen::ephem::Body::Mars,    0.0},
  /*Luna 9*/           {TK_MOON_LAND,   fen::ephem::Body::Moon,    0.0},
  /*Surveyor 1*/       {TK_MOON_LAND,   fen::ephem::Body::Moon,    0.0},
  /*Lunar Orbiter 1*/  {TK_MOON_ORBIT,  fen::ephem::Body::Moon,    0.0},
  /*Apollo 8*/         {TK_MOON_ORBIT,  fen::ephem::Body::Moon,    0.0},
  /*Apollo 11*/        {TK_MOON_LAND,   fen::ephem::Body::Moon,    0.0},
  /*Luna 16*/          {TK_MOON_RETURN, fen::ephem::Body::Moon,    0.0},
  /*Lunokhod 1*/       {TK_MOON_LAND,   fen::ephem::Body::Moon,    0.0},
  /*Apollo 15*/        {TK_MOON_LAND,   fen::ephem::Body::Moon,    0.0},
  /*Venera 7*/         {TK_LAND,        fen::ephem::Body::Venus,   0.0},
  /*Mariner 9*/        {TK_ORBIT,       fen::ephem::Body::Mars,    0.0},
  /*Mariner 10*/       {TK_FLYBY,       fen::ephem::Body::Mercury, 0.0},
  /*Viking 1*/         {TK_LAND,        fen::ephem::Body::Mars,    0.0},
  /*Venera 9*/         {TK_LAND,        fen::ephem::Body::Venus,   0.0},
  /*Magellan*/         {TK_ORBIT,       fen::ephem::Body::Venus,   0.0},
  /*Mars Pathfinder*/  {TK_LAND,        fen::ephem::Body::Mars,    0.0},
  /*MER Spirit/Oppy*/  {TK_LAND,        fen::ephem::Body::Mars,    0.0},
  /*MESSENGER*/        {TK_ORBIT,       fen::ephem::Body::Mercury, 0.0},
  /*Curiosity (MSL)*/  {TK_LAND,        fen::ephem::Body::Mars,    0.0},
  /*Perseverance*/     {TK_LAND,        fen::ephem::Body::Mars,    0.0},
  /*Parker Solar Probe*/{TK_SUN,        fen::ephem::Body::Sun,     0.05},
  /*Pioneer 10*/       {TK_FLYBY,       fen::ephem::Body::Jupiter, 0.0},
  /*Voyager 1*/        {TK_FLYBY,       fen::ephem::Body::Saturn,  0.0},
  /*Voyager 2*/        {TK_FLYBY,       fen::ephem::Body::Neptune, 0.0},
  /*Galileo*/          {TK_ORBIT,       fen::ephem::Body::Jupiter, 0.0},
  /*Hubble (HST)*/     {TK_LEO,         B_NONE,                    0.0},
  /*Dawn*/             {TK_ORBIT,       B_NONE,                    2.77},   // Ceres
  /*New Horizons*/     {TK_FLYBY,       fen::ephem::Body::Pluto,   0.0},
  /*Juno*/             {TK_ORBIT,       fen::ephem::Body::Jupiter, 0.0},
  /*Cassini-Huygens*/  {TK_ORBIT,       fen::ephem::Body::Saturn,  0.0},
  /*Rosetta / Philae*/ {TK_ORBIT,       B_NONE,                    3.50},   // 67P
  /*Stardust*/         {TK_RETURN,      B_NONE,                    3.40},   // Wild 2
  /*Hayabusa2*/        {TK_RETURN,      B_NONE,                    1.19},   // Ryugu
  /*OSIRIS-REx*/       {TK_RETURN,      B_NONE,                    1.13},   // Bennu
  /*JWST*/             {TK_FLYBY,       B_NONE,                    1.01},   // point L2
  /*DART*/             {TK_FLYBY,       B_NONE,                    1.64},   // Didymos
  /*Artemis I*/        {TK_MOON_ORBIT,  fen::ephem::Body::Moon,    0.0},
};
static_assert(sizeof(MISSION_PROFILE) / sizeof(MISSION_PROFILE[0]) == MISSION_COUNT,
              "MISSION_PROFILE doit avoir exactement MISSION_COUNT lignes");

// --- ARBRE DE COMPETENCES ULTRA-COMPLET (facon ARK / Solar Expanse) -----------
// Grande toile de ~71 technologies REELLES, organisees en 10 CATEGORIES (branches
// horizontales) et par PROFONDEUR (maturite, axe x). Chaque noeud coute des POINTS
// DE RECHERCHE et exige : son predecesseur DANS la branche (calcule : meme `cat`,
// `depth`-1) + un eventuel PREREQUIS CROISE `xreq` (id d'un autre noeud) + le palier
// d'agence `tier`. On le RECHERCHE d'un clic (etat local 3D ; n'altere pas la
// physique). L'app resout prereq/xreq par id -> aucun index code en dur (robuste).
inline const char* TECH_CATS[] = {
  "PROPULSION", "LANCEURS", "STRUCTURES", "AVIONIQUE", "ENERGIE",
  "VOL HABITE", "SCIENCE", "COMMS", "ROBOTIQUE", "INFRASTRUCTURE",
};
inline constexpr int TECH_CAT_COUNT = 10;

struct TechNode {
  const char* id;
  const char* name;
  int         cat;       // 0..TECH_CAT_COUNT-1 (branche, y)
  int         depth;     // position dans la branche (x, contigu depuis 0)
  int         tier;      // palier d'agence requis (Tier)
  int         cost;      // points de recherche
  const char* xreq;      // prerequis CROISE (id d'un autre noeud) ou nullptr
  const char* desc;      // technologie reelle
};

inline constexpr TechNode TECH_NODES[] = {
  // --- PROPULSION (cat 0) ----------------------------------------------------
  {"p_solid",  "Propergol solide",     0, 0, T_STARTUP,  1, nullptr,       "Poudre : boosters simples et fiables"},
  {"p_kero",   "Kerolox (RP-1/LOX)",   0, 1, T_LEO,      2, nullptr,       "Ergols liquides kerosene / oxygene"},
  {"p_hyper",  "Hypergols",            0, 2, T_CREWED,   2, nullptr,       "UDMH/N2O4 : allumage spontane, stockables"},
  {"p_cryo",   "Cryogenie LH2/LOX",    0, 3, T_MOON,     3, nullptr,       "Etage superieur haute impulsion (Centaur)"},
  {"p_staged", "Combustion etagee",    0, 4, T_PLANETS,  4, nullptr,       "Cycle a haute pression (RD-180, Raptor)"},
  {"p_ion",    "Propulsion ionique",   0, 5, T_PLANETS,  4, "nrg_rtg",     "Moteur a xenon : faible poussee, efficace"},
  {"p_sail",   "Voile solaire",        0, 6, T_OUTER,    3, nullptr,       "Poussee par la pression de radiation"},
  {"p_ntr",    "Nucleaire thermique",  0, 7, T_FRONTIER, 6, "nrg_reactor", "NERVA : hydrogene chauffe par un reacteur"},
  // --- LANCEURS (cat 1) : depth ordonne par palier (chaine coherente) ---------
  {"l_sound",  "Fusee-sonde",          1, 0, T_STARTUP,  1, nullptr,       "Vol sous-orbital, premiers essais"},
  {"l_small",  "Lanceur leger",        1, 1, T_LEO,      2, "p_kero",      "Petites charges en orbite basse"},
  {"l_med",    "Lanceur moyen",        1, 2, T_CREWED,   2, nullptr,       "Classe Soyouz / Atlas"},
  {"l_upper",  "Etage superieur cryo", 1, 3, T_MOON,     3, "p_cryo",      "Centaur : relance en orbite"},
  {"l_heavy",  "Lanceur lourd",        1, 4, T_MOON,     3, nullptr,       "Classe Titan / Ariane 5 / Delta IV"},
  {"l_super",  "Super-lourd",          1, 5, T_MOON,     4, "p_cryo",      "Classe Saturn V / SLS / Starship"},
  {"l_air",    "Air-launch",           1, 6, T_PLANETS,  4, nullptr,       "Largage depuis un avion porteur"},
  {"l_reuse",  "Lanceur recuperable",  1, 7, T_OUTER,    5, "s_metal",     "Retour et re-vol du 1er etage"},
  // --- STRUCTURES & PROTECTION THERMIQUE (cat 2) -----------------------------
  {"s_al",     "Alliages aluminium",   2, 0, T_STARTUP,  1, nullptr,       "Reservoirs et fuselage legers"},
  {"s_ablat",  "Bouclier ablatif",     2, 1, T_LEO,      2, nullptr,       "Protection thermique de rentree"},
  {"s_comp",   "Composites",           2, 2, T_CREWED,   2, nullptr,       "Carbone : rigide et leger"},
  {"s_metal",  "TPS metallique",       2, 3, T_MOON,     3, nullptr,       "Tuiles / metal reutilisable"},
  {"s_pica",   "PICA / PICA-X",        2, 4, T_PLANETS,  3, nullptr,       "Ablatif haute vitesse (Stardust)"},
  {"s_infl",   "Structures gonflables",2, 5, T_PLANETS,  4, nullptr,       "Habitats et boucliers deployables"},
  {"s_orbit",  "Assemblage orbital",   2, 6, T_MOON,     4, "hab_dock",    "Construire en orbite (ISS)"},
  // --- AVIONIQUE & GNC (cat 3) -----------------------------------------------
  {"a_seq",    "Sequenceur",           3, 0, T_STARTUP,  1, nullptr,       "Programme de vol cable, minuterie"},
  {"a_inert",  "Centrale inertielle",  3, 1, T_LEO,      2, nullptr,       "Gyroscopes + accelerometres"},
  {"a_star",   "Viseur d'etoiles",     3, 2, T_CREWED,   2, nullptr,       "Orientation absolue par les etoiles"},
  {"a_digital","Calculateur numerique",3, 3, T_MOON,     3, nullptr,       "Ordinateur de bord (AGC)"},
  {"a_dsn",    "Navigation profonde",  3, 4, T_MOON,     3, "com_dsn",     "Radiometrie Doppler longue distance"},
  {"a_edl",    "EDL autonome",         3, 5, T_PLANETS,  4, nullptr,       "Entree-descente-atterrissage guides"},
  {"a_ai",     "IA de bord",           3, 6, T_FRONTIER, 6, nullptr,       "Decision autonome, evitement"},
  // --- ENERGIE (cat 4) -------------------------------------------------------
  {"nrg_bat",  "Batteries",            4, 0, T_STARTUP,  1, nullptr,       "Piles argent-zinc, autonomie courte"},
  {"nrg_solar","Cellules solaires",    4, 1, T_LEO,      2, nullptr,       "Photovoltaique de base"},
  {"nrg_deploy","Panneaux deployables",4, 2, T_CREWED,   2, nullptr,       "Grands panneaux orientables"},
  {"nrg_rtg",  "RTG (radioisotope)",   4, 3, T_MOON,     3, nullptr,       "Thermoelectrique au Pu-238"},
  {"nrg_stirling","ASRG / Stirling",   4, 4, T_PLANETS,  4, nullptr,       "Radioisotope a haut rendement"},
  {"nrg_array","Grands reseaux",       4, 5, T_OUTER,    4, nullptr,       "Reseaux geants (Juno) loin du Soleil"},
  {"nrg_reactor","Reacteur nucleaire", 4, 6, T_FRONTIER, 6, nullptr,       "Fission de puissance (Kilopower)"},
  // --- VOL HABITE (cat 5) ----------------------------------------------------
  {"hab_capsule","Capsule pressurisee",5, 0, T_LEO,      2, nullptr,       "Cabine, siege, bouclier"},
  {"hab_eva",  "Scaphandre EVA",       5, 1, T_CREWED,   3, nullptr,       "Combinaison + PLSS pour sortie"},
  {"hab_dock", "Rendez-vous & amarrage",5,2, T_CREWED,   3, nullptr,       "Approche et jonction orbitale"},
  {"hab_eclss","Support-vie (ECLSS)",  5, 3, T_CREWED,   3, nullptr,       "Air, eau, thermique, CO2"},
  {"hab_long", "Longue duree",         5, 4, T_MOON,     4, nullptr,       "Sejours de plusieurs mois"},
  {"hab_recyc","Recyclage eau/air",    5, 5, T_PLANETS,  4, nullptr,       "Boucle fermee (ECLSS avance)"},
  {"hab_med",  "Medecine spatiale",    5, 6, T_OUTER,    5, nullptr,       "Contre-mesures os / muscles / radiations"},
  {"hab_grav", "Gravite artificielle", 5, 7, T_FRONTIER, 6, nullptr,       "Habitat en rotation"},
  // --- SCIENCE (cat 6) -------------------------------------------------------
  {"sc_cam",   "Imageurs",             6, 0, T_STARTUP,  1, nullptr,       "Cameras TV puis CCD haute resolution"},
  {"sc_spec",  "Spectrometres",        6, 1, T_CREWED,   2, nullptr,       "Composition (masse, IR, gamma)"},
  {"sc_mag",   "Magnetometre",         6, 2, T_CREWED,   2, nullptr,       "Champs magnetiques et plasma"},
  {"sc_radar", "Radar / altimetre",    6, 3, T_PLANETS,  3, nullptr,       "Cartographie sous les nuages (Magellan)"},
  {"sc_drill", "Foreuse & manipulation",6,4, T_PLANETS,  4, "rob_arm",     "Bras, foreuse, prelevement in situ"},
  {"sc_lab",   "Labo in situ",         6, 5, T_PLANETS,  5, nullptr,       "Analyse a bord (SAM, GCMS)"},
  {"sc_return","Retour d'echantillons",6, 6, T_FRONTIER, 6, nullptr,       "Capsule scellee ramenee sur Terre"},
  // --- COMMUNICATIONS (cat 7) ------------------------------------------------
  {"com_s",    "Bande S",              7, 0, T_STARTUP,  1, nullptr,       "Liaison radio de base, telemesure"},
  {"com_x",    "Bande X",              7, 1, T_CREWED,   2, nullptr,       "Haut debit vers l'espace lointain"},
  {"com_ka",   "Bande Ka",             7, 2, T_MOON,     3, nullptr,       "Tres haut debit (MRO)"},
  {"com_dsn",  "Deep Space Network",   7, 3, T_MOON,     3, nullptr,       "Antennes 70 m, poursuite lointaine"},
  {"com_relay","Reseau relais",        7, 4, T_PLANETS,  4, nullptr,       "Orbiteurs-relais (Mars)"},
  {"com_opt",  "Optique / laser",      7, 5, T_FRONTIER, 5, nullptr,       "Communication laser tres haut debit"},
  // --- ROBOTIQUE (cat 8) -----------------------------------------------------
  {"rob_arm",  "Bras robotique",       8, 0, T_CREWED,   2, nullptr,       "Manipulation, Canadarm"},
  {"rob_rover","Rover telecommande",   8, 1, T_MOON,     3, nullptr,       "Mobilite pilotee (Lunokhod)"},
  {"rob_auto", "Rover autonome",       8, 2, T_PLANETS,  4, "a_edl",       "Navigation autonome au sol"},
  {"rob_heli", "Helicoptere / drone",  8, 3, T_PLANETS,  5, nullptr,       "Vol atmospherique (Ingenuity)"},
  {"rob_asm",  "Assemblage robotique", 8, 4, T_OUTER,    5, "s_orbit",     "Montage orbital sans equipage"},
  {"rob_swarm","Essaims",              8, 5, T_FRONTIER, 6, nullptr,       "Flottilles coordonnees"},
  // --- INFRASTRUCTURE (cat 9) ------------------------------------------------
  {"inf_ground","Station sol",         9, 0, T_STARTUP,  1, nullptr,       "Poursuite et telemesure au sol"},
  {"inf_pad",  "Pas de tir",           9, 1, T_LEO,      2, nullptr,       "Aire de lancement dediee"},
  {"inf_vab",  "VAB / integration",    9, 2, T_CREWED,   2, nullptr,       "Hall d'assemblage vertical"},
  {"inf_station","Station orbitale",   9, 3, T_MOON,     4, "hab_long",    "Avant-poste habite permanent"},
  {"inf_isru", "ISRU (in situ)",       9, 4, T_OUTER,    5, nullptr,       "Fabriquer ergols/oxygene sur place (MOXIE)"},
  {"inf_depot","Depot d'ergols",       9, 5, T_OUTER,    5, "p_cryo",      "Ravitaillement orbital cryogenique"},
  {"inf_base", "Base permanente",      9, 6, T_FRONTIER, 7, "inf_isru",    "Colonie autonome (Lune / Mars)"},
};
inline constexpr int TECH_COUNT = static_cast<int>(sizeof(TECH_NODES) / sizeof(TECH_NODES[0]));

} // namespace spgame

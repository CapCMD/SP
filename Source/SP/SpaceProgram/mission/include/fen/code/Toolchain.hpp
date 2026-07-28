// fen/code/Toolchain.hpp — LA TOOLCHAIN EMBARQUÉE ET SON BAC À SABLE [GDD 15.1, 18]
//
// « Le joueur écrit du VRAI C++, compilé et exécuté par une toolchain
// embarquée » [GDD décision 1]. Pas un langage de script maison, pas un
// interpréteur : le compilateur du système, les en-têtes `ares::vol`, un
// exécutable, un processus.
//
// ═══ LES QUATRE EXIGENCES DE [GDD 18], ET COMMENT ELLES SONT TENUES ═══
//   TAILLE      — « embarquer compilateur, en-têtes et outils associés —
//                 plusieurs centaines de Mo à budgéter explicitement dans la
//                 distribution ». **FAIT le 2026-07-28, côté mécanisme** :
//                   . les EN-TÊTES partent avec le jeu — `Content/SP/Sdk`
//                     (283 Ko, 49 fichiers), produit depuis l'arbre source par
//                     `Tools/stage_sdk.py` et empaqueté en NON-UFS (ils sont lus
//                     par `cl.exe`, pas par Unreal : dans un .pak ils seraient
//                     inatteignables). PREUVE : l'exemple de [GDD 15.3] compile
//                     contre ce seul dossier, sans l'arbre source ;
//                   . le COMPILATEUR se cherche d'abord dans
//                     `Content/SP/Toolchain/VC/Auxiliary/Build/vcvars64.bat`,
//                     point de dépôt de la distribution, et seulement ensuite
//                     sur la machine (mode développement).
//                 CE QUI RESTE, ET C'EST UN ACTE D'EXPLOITATION, PAS DE CODE :
//                 déposer les MSVC Build Tools dans ce dossier. Ils pèsent de
//                 300 Mo à plusieurs Go — c'est LE poste de budget que [GDD 18]
//                 demande de déclarer — et portent leur propre licence de
//                 redistribution. Le jeu n'en dépend pas pour fonctionner : sans
//                 eux, la chaîne rend `Indisponible` et le poste imprime le
//                 chemin attendu.
//   ISOLATION   — « exécution du code joueur en processus séparé, limites de
//                 temps et de mémoire, interception des signaux ». Le code
//                 compilé tourne dans un AUTRE processus, avec un délai maximal.
//                 Un pointeur invalide y meurt seul : il rend un code de sortie
//                 anormal, que le jeu lit comme un ÉCHEC DE MISSION. « Un
//                 pointeur invalide produit un échec de mission, jamais un
//                 crash du jeu » — c'est l'oracle central de ce fichier.
//   DÉTERMINISME— « journalisation des exécutions en vol avec leurs entrées ».
//                 `EntreesVol` est écrit sur disque à côté du résultat : une
//                 exécution se REJOUE au lieu de se recalculer.
//   HORS-LIGNE  — rien ne sort de la machine ; le code du joueur ne s'exécute
//                 que chez son auteur [GDD 16, 18].
//
// ═══ CE QUI EST PUR ET CE QUI NE L'EST PAS ═══
// Ce header porte le CONTRAT (entrées, décisions, diagnostics) en C++ pur, donc
// sous oracle. Lancer un processus et attendre avec un délai est de l'appel
// système : c'est dans `Toolchain.cpp`, sous `#ifdef _WIN32`, exactement comme
// la frontière `app/` ↔ `UEBridge/` sépare déjà le modèle de la plateforme.
#pragma once
#include <string>
#include <vector>

#include "fen/core/Vec3.hpp"

namespace fen::code {

// ═══ CE QUE LE CODE DU JOUEUR REÇOIT ═══ — l'état de vol, sérialisable, donc
// rejouable. C'est la « journalisation des exécutions avec leurs entrées ».
struct EntreesVol {
  Vec3   pos{}, vel{};          // solution de navigation (jamais la vérité)
  double sigma3_m{0.0};         // incertitude 3σ de cette solution
  // LE POINT DE VISÉE, RAMENÉ À L'INSTANT COURANT. `ares::vol::Cible::
  // ecart_projete` est une DIFFÉRENCE DE POSITIONS : lui donner le point
  // d'arrivée à des mois de là rendrait un « écart » d'une unité astronomique,
  // qui ne décrit rien. On lui donne donc le point où l'estimé DEVRAIT être
  // maintenant pour que sa projection touche — l'écart y devient exactement le
  // manque au but projeté. C'est la cible linéarisée d'une navigation par plan
  // B, et c'est une APPROXIMATION DÉCLARÉE [GDD 6.8] : le premier ordre.
  Vec3   cible{};
  double tolerance_m{0.0};
  double dv_disponible{0.0};    // réserve de correction
  // HORIZON DE LA MANŒUVRE (s) : le temps qui reste jusqu'au point de visée.
  // C'est le `tau` du `Solveur` de [GDD 15.3] — « correction proportionnelle à
  // l'écart sur un temps caractéristique ». Le laisser à un jour quand
  // l'arrivée est à huit mois ferait commander un Δv deux cents fois trop grand
  // : un défaut de cadrage, pas un choix du joueur.
  double tau_s{86400.0};
  std::string serialiser() const;
  static EntreesVol lire(const std::string& texte);
};

// ═══ CE QUE LE CONTEXTE A ENREGISTRÉ ═══ — les décisions, pas un « retour ».
struct DecisionsVol {
  bool   execute{false};
  Vec3   dv{};                  // manœuvre exécutée (inertiel)
  int    differees{0};
  double replan_s{0.0};
  std::vector<std::string> alertes;
  std::vector<std::string> journal;
  std::string serialiser() const;
  static DecisionsVol lire(const std::string& texte);
};

// ═══ L'ISSUE D'UNE EXÉCUTION ═══ — et elle distingue les quatre façons de rater.
enum class IssueCode {
  Ok = 0,
  ErreurCompilation,   // ne compile pas : coût NUL, détecté avant tout [GDD 15.5]
  Plantage,            // pointeur invalide, division par zéro… -> échec de mission
  Delai,               // boucle infinie : le bac à sable la tue
  Indisponible,        // pas de compilateur sur la machine (voir en-tête)
};

inline const char* issue_nom(IssueCode i) {
  switch (i) {
    case IssueCode::Ok:                return "OK";
    case IssueCode::ErreurCompilation: return "ERREUR DE COMPILATION";
    case IssueCode::Plantage:          return "PLANTAGE DU CODE DE VOL";
    case IssueCode::Delai:             return "DELAI DEPASSE";
    default:                           return "TOOLCHAIN INDISPONIBLE";
  }
}

struct ResultatToolchain {
  IssueCode    issue{IssueCode::Indisponible};
  std::string  diagnostics;     // la sortie du compilateur, telle quelle
  DecisionsVol decisions;
  double       duree_ms{0.0};
  int          code_sortie{0};
  bool ok() const { return issue == IssueCode::Ok; }
};

// Réglages de la chaîne. Les chemins sont FOURNIS par l'appelant : le modèle ne
// devine pas où vit le projet.
struct ToolchainConfig {
  std::string dossier_travail;        // où écrire source, exe, entrées, résultat
  std::vector<std::string> includes;  // en-têtes ARES
  // LES UNITÉS DE COMPILATION DE L'API. Depuis que `ares::vol::Solveur` résout la
  // correction sur la vraie matrice de transition, l'API n'est plus faite que
  // d'en-têtes : le programme du joueur doit LIER le propagateur képlérien. Les
  // chemins viennent de la plateforme, comme les includes — le modèle ne devine
  // pas où vit le projet.
  std::vector<std::string> sources;
  std::string vcvars;                 // script d'environnement du compilateur
  int    timeout_ms{4000};            // limite de temps du bac à sable [GDD 18]
  // LIMITE DE MÉMOIRE [GDD 18], au même titre que la limite de temps : le code
  // du joueur tourne dans un job object plafonné. Une allocation en boucle est
  // arrêtée par le système d'exploitation — un `bad_alloc` dans le processus
  // fils est un ÉCHEC DE MISSION, pas un jeu qui sature la machine.
  int    memoire_max_mo{256};
};

// LE HARNAIS : le `main()` que la toolchain ajoute autour du code du joueur. Il
// lit les entrées, construit le `Contexte` de `ares::vol`, appelle la fonction
// du joueur, et écrit les décisions. Le joueur ne l'écrit pas et ne le voit pas :
// c'est le simulateur qui l'appelle, comme en vol réel.
std::string harnais_source();

// LA CHAÎNE COMPLÈTE : compiler, puis exécuter dans un processus séparé, avec
// délai. Ne lance JAMAIS d'exception vers le jeu — toute issue est un
// `ResultatToolchain`.
ResultatToolchain compiler_et_executer(const std::string& source_joueur,
                                       const EntreesVol& entrees,
                                       const ToolchainConfig& cfg);

// Le squelette que le poste propose au joueur (mode Pro) : la signature exacte
// que le harnais appelle, et rien de plus. Ce n'est pas une « procédure
// rejouable » [GDD 2.4] — c'est la déclaration de la fonction, l'équivalent du
// prototype que tout en-tête donne.
inline const char* squelette_vol() {
  return
"#include <ares/vol.hpp>\n"
"using namespace ares::vol;\n"
"\n"
"// Correction de mi-parcours. Appelee par le simulateur au noeud de manoeuvre.\n"
"void sequence_correction(Contexte& ctx) {\n"
"    Etat estime = ctx.navigation().solution();\n"
"\n"
"    // Refuser d'agir sur une solution degradee\n"
"    if (estime.incertitude_3sigma() > metres(12000)) {\n"
"        ctx.alerte(\"Solution de navigation degradee - correction reportee\");\n"
"        ctx.replanifier(heures(48));\n"
"        return;\n"
"    }\n"
"\n"
"    Ecart e = ctx.cible().ecart_projete(estime);\n"
"    if (e.norme() < ctx.cible().tolerance()) return;\n"
"\n"
"    Manoeuvre m = ctx.solveur().corriger(estime, ctx.cible());\n"
"\n"
"    // Garde-fou : ne jamais engager plus de 35 % des reserves sans le sol\n"
"    if (m.dv() > ctx.reserves().dv_disponible() * 0.35) {\n"
"        ctx.alerte(\"Correction > 35%% des reserves - validation sol requise\");\n"
"        ctx.differer(m);\n"
"        return;\n"
"    }\n"
"\n"
"    ctx.executer(m);\n"
"    ctx.journal_bord(\"Correction executee : %.2f m/s\", m.dv());\n"
"}\n";
}

} // namespace fen::code

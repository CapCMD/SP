// fen/career/Carnet.hpp — CE QUE LE CARNET CONTIENT [GDD 15.4, 2.3, 3.5, 9.3]
//
// « Le carnet — documentation personnelle du personnage : formules, procédures,
// man pages de l'API, archives de graphes, journal de reconstitution d'une
// absence. Transmis en passation. » [GDD 15.4]
//
// `career::Notebook` existait depuis le premier jour : sérialisé, transmis au
// successeur… et **VIDE**. Personne n'y écrivait, personne ne le lisait. Encore
// un modèle que rien ne consomme — la famille de `Mission::phase` (piège n°20b),
// de `show_moons` (n°41), de `ModeAide` et de `comms_delay_s`. Un conteneur
// fidèlement persisté n'est pas une fonctionnalité.
//
// ═══ LE CARNET NE S'ÉCRIT PAS À LA MAIN ═══
// Ce ne sont pas des notes que le joueur tape : ce sont les FAITS de sa partie,
// rédigés au moment où ils ont lieu. Un carnet qu'il faudrait remplir soi-même
// serait vide chez tout le monde, et « transmis en passation » ne vaudrait rien.
// Chaque entrée ci-dessous est donc DÉRIVÉE d'un événement du modèle, et son
// texte ne contient que des grandeurs que le modèle possède déjà.
//
// ═══ CE QUI N'EST PAS ICI ═══
// Les « formules » de [GDD 15.4] : le GDD les cite dans une énumération de ce que
// le personnage note, pas comme un système. Les inscrire reviendrait à écrire un
// aide-mémoire d'astrodynamique — du contenu que le ch. 20 ne demande pas.
// Les MAN PAGES, elles, sont ici : elles ne sont pas du contenu, elles sont une
// LECTURE de l'API (`noeuds_disponibles`), donc exactes par construction.
//
// C++ pur, aucune dépendance UE.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

#include "fen/career/Career.hpp"
#include "fen/mission/Graphe.hpp"
#include "fen/mission/MissionFsm.hpp"

namespace fen::career {

// ═══ LES MAN PAGES DE L'API ═══ [GDD 15.4, 2.2]
// « L'équivalence est stricte : les nœuds Normal exposent exactement les
// fonctions de l'API C++. » Cette page ne RECOPIE pas la liste des primitives —
// elle la LIT. Le jour où un nœud est ajouté, retiré ou renommé, la page suit
// toute seule ; une copie, elle, aurait péri au premier changement.
inline NotebookEntry man_pages_api(double date_days) {
  NotebookEntry e;
  e.title = "Man pages — API de correction";
  e.date_days = date_days;
  std::string b =
      "Chaque primitive du graphe EST une fonction de l'API C++ [GDD 2.2].\n"
      "Le mode ne change que l'ecriture, jamais ce qu'on peut calculer.\n\n";
  for (const mission::NoeudDef& d : mission::noeuds_disponibles()) {
    char buf[320];
    std::snprintf(buf, sizeof buf, "  %-16s %s\n      %s -> %s%s\n",
                  d.nom, d.appel,
                  mission::type_nom(d.entree), mission::type_nom(d.sortie),
                  d.besoin_etat ? "   (consomme aussi l'etat courant)" : "");
    b += buf;
  }
  e.body = std::move(b);
  return e;
}

// ═══ L'ARCHIVE D'UN GRAPHE ═══ [GDD 2.3]
// « Le passage Normal → Pro est possible et UNIDIRECTIONNEL ; les graphes
// existants sont ARCHIVÉS EN LECTURE SEULE dans le carnet (consultables, non
// exécutables), le joueur devant réécrire en C++. Cette perte est
// intentionnelle. »
//
// « Lecture seule » se tient ici SANS drapeau : une entrée de carnet est du
// TEXTE. Il n'existe aucun chemin qui reconstruise un graphe exécutable depuis
// une chaîne de caractères — l'archive est consultable et rien d'autre, par
// construction et non par interdiction.
inline NotebookEntry archive_graphe(const std::vector<mission::TypeNoeud>& chaine,
                                    double date_days) {
  NotebookEntry e;
  e.title = "Graphe archive — passage en mode PRO";
  e.date_days = date_days;
  std::string b =
      "Archive en LECTURE SEULE [GDD 2.3]. Ce graphe n'est plus executable :\n"
      "en mode PRO le calcul se reecrit en C++. La perte est voulue.\n\n";
  if (chaine.empty()) {
    b += "  (aucun noeud assemble)\n";
  } else {
    for (std::size_t k = 0; k < chaine.size(); ++k) {
      const mission::NoeudDef& d = mission::noeud_def(chaine[k]);
      char buf[240];
      std::snprintf(buf, sizeof buf, "  %2d. %-16s %s\n",
                    static_cast<int>(k + 1), d.nom, d.appel);
      b += buf;
    }
  }
  e.body = std::move(b);
  return e;
}

// ═══ LE RETOUR D'EXPÉRIENCE D'UNE MISSION ═══ [GDD 15.4]
// `NotebookEntry::mission_ref` existait pour ça et n'était jamais renseigné.
// On n'écrit que des FAITS du vol — ceux dont le débrief dispose déjà, et pas
// une ligne de commentaire de plus : ce que le joueur relira dans dix ans de
// temps de jeu doit être ce qui s'est passé, pas ce qu'on en pensait.
inline NotebookEntry debrief_mission(const mission::Mission& m, double dv_margin_ms,
                                     double date_days) {
  NotebookEntry e;
  e.date_days = date_days;
  e.mission_ref = m.contract.id;
  e.title = std::string(m.flight_success ? "Reussite — " : "Echec — ") + m.contract.id;

  static const char* kConduite[4] = {
      "personne — aucun rendez-vous tenu",
      "moi, depuis le terminal",
      "le logiciel de bord",
      "l'adjoint, en mon absence [GDD 9.3]",
  };
  const int c = (m.vol_conduit_par >= 0 && m.vol_conduit_par < 4) ? m.vol_conduit_par : 0;

  char buf[900];
  if (m.nav_evaluee) {
    std::snprintf(buf, sizeof buf,
                  "Corrections conduites par : %s\n"
                  "Manque au but reel   : %.0f km\n"
                  "Delta-v de correction: %.1f m/s depenses sur %.0f provisionnes\n"
                  "Arc de poursuite     : %.1f j exploites\n"
                  "Logiciel embarque    : %s%s\n",
                  kConduite[c], m.nav_miss_km, m.nav_dv_required, dv_margin_ms,
                  m.arc_poursuite_j,
                  m.code_embarque ? "oui" : "non",
                  (m.code_embarque && m.code_non_couvert) ? " (HORS DOMAINE)" : "");
  } else {
    std::snprintf(buf, sizeof buf,
                  "Vol sans navigation calculee (cible non nommee ou poussee continue).\n"
                  "Issue : %s\n",
                  m.flight_success ? "nominale" : "echec");
  }
  e.body = buf;
  if (m.flight_has_anomaly && !m.flight_anomaly.what.empty()) {
    e.body += "Anomalie : " + m.flight_anomaly.what + "\n";
  }
  return e;
}

// ═══ LE JOURNAL DE RECONSTITUTION D'UNE ABSENCE ═══ [GDD 9.3, 15.4]
// « Journal de reconstitution d'une absence » : au retour, le personnage doit
// pouvoir LIRE ce qu'ARES a fait sans lui. Sans cette page, une absence de
// plusieurs décennies terrestres serait un trou noir — or [GDD 9.3] promet une
// agence qui « fonctionne normalement », donc qui a des comptes à rendre.
inline NotebookEntry journal_absence(double depart_days, double retour_days,
                                     int missions_menees, double tresorerie_me) {
  NotebookEntry e;
  e.title = "Reconstitution d'absence";
  e.date_days = retour_days;
  char buf[420];
  std::snprintf(buf, sizeof buf,
                "Absent du jour %.0f au jour %.0f (%.0f jours).\n"
                "ARES a fonctionne sous l'adjoint [GDD 9.3].\n"
                "Missions menees pendant l'absence : %d\n"
                "Tresorerie au retour              : %.0f M EUR\n",
                depart_days, retour_days, retour_days - depart_days,
                missions_menees, tresorerie_me);
  e.body = buf;
  return e;
}

} // namespace fen::career

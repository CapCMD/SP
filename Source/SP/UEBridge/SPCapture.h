// SPCapture.h — CAPTURE HEADLESS, comme le binaire de référence.
//
// Le jeu Vulkan d'origine se vérifie en ligne de commande :
//   solar_system_map.exe --iss --frames 150 --capture sortie.bmp
// C'est ce qui permet de COMPARER le portage UE à la référence sans piloter
// l'éditeur ni cliquer. On donne ici la même capacité au build UE :
//
//   UnrealEditor.exe SP.uproject -game -windowed -ResX=1280 -ResY=720
//       -spscene=iss -spframes=150 -spcapture=C:\...\sortie.png
//
//   -spscene=menu|iss|map   scène à ouvrir d'emblée (démarre une partie de test)
//   -spframes=N             nombre de frames avant la capture (chargement)
//   -spoeil=x,y,z[,yaw,pitch]  place l'œil du pawn dans la station (m, deg)
//   -spcapture=<chemin>     fichier de sortie ; le jeu quitte juste après
//
// Sans -spcapture, ce module est totalement inerte : aucun effet sur le jeu.
#pragma once

#include "CoreMinimal.h"

namespace SPCapture
{
	// Lit la ligne de commande une fois. true si une capture est demandée.
	bool IsRequested();
	// Scène demandée : 0 menu, 1 station (ISS), 2 carte. -1 = non précisée.
	int  RequestedScene();
	// Corps à focaliser sur la carte (`-spfocus=<fen::ephem::Body>`), équivalent
	// du `--focus N` du binaire de référence. -1 = vue système.
	int  RequestedFocus();
	// Poste à ouvrir d'emblée (`-sppost=<0..7>`), équivalent du `--panel N` de la
	// référence. -1 = aucun.
	int  RequestedPost();
	// Distance de vue imposée en km (`-spdist=<km>`) ; sinon la distance de
	// cadrage du corps focalisé. <= 0 = non précisée. Utile pour cadrer un objet
	// proche d'un corps (Novellus en LEO).
	double RequestedDist();
	// `-sphandoff` : GÈLE LA SCÈNE À L'INSTANT DE LA REPRISE (incr. 3c-3), c.-à-d.
	// à la toute fin du vol [M] d'entrée — plan système actif, caméra amarrée sur
	// l'œil du pawn, intérieur en coexistence. C'est l'ORACLE VISUEL du handoff :
	// l'image doit être celle de `-spscene=iss` (la première personne canonique).
	// Si les deux diffèrent, la coupure est toujours là, simplement déplacée.
	bool RequestedHandoff();
	// `-spcadence=<0..4>` : cadence du temps au démarrage (`fen::game::TimeRate` —
	// 0 pause, 1 réel, 2 jour/s, 3 semaine/s, 4 mois/s). Sert à vérifier de bout en
	// bout que le temps COULE [GDD 14.2] : deux captures à `-spframes` différents
	// doivent montrer une date, une heure et un ciel qui ont avancé. -1 = non
	// précisée (la partie reste en pause, son état par défaut).
	int RequestedCadence();
	// `-spvol[=<phase>]` : place une mission EN VOL à l'instant de la capture.
	// Même office que `-sphandoff` : rendre VÉRIFIABLE un instant qui, autrement,
	// ne s'atteint qu'en jouant la boucle de mission entière. C'est l'oracle
	// visuel du RYTHME IMPOSÉ [GDD 14.3] — le bandeau du temps doit nommer la
	// phase et fermer les crans au-dessus du plafond.
	// `<phase>` : ascension (défaut) | parking | injection | croisiere |
	// insertion | edl. Depuis que la chronologie DATE le vol
	// (fen/mission/FlightTimeline.hpp), l'insertion et l'EDL existent enfin à une
	// date — donc ils se capturent, et le plafond qui ne mordait qu'à l'ascension
	// se vérifie ailleurs. La position dans la chronologie est CALCULÉE (milieu
	// du segment visé), jamais un décalage écrit à la main.
	bool RequestedVol();
	// `-spvecu` : LE JOUEUR EST À BORD [GDD 9, décision 18] — la mission VÉCUE.
	// Implique `-spvol=croisiere` (dont il réutilise toute la machinerie) et
	// embarque l'Architecte. L'état demande une carrière menée jusqu'au rang
	// terminal et le support-vie long séjour qualifié : aucune capture ne peut y
	// arriver en jouant, donc sans ce drapeau la télémétrie vitale du poste
	// CONTRÔLE et le gel de l'agence [GDD 9.3] ne se photographient jamais.
	// À combiner avec `-sppost=3` et `-spcadence=4`.
	bool RequestedVecu();
	// `-spantimatiere` : QUALIFIE LA FILIÈRE DE FIN D'ARBRE et fait couler son
	// stock [GDD 5.12.12, 19.3]. Le bloc ANTIMATIÈRE du poste AGENCE — débit de
	// l'usine, plafond réel AVEC SA CAUSE, écart au seuil relativiste — ne
	// s'affiche que filière qualifiée, ce qui demande plusieurs vies de jeu : sans
	// ce drapeau, la surface qui porte toute la calibration de fin de jeu
	// [Annexe E] n'est photographiable par aucune capture. À combiner avec
	// `-sppost=0`.
	bool RequestedAntimatiere();
	const TCHAR* RequestedVolPhase();
	// `-spcode` : OUVRE L'ATELIER LOGICIEL du mode PRO [GDD 15.1, 15.5]. La partie
	// de capture démarre en NORMAL, où cette face du poste n'existe pas — et le
	// mode d'aide se choisit à la création d'une partie, écran qu'une capture ne
	// traverse pas. Même office que `-spvol` : rendre vérifiable un état qu'on
	// n'atteint autrement qu'en jouant. À combiner avec `-sppost=3`.
	bool RequestedCode();
	// `-spcode=vol` : mode PRO, mais sur la face CONDUITE DE MISSION — celle où
	// le logiciel embarqué décide. Faux alors ; vrai pour `-spcode` nu.
	bool RequestedCodeAtelier();
	// `-spoeil=x,y,z[,yaw,pitch]` : POSE L'ŒIL DU PAWN dans la station (repère
	// station, mètres ; cap en degrés). Le jeu fait apparaître le joueur dans le
	// module NOVELLUS, mais les endroits qui prouvent quelque chose sont ailleurs —
	// la CUPOLA, par exemple, est à 38 m de là, derrière deux nœuds, et une capture
	// ne peut pas y aller à pied. Faux si le drapeau est absent (le point
	// d'apparition du jeu fait alors foi).
	bool RequestedOeil(double OutM[3], double& OutYawDeg, double& OutPitchDeg);
	// À appeler chaque frame ; déclenche la capture puis la sortie du jeu.
	void Tick();
}

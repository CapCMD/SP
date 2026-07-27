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
	// `-spvol` : place une mission EN VOL (phase d'ASCENSION) à l'instant de la
	// capture. Même office que `-sphandoff` : rendre VÉRIFIABLE un instant qui,
	// autrement, ne s'atteint qu'en jouant la boucle de mission entière. C'est
	// l'oracle visuel du RYTHME IMPOSÉ [GDD 14.3] — le bandeau du temps doit
	// nommer la phase et fermer les crans au-dessus du plafond.
	bool RequestedVol();
	// À appeler chaque frame ; déclenche la capture puis la sortie du jeu.
	void Tick();
}

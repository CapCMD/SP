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
	// À appeler chaque frame ; déclenche la capture puis la sortie du jeu.
	void Tick();
}

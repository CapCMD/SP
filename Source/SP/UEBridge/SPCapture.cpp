// SPCapture.cpp — capture headless (cf. SPCapture.h). Inerte sans -spcapture.
#include "app/bridge_flags.hpp"

#include "SPCapture.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace
{
	bool   bParsed = false;
	bool   bRequested = false;
	int32  Scene = -1;
	int32  Focus = -1;
	int32  Post = -1;
	double Dist = -1.0;
	bool   bHandoff = false;
	int32  Cadence = -1;
	bool   bVol = false;
	bool   bCode = false;
	bool   bCodeAtelier = true;
	FString VolPhase = TEXT("ascension");
	bool   bOeil = false;
	double OeilM[3] = {0.0, 0.0, 0.0};
	double OeilCap[2] = {0.0, 0.0};       // yaw, pitch (deg)
	int32  Frames = 150;
	FString OutPath;
	int32  Counter = 0;
	bool   bDone = false;

	void ParseOnce()
	{
		if (bParsed) return;
		bParsed = true;
		const TCHAR* Cmd = FCommandLine::Get();
		FString Path;
		if (!FParse::Value(Cmd, TEXT("-spcapture="), Path) || Path.IsEmpty()) return;
		OutPath = Path;
		bRequested = true;
		FParse::Value(Cmd, TEXT("-spframes="), Frames);
		FString S;
		if (FParse::Value(Cmd, TEXT("-spscene="), S))
		{
			if (S.Equals(TEXT("menu"), ESearchCase::IgnoreCase))      Scene = 0;
			else if (S.Equals(TEXT("iss"), ESearchCase::IgnoreCase))  Scene = 1;
			else if (S.Equals(TEXT("map"), ESearchCase::IgnoreCase))  Scene = 2;
		}
		FParse::Value(Cmd, TEXT("-spfocus="), Focus);
		FParse::Value(Cmd, TEXT("-sppost="), Post);
		// -spdist=<km> : distance de vue imposée (sinon distance_cadrage du focus).
		// Utile pour cadrer un objet proche d'un corps (ex. Novellus en LEO).
		float DistF = -1.0f;
		if (FParse::Value(Cmd, TEXT("-spdist="), DistF)) Dist = DistF;
		bHandoff = FParse::Param(Cmd, TEXT("sphandoff"));
		FParse::Value(Cmd, TEXT("-spcadence="), Cadence);
		// `-spcode` ouvre l'ATELIER ; `-spcode=vol` reste sur la CONDUITE DE
		// MISSION, en mode Pro — c'est là que le logiciel embarqué décide. Les
		// deux faces du poste doivent se photographier, sinon l'une des deux ne
		// serait jamais vérifiée.
		bCode = FParse::Param(Cmd, TEXT("spcode"));
		FString CodeVue;
		if (FParse::Value(Cmd, TEXT("-spcode="), CodeVue) && !CodeVue.IsEmpty())
		{
			bCode = true;
			bCodeAtelier = !CodeVue.Equals(TEXT("vol"), ESearchCase::IgnoreCase);
		}
		bVol = FParse::Param(Cmd, TEXT("spvol"));
		// `-spvol=<phase>` : la phase de la chronologie à épingler. Le drapeau nu
		// vaut « ascension », ce qu'il a toujours voulu dire.
		if (!FParse::Value(Cmd, TEXT("-spvol="), VolPhase) || VolPhase.IsEmpty())
			VolPhase = TEXT("ascension");
		else
			bVol = true;
		// -spoeil=x,y,z[,yaw,pitch] : POSE L'ŒIL DU PAWN dans la station (repère
		// station, mètres et degrés — le même que `NOVELLUS_OEIL_M`). Le point
		// d'apparition du jeu est dans le module Novellus ; des endroits qui
		// COMPTENT sont ailleurs (la CUPOLA est à 38 m de là, derrière deux
		// nœuds), et une capture ne peut pas y aller à pied. Même office que
		// `-sphandoff` et `-spvol` : rendre vérifiable un état qu'on n'atteint
		// autrement qu'en jouant.
		// `bShouldStopOnSeparator = false` : par DÉFAUT `FParse::Value` s'arrête sur
		// une virgule (Parse.h:71), et ne rendait donc que « -18.2 » du triplet.
		FString Oeil;
		if (FParse::Value(Cmd, TEXT("-spoeil="), Oeil, /*bShouldStopOnSeparator=*/false))
		{
			TArray<FString> Champs;
			Oeil.ParseIntoArray(Champs, TEXT(","), true);
			if (Champs.Num() >= 3)
			{
				for (int32 k = 0; k < 3; ++k) OeilM[k] = FCString::Atod(*Champs[k]);
				for (int32 k = 0; k < 2 && 3 + k < Champs.Num(); ++k)
					OeilCap[k] = FCString::Atod(*Champs[3 + k]);
				bOeil = true;
			}
		}
		UE_LOG(LogTemp, Log,
		       TEXT("[SPCapture] scene=%d focus=%d post=%d dist=%.0f handoff=%d cadence=%d vol=%d(%s) "
		            "oeil=%d(%.2f,%.2f,%.2f cap %.0f/%.0f) frames=%d -> %s"),
		       Scene, Focus, Post, Dist, bHandoff ? 1 : 0, Cadence, bVol ? 1 : 0, *VolPhase,
		       bOeil ? 1 : 0, OeilM[0], OeilM[1], OeilM[2], OeilCap[0], OeilCap[1],
		       Frames, *OutPath);
	}
}

bool SPCapture::IsRequested() { ParseOnce(); return bRequested; }
int  SPCapture::RequestedScene() { ParseOnce(); return Scene; }
int  SPCapture::RequestedFocus() { ParseOnce(); return Focus; }
int  SPCapture::RequestedPost() { ParseOnce(); return Post; }
double SPCapture::RequestedDist() { ParseOnce(); return Dist; }
bool SPCapture::RequestedHandoff() { ParseOnce(); return bHandoff; }
int  SPCapture::RequestedCadence() { ParseOnce(); return Cadence; }
bool SPCapture::RequestedVol() { ParseOnce(); return bVol; }
const TCHAR* SPCapture::RequestedVolPhase() { ParseOnce(); return *VolPhase; }
bool SPCapture::RequestedCode() { ParseOnce(); return bCode; }
bool SPCapture::RequestedCodeAtelier() { ParseOnce(); return bCode && bCodeAtelier; }

bool SPCapture::RequestedOeil(double OutM[3], double& OutYawDeg, double& OutPitchDeg)
{
	ParseOnce();
	if (!bOeil) return false;
	for (int32 k = 0; k < 3; ++k) OutM[k] = OeilM[k];
	OutYawDeg = OeilCap[0];
	OutPitchDeg = OeilCap[1];
	return true;
}

void SPCapture::Tick()
{
	ParseOnce();
	if (!bRequested || bDone) return;
	++Counter;

	// On passe par le système de capture d'UE plutôt que de lire le back buffer
	// à la main : lui seul sait attendre la fin de la frame ET composer l'UI
	// Slate par-dessus la 3D (une lecture manuelle rend un tampon vide).
	if (Counter == Frames)
	{
		FScreenshotRequest::RequestScreenshot(OutPath, /*bShowUI*/ true,
		                                      /*bAddFilenameSuffix*/ false);
		UE_LOG(LogTemp, Log, TEXT("[SPCapture] capture demandee -> %s"), *OutPath);
		return;
	}
	// battement AVANT sortie : l'écriture (readback GPU + disque) est asynchrone,
	// et l'extinction peut planter (teardown) avant le flush. Large marge.
	if (Counter >= Frames + 300)
	{
		bDone = true;
		UE_LOG(LogTemp, Log, TEXT("[SPCapture] terminee, sortie."));
		FPlatformMisc::RequestExit(false);
	}
}

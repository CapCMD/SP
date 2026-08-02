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
	bool   bVecu = false;
	bool   bVaisseau = false;
	bool   bPassation = false;
	int32  VaisseauDistM = 50;            // « plan vaisseau (mètres) » [GDD 17.4]
	bool   bAntimatiere = false;
	bool   bNep = false;
	bool   bNepQualifie = false;
	bool   bRentree = false;
	FString RentreeCaps;
	bool   bTour = false;
	FString TourId;
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
		// `-spvecu` : LE JOUEUR EST À BORD [GDD 9, décision 18]. Même office que
		// `-spvol`, dont il RÉUTILISE toute la machinerie plutôt que d'en poser une
		// seconde : un embarquement suppose un vol habité en cours, et c'est
		// exactement ce que `-spvol` sait mettre en place. On se contente donc de
		// l'allumer et de choisir la CROISIÈRE — la seule phase où le monde peut
		// être accéléré (le plafond de [GDD 14.3] fige tout le reste), donc la
		// seule où « vivre » une mission veut dire quelque chose.
		// L'état ne s'atteint autrement qu'en menant une carrière entière jusqu'au
		// rang terminal : sans ce drapeau, aucune capture ne peut le photographier.
		bVecu = FParse::Param(Cmd, TEXT("spvecu"));
		if (bVecu)
		{
			bVol = true;
			if (!FParse::Value(Cmd, TEXT("-spvol="), VolPhase) || VolPhase.IsEmpty())
				VolPhase = TEXT("croisiere");
		}
		// `-spvaisseau` : CADRE LE VAISSEAU CONÇU, DANS SON MONDE [GDD 12.2, 17.2,
		// 17.4]. « De la vue système au plan vaisseau (mètres) par simple zoom » :
		// l'instant existe, mais il demande un vol en cours ET une caméra à
		// quelques dizaines de mètres d'un objet qui est à des centaines de
		// millions de kilomètres — aucune capture ne peut l'atteindre autrement.
		// Il RÉUTILISE `-spvol=croisiere` au lieu de poser un second état, et se
		// contente de verrouiller le focus sur le vaisseau (`FOCUS_VAISSEAU`,
		// exactement comme le clic du joueur le ferait) à distance de coque.
		// `-spdist=<km>` reste maître si on veut s'éloigner.
		// ⚠ LA DISTANCE EST EN MÈTRES ENTIERS, ET CE N'EST PAS UN CAPRICE :
		// `-spdist=` passe par un parse FLOTTANT dépendant de la LOCALE. Sur une
		// machine française, « 0.4 » rend 0,0 — le drapeau semblait donc appliqué
		// (le journal affichait bien une valeur) alors que la caméra ne bougeait
		// pas d'un mètre entre deux captures. Mesuré le 2026-08-01. Un entier en
		// mètres n'a pas de séparateur décimal, donc pas de locale.
		// `-sppassation` : l Architecte arrive en fin de vie [GDD 3.4, 3.5].
		bPassation = FParse::Param(Cmd, TEXT("sppassation"));
		bVaisseau = FParse::Param(Cmd, TEXT("spvaisseau"));
		int32 VaisseauM = 0;
		if (FParse::Value(Cmd, TEXT("-spvaisseau="), VaisseauM) && VaisseauM > 0)
		{
			bVaisseau = true;
			VaisseauDistM = VaisseauM;
		}
		if (bVaisseau)
		{
			bVol = true;
			if (!FParse::Value(Cmd, TEXT("-spvol="), VolPhase) || VolPhase.IsEmpty())
				VolPhase = TEXT("croisiere");
		}
		// `-spantimatiere` : LA FILIÈRE DE FIN D'ARBRE EST QUALIFIÉE ET SON STOCK
		// A COULÉ [GDD 5.12.12, 19.3]. Le bloc ANTIMATIÈRE du poste AGENCE ne
		// s'affiche que si le nœud `antimatiere` est opérationnel — délibérément,
		// pour ne pas faire de bruit pendant toute la partie. Il est donc INVISIBLE
		// à toute capture, et c'est précisément la surface qui porte la calibration
		// de fin de jeu : débit de l'usine, plafond réel et sa cause, écart au
		// seuil relativiste. Même office que `-spvol` et `-spvecu` — poser dans le
		// MODÈLE un état qui demande autrement plusieurs vies de jeu.
		bAntimatiere = FParse::Param(Cmd, TEXT("spantimatiere"));
		// `-spnep` : UNE FILIÈRE ALIMENTÉE DANS L'ATELIER [GDD 5.12.1, 6.2, 6.5].
		// Le poste CONCEPTION s'ouvre sur une pile chimique, qui ne porte NI
		// centrale NI radiateurs — correctement. La ligne qui prouve que
		// « énergie ≠ propulsion » a désormais un consommateur n'apparaît donc sur
		// AUCUNE capture par défaut. Même office que `-spantimatiere` : poser
		// l'ÉTAT DU MODÈLE, jamais l'affichage.
		FString NepVal;
		if (FParse::Value(Cmd, TEXT("spnep="), NepVal))
		{
			bNep = true;
			bNepQualifie = NepVal.Equals(TEXT("qualifie"), ESearchCase::IgnoreCase);
		}
		else
		{
			bNep = FParse::Param(Cmd, TEXT("spnep"));
		}
		// `-sprentree[=<id de capsule>]` : LE BOUCLIER EST OPPOSABLE [GDD 9.2].
		// La conception de départ n'a AUCUNE capsule montée (« charge nue »), donc
		// la ligne RENTREE n'apparaît sur aucune capture par défaut — et c'est
		// exact, une sonde ne revient pas. Ce drapeau monte une capsule et pose un
		// retour LUNAIRE, l'état qui fait parler le corridor. Défaut SOYUZ-SA : la
		// capsule jamais qualifiée au-delà de l'orbite basse, donc le refus.
		// `-sprentree=APOLLO-CM` montre l'inverse.
		if (!FParse::Value(Cmd, TEXT("sprentree="), RentreeCaps))
			RentreeCaps.Empty();
		bRentree = !RentreeCaps.IsEmpty() || FParse::Param(Cmd, TEXT("sprentree"));
		// `-sptour[=<id>]` : L'ASSISTANCE GRAVITATIONNELLE COMME DÉCISION [GDD 5.11].
		// La ligne TRAJECTOIRE du poste CONTRÔLE n'apparaît que pour une mission
		// dont un tour du catalogue rejoint la cible — donc jamais sur la mission
		// de départ, qui va en orbite basse. Ce drapeau pilote CAT-13 (orbiteur du
		// système solaire externe, rang Senior), et le tour est ensuite CHOISI par
		// le chemin du jeu (`Session::choisir_tour`) : le Δv, la masse et la durée
		// affichés sont ceux que l'optimiseur trouve, jamais des nombres écrits à
		// la main. `-sptour` nu montre le transfert DIRECT, ce qui est l'autre
		// moitié du troc. À combiner avec `-sppost=3`.
		if (!FParse::Value(Cmd, TEXT("sptour="), TourId))
			TourId.Empty();
		bTour = !TourId.IsEmpty() || FParse::Param(Cmd, TEXT("sptour"));
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
		       TEXT("[SPCapture] scene=%d focus=%d post=%d dist=%.0f handoff=%d cadence=%d vol=%d(%s) vecu=%d "
		            "antimatiere=%d nep=%d oeil=%d(%.2f,%.2f,%.2f cap %.0f/%.0f) frames=%d -> %s"),
		       Scene, Focus, Post, Dist, bHandoff ? 1 : 0, Cadence, bVol ? 1 : 0, *VolPhase, bVecu ? 1 : 0,
		       bAntimatiere ? 1 : 0, bNep ? 1 : 0,
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
bool SPCapture::RequestedVecu() { ParseOnce(); return bVecu; }
bool SPCapture::RequestedPassation() { ParseOnce(); return bPassation; }
bool SPCapture::RequestedVaisseau() { ParseOnce(); return bVaisseau; }
int  SPCapture::VaisseauDistanceM() { ParseOnce(); return VaisseauDistM; }
bool SPCapture::RequestedAntimatiere() { ParseOnce(); return bAntimatiere; }
bool SPCapture::RequestedNep() { ParseOnce(); return bNep; }
bool SPCapture::RequestedNepQualifie() { ParseOnce(); return bNepQualifie; }
bool SPCapture::RequestedRentree() { ParseOnce(); return bRentree; }
FString SPCapture::RentreeCapsule() { ParseOnce(); return RentreeCaps; }
bool SPCapture::RequestedTour() { ParseOnce(); return bTour; }
FString SPCapture::TourChoisi() { ParseOnce(); return TourId; }
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

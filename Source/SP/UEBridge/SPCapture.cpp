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
		UE_LOG(LogTemp, Log, TEXT("[SPCapture] scene=%d focus=%d post=%d frames=%d -> %s"),
		       Scene, Focus, Post, Frames, *OutPath);
	}
}

bool SPCapture::IsRequested() { ParseOnce(); return bRequested; }
int  SPCapture::RequestedScene() { ParseOnce(); return Scene; }
int  SPCapture::RequestedFocus() { ParseOnce(); return Focus; }
int  SPCapture::RequestedPost() { ParseOnce(); return Post; }

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
	// quelques frames de battement : l'écriture du fichier est asynchrone.
	if (Counter >= Frames + 30)
	{
		bDone = true;
		UE_LOG(LogTemp, Log, TEXT("[SPCapture] terminee, sortie."));
		FPlatformMisc::RequestExit(false);
	}
}

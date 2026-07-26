// SPGameSubsystem.cpp — voir l'entête. Zéro ImGui.

// Les entêtes du jeu AVANT tout entête UE (macros PI/check, cf. SP.Build.cs).
#include "app/session.hpp"

#include "SPGameSubsystem.h"

#include "SPCapture.h"
#include "SPHud.h"
#include "SPPlayerController.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

// pimpl : `fen::app::Session` est du C++ pur, il ne peut pas traverser un .h UE.
struct FSPSessionHolder
{
	fen::app::Session Session;
};

USPGameSubsystem::USPGameSubsystem() = default;
USPGameSubsystem::~USPGameSubsystem() = default;   // FSPSessionHolder est complet ici

bool USPGameSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) return false;
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

TStatId USPGameSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USPGameSubsystem, STATGROUP_Tickables);
}

void USPGameSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	Holder = new FSPSessionHolder();
	// Sauvegardes dans Saved/ du projet (le binaire d'origine écrivait à côté de
	// lui) : le dossier sert aussi de base au scan des parties.
	Holder->Session.chemin_sauvegarde = TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("agence.sauvegarde.txt")));

	if (UGameViewportClient* Viewport = InWorld.GetGameViewport())
	{
		SAssignNew(Hud, SSPHud).Session(&Holder->Session);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 1000);
	}

	if (ASPPlayerController* PC = Cast<ASPPlayerController>(InWorld.GetFirstPlayerController()))
	{
		PC->SetSession(&Holder->Session);
	}
	else
	{
		// Sans ASPGameMode (GlobalDefaultGameMode dans DefaultEngine.ini), plus
		// aucune entrée n'arrive : autant le dire fort.
		UE_LOG(LogTemp, Error,
		       TEXT("[SP] ASPPlayerController absent : verifier GlobalDefaultGameMode=/Script/SP.SPGameMode"));
	}
}

// La capture headless (-spscene=menu|iss|map) ouvre une partie de test, comme
// les drapeaux du binaire de référence. Sans -spcapture, inerte.
void USPGameSubsystem::ArmerCapture()
{
	if (bCaptureArmed || !SPCapture::IsRequested()) return;
	bCaptureArmed = true;
	const int Scene = SPCapture::RequestedScene();
	if (Scene < 1) return;
	fen::app::Session& S = Holder->Session;
	S.nouvelle_partie("CAPTURE", fen::app::ModeAide::Normal);
	// -spscene=iss|map : même monde, cadrage différent (map=2 -> plan système).
	S.scene = fen::app::SceneJeu::Monde;
	S.cadrage = (Scene == 2) ? fen::app::Cadrage::Systeme : fen::app::Cadrage::Bord;
	// `-spfocus=N` : équivalent du `--focus N` de la référence. On pose la
	// distance de cadrage d'emblée pour que la capture ne saisisse pas le vol
	// en cours de route.
	const int Focus = SPCapture::RequestedFocus();
	if (Focus >= 0)
	{
		fen::app::g_render_bridge.focus_body = Focus;
		fen::app::g_render_bridge.cam.dist_km = fen::app::distance_cadrage(Focus);
	}
	// -spdist=<km> : distance de vue imposée (cadrer un objet proche d'un corps).
	const double Dist = SPCapture::RequestedDist();
	if (Dist > 0.0) fen::app::g_render_bridge.cam.dist_km = Dist;
	// `-sppost=N` : ouvre un poste d'emblée (équivalent `--panel N`).
	const int Post = SPCapture::RequestedPost();
	if (Post >= 0 && S.scene == fen::app::SceneJeu::Monde &&
	    S.cadrage == fen::app::Cadrage::Bord) S.poste_ouvert = Post;
}

// Résolution / plein écran. N'a de sens qu'en jeu autonome : en PIE la fenêtre
// appartient à l'éditeur, on acquitte simplement la demande.
void USPGameSubsystem::AppliquerAffichage()
{
	fen::app::Session& S = Holder->Session;
	S.appliquer_affichage = false;
	const UWorld* W = GetWorld();
	if (!W || W->WorldType != EWorldType::Game) return;
	if (UGameUserSettings* Reglages = UGameUserSettings::GetGameUserSettings())
	{
		Reglages->SetScreenResolution(FIntPoint(S.res_w(), S.res_h()));
		Reglages->SetFullscreenMode(S.plein_ecran ? EWindowMode::WindowedFullscreen
		                                          : EWindowMode::Windowed);
		Reglages->ApplySettings(false);
	}
}

void USPGameSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!Holder) return;

	ArmerCapture();

	// L'ÉTAT AVANT LE MONDE : Session::tick publie le pont, les subsystems de
	// monde (station, carte, ciel) le liront dans leur propre Tick.
	Holder->Session.tick(DeltaTime);

	// CAPTURE de CONTROLE (`-sppost=3`) : la couche ARES ne s'initialise qu'au
	// premier tick, donc on accepte un contrat ICI (une fois) pour que le poste
	// ait une mission à piloter. Scaffolding de capture uniquement.
	if (SPCapture::RequestedPost() == 3 && !bCaptureContractAccepted &&
	    Holder->Session.jeu.ares.initialisee())
	{
		auto& G = *Holder->Session.jeu.ares.etat;
		const auto pend = G.inbox.pending_contracts();
		if (!pend.empty())
		{
			Holder->Session.accepter_contrat(pend[0]->contract_id);
			Holder->Session.piloter_premiere_mission();
			bCaptureContractAccepted = true;
		}
	}

	SPCapture::Tick();

	if (Holder->Session.appliquer_affichage) AppliquerAffichage();

	if (Holder->Session.quitter)
	{
		Holder->Session.quitter = false;
		if (UWorld* W = GetWorld())
			UKismetSystemLibrary::QuitGame(W, W->GetFirstPlayerController(),
			                               EQuitPreference::Quit, false);
	}
}

void USPGameSubsystem::Deinitialize()
{
	if (Hud.IsValid())
	{
		if (const UWorld* W = GetWorld())
			if (UGameViewportClient* Viewport = W->GetGameViewport())
				Viewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
		Hud.Reset();
	}
	delete Holder;
	Holder = nullptr;
	Super::Deinitialize();
}

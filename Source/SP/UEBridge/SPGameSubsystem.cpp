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
	// `-spvol` : ÉPINGLE UNE MISSION EN ASCENSION, pour capturer le RYTHME IMPOSÉ
	// [GDD 14.3]. Sans ce drapeau l'instant ne s'atteint qu'en jouant toute la
	// boucle de mission (contrat, conception, fenêtre, qualification, feu vert) —
	// invérifiable en capture. Même office que `-sphandoff`, et même précaution :
	// on pose l'ÉTAT DU MODÈLE, jamais le pont, sinon la capture ne prouverait que
	// l'existence du drapeau. À combiner avec `-spcadence=4` : la demande « mois/s »
	// doit ressortir bornée au temps réel.
	if (SPCapture::RequestedVol())
	{
		S.jeu.ares.assurer(S.jeu.agence, S.jeu.epoch_courant());
		if (S.jeu.ares.initialisee())
		{
			auto& G = *S.jeu.ares.etat;
			fen::mission::Mission M;
			M.contract.id = "CAP-VOL";
			M.contract.title = "Capture : mission en vol";
			M.contract.family = "sat";
			M.state = fen::mission::MissionState::Launched;
			M.state_entered_days = G.clock.now_days();
			G.missions.push_back(std::move(M));
		}
	}
	// `-spcadence=N` : fait COULER le temps d'emblée [GDD 14.2], pour vérifier de
	// bout en bout (date, heure, positions des corps) que deux captures prises à
	// des `-spframes` différents montrent un monde qui a avancé.
	// Le drapeau de capture passe par la MÊME porte que le joueur : une capture
	// doit montrer le monde tel qu'il se joue, plafond de mission compris
	// [GDD 14.3] — sinon l'oracle visuel mentirait sur ce point précis.
	const int Cad = SPCapture::RequestedCadence();
	if (Cad >= 0 && Cad <= 4) S.jeu.regler_cadence(static_cast<fen::game::TimeRate>(Cad));
	// `-sppost=N` : ouvre un poste d'emblée (équivalent `--panel N`).
	const int Post = SPCapture::RequestedPost();
	if (Post >= 0 && S.scene == fen::app::SceneJeu::Monde &&
	    S.cadrage == fen::app::Cadrage::Bord) S.poste_ouvert = Post;
}

// `-sphandoff` : ÉPINGLE LA SESSION À L'ULTIME INSTANT DU VOL [M] D'ENTRÉE
// (incr. 3c-3), pour capturer la reprise en première personne. On pilote le
// MODÈLE (le vol de caméra), jamais le pont directement : c'est donc le VRAI
// chemin de code qui pose la caméra, sinon la capture ne prouverait rien.
// À utiliser avec `-spscene=map`. L'image attendue est celle de `-spscene=iss`.
void USPGameSubsystem::EpinglerHandoff()
{
	if (!SPCapture::RequestedHandoff()) return;
	fen::app::Session& S = Holder->Session;
	if (!S.jeu.agence.creee) return;              // ArmerCapture n'a pas encore ouvert la partie
	const fen::app::Session::PoseBord Pb = S.pose_bord();
	S.cadrage = fen::app::Cadrage::Systeme;
	S.vol_cam.actif = true;
	S.vol_cam.sens = fen::app::SensVol::VersBord;
	S.vol_cam.dist_depart_km  = fen::app::Session::DIST_SYSTEME_KM;
	S.vol_cam.dist_arrivee_km = Pb.dist_km;
	S.vol_cam.yaw_depart   = fen::app::Session::YAW_SYSTEME;   S.vol_cam.yaw_arrivee   = Pb.yaw;
	S.vol_cam.pitch_depart = fen::app::Session::PITCH_SYSTEME; S.vol_cam.pitch_arrivee = Pb.pitch;
	// Durée immense + progrès au bout : le vol est AU POINT D'ARRIVÉE sans jamais
	// franchir `fini()`, donc la main ne passe pas et l'état reste capturable.
	S.vol_cam.duree_s = 1.0e9;
	S.vol_cam.progres = 1.0 - 1.0e-9;
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
	EpinglerHandoff();

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

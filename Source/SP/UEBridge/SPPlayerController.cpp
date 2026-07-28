// SPPlayerController.cpp — l'entrée native. Voir l'entête pour la doctrine.

// Les entêtes du jeu AVANT tout entête UE (macros PI/check, cf. SP.Build.cs).
#include "app/session.hpp"
#include "fen/ephem/Ephemeris.hpp"

#include "SPPlayerController.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

ASPPlayerController::ASPPlayerController()
{
	bShowMouseCursor = true;
	PrimaryActorTick.bCanEverTick = true;
}

ASPGameMode::ASPGameMode()
{
	PlayerControllerClass = ASPPlayerController::StaticClass();
	// Aucun pawn par défaut : le pawn de la station est créé et possédé par
	// USPStationSubsystem quand la scène ISS s'active.
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = false;
}

// ---------------------------------------------------------------------------
void ASPPlayerController::AppliquerModeEntree()
{
	if (!Session) return;
	// Une modale (faillite, réglages) reprend TOUTE l'entrée : elle porte une
	// décision, la scène derrière ne doit plus répondre. Un POSTE OUVERT fait de
	// même pour la station : le joueur clique dans le panneau, il ne se déplace
	// plus (mais garde la souris pour agir sur les boutons du poste).
	const bool bModale = (Session->modal != fen::app::Modal::Aucun);
	const bool bPoste  = (Session->scene == fen::app::SceneJeu::Monde &&
	                      Session->cadrage == fen::app::Cadrage::Bord &&
	                      Session->poste_ouvert >= 0);
	// Clé de cache : scène (0/1) + cadrage (×2) + modale + poste. Le cadrage
	// distingue les deux modes d'entrée du Monde (bord 1re personne / système).
	const int32 Etat = static_cast<int32>(Session->scene) +
	                   static_cast<int32>(Session->cadrage) * 2 +
	                   (bModale ? 100 : 0) + (bPoste ? 200 : 0);
	if (Etat == SceneAppliquee) return;
	SceneAppliquee = Etat;

	if (bModale)
	{
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		bShowMouseCursor = true;
		return;
	}

	if (bPoste)
	{
		// Curseur visible et cliquable, mais le clavier reste pollé (Échap ferme
		// le poste via PlayerTick). GameAndUI : les deux à la fois.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		bShowMouseCursor = true;
		return;
	}

	if (Session->scene == fen::app::SceneJeu::Titre)
	{
		// Le menu Slate est le seul interactif : entrée UI seule.
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		bShowMouseCursor = true;
	}
	else if (Session->cadrage == fen::app::Cadrage::Bord)
	{
		// À BORD, PREMIÈRE PERSONNE : curseur capturé, le regard suit la souris.
		// C'est précisément ce qu'ImGui rendait impossible.
		FInputModeGameOnly Mode;
		Mode.SetConsumeCaptureMouseDown(true);
		SetInputMode(Mode);
		bShowMouseCursor = false;
	}
	else   // Cadrage::Systeme : le plan système du Monde, façon NASA Eyes.
	{
		// Curseur visible, molette et glisser pilotent la caméra, le clic
		// sélectionne un corps.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		bShowMouseCursor = true;
		bGlisse = false;
	}
}

// ---------------------------------------------------------------------------
void ASPPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!Session) return;
	AppliquerModeEntree();

	auto& B = fen::app::g_render_bridge;

	// Modale ouverte : plus aucune commande de jeu (ni ambulation, ni caméra).
	if (Session->modal != fen::app::Modal::Aucun)
	{
		auto& In = B.station_in;
		In.fwd = 0.0f; In.right = 0.0f; In.up = 0.0f; In.agrippe = false;
		In.look_dx = 0.0f; In.look_dy = 0.0f;
		bPrecM = bPrecE = bPrecF5 = bPrecEsc = false;
		return;
	}

	// fronts descendants (une action par appui, jamais par frame)
	const bool bM   = IsInputKeyDown(EKeys::M);
	const bool bE   = IsInputKeyDown(EKeys::E);
	const bool bF5  = IsInputKeyDown(EKeys::F5);
	const bool bEsc = IsInputKeyDown(EKeys::Escape);
	const bool bMDown   = bM   && !bPrecM;
	const bool bEDown   = bE   && !bPrecE;
	const bool bF5Down  = bF5  && !bPrecF5;
	const bool bEscDown = bEsc && !bPrecEsc;
	bPrecM = bM; bPrecE = bE; bPrecF5 = bF5; bPrecEsc = bEsc;

	// ═══ CADENCE DU TEMPS [GDD 14.2] ═══ : dans TOUT le Monde, aux deux cadrages,
	// et même poste ouvert. Ce n'est PAS le « curseur de temps » que [GDD 14]
	// interdit — ce sont les mêmes CINQ CRANS discrets que le poste AGENCE, qui
	// passent par le système temporel de l'agence et en portent les coûts. Le
	// raccourci ne fait qu'y donner accès de partout : sans lui, lancer le temps
	// obligeait à rentrer à bord et à marcher jusqu'au module AGENCE, y compris
	// depuis le plan système. Injouable, signalé à l'essai en PIE.
	TickCadence();

	if (Session->scene == fen::app::SceneJeu::Titre)
	{
		// DÉCOR DU MENU : le monde vu au plan système tourne lentement derrière
		// le panneau (fond étoilé + orbites ténues, cf. ref_menu.png).
		// Présentation pure — aucun état de jeu n'en dépend.
		B.focus_body = -1;
		B.cam.dist_km = 6.5e8;
		B.cam.pitch = 1.15;
		B.cam.yaw = B.cam.yaw.load() + DeltaTime * 0.012;
	}
	else if (Session->cadrage == fen::app::Cadrage::Bord)
	{
		// À BORD DE NOVELLUS, première personne.
		if (bEscDown)
		{
			if (Session->poste_ouvert >= 0) Session->poste_ouvert = -1;
			else                            Session->retour_menu();
		}
		else if (Session->poste_ouvert < 0)
		{
			// [M] = SIGNET DE CAMÉRA : VOL continu vers le plan système, ancré
			// sur la Terre, SANS quitter le Monde [GDD v1.2 ch.8.3, 17.4].
			if (bMDown)  Session->demarrer_vol_cadrage();
			if (bF5Down) Session->sauvegarder_partie();
			if (bEDown)
			{
				const int Proche = B.station_out.near_post.load();
				if (Proche >= 0) Session->poste_ouvert = Proche;
			}
		}
		// Le vol a pu basculer le cadrage : ne piloter l'ambulation que si l'on
		// est encore effectivement à bord.
		if (Session->cadrage == fen::app::Cadrage::Bord) TickStation(DeltaTime);
	}
	else   // Cadrage::Systeme : le plan système du Monde.
	{
		// [M] = vol de RETOUR vers le bord ; Échap = retour immédiat. Pendant le
		// vol, le zoom/orbite manuel est suspendu (la transition pilote la caméra).
		if (bMDown)        Session->demarrer_vol_cadrage();
		else if (bEscDown) Session->retour_bord_immediat();
		if (bF5Down) Session->sauvegarder_partie();
		if (!Session->vol_cam.actif) TickCarte(DeltaTime);
	}
}

// ---------------------------------------------------------------------------
// LA CADENCE DU TEMPS [GDD 14.2]. Cinq crans discrets, jamais un facteur libre :
// un facteur arbitraire casserait la reproductibilité des prélèvements
// (fen::game::TimeRate). Touches choisies pour ne rien recouvrir de l'ambulation
// (ZQSD, espace, ctrl, maj) ni des raccourcis existants (E, M, F5, Échap) ; UE
// nomme les touches par leur POSITION physique, la rangée 1-5 marche donc aussi
// bien en AZERTY qu'en QWERTY (même raison que W/A/S/D).
//   [P]     pause / reprise à la dernière cadence
//   [1..5]  pause, temps réel, jour/s, semaine/s, mois/s
void ASPPlayerController::TickCadence()
{
	if (!Session || Session->scene != fen::app::SceneJeu::Monde) return;
	using fen::game::TimeRate;

	// Toutes les demandes passent par `regler_cadence` : c'est elle qui borne au
	// plafond de la mission [GDD 14.3]. La MÉMOIRE, elle, garde le cran DEMANDÉ et
	// non le cran obtenu — sinon une ascension traversée en pause ferait oublier
	// définitivement au joueur la cadence à laquelle il jouait.
	const bool bP = IsInputKeyDown(EKeys::P);
	if (bP && !bPrecP)
	{
		if (Session->jeu.cadence == TimeRate::Paused)
			Session->jeu.regler_cadence(static_cast<TimeRate>(FMath::Clamp(CadenceMemo, 1, 4)));
		else
		{
			// On ne mémorise PAS une cadence qu'on n'a pas choisie : sous plafond,
			// la valeur courante est celle que la mission impose, pas celle que le
			// joueur jouait. La mémoriser lui ferait perdre son réglage de croisière
			// à la première pause pendant une ascension.
			if (!Session->jeu.plafond_temps().constrained)
				CadenceMemo = static_cast<int32>(Session->jeu.cadence);
			Session->jeu.regler_cadence(TimeRate::Paused);
		}
	}
	bPrecP = bP;

	static const FKey Crans[5] = {EKeys::One, EKeys::Two, EKeys::Three,
	                              EKeys::Four, EKeys::Five};
	for (int32 k = 0; k < 5; ++k)
	{
		const bool bDown = IsInputKeyDown(Crans[k]);
		if (bDown && !bPrecCran[k])
		{
			if (k > 0) CadenceMemo = k;
			Session->jeu.regler_cadence(static_cast<TimeRate>(k));
		}
		bPrecCran[k] = bDown;
	}
}

// ---------------------------------------------------------------------------
// L'AMBULATION À BORD. Le pawn (SPStation) consomme ces commandes : on garde le
// pont comme frontière, mais les valeurs viennent maintenant du vrai pipeline
// d'entrée UE, pas d'un HUD qui volait le clavier.
void ASPPlayerController::TickStation(float DeltaTime)
{
	auto& In = fen::app::g_render_bridge.station_in;

	if (Session->poste_ouvert >= 0)   // panneau ouvert : on ne bouge plus
	{
		In.fwd = 0.0f; In.right = 0.0f; In.up = 0.0f; In.agrippe = false;
		In.look_dx = 0.0f; In.look_dy = 0.0f;
		return;
	}

	// ZQSD (AZERTY) et WASD (QWERTY) partagent les mêmes codes physiques : UE
	// nomme les touches par leur position, W/A/S/D couvre donc les deux.
	const bool Av = IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up);
	const bool Ar = IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down);
	const bool Ga = IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left);
	const bool Dr = IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right);
	In.fwd   = (Av ? 1.0f : 0.0f) - (Ar ? 1.0f : 0.0f);
	In.right = (Dr ? 1.0f : 0.0f) - (Ga ? 1.0f : 0.0f);
	In.up    = (IsInputKeyDown(EKeys::SpaceBar) ? 1.0f : 0.0f) -
	           (IsInputKeyDown(EKeys::LeftControl) ? 1.0f : 0.0f);
	In.agrippe = IsInputKeyDown(EKeys::LeftShift);

	// LE REGARD : souris capturée, delta brut. Le pawn accumule et applique.
	float Dx = 0.0f, Dy = 0.0f;
	GetInputMouseDelta(Dx, Dy);
	In.look_dx = In.look_dx.load() + Dx;
	In.look_dy = In.look_dy.load() - Dy;   // axe Y de la souris UE : vers le haut
}

// ---------------------------------------------------------------------------
// LA CAMÉRA DE LA CARTE. Reprend au mot près la commande de l'ancien HUD ImGui
// (zoom logarithmique, orbite, picking) — seule la SOURCE des événements change.
void ASPPlayerController::TickCarte(float DeltaTime)
{
	auto& B = fen::app::g_render_bridge;
	const int FocusCourant = B.focus_body.load();
	int NouveauFocus = FocusCourant;

	// --- survol : le picking se fait sur la projection écran publiée par UE ---
	const int Survol = CorpsSousCurseur();
	B.hover_body = Survol;

	// --- zoom : LOGARITHMIQUE ------------------------------------------------
	// Du rayon d'une planète à la ceinture de Kuiper il y a 9 ordres de
	// grandeur : seul un pas multiplicatif est utilisable.
	const float Molette = GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	if (!FMath::IsNearlyZero(Molette))
	{
		const double F = FMath::Exp(-Molette * 0.22);
		// NOVELLUS (focus spécial, hors enum Body) : la borne de zoom est son
		// ENVELOPPE, pas body_radius (qui n'a pas d'entrée pour lui). La molette
		// s'arrête donc AU RAS DE LA COQUE : entrer à bord est le rôle de [M] (vol
		// de caméra + handoff, incr. 3c-3), jamais celui du zoom manuel.
		const double RFocus = (FocusCourant == fen::app::FOCUS_STATION)
			? fen::app::STATION_ENVERGURE_M * 0.5 / 1000.0
			: FocusCourant < 0
				? 1.0e6
				: fen::ephem::body_radius(static_cast<fen::ephem::Body>(FocusCourant)) / 1000.0;
		B.cam.dist_km = FMath::Clamp(B.cam.dist_km.load() * F, RFocus * 1.15, 2.0e10);
	}

	// --- orbite : glisser bouton gauche --------------------------------------
	float Mx = 0.0f, My = 0.0f;
	GetMousePosition(Mx, My);
	const FVector2D Souris(Mx, My);
	const bool bBouton = IsInputKeyDown(EKeys::LeftMouseButton);

	if (bBouton && !bGlisse) { bGlisse = true; PosClicBas = Souris; }
	else if (bBouton && bGlisse)
	{
		const FVector2D D = Souris - PosClicBas;
		if (!D.IsNearlyZero())
		{
			// SENS DE ROTATION : on SAISIT le monde et on le tire, comme dans Eyes
			// on the Solar System — glisser vers la droite fait partir l'objet vers
			// la droite. La caméra étant en (cos p·cos y, cos p·sin y, sin p) et
			// regardant le point visé, un yaw CROISSANT la déplace vers SA GAUCHE,
			// donc l'objet vers la droite : le signe est donc « + ».
			// (C'était « − » : glisser à droite envoyait l'objet à gauche.)
			B.cam.yaw = B.cam.yaw.load() + D.X * 0.006;
			// Le pitch, lui, était déjà dans le bon sens : tirer vers le bas fait
			// basculer le sommet vers soi, donc monte l'œil.
			B.cam.pitch = FMath::Clamp(B.cam.pitch.load() + D.Y * 0.006, -1.5, 1.5);
			PosClicBas = Souris;
		}
	}
	else if (!bBouton && bGlisse)
	{
		// CLIC FRANC (relâché sans avoir glissé) sur un corps = focus.
		bGlisse = false;
		if (Survol >= 0 && FVector2D::Distance(Souris, PosClicBas) < 3.0)
			NouveauFocus = Survol;
	}

	if (NouveauFocus != FocusCourant)
	{
		B.focus_body = NouveauFocus;
		B.cam.dist_km = fen::app::distance_cadrage(NouveauFocus);   // cadrage d'arrivée
	}
	(void)DeltaTime;
}

// ---------------------------------------------------------------------------
// Le corps le plus proche du curseur, dans un rayon de 18 px (ou son rayon
// apparent s'il est plus gros). Les coordonnées publiées sont NORMALISÉES.
int ASPPlayerController::CorpsSousCurseur() const
{
	auto& S = fen::app::g_render_bridge.screen;
	FVector2D VP(1280.0, 720.0);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
	float Mx = 0.0f, My = 0.0f;
	const_cast<ASPPlayerController*>(this)->GetMousePosition(Mx, My);

	int Meilleur = -1;
	double D2Meilleur = TNumericLimits<double>::Max();
	const int32 N = FMath::Min(S.n.load(), fen::app::RenderBridge::ScreenBodies::MAX);
	for (int32 i = 0; i < N; ++i)
	{
		const auto& It = S.items[i];
		if (!It.on_screen) continue;
		const double Px = It.nx * VP.X, Py = It.ny * VP.Y;
		const double RPx = It.r_norm * VP.X;
		const double Seuil = FMath::Max(18.0 * (VP.Y / 720.0), RPx);
		const double Dx = Mx - Px, Dy = My - Py;
		const double D2 = Dx * Dx + Dy * Dy;
		if (D2 < Seuil * Seuil && D2 < D2Meilleur) { D2Meilleur = D2; Meilleur = It.body; }
	}
	return Meilleur;
}

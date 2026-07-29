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
	// `-spcode` : la partie démarre en PRO, et le poste CONTRÔLE s'ouvre sur sa
	// face ATELIER LOGICIEL [GDD 15.1]. Le mode d'aide se choisit à la création
	// d'une partie — un écran qu'une capture ne traverse pas ; sans ce drapeau,
	// l'éditeur de code serait un écran que rien ne peut photographier.
	S.nouvelle_partie("CAPTURE", SPCapture::RequestedCode() ? fen::app::ModeAide::Pro
	                                                       : fen::app::ModeAide::Normal);
	S.atelier_logiciel = SPCapture::RequestedCodeAtelier();
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
			using fen::mission::FlightPhase;
			auto& G = *S.jeu.ares.etat;
			const FString Ph = FString(SPCapture::RequestedVolPhase()).ToLower();

			// La FAMILLE décide du profil de vol : une charge GEO n'a ni croisière
			// interplanétaire ni EDL. On prend donc celle qui PORTE la phase visée.
			fen::mission::Mission M;
			M.contract.id = "CAP-VOL";
			M.contract.title = "Capture : mission en vol";
			M.contract.family = (Ph == TEXT("edl")) ? "surface"
			                  : (Ph == TEXT("ascension") || Ph == TEXT("parking")) ? "sat"
			                  : "mars_habite";
			// `-spvecu` : le vol doit être HABITÉ, sinon il n'y a personne à
			// rejoindre. `mars_habite` l'est dans le catalogue [CAT-09] ; on le
			// dit ici parce que la mission de capture est construite à la main.
			if (SPCapture::RequestedVecu())
			{
				M.contract.family = "mars_habite";
				M.contract.title = "Capture : mission habitee vecue";
				M.contract.crewed = true;
			}
			// ═══ ET SES TERMES SONT CEUX DE SA FAMILLE ═══ [GDD 4.1]
			// Sans cette ligne, la mission de capture naissait avec des termes
			// NULS : « BUDGET CONTRAT 0 M EUR », « CALENDRIER 0 / 0 mois »,
			// « P(SUCCES) 0,0 % » et un VERROU rouge dans CHAQUE image en vol —
			// une alarme fausse, donc une alarme qu'on apprend à ne plus lire, et
			// un bilan de viabilité que la capture ne pouvait pas vérifier. Même
			// table que le catalogue, un seul endroit où elle vit.
			M.contract.terms = fen::mission::contract_terms_for_family(M.contract.family);
			// `-spvol=conception` : la mission N'EST PAS PARTIE. C'est la phase où
			// vit l'étude de navigation (dispersion d'injection, correction à
			// prévoir) — on ne reconçoit pas un véhicule en vol, donc ce cadran ne
			// se capture pas autrement.
			const bool bConception = (Ph == TEXT("conception"));
			M.state = bConception ? fen::mission::MissionState::Design
			                      : fen::mission::MissionState::Launched;

			// La phase visée dans la chronologie. « injection » = la PREMIÈRE
			// manœuvre critique, « insertion » = la DERNIÈRE : le même nom de
			// phase désigne deux instants du vol.
			const FlightPhase Cible = (Ph == TEXT("parking"))   ? FlightPhase::LeoOps
			                        : (Ph == TEXT("injection") || Ph == TEXT("insertion") ||
			                           Ph == TEXT("tcm") || Ph == TEXT("tcm2"))
			                              ? FlightPhase::CriticalManeuver
			                        : (Ph == TEXT("croisiere")) ? FlightPhase::TransferCruise
			                        : (Ph == TEXT("edl"))       ? FlightPhase::Edl
			                                                    : FlightPhase::Launch;
			// « injection » = la 1re manœuvre critique, « tcm » = la 2e (TCM-1),
			// « tcm2 » = la 3e, « insertion » = la dernière. Le même nom de phase
			// désigne quatre instants d'un vol interplanétaire.
			const int Occurrence = (Ph == TEXT("tcm")) ? 1 : (Ph == TEXT("tcm2")) ? 2 : 0;
			const bool bDerniere = (Ph == TEXT("insertion"));

			// ON FAIT AVANCER LE MONDE, ON NE RECULE PAS LA MISSION. Reculer la
			// date du feu vert produisait un vol PARTI HORS FENÊTRE — légal pour
			// Lambert, mais qu'aucune mission ne volerait, et l'arc plongeait à
			// 0,26 UA du Soleil (piège n°63). Le jeu, lui, interdit de lancer hors
			// fenêtre (`launch_window_gate`) : la capture suit donc le MÊME
			// chemin — le monde attend l'ouverture, on lance, puis le monde avance
			// jusqu'à la phase voulue. C'est ce que fait le joueur, en accéléré.
			auto AvancerMonde = [&S](double Jours)
			{
				if (Jours <= 0.0) return;
				S.jeu.avancer_temps(Jours);
				S.jeu.ares.assurer(S.jeu.agence, S.jeu.epoch_courant());
			};

			// 1) attendre l'ouverture de la fenêtre (familles à fenêtre synodique).
			const auto WT = fen::mission::window_target_for_family(M.contract.family);
			if (WT.impose)
			{
				const auto W = fen::astro::launch_window(
					S.jeu.eph, WT.dep, WT.arr, fen::Epoch{S.jeu.epoch_courant()});
				if (W.ok && !W.open && W.next_open_days > 0.0) AvancerMonde(W.next_open_days);
			}
			// 2) FEU VERT ICI : la durée de transit est celle de la géométrie du
			//    ciel au moment où le vol part — la règle du modèle, pas une
			//    commodité de capture.
			auto& G2 = *S.jeu.ares.etat;
			M.state_entered_days = G2.clock.now_days();
			M.tof_days = fen::mission::transfer_tof_days(
				M, fen::Epoch{S.jeu.epoch_courant()}, S.jeu.eph);
			// 3) laisser le monde courir jusqu'au MILIEU du segment visé. Le jour
			//    où une durée de phase change, la capture suit toute seule.
			if (!bConception)
			{
				const fen::mission::FlightTimeline TL = fen::mission::build_flight_timeline(M);
				int Idx = -1, Restant = Occurrence;
				for (int i = 0; i < TL.n; ++i)
					if (TL.seg[i].phase == Cible && TL.seg[i].t1_days > TL.seg[i].t0_days)
					{
						Idx = i;
							if (!bDerniere && --Restant < 0) break;
					}
				if (Idx >= 0) AvancerMonde(0.5 * (TL.seg[Idx].t0_days + TL.seg[Idx].t1_days));
			}

			auto& G3 = *S.jeu.ares.etat;
			// L'ÉTAT VRAI DU VOL doit exister, sinon le poste n'a rien à montrer :
			// c'est ce que le feu vert fait dans une vraie partie. On passe par la
			// MÊME porte (`tirer_navigation`), jamais par une pose directe — sinon
			// la capture prouverait l'existence du drapeau, pas celle du modèle.
			if (!bConception) S.tirer_navigation(M);
			UE_LOG(LogTemp, Log,
			       TEXT("[SPCapture] vol epingle : phase=%s famille=%hs tof=%.0f j "
			            "arrivee dans %.2f j"),
			       *Ph, M.contract.family.c_str(), M.tof_days,
			       fen::mission::flight_arrival(M, G3.clock.now_days()).reste_jours);

			// ═══ `-spvecu` : ON MONTE À BORD [GDD 9] ═══
			// DANS LE BON ORDRE, et c'est le modèle qui l'impose : on embarque
			// AVANT le feu vert (`Session::peut_embarquer` refuse un vol déjà
			// parti — on ne rattrape pas un vaisseau en route). La mission est
			// donc déposée en QUALIFICATION, l'Architecte y monte par la MÊME
			// porte que le bouton du poste, puis on la fait décoller avec les
			// dates que la chronologie vient de calculer — ce que fait le feu
			// vert dans une vraie partie.
			//
			// Les conditions de [GDD 9.2] sont posées dans le MODÈLE, pas
			// contournées : une mission longue exige le rang terminal ET le
			// support-vie long séjour qualifié. C'est l'état qu'aurait un joueur
			// arrivé là en jouant — on le lui donne, on ne désactive pas la porte.
			if (SPCapture::RequestedVecu())
			{
				const double Entree = M.state_entered_days;
				const double Tof = M.tof_days;
				// LU AVANT LE DÉPLACEMENT, comme `Entree` et `Tof` juste au-dessus :
				// après `std::move`, `M.contract.family` est une chaîne vidée. La
				// première version de la boucle de prérequis ci-dessous lisait `M`
				// APRÈS le move et n'accordait donc rien — le journal de bord l'a
				// dit en une ligne (« maturite requise : support-vie long sejour »)
				// là où la capture seule ne montrait qu'un bloc manquant.
				const std::string Famille = M.contract.family;
				M.state = fen::mission::MissionState::Qualification;
				G3.missions.push_back(std::move(M));

				G3.career.rank = fen::career::Rank::Directeur;
				auto Qualifier = [&G3](const char* Id)
				{
					if (fen::tech::TechNode* N = G3.tree.find_mut(Id))
						N->trl = fen::tech::TRL_OPERATIONAL;
				};
				// ═══ ON ACCORDE CE QUE LE CONTRAT EXIGE, PAS UNE LISTE À LA MAIN ═══
				// Une liste écrite ici se périme dès qu'un prérequis change dans le
				// catalogue — et c'est arrivé : l'assemblage orbital est devenu
				// nécessaire à une architecture martienne blindée (182 t contre
				// 130 t de plafond), la capture ne l'accordait pas, et le poste
				// affichait un verrou parfaitement juste mais sans intérêt pour une
				// image censée montrer un vol EN COURS. On lit donc les prérequis
				// de l'entrée de catalogue de la MÊME famille : le jour où ils
				// changent, la capture suit toute seule.
				for (const auto& E : G3.catalog.entries())
					if (E.contract.family == Famille)
						for (const auto& T : E.contract.prerequisites.required_tech)
							Qualifier(T.c_str());
				// Le recyclage quasi fermé fait partie d'une architecture de fin
				// d'arbre : sans lui la télémétrie afficherait les boucles ISS. Il
				// n'est prérequis d'aucun contrat — c'est un CHOIX d'architecture.
				Qualifier("recyclage_partiel");
				Qualifier("recyclage_ferme");
				// ET LA CAMPAGNE DE MISE EN ORBITE [GDD 5.2 branche 1] : 182 t
				// blindées demandent deux tirs du super-lourd, donc le rendez-vous
				// automatisé. Un architecte au rang terminal qui part pour Mars les
				// a forcément — les lui refuser peindrait un verrou de progression
				// sur une capture qui documente autre chose.
				Qualifier("lanceur_lourd");
				Qualifier("lanceur_super_lourd");
				Qualifier("rdv_automatise");

				S.piloter_premiere_mission();
				// UNE ARCHITECTURE QUI A CHOISI DE PROTÉGER SON ÉQUIPAGE [GDD 6.6] :
				// 10 g/cm² de matériau riche en hydrogène. Sans ce choix, la
				// télémétrie de dose afficherait un blindage nul et la capture
				// montrerait le pire cas au lieu d'un arbitrage.
				S.mission_plan.blindage = fen::env::Shielding{10.0, 1.0};
				// ET ON ÉVALUE LE PLAN, comme le poste CONCEPTION le ferait
				// [GDD 4.1]. Sans cet appel, `MissionPlan::evaluated` reste faux
				// et le bilan de viabilité s'affiche « CONCEPTION NON EVALUEE » :
				// juste, mais sans intérêt pour une capture censée montrer un vol
				// en cours. C'est le pendant des termes du contrat ci-dessus —
				// une mission fabriquée à la main doit traverser les mêmes portes
				// qu'une mission jouée, sinon la capture ne prouve rien.
				S.evaluer_plan();
				const bool bMonte = S.embarquer();

				// LE DÉCOLLAGE : on rend à la mission l'état de vol déjà calculé.
				if (fen::mission::Mission* Mp = S.mission_courante())
				{
					Mp->state = fen::mission::MissionState::Launched;
					Mp->state_entered_days = Entree;
					Mp->tof_days = Tof;
				}
				UE_LOG(LogTemp, Log,
				       TEXT("[SPCapture] mission vecue : embarque=%d (%hs) — autonomie %.0f j, "
				            "agence gelee=%d"),
				       bMonte ? 1 : 0, S.dernier_refus_embarquement.c_str(),
				       G3.lived.days_left(), G3.finance.suspended ? 1 : 0);
			}
			else
			{
				G3.missions.push_back(std::move(M));
			}
		}
	}
	// ═══ `-spantimatiere` : LA FILIÈRE DE FIN D'ARBRE EXISTE ═══
	// [GDD 5.12.12, 19.3] Le bloc ANTIMATIÈRE du poste AGENCE ne s'affiche que si
	// le nœud est opérationnel — et l'y amener demande une carrière entière puis
	// plusieurs vies d'accumulation. On pose donc l'ÉTAT DU MODÈLE, comme
	// `-spvol` et `-spvecu` : le nœud est qualifié PAR LE MÊME champ que la
	// recherche (`trl`), et le stock est obtenu en faisant COULER la production
	// réelle sur l'horizon de calibration, jamais en écrivant un nombre de
	// grammes. Une capture qui poserait le stock à la main ne prouverait que
	// l'existence de la ligne d'affichage.
	// `assurer` D'ABORD, comme le fait `-spvol` : la couche ARES n'est pas encore
	// montée au sortir de `nouvelle_partie`, et sans cet appel le test
	// `initialisee()` est faux — le drapeau s'appliquait donc à rien, et la
	// capture montrait un arbre au défaut sans rien signaler.
	if (SPCapture::RequestedAntimatiere())
		S.jeu.ares.assurer(S.jeu.agence, S.jeu.epoch_courant());
	if (SPCapture::RequestedAntimatiere() && S.jeu.ares.initialisee())
	{
		fen::game::GameState& Ga = *S.jeu.ares.etat;
		for (const char* Id : {"fission_spatiale", "nep_megawatt", "fusion", "antimatiere"})
			if (fen::tech::TechNode* N = Ga.tree.find_mut(Id))
				N->trl = fen::tech::TRL_OPERATIONAL;
		Ga.antimatiere.prod =
			fen::rel::AntimatterProduction::for_tier(fen::app::antimatter_tier(Ga));
		Ga.antimatiere.tick(
			fen::rel::AntimatterProduction::CALIB_HORIZON_YEARS * 365.25, true);
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

#if 0	// DIAGNOSTIC — la mission vécue avance-t-elle vraiment à bord ?
	// GARDÉ SOUS `#if 0`, comme celui du ciel : c'est lui qui a tranché entre
	// « le modèle ne consomme rien » et « l'écran ne se rafraîchit pas » (piège
	// n°74), en trois lignes de journal contre une capture qui ne pouvait pas le
	// dire. Le réécrire coûterait plus cher que le lire.
	if (SPCapture::RequestedVecu() && Holder->Session.jeu.ares.initialisee())
	{
		static int Compteur = 0;
		if ((Compteur++ % 200) == 0)
		{
			auto& G = *Holder->Session.jeu.ares.etat;
			int Etat = -1; int Ph = -1;
			for (const auto& mm : G.missions)
				if (mm.contract.id == G.lived.mission_id)
				{ Etat = (int)mm.state; Ph = (int)mm.phase; break; }
			UE_LOG(LogTemp, Warning,
			       TEXT("[DIAG vecu] f=%d dt=%.4f mois=%.4f cadence=%d actif=%d etat=%d phase=%d "
			            "jours_restants=%.3f dose=%.5f"),
			       Compteur, DeltaTime, Holder->Session.jeu.agence.mois,
			       (int)Holder->Session.jeu.cadence, G.lived.active ? 1 : 0, Etat, Ph,
			       G.lived.days_left(), G.dose_architecte.career_sv);
			// LES DEUX HORLOGES [GDD 6.7] : le bloc « VIE À BORD » est sous la
			// ligne de flottaison du défilement, donc AUCUNE capture ne peut
			// montrer ce chiffre — une mesure, ici, vaut mieux que dix images.
			UE_LOG(LogTemp, Warning,
			       TEXT("[DIAG horloge] t_terre=%.1f j  tau_bord=%.1f j  ecart=%+.4f ms  "
			            "a_croisiere=%.4f UA  medical=%.2f  age_bio=%.4f ans"),
			       G.dual_clock.t_earth / 86400.0, G.dual_clock.tau_board / 86400.0,
			       -G.dual_clock.aging_gap() * 1000.0,
			       G.lived.horloge.a_croisiere_m / fen::cst::AU,
			       G.lived.facteur_risque_medical,
			       G.character.age_bio_s / fen::career::YEAR_S);
		}
	}
#endif

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

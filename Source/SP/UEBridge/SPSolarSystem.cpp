// SPSolarSystem.cpp — LA CARTE DU SYSTÈME SOLAIRE (écran principal du jeu).
// Portage UE5 natif de render/app/solar_system_map.cpp (référence Vulkan `spr`).
//
// ═══ ÉCHELLE VRAIE ═══ (plus aucune exagération à déclarer)
//   1 unité UE = 1 km. Les rayons sont les rayons RÉELS, les distances les
//   distances RÉELLES : la Terre fait 6 371 u de rayon, l'orbite GEO 42 164 u,
//   Neptune est à 4,5e9 u. Une planète devient donc sous-pixellique dès qu'on
//   s'éloigne — c'est le comportement voulu (`body_min_size = 0` de la
//   référence) : le HUD prend alors le relais avec un MARQUEUR et un libellé.
//
// ═══ RENDU RELATIF À LA CAMÉRA (floating origin) ═══
//   Les positions viennent de l'éphéméride en double (m), sont converties en km
//   double, et l'ORIGINE DE RENDU EST L'ŒIL : on place chaque objet à
//   (monde − œil). La caméra reste à l'origine du monde UE. Conséquence : la
//   précision est maximale là où on regarde, et l'erreur relative d'un corps
//   lointain (float, ~1e-7) reste très inférieure au pixel.
//   Repère : écliptique J2000 (droitier, z-up) -> UE (gaucher, z-up) via y -> −y.
//
// ═══ L'ENTRÉE EST NATIVE, LE PONT RESTE LA FRONTIÈRE ═══
//   Depuis le passage en rendu total UE5, la souris et le clavier arrivent
//   normalement dans le pipeline UE (ASPPlayerController) : c'est lui qui
//   COMMANDE la caméra via le pont (RenderBridge::cam), et ce fichier
//   l'APPLIQUE. En retour il PUBLIE la projection écran des corps
//   (RenderBridge::screen), dont se servent le HUD pour ses marqueurs et
//   libellés, et le contrôleur pour le picking. Sens unique des deux côtés.

// Les entêtes du jeu AVANT tout entête UE (macros PI/check, cf. SP.Build.cs).
#include "app/bridge_flags.hpp"
#include "fen/core/Constants.hpp"
#include "fen/ephem/BodyOrientation.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/ephem/Satellites.hpp"

#include "SPSolarSystem.h"

#include "SPCameraPost.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace {

using fen::ephem::Body;

struct FBodyDef
{
	Body B;
	Body Parent;             // Sun pour les planètes ; la planète pour les lunes
	const TCHAR* Asset;      // nom d'asset sous /Game/SolarSystem (import GLB)
	FLinearColor Color;      // repli si le GLB n'est pas importé ; couleur du marqueur
	bool bMoon;
};

// ORDRE : chaque lune SUIT son parent. Ce n'est pas cosmétique — la règle de
// séparabilité écran (piège n°41) teste une lune contre l'item déjà publié de son
// parent : il doit donc être calculé avant.
// LA PÉRIODE SIDÉRALE A DISPARU DE CETTE TABLE (2026-07-27) : c'était le repli de
// l'ancien `OrientationAt`, une rotation à axe et à phase inventés. L'orientation
// vient maintenant ENTIÈREMENT du cœur (`ephem/BodyOrientation` pour les corps à
// éléments IAU, `ephem/Satellites` pour le verrou synchrone des lunes) — il n'y a
// plus de chiffre d'orientation dupliqué ici, donc plus de chiffre à désaccorder.
const FBodyDef GBodies[] = {
	{Body::Sun,      Body::Sun,      TEXT("Sun"),     FLinearColor(1.00f, 0.85f, 0.30f), false},
	{Body::Mercury,  Body::Sun,      TEXT("Mercury"), FLinearColor(0.55f, 0.50f, 0.45f), false},
	{Body::Venus,    Body::Sun,      TEXT("Venus"),   FLinearColor(0.90f, 0.75f, 0.50f), false},
	{Body::EarthBary,Body::Sun,      TEXT("Earth"),   FLinearColor(0.25f, 0.45f, 0.90f), false},
	{Body::Moon,     Body::EarthBary,TEXT("Moon"),    FLinearColor(0.62f, 0.62f, 0.62f), true},
	{Body::Mars,     Body::Sun,      TEXT("Mars"),    FLinearColor(0.85f, 0.45f, 0.25f), false},
	{Body::Phobos,   Body::Mars,     TEXT("Phobos"),  FLinearColor(0.55f, 0.50f, 0.46f), true},
	{Body::Deimos,   Body::Mars,     TEXT("Deimos"),  FLinearColor(0.58f, 0.53f, 0.48f), true},
	{Body::Jupiter,  Body::Sun,      TEXT("Jupiter"), FLinearColor(0.80f, 0.65f, 0.50f), false},
	{Body::Io,       Body::Jupiter,  TEXT("Io"),      FLinearColor(0.92f, 0.84f, 0.45f), true},
	{Body::Europa,   Body::Jupiter,  TEXT("Europa"),  FLinearColor(0.85f, 0.80f, 0.72f), true},
	{Body::Ganymede, Body::Jupiter,  TEXT("Ganymede"),FLinearColor(0.68f, 0.63f, 0.58f), true},
	{Body::Callisto, Body::Jupiter,  TEXT("Callisto"),FLinearColor(0.48f, 0.44f, 0.40f), true},
	{Body::Saturn,   Body::Sun,      TEXT("Saturn"),  FLinearColor(0.85f, 0.75f, 0.55f), false},
	{Body::Mimas,    Body::Saturn,   TEXT("Mimas"),   FLinearColor(0.72f, 0.72f, 0.70f), true},
	{Body::Enceladus,Body::Saturn,   TEXT("Enceladus"),FLinearColor(0.95f,0.96f, 0.97f), true},
	{Body::Tethys,   Body::Saturn,   TEXT("Tethys"),  FLinearColor(0.88f, 0.88f, 0.86f), true},
	{Body::Dione,    Body::Saturn,   TEXT("Dione"),   FLinearColor(0.80f, 0.80f, 0.78f), true},
	{Body::Rhea,     Body::Saturn,   TEXT("Rhea"),    FLinearColor(0.78f, 0.78f, 0.76f), true},
	{Body::Titan,    Body::Saturn,   TEXT("Titan"),   FLinearColor(0.80f, 0.65f, 0.30f), true},
	{Body::Iapetus,  Body::Saturn,   TEXT("Iapetus"), FLinearColor(0.62f, 0.58f, 0.52f), true},
	{Body::Uranus,   Body::Sun,      TEXT("Uranus"),  FLinearColor(0.55f, 0.75f, 0.85f), false},
	{Body::Miranda,  Body::Uranus,   TEXT("Miranda"), FLinearColor(0.70f, 0.70f, 0.70f), true},
	{Body::Umbriel,  Body::Uranus,   TEXT("Umbriel"), FLinearColor(0.52f, 0.52f, 0.52f), true},
	{Body::Titania,  Body::Uranus,   TEXT("Titania"), FLinearColor(0.66f, 0.63f, 0.60f), true},
	{Body::Oberon,   Body::Uranus,   TEXT("Oberon"),  FLinearColor(0.60f, 0.56f, 0.53f), true},
	{Body::Neptune,  Body::Sun,      TEXT("Neptune"), FLinearColor(0.35f, 0.50f, 0.90f), false},
	{Body::Triton,   Body::Neptune,  TEXT("Triton"),  FLinearColor(0.82f, 0.80f, 0.76f), true},
	{Body::Pluto,    Body::Sun,      TEXT("Pluto"),   FLinearColor(0.65f, 0.60f, 0.55f), false},
	{Body::Charon,   Body::Pluto,    TEXT("Charon"),  FLinearColor(0.58f, 0.56f, 0.55f), true},
};
constexpr int32 NUM_BODIES = UE_ARRAY_COUNT(GBodies);

// ═══ ÉCHELLE RÉELLE : 1 u = 1 cm (unité native UE), AUCUNE compression. ═══
// Le rendu reste caméra-relatif (œil = origine, positions km rebasées en double),
// mais on convertit en cm RÉELS : un objet de 100 m fait 10 000 u (taille NORMALE)
// -> fini la micro-échelle qui cassait Nanite/culling/précision. Les corps
// lointains restent des marqueurs HUD (pas de conflit de z-buffer).
constexpr double KM_PER_M  = 1.0e-3;
constexpr double UU_PER_KM = 1.0e5;      // 1 km = 100 000 cm = 100 000 u
// ÉCHANTILLONNAGE DES ORBITES. 192 points suffisaient tant qu'une orbite se
// regardait de loin ; vue de près (les lunes d'une planète cadrée), le polygone
// se voyait — segments longs, cercles « pas ronds ». Le coût est payé au CACHE
// (reconstruit rarement) et à l'ÉMISSION (par segment) : 512 reste largement
// sous le budget d'un batch de lignes, et la courbe devient lisse au gros plan.
constexpr int32  ORBIT_SAMPLES = 512;
constexpr double ORBIT_REDRAW_DAYS = 2.0;      // dérive d'éphéméride sous le pixel
// ═══ TRAÎNÉE PLUTÔT QU'ANNEAU FERMÉ ═══ (2026-07-27, demande utilisateur)
// La trajectoire s'allume DERRIÈRE le corps et s'estompe vers la queue, façon
// Eyes on the Solar System. Fraction de période allumée : assez longue pour que
// la forme de l'orbite se lise, assez courte pour que la queue meure avant de
// rattraper la tête (une trace qui se rejoint redevient un anneau).
constexpr double ORBIT_TRAIL_FRAC = 0.72;
// Exposant du fondu (1 = linéaire). Au-dessus, la tête reste franche plus
// longtemps et la queue part vite — c'est le profil de la référence.
constexpr double ORBIT_TRAIL_GAMMA = 1.35;
// En deçà de cette période, la trace se REBOUCLE (fenêtre = une période pile).
// N'est vrai que d'un modèle exactement périodique : ce seuil ne s'applique qu'aux
// lunes de `Satellites.hpp` (cercles parfaits). Il vaut plusieurs fois la
// péremption du cache, pour que ces orbites très rapides (Phobos : 7,7 h) ne
// forcent pas un ré-échantillonnage à chaque poignée de minutes de jeu.
constexpr double ORBIT_CYCLE_MAX_S = 8.0 * 86400.0;
constexpr int32  FLEET_PER_CAT = 6;
constexpr int32  FLEET_GEO0 = 0, FLEET_MARS0 = 6, FLEET_PROBE0 = 12, FLEET_GEOFLIGHT = 18;
constexpr int32  FLEET_TOTAL = 19;
// Taille ANGULAIRE des marqueurs de scène (fraction de la distance à l'œil) :
// ils gardent une taille écran constante quel que soit le zoom.
constexpr double MARKER_ANG = 0.0022;
// Un corps n'est rendu en GÉOMÉTRIE que si sa distance à l'œil < rayon × ce
// facteur (sinon MARQUEUR HUD). Garde la géométrie dans le domaine où le float
// est précis (~sub-km sur le corps) : au-delà, à l'échelle réelle, un corps à
// ~1e14 u faceterait. ~ correspond à un rayon apparent de quelques pixels.
constexpr double BODY_GEOM_FACTOR = 400.0;
// PLAFOND DE PRÉCISION GPU. UE convertit la matrice monde de CHAQUE primitive en
// format GPU (tuile+offset float) et exige que l'origine reste sous
// UE_DF_FLOAT_MAX_VALUE = (1<<23)/4 - 1 ≈ 2,10e6 u (Core/DoubleFloat.cpp:19).
// À l'échelle réelle (1 u = 1 cm), un composant rebasé sur un corps focalisé
// LOINTAIN (lumière solaire, marqueurs station/flotte à des dizaines d'UA de
// l'œil) dépasse ce plafond de 8 ordres de grandeur -> ensure « precision loss
// while converting matrix to GPU format » + transform corrompu. On BORNE donc
// la position de rendu des COMPOSANTS (pas les lignes d'orbite) à ce rayon, sous
// le plafond avec marge. L'œil étant à l'origine, borner radialement préserve
// EXACTEMENT la position écran (projection invariante par échelle radiale) ;
// seule la profondeur du marqueur change — invisible pour un point/marqueur.
constexpr double RENDER_MAX_UU = 1.0e6;
// PLANCHER DE RENDU : la SURFACE du corps le plus proche ne doit jamais rendre en
// deçà. 1e4 u = 100 m — très au-delà du plan de clipping proche (10 u) et de
// l'enveloppe de Novellus (55 m), donc la Terre vue de la cupola est devant
// l'œil et derrière le hublot, pas noyée dans la cabine. Voir l'homothétie.
constexpr double RENDER_MIN_UU = 1.0e4;
// Genou de la COURBURE des lointains : en deçà, la mise à l'échelle est purement
// linéaire (le premier plan est rendu à son facteur exact) ; au-delà, les
// distances sont repliées dans (Genou, RENDER_MAX_UU] par une hyperbole. Le
// repli est STRICTEMENT CROISSANT : deux corps à des distances différentes ne
// peuvent pas atterrir sur la même sphère — c'était le défaut de la borne
// individuelle (Lune et Terre qui s'interpénétraient), et il ne revient pas.
// La taille angulaire reste EXACTE : le rayon subit le même facteur que la
// position, et une mise à l'échelle radiale de centre l'œil ne change pas la
// projection.
constexpr double RENDER_KNEE_UU = 0.5e6;

// Distance de rendu d'un corps, à partir de sa distance vraie mise à l'échelle.
// Identité sous le genou ; hyperbole croissante et bornée au-dessus.
double RemapDist(double DistUU)
{
	if (DistUU <= RENDER_KNEE_UU) return DistUU;
	return RENDER_KNEE_UU +
	       (RENDER_MAX_UU - RENDER_KNEE_UU) * (1.0 - RENDER_KNEE_UU / DistUU);
}

// ═══ REBASÉ (km) -> RENDU (cm réels) ═══
// Ancienne « compression scaled space » SUPPRIMÉE (décision 2026-07-25 : rendu à
// échelle réelle). On applique juste le facteur d'échelle constant km->cm. La
// profondeur des corps lointains est gérée par le LOD (marqueurs HUD), plus par
// une homothétie. Nom conservé pour limiter la diffusion du changement.
FVector CompressKm(const FVector& RelKm, double* OutFactor = nullptr)
{
	// Échelle réelle CONSTANTE (plus de compression de profondeur) : rebasé (km,
	// relatif à l'œil) -> cm réels. Le facteur est le même pour la position ET le
	// rayon (OutFactor), donc la taille angulaire reste exacte.
	if (OutFactor) *OutFactor = UU_PER_KM;
	return RelKm * UU_PER_KM;
}

// ═══ L'ASPECT DU VIEWPORT, LU LÀ OÙ IL EST VRAI ═══ (2026-07-27)
// `GEngine->GameViewport` est le viewport GLOBAL : en PIE il peut ne pas être
// celui qui rend ce monde. On demande d'abord au joueur local de CE monde, qui
// est la source dont se sert le moteur lui-même pour bâtir sa matrice de
// projection ; le viewport global n'est qu'un repli, et le 16:9 un dernier
// recours SIGNALÉ (un repli silencieux est exactement ce qui a fait décaler les
// marqueurs pendant des jours).
double ViewportAspect(const UWorld* W)
{
	if (W)
		if (const APlayerController* PC = W->GetFirstPlayerController())
			if (const ULocalPlayer* LP = PC->GetLocalPlayer())
				if (LP->ViewportClient && LP->ViewportClient->Viewport)
				{
					const FIntPoint S = LP->ViewportClient->Viewport->GetSizeXY();
					const double Wpx = S.X * LP->Size.X, Hpx = S.Y * LP->Size.Y;
					if (Wpx > 1.0 && Hpx > 1.0) return Wpx / Hpx;
				}
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D VP;
		GEngine->GameViewport->GetViewportSize(VP);
		if (VP.X > 1.0 && VP.Y > 1.0) return VP.X / VP.Y;
	}
	static bool bDit = false;
	if (!bDit)
	{
		bDit = true;
		UE_LOG(LogTemp, Warning,
		       TEXT("[SPSolarSystem] aucun viewport interrogeable : aspect suppose 16:9. "
		            "Marqueurs et tracés divergeront si la fenêtre n'est pas en 16:9."));
	}
	return 16.0 / 9.0;
}

// L'éphéméride Standish est sans état : une instance partagée suffit.
const fen::ephem::StandishEphemeris GEph;

// écliptique (droitier, m) -> UE (gaucher, km) : miroir en y. DÉCLARÉ.
FVector EclToUeKm(const fen::Vec3& Meters)
{
	return FVector(Meters.x * KM_PER_M, -Meters.y * KM_PER_M, Meters.z * KM_PER_M);
}
FVector EclToUeKmd(double Xm, double Ym, double Zm)
{
	return FVector(Xm * KM_PER_M, -Ym * KM_PER_M, Zm * KM_PER_M);
}

const FBodyDef* FindDef(Body B)
{
	for (const FBodyDef& D : GBodies) if (D.B == B) return &D;
	return nullptr;
}

// Rayon VRAI du corps, en km (aucune exagération).
double BodyRadiusKm(Body B)
{
	return fen::ephem::body_radius(B) * KM_PER_M;
}

// Position MONDE d'un corps (km, axes UE, héliocentrique). Les lunes sont à leur
// distance RÉELLE de leur planète : à l'échelle vraie, rien n'a besoin d'être
// écarté artificiellement.
FVector BodyWorldKm(const FBodyDef& Def, double EpochTdb)
{
	if (Def.B == Body::Sun) return FVector::ZeroVector;
	const fen::Epoch E{EpochTdb};
	if (Def.bMoon)
	{
		const FVector ParentPos = (Def.Parent == Body::Sun)
			? FVector::ZeroVector
			: EclToUeKm(GEph.state(Def.Parent, Body::Sun, E).r);
		return ParentPos + EclToUeKm(GEph.state(Def.B, Def.Parent, E).r);
	}
	return EclToUeKm(GEph.state(Def.B, Body::Sun, E).r);
}

// Période de la TRACE (s) : la durée au bout de laquelle le corps revient là où
// il était, telle que le MODÈLE qui le déplace la définit.
//
// PIÈGE MESURÉ À L'ORACLE (2026-07-27, `diag_orbites`). Elle était tirée du seul
// vis-viva, avec le µ du parent SEUL — alors que `Satellites.hpp` fait tourner
// ses lunes sur µ(parent) + µ(lune). L'écart est invisible partout... sauf sur
// PLUTON-CHARON, quasi-binaire : la période sortait 29 % trop longue et la trace
// de Charon se rebouclait 104° trop loin (écart de fermeture mesuré : 1,57 fois
// le rayon de l'orbite). Pour un corps dont le modèle EST périodique, la période
// se demande donc au modèle lui-même — le vis-viva ne sert que de repli, pour les
// corps que Standish propage par éléments osculateurs.
double TracePeriodS(const FBodyDef& Def, double EpochTdb)
{
	if (Def.B == Body::Sun) return 0.0;
	if (const fen::ephem::SatelliteDef* S = fen::ephem::satellite_def(Def.B))
		return fen::ephem::satellite_period_days(*S) * 86400.0;
	const Body Centre = Def.bMoon ? Def.Parent : Body::Sun;
	const fen::ephem::PosVel PV = GEph.state(Def.B, Centre, fen::Epoch{EpochTdb});
	const double Mu = fen::ephem::body_mu(Centre);
	const double R = FMath::Sqrt(PV.r.x * PV.r.x + PV.r.y * PV.r.y + PV.r.z * PV.r.z);
	const double V2 = PV.v.x * PV.v.x + PV.v.y * PV.v.y + PV.v.z * PV.v.z;
	if (R <= 0.0 || Mu <= 0.0) return 0.0;
	const double Denom = 2.0 / R - V2 / Mu;         // 1/a (vis-viva)
	if (Denom <= 0.0) return 0.0;                    // trajectoire non fermée
	const double A = 1.0 / Denom;
	// UE_DOUBLE_TWO_PI et pas fen::cst::TWO_PI : UnrealMathUtility définit TWO_PI
	// en MACRO — toute constante fen homonyme est inutilisable après les includes UE.
	return UE_DOUBLE_TWO_PI * FMath::Sqrt(A * A * A / Mu);
}

// écliptique (droitier, m) -> UE (gaucher) : le MÊME miroir en y que les positions,
// pour une DIRECTION (pas d'unité à convertir).
FVector DirEclToUe(const fen::Vec3& V) { return FVector(V.x, -V.y, V.z); }

// ═══ DU REPÈRE DU CORPS À LA ROTATION DU MESH ═══ (2026-07-27)
//
// Le repère lié au corps est calculé par le CŒUR (`ephem::body_frame_ecliptic`
// pour les corps à éléments IAU, `ephem::satellite_frame_ecliptic` pour les lunes
// verrouillées) : le rendu ne recalcule aucune orientation, il change de repère.
// Deux changements de convention se composent ici, et il faut les DEUX.
//
// 1. ÉCLIPTIQUE -> UE : le miroir en y renverse la chiralité. Le triplet du corps
//    (droitier par construction) se lit gaucher une fois ses composantes en UE.
// 2. LE MESH N'EST PAS ORIENTÉ COMME LA CARTE. `SM_SP_Body` sort de
//    `FSphereGenerator` (via `append_sphere_lat_long`, aucune transformation
//    ajoutée) : sommet = (cos θ sin φ, sin θ sin φ, cos φ), U = 1 − θ/2π et
//    V = φ/π. Avec une équirectangulaire centrée sur la longitude 0 et le nord en
//    haut (U = 0,5 + λ/360), il vient θ = π − λ, donc :
//        longitude 0  -> local −X        longitude +90° est -> local +Y
//        pôle nord    -> local +Z
//    LE MÉRIDIEN ORIGINE EST SUR −X, pas sur +X. C'est une propriété de l'asset,
//    lue dans le générateur du moteur, pas un réglage à tâtonner.
//
// Les deux renversements se composent en une rotation PROPRE (det = +1, vérifié :
// (−1)·(−1)) — le quaternion se bâtit donc directement, sans miroir résiduel.
// Les lignes d'une FMatrix UE sont les IMAGES des axes locaux (convention
// vecteur-ligne : v_monde = v_local × M).
FQuat MeshRotationFrom(const fen::ephem::BodyFrame& F)
{
	const FVector X = DirEclToUe(F.x), Y = DirEclToUe(F.y), Z = DirEclToUe(F.z);
	return FMatrix(-X, Y, Z, FVector::ZeroVector).ToQuat().GetNormalized();
}

// ═══ L'ATTITUDE DE NOVELLUS : LA CUPOLA REGARDE LA TERRE ═══ (2026-07-27)
//
// LE MODÈLE EXTÉRIEUR ÉTAIT POSÉ SANS AUCUNE ORIENTATION (position + échelle, pas
// de rotation) : la station gardait donc un cap fixe dans le référentiel inertiel,
// donc sa cupola balayait le vide, l'espace, la Terre, le vide, au fil de l'orbite.
// L'ISS ne vole pas comme ça — elle tient une attitude LVLH (« XVV », torque
// equilibrium) : axe de vol dans le vecteur vitesse, et le NADIR vers la Terre,
// où sont la cupola et les fenêtres d'observation. Elle fait un tour complet par
// orbite dans le repère inertiel, ce qui est exactement ce qu'on doit voir.
//
// LE CALCUL A ÉMIGRÉ EN C++ PUR (`app/novellus_orbite.hpp`), et ce n'est pas un
// rangement : TROIS consommateurs doivent voir la MÊME attitude, à la frame près —
// ce modèle extérieur, la géométrie INTÉRIEURE (SPStation) et la pose de caméra du
// handoff (`Session::pose_bord`, qui vit en C++ pur). Deux d'entre eux se relaient
// à la traversée de la coque : la moindre divergence ferait SAUTER l'orientation
// de la station à cet instant précis. Le pont porte donc les trois vecteurs, déjà
// dans le repère de rendu, et le rendu ne dérive rien [doctrine du pont]. Le repère
// du modèle (+X avant, +Y tribord, +Z zénith) y est documenté et MESURÉ par
// `Tools/diag_iss_repere.py`.
//
// Ici il ne reste que le changement de représentation : les lignes d'une FMatrix UE
// sont les IMAGES des axes locaux, et ce sont exactement les trois vecteurs publiés.
FQuat StationAttitude(const fen::app::RenderBridge::StationWorld& St)
{
	const FVector Fwd(St.att_avant[0], St.att_avant[1], St.att_avant[2]);
	const FVector Stbd(St.att_tribord[0], St.att_tribord[1], St.att_tribord[2]);
	const FVector Up(St.att_zenith[0], St.att_zenith[1], St.att_zenith[2]);
	return FMatrix(Fwd, Stbd, Up, FVector::ZeroVector).ToQuat().GetNormalized();
}

// Orientation propre à l'époque : fonction PURE de l'époque, déterministe et
// rejouable. Deux régimes, tous deux SANS phase à deviner :
//   . lune de `Satellites.hpp` -> verrou synchrone (même face à son primaire) ;
//   . corps à éléments IAU     -> pôle + méridien origine W(t) [WGCCRE].
// Il n'y a pas de troisième cas : la table `GBodies` est exactement l'union des
// deux (30 = 12 IAU + 19 lunes − Titan compté deux fois). `BuildScene` le VÉRIFIE
// au démarrage plutôt que de faire confiance à ce commentaire.
FQuat OrientationAt(const FBodyDef& Def, double EpochTdb)
{
	using namespace fen::ephem;
	const fen::Epoch E{EpochTdb};
	if (const SatelliteDef* S = satellite_def(Def.B))
		return MeshRotationFrom(satellite_frame_ecliptic(*S, E));
	if (has_orientation(Def.B))
		return MeshRotationFrom(body_frame_ecliptic(Def.B, E));
	return FQuat::Identity;      // signalé par BuildScene, jamais atteint en l'état
}

UStaticMesh* FindImportedMesh(const TCHAR* AssetName)
{
	// 1) chemin conventionnel du script d'import (Tools/import_glb_planets.py)
	const FString Direct = FString::Printf(TEXT("/Game/SolarSystem/%s.%s"), AssetName, AssetName);
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Direct)) return M;
	// 2) l'import Interchange nomme le mesh d'après le nœud GLB. Un GLB peut
	// contenir PLUSIEURS meshes (Saturne : "Sphere" = corps + "Circle" =
	// anneaux) : préférer le mesh au nom du corps, sinon "Sphere*" (le corps),
	// sinon le premier StaticMesh trouvé.
	const FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(
		FName(*FString::Printf(TEXT("/Game/SolarSystem/%s"), AssetName)), Assets, true);
	const FAssetData* First = nullptr;
	const FAssetData* Named = nullptr;
	const FAssetData* Sphere = nullptr;
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName()) continue;
		if (!First) First = &A;
		const FString Name = A.AssetName.ToString();
		if (!Named && Name.Equals(AssetName, ESearchCase::IgnoreCase)) Named = &A;
		if (!Sphere && Name.StartsWith(TEXT("Sphere"), ESearchCase::IgnoreCase)) Sphere = &A;
	}
	const FAssetData* Pick = Named ? Named : (Sphere ? Sphere : First);
	return Pick ? Cast<UStaticMesh>(Pick->GetAsset()) : nullptr;
}

// Mesh annexe d'un corps (les anneaux de Saturne : nœud GLB "Circle").
UStaticMesh* FindImportedMeshNamed(const TCHAR* AssetName, const TCHAR* NodeName)
{
	const FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(
		FName(*FString::Printf(TEXT("/Game/SolarSystem/%s"), AssetName)), Assets, true);
	for (const FAssetData& A : Assets)
		if (A.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName() &&
		    A.AssetName.ToString().StartsWith(NodeName, ESearchCase::IgnoreCase))
			return Cast<UStaticMesh>(A.GetAsset());
	return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
ASPSolarSystemMapActor::ASPSolarSystemMapActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

// ---------------------------------------------------------------------------
bool USPSolarSystemSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) return false;
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

void USPSolarSystemSubsystem::Deinitialize()
{
	fen::app::g_render_bridge.carte3d_active = false;
	Super::Deinitialize();
}

TStatId USPSolarSystemSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USPSolarSystemSubsystem, STATGROUP_Tickables);
}

void USPSolarSystemSubsystem::BuildScene()
{
	UWorld* W = GetWorld();
	if (!W) return;

	MapActor = W->SpawnActor<ASPSolarSystemMapActor>();
	// LA sphère des corps, générée par `Tools/make_body_sphere.py`. Une seule pour
	// tous : ce qui distingue deux corps est leur matériau et leur rayon, pas leur
	// maillage — une planète est une sphère.
	UStaticMesh* BodySphere =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Game/SP/SM_SP_Body.SM_SP_Body"));
	if (!BodySphere)
		UE_LOG(LogTemp, Warning,
		       TEXT("[SPSolarSystem] /Game/SP/SM_SP_Body absent : lancer Tools/make_body_sphere.py"));
	UStaticMesh* Sphere =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	// Soleil et marqueurs : émissifs (param vectoriel "Color" vérifié dans l'uasset).
	UMaterialInterface* EmissiveMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));

	// ═══ AUCUN CORPS SANS ORIENTATION ═══ (2026-07-27)
	// `OrientationAt` n'a plus de repli : un corps qui n'est ni une lune verrouillée
	// ni un corps à éléments IAU rendrait FIGÉ, sans que rien ne le dise. On le
	// vérifie une fois au démarrage plutôt que de le supposer dans un commentaire —
	// c'est ce genre de silence qui a laissé les dix-huit lunes tourner faux.
	for (const FBodyDef& Def : GBodies)
		if (!fen::ephem::satellite_def(Def.B) && !fen::ephem::has_orientation(Def.B))
			UE_LOG(LogTemp, Warning,
			       TEXT("[SPSolarSystem] %s : ni lune verrouillee ni elements IAU -> rendu SANS rotation"),
			       Def.Asset);

	for (const FBodyDef& Def : GBodies)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(MapActor);
		C->SetupAttachment(MapActor->GetRootComponent());
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		// ═══ PAS DE NANITE SUR LES CORPS ═══
		// Les GLB s'importent en Nanite par défaut. Un corps est une sphère de
		// ~15 000 triangles : Nanite ne lui apporte RIEN (il est fait pour la
		// géométrie dense), et à l'échelle réelle il lui coûte sa correction — le
		// composant est posé à ~1e9 u de l'origine avec un facteur d'échelle de
		// ~1e8, et le culling par clusters de Nanite rend alors une géométrie
		// DÉCHIRÉE (fragments détachés, pans manquants, « croissant sombre »).
		// L'ISS, elle, garde Nanite : 669 meshes réellement denses, et posée près
		// de l'œil. DIAGNOSTIC : constaté en capture sur la Lune et Io, matériaux
		// et normales innocentés d'abord (les corriger n'a rien changé).
		C->bDisallowNanite = true;

		const double RadiusUU = BodyRadiusKm(Def.B);   // ÉCHELLE VRAIE
		double MeshRadius = 50.0;               // sphère moteur : 50 u de rayon
		const bool bStar = (Def.B == Body::Sun);
		// ═══ LES CORPS SONT FAITS PAR NOUS, PLUS IMPORTÉS ═══ (décision utilisateur,
		// 2026-07-27). Une seule sphère générée par UE (`Tools/make_body_sphere.py` :
		// lat-long 128 x 256 = 64 512 tris, UV équirectangulaires, faces sorties par
		// CONSTRUCTION) et un matériau par corps bâti sur les textures du projet
		// (`Tools/make_body_materials.py`). Ce qui disparaît avec les GLB :
		//   . les faces retournées (on voyait l'intérieur de l'hémisphère arrière) ;
		//   . le matériau d'import générique, translucide et deux faces ;
		//   . la COQUILLE DE NUAGES low-poly de la Terre — les nuages sont désormais
		//     une CARTE dans le matériau, il n'y a plus de seconde géométrie ;
		//   . la silhouette polygonale (4x plus de triangles qu'avant tessellation).
		// Le rayon du mesh vaut 100 par construction : le calcul d'échelle du rendu
		// (rayon réel / rayon du mesh) est inchangé.
		if (BodySphere)
		{
			C->SetStaticMesh(BodySphere);
			MeshRadius = FMath::Max(1.0f, BodySphere->GetBounds().SphereRadius);
		}
		else if (Sphere)   // repli : la sphère du moteur, si l'outil n'a pas tourné
		{
			C->SetStaticMesh(Sphere);
			MeshRadius = FMath::Max(1.0f, Sphere->GetBounds().SphereRadius);
		}
		// Le matériau du corps : instance dédiée (MI_SP_<Corps>), dérivée en MID pour
		// que le rendu puisse lui passer la DIRECTION DU SOLEIL à chaque frame — c'est
		// elle qui décide où est la nuit. Le Soleil, lui, hérite d'un matériau NON
		// ÉCLAIRÉ : une étoile ne dépend d'aucune lumière.
		{
			const FString Chemin =
				FString::Printf(TEXT("/Game/SP/Bodies/MI_SP_%s.MI_SP_%s"), Def.Asset, Def.Asset);
			UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, *Chemin);
			if (Mi)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Mi, C);
				C->SetMaterial(0, M);
				BodyMids.Add(M);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[SPSolarSystem] materiau absent : %s"), *Chemin);
				BodyMids.Add(nullptr);
				// Repli lisible plutôt qu'un corps blanc : la couleur de la table.
				if (!bStar && BaseMat)
				{
					UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
					M->SetVectorParameterValue(TEXT("Color"), Def.Color);
					C->SetMaterial(0, M);
				}
				else if (bStar && EmissiveMat)
				{
					UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(EmissiveMat, C);
					M->SetVectorParameterValue(TEXT("Color"), Def.Color * 60.0f);
					C->SetMaterial(0, M);
				}
			}
		}
		C->SetWorldScale3D(FVector(RadiusUU / MeshRadius));
		C->RegisterComponent();
		MapActor->BodyMeshes.Add(C);
		BodyMeshRadius.Add(MeshRadius);   // l'échelle est refaite à chaque frame

		// ═══ LES ANNEAUX DE SATURNE, FAITS PAR NOUS AUSSI ═══ (2026-07-27)
		// Ils venaient du nœud « Circle » du GLB et NE RENDAIENT PLUS (défaut
		// antérieur au chantier des lunes, constaté en capture). Même remède que
		// pour les corps : `Tools/make_ring_mesh.py` génère une couronne aux rayons
		// RÉELS (bord interne C 74 500 km, bord externe A 136 780 km, rapportés au
		// rayon équatorial de Saturne = 100 dans le repère du mesh parent) avec des
		// UV RADIALES — U = rayon normalisé, ce qu'attend le ruban
		// `8k_saturn_ring_alpha.png`, dont l'alpha porte les divisions.
		// Enfant du corps : il hérite position, échelle et ORIENTATION, donc les
		// anneaux restent dans le plan équatorial incliné à 26,7° [IAU].
		if (Def.B == Body::Saturn)
		{
			UStaticMesh* Rings =
				LoadObject<UStaticMesh>(nullptr, TEXT("/Game/SP/SM_SP_Ring.SM_SP_Ring"));
			if (!Rings)
				UE_LOG(LogTemp, Warning,
				       TEXT("[SPSolarSystem] /Game/SP/SM_SP_Ring absent : lancer Tools/make_ring_mesh.py"));
			if (Rings)
			{
				UStaticMeshComponent* R = NewObject<UStaticMeshComponent>(MapActor);
				R->SetupAttachment(C);
				R->SetMobility(EComponentMobility::Movable);
				R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				R->SetCastShadow(false);
				R->bDisallowNanite = true;
				R->SetStaticMesh(Rings);
				if (UMaterialInterface* RM = LoadObject<UMaterialInterface>(
						nullptr, TEXT("/Game/SP/Bodies/M_SP_Ring.M_SP_Ring")))
					R->SetMaterial(0, RM);
				R->RegisterComponent();
			}
		}
	}

	// ═══ LE SOLEIL EST UNE LUMIÈRE DIRECTIONNELLE ═══ (2026-07-27)
	// C'était une PointLight qu'on plaçait à 1e12 u pour obtenir des rayons quasi
	// parallèles. Mais 1e12 u, c'est SIX ordres de grandeur au-dessus du plafond de
	// précision GPU (RENDER_MAX_UU) : la transformation de la lumière sortait
	// corrompue et l'éclairage se cassait en BANDES sur l'hémisphère éclairé —
	// pans noirs et morceaux détachés, qu'on prenait pour de la géométrie déchirée
	// (constaté sur la Terre, la Lune et Io).
	// Une lumière DIRECTIONNELLE n'a pas de position, seulement une direction :
	// elle est insensible par construction à ce plafond. Et c'est le modèle JUSTE —
	// à ces distances le Soleil est une source à l'infini (0,53° vus de la Terre),
	// ce que le document annonçait déjà comme « plus juste » sans l'avoir fait.
	// Disparaissent avec elle : le rayon d'atténuation, l'exposant de décroissance
	// et l'erreur d'angle déclarée du placement à 1e12 u.
	MapActor->SunLight = NewObject<UDirectionalLightComponent>(MapActor);
	MapActor->SunLight->SetupAttachment(MapActor->GetRootComponent());
	MapActor->SunLight->SetMobility(EComponentMobility::Movable);
	MapActor->SunLight->SetIntensity(3.2f);            // lux ; calé par capture
	MapActor->SunLight->SetCastShadows(false);         // coût ; occultation = canaux (piège n°35)
	MapActor->SunLight->RegisterComponent();

	// Marqueur du vaisseau en vol (position ESTIMÉE [GDD 7.5]).
	MapActor->VehicleMarker = NewObject<UStaticMeshComponent>(MapActor);
	MapActor->VehicleMarker->SetupAttachment(MapActor->GetRootComponent());
	MapActor->VehicleMarker->SetMobility(EComponentMobility::Movable);
	MapActor->VehicleMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MapActor->VehicleMarker->SetCastShadow(false);
	if (Sphere) MapActor->VehicleMarker->SetStaticMesh(Sphere);
	if (EmissiveMat)
	{
		UMaterialInstanceDynamic* M =
			UMaterialInstanceDynamic::Create(EmissiveMat, MapActor->VehicleMarker);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 1.0f, 0.9f) * 5.0f);
		MapActor->VehicleMarker->SetMaterial(0, M);
	}
	MapActor->VehicleMarker->SetVisibility(false);
	MapActor->VehicleMarker->RegisterComponent();

	// Marqueur de NOVELLUS [GDD v1.2 11.1, 17.3] : la station EST dans le monde
	// (orbite LEO). Bleu, façon losange de la référence (ref_issfocus.png).
	MapActor->StationMarker = NewObject<UStaticMeshComponent>(MapActor);
	MapActor->StationMarker->SetupAttachment(MapActor->GetRootComponent());
	MapActor->StationMarker->SetMobility(EComponentMobility::Movable);
	MapActor->StationMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MapActor->StationMarker->SetCastShadow(false);
	if (Sphere) MapActor->StationMarker->SetStaticMesh(Sphere);
	if (EmissiveMat)
	{
		UMaterialInstanceDynamic* M =
			UMaterialInstanceDynamic::Create(EmissiveMat, MapActor->StationMarker);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.3f, 0.7f, 1.0f) * 4.0f);
		MapActor->StationMarker->SetMaterial(0, M);
	}
	MapActor->StationMarker->SetVisibility(false);
	MapActor->StationMarker->RegisterComponent();

	// Marqueurs de flotte [GDD 8.3] : petits points émissifs à leur position VRAIE.
	const FLinearColor FleetCols[4] = {
		FLinearColor(0.45f, 0.75f, 1.0f) * 3.0f,   // relais GEO : bleu clair
		FLinearColor(1.0f, 0.55f, 0.25f) * 3.0f,   // orbiteurs Mars : orange
		FLinearColor(0.9f, 0.9f, 0.9f) * 3.0f,     // sondes lointaines : blanc
		FLinearColor(0.2f, 1.0f, 0.9f) * 5.0f,     // vol GEO en cours : cyan vif
	};
	for (int32 i = 0; i < FLEET_TOTAL; ++i)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(MapActor);
		M->SetupAttachment(MapActor->GetRootComponent());
		M->SetMobility(EComponentMobility::Movable);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		if (Sphere) M->SetStaticMesh(Sphere);
		const int32 Cat = (i == FLEET_GEOFLIGHT) ? 3 : i / FLEET_PER_CAT;
		if (EmissiveMat)
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(EmissiveMat, M);
			Mat->SetVectorParameterValue(TEXT("Color"), FleetCols[Cat]);
			M->SetMaterial(0, Mat);
		}
		M->SetVisibility(false);
		M->RegisterComponent();
		MapActor->FleetMarkers.Add(M);
	}

	// Caméra carte : posée à l'origine (le monde est rebasé sur l'œil à chaque
	// frame) ; seule sa ROTATION change.
	MapCamera = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (MapCamera && MapCamera->GetCameraComponent())
	{
		UCameraComponent* Cam = MapCamera->GetCameraComponent();
		Cam->SetConstraintAspectRatio(false);
		Cam->SetFieldOfView(45.0f);
		// L'image du monde unique — le MÊME réglage que la caméra de bord, sans
		// quoi la reprise du vol [M] sauterait en exposition (cf. SPCameraPost.h).
		SPCameraPost::Appliquer(Cam->PostProcessSettings);
	}

	bBuilt = true;
}

// LA GÉOMÉTRIE DU MONDE REND-ELLE ? Vrai au plan système, au décor du menu, et
// désormais AUSSI à bord — le monde est unique, et depuis la cupola on doit voir
// la Terre. Ce n'est plus la même question que « la carte a-t-elle l'œil ».
void USPSolarSystemSubsystem::SetMapActive(bool bActive)
{
	if (MapActor) MapActor->SetActorHiddenInGame(!bActive);
}

// LA CARTE A-T-ELLE L'ŒIL ? C'est ce qui commande la CAMÉRA et les remises à zéro
// du cadrage. À bord, la réponse est non : le pawn tient la caméra (SPStation), le
// plan système n'est qu'un décor autour de lui.
void USPSolarSystemSubsystem::SetMapHasEye(bool bHasEye)
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (bHasEye)
	{
		LastOrbitEpoch = -1.0e300;         // force le retracé des orbites
		bFocusPrimed = false;              // recadre sans animation parasite
		SmoothDistKm = -1.0;               // ... et sans vol parasite à l'entrée
		if (!PC) return;
		PreviousViewTarget = PC->GetViewTarget();
		if (MapCamera) PC->SetViewTargetWithBlend(MapCamera, 0.0f);
	}
	else
	{
		// Les traits d'orbite ne sont émis que quand la carte a l'œil : on les
		// purge en la lui retirant, sinon ils resteraient tendus en travers de
		// l'intérieur de la station.
		if (ULineBatchComponent* LB =
		        W ? W->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr)
			LB->Flush();
		if (!PC) return;
		// Ne rendre la caméra QUE si personne ne l'a déjà reprise : la station
		// s'active dans la même frame que la désactivation du décor du menu, et
		// l'ordre des Tick entre subsystems n'est pas garanti.
		if (PreviousViewTarget && PC->GetViewTarget() == MapCamera)
			PC->SetViewTargetWithBlend(PreviousViewTarget, 0.0f);
	}
}

// Le point VISÉ : le corps focalisé, ou le Soleil (vue système).
FVector USPSolarSystemSubsystem::FocusWorldKm(double EpochTdb) const
{
	const int Focus = fen::app::g_render_bridge.focus_body.load();
	// NOVELLUS : pas un corps du catalogue, mais focalisable. Sa position monde =
	// Terre + offset LEO publié par le jeu [GDD v1.2 11.1, 17.3].
	if (Focus == fen::app::FOCUS_STATION)
	{
		const auto& St = fen::app::g_render_bridge.station;
		if (St.valid.load())
		{
			const FBodyDef* Terre = FindDef(Body::EarthBary);
			const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
			return TerrePos + EclToUeKmd(St.rel_m[0], St.rel_m[1], St.rel_m[2]);
		}
		return FVector::ZeroVector;
	}
	if (Focus < 0 || Focus >= static_cast<int>(Body::COUNT)) return FVector::ZeroVector;
	const FBodyDef* Def = FindDef(static_cast<Body>(Focus));
	return Def ? BodyWorldKm(*Def, EpochTdb) : FVector::ZeroVector;
}

// Charge le modèle ISS EXTÉRIEUR (multi-mesh NANITE) à l'échelle RÉELLE : le mesh
// est déjà en cm réels (~109 m = 10 900 u), donc échelle ~1.0 et Nanite fonctionne
// normalement. Rattaché au MapActor sous ExtRoot ; UpdateScene le positionne
// caméra-relatif, visible de près (LOD).
void USPSolarSystemSubsystem::BuildExteriorStation()
{
	bExtBuilt = true;                     // une seule tentative (succès ou non)
	UWorld* W = GetWorld();
	if (!W || !MapActor) return;

	const FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(
		FName(TEXT("/Game/ISS/Exterior/ISS_stationary/StaticMeshes")), Assets, true);

	TArray<UStaticMesh*> Meshes;
	FBox Bounds(ForceInit);
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName()) continue;
		UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset());
		if (!M) continue;
		Meshes.Add(M);
		const FBoxSphereBounds B = M->GetBounds();
		Bounds += FBox(B.Origin - B.BoxExtent, B.Origin + B.BoxExtent);
	}
	if (Meshes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
		       TEXT("[SPSolarSystem] aucun mesh ISS exterieur sous /Game/ISS/Exterior/ISS_stationary/StaticMeshes"));
		return;
	}

	const FVector Size = Bounds.GetSize();
	const double SpanUU = FMath::Max3(Size.X, Size.Y, Size.Z);
	const double EnvM = 109.0;             // envergure RÉELLE de l'ISS (~109 m)
	// envergure réelle -> u (1 u = 1 cm) : 109 m = 10 900 u. Le mesh est déjà en
	// cm réels (span ~10 829 u = 108 m), donc l'échelle vaut ~1.0 (normalisée ici).
	ExtScaleKm = (SpanUU > 1.0) ? ((EnvM * 100.0) / SpanUU) : 1.0;
	ExtCentreUU = Bounds.GetCenter();

	// ExtRoot : composant enfant du MapActor (dont le rendu est déjà PROUVÉ), pas
	// un acteur séparé.
	ExtRoot = NewObject<USceneComponent>(MapActor, TEXT("ExtStationRoot"));
	ExtRoot->SetupAttachment(MapActor->GetRootComponent());
	ExtRoot->SetMobility(EComponentMobility::Movable);
	ExtRoot->RegisterComponent();

	for (UStaticMesh* M : Meshes)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(MapActor);
		C->SetupAttachment(ExtRoot);       // AVANT RegisterComponent (piège #4)
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		// NANITE RÉACTIVÉ : à l'échelle réelle (ISS ~10 900 u) Nanite fonctionne
		// normalement — plus besoin du repli forcé. C'était TOUTE la difficulté du
		// micro-échelle, supprimée à la racine par le rendu à échelle réelle.
		// Pas de contribution Lumen : évite la préparation des champs de distance
		// de centaines de pièces (qui bloque le rendu, très visible en headless).
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		// Matériaux RÉELS conservés (ISS texturée) : éclairée par le Soleil de la
		// carte + une lumière d'appoint locale (ExtLight), comme ref_issfocus.png.
		C->RegisterComponent();
		ExtParts.Add(C);
	}
	ExtRoot->SetVisibility(false, true);   // caché jusqu'à l'approche (propage aux enfants)

	// Lumière d'appoint (pour un futur rendu en matériaux réels), sur le MapActor
	// (repère NON scalé) : UpdateScene la place sur Novellus, allumée avec le modèle.
	if (MapActor)
	{
		ExtLight = NewObject<UPointLightComponent>(MapActor);
		ExtLight->SetupAttachment(MapActor->GetRootComponent());
		ExtLight->SetMobility(EComponentMobility::Movable);
		ExtLight->bUseInverseSquaredFalloff = false;
		ExtLight->SetAttenuationRadius(2.0e5f);   // 2 km en u : couvre les ~109 m avec marge
		ExtLight->SetLightFalloffExponent(0.5f);
		ExtLight->SetIntensity(60.0f);
		ExtLight->SetLightColor(FLinearColor(1.0f, 0.98f, 0.95f));
		ExtLight->SetCastShadows(false);
		ExtLight->SetVisibility(false);
		ExtLight->RegisterComponent();
	}

	UE_LOG(LogTemp, Log,
	       TEXT("[SPSolarSystem] ISS exterieur : %d meshes (Nanite, echelle reelle), span %.1f u -> %.0f m (x%.6g)"),
	       Meshes.Num(), SpanUU, EnvM, ExtScaleKm);
}

// Place TOUT ce qui est monde, rebasé sur l'œil.
void USPSolarSystemSubsystem::UpdateScene(double EpochTdb, const FVector& CamWorldKm)
{
	auto& Bridge = fen::app::g_render_bridge;
	// R(P) : monde (km) -> rendu (l'œil est l'origine). Position de COMPOSANT :
	// bornée radialement à RENDER_MAX_UU pour rester sous le plafond de précision
	// GPU d'UE (cf. RENDER_MAX_UU). Même direction depuis l'œil -> même position
	// écran ; seule la profondeur change. Les lignes d'orbite ont leur R propre.
	// `RenderRot` : identité au plan système, inverse de l'attitude À BORD — le
	// rendu s'y fait dans le repère de la STATION (voir GetRenderRot). Elle
	// s'applique APRÈS la compression : celle-ci est radiale autour de l'œil, donc
	// une rotation autour de l'œil commute avec elle, et l'ordre ne change rien —
	// on la met là où il n'y a qu'une ligne à écrire.
	auto R = [&CamWorldKm, this](const FVector& WorldKm) {
		const FVector P = CompressKm(WorldKm - CamWorldKm);
		const double M = P.Size();
		return RenderRot.RotateVector((M > RENDER_MAX_UU) ? P * (RENDER_MAX_UU / M) : P);
	};
	// Taille d'un marqueur pour qu'il garde une taille écran constante.
	auto MarkerScale = [](const FVector& Rendered) {
		return FMath::Max(1.0e-6, Rendered.Size() * MARKER_ANG) / 50.0;   // sphère = 50 u
	};

	// --- les corps : GÉOMÉTRIE de PRÈS, sinon MARQUEUR HUD -------------------
	// À l'échelle réelle un corps lointain est à ~1e13-1e14 u, où le float perd
	// des centaines de km -> facettes, corps délavés. On ne rend sa GÉOMÉTRIE
	// que s'il est assez proche pour être GROS (donc précis) ; sinon caché, le
	// HUD le montre au marqueur (projeté en double). [pas de compression]
	// ═══ UNE SEULE HOMOTHÉTIE POUR TOUS LES CORPS ═══
	// Borner CHAQUE corps séparément à RENDER_MAX_UU (première tentative du
	// 2026-07-27) remet tous les corps lointains à la MÊME distance de l'œil : la
	// Lune et la Terre, cadrées ensemble, atterrissaient sur la même sphère et
	// s'INTERPÉNÉTRAIENT (dôme pâle en travers de la Terre, vu en capture). Une
	// homothétie de centre l'œil et de facteur UNIQUE conserve, elle, à la fois
	// les tailles angulaires ET l'ordre des profondeurs — c'est la seule
	// contraction qui ne ment sur rien. On la calibre sur le corps le plus
	// éloigné effectivement rendu en géométrie.
	//
	// ═══ ... MAIS ELLE NE DOIT PAS ÉCRASER LE CORPS LE PLUS PROCHE ═══ (2026-07-27)
	// Calibrée sur le corps le plus LOINTAIN, l'homothétie unique porte tout le
	// poids de la dynamique de la scène. Depuis Novellus, cette dynamique est
	// démente : la Terre est à 6 796 km, le Soleil à 1,5e8 — un rapport de 22 000.
	// Le facteur tombait à 6,6e-8 et la Terre était rendue en SPHÈRE DE 42 cm à
	// 45 cm de l'œil. Taille angulaire juste (70°), position juste (mesurée : le
	// centre tombe pile au nadir) — et pourtant RIEN à l'écran : tout l'hémisphère
	// visible se trouvait entre 2,7 et 5,5 u de profondeur, c'est-à-dire ENTIÈREMENT
	// devant le plan de clipping proche (10 u). On voyait les étoiles à travers la
	// Terre. Le défaut n'est pas propre au rendu à bord : il frappait déjà tout
	// cadrage serré sur un corps depuis un objet proche.
	//
	// LE FACTEUR EST DONC BORNÉ PAR LE BAS, par ce que le PREMIER PLAN exige : la
	// surface du corps le plus proche doit rendre au-delà de `RENDER_MIN_UU`. Et
	// comme les deux contraintes ne peuvent pas toujours être satisfaites ensemble,
	// c'est celle du premier plan qui gagne — le lointain, lui, se rattrape par une
	// COURBURE (voir `RemapDist`) au lieu d'un écrasement uniforme.
	//
	// CE QUI GARANTIT L'ABSENCE DE RÉGRESSION : toute mise à l'échelle RADIALE de
	// centre l'œil laisse la projection EXACTEMENT invariante — position écran et
	// taille angulaire inchangées, puisque la position et le rayon subissent le même
	// facteur. Changer de facteur ne change donc pas l'image ; cela ne change que la
	// PROFONDEUR, c'est-à-dire précisément ce qui était cassé.
	double DistMaxGeomUU = 0.0;
	double SurfaceMinUU = TNumericLimits<double>::Max();
	for (int32 i = 0; i < NUM_BODIES && i < MapActor->BodyMeshes.Num(); ++i)
	{
		const FBodyDef& Def = GBodies[i];
		const double DistKm = (BodyWorldKm(Def, EpochTdb) - CamWorldKm).Size();
		if (DistKm < BodyRadiusKm(Def.B) * BODY_GEOM_FACTOR)
		{
			DistMaxGeomUU = FMath::Max(DistMaxGeomUU, DistKm * UU_PER_KM);
			// distance à la SURFACE, pas au centre : c'est elle qui doit dégager le
			// plan proche et la coque de la station.
			SurfaceMinUU = FMath::Min(
				SurfaceMinUU, FMath::Max(1.0, (DistKm - BodyRadiusKm(Def.B)) * UU_PER_KM));
		}
	}
	const double KLoin = (DistMaxGeomUU > RENDER_MAX_UU)
		? RENDER_MAX_UU / DistMaxGeomUU : 1.0;
	const double KPres = (SurfaceMinUU < TNumericLimits<double>::Max())
		? RENDER_MIN_UU / SurfaceMinUU : 0.0;
	const double HomoK = FMath::Min(1.0, FMath::Max(KLoin, KPres));

	for (int32 i = 0; i < NUM_BODIES && i < MapActor->BodyMeshes.Num(); ++i)
	{
		const FBodyDef& Def = GBodies[i];
		UStaticMeshComponent* C = MapActor->BodyMeshes[i];
		const FVector RelKm = BodyWorldKm(Def, EpochTdb) - CamWorldKm;
		const double DistKm = RelKm.Size();
		const bool bGeom = DistKm < BodyRadiusKm(Def.B) * BODY_GEOM_FACTOR;
		// PROPAGE AUX ENFANTS (bPropagateToChildren=true) : sinon les anneaux de
		// Saturne (mesh enfant) restent visibles quand le corps est caché. Et comme
		// Saturne est toujours loin -> jamais affiché -> sa transform reste celle,
		// initiale, de BuildScene (origine + échelle énorme) : l'anneau devient un
		// disque géant centré sur l'œil, vu par la tranche = un trait blanc en
		// grand cercle balayant le ciel. [bug du "trait" traqué le 2026-07-26]
		// La GÉOMÉTRIE d'une lune obéit au même critère que celle d'une planète :
		// assez proche pour être grosse, donc précise. Rien de plus — plus de
		// drapeau d'affichage (cf. bridge_flags.hpp).
		if (!bGeom) { C->SetVisibility(false, true); continue; }
		C->SetVisibility(true, true);
		// rotation AVANT la position : SetWorldRotation ne touche pas la
		// translation, et les enfants (anneaux de Saturne) suivent.
		// `RenderRot` COMPOSÉE PAR LA GAUCHE : l'orientation du corps est donnée dans
		// le repère de rendu INERTIEL (pôle IAU, méridien origine) ; à bord, le repère
		// de rendu est celui de la station, et il faut y transporter le corps ENTIER
		// — sa position comme son axe. N'appliquer la rotation qu'à la position
		// laisserait la Terre au bon endroit mais avec un pôle pointant n'importe où.
		C->SetWorldRotation(RenderRot * OrientationAt(Def, EpochTdb));
		// Le rayon suit la MÊME homothétie que la position : la taille angulaire
		// vue de l'œil reste donc exactement la vraie.
		double Fac = 1.0;
		FVector P = CompressKm(RelKm, &Fac);
		// ═══ LE PLAFOND DE PRÉCISION GPU S'APPLIQUE AUSSI AUX CORPS ═══
		// Les marqueurs et les lumières passaient par le borneur `R()` ; les CORPS,
		// eux, étaient posés à leur distance de rendu brute. Or cadrer un corps le
		// place à ~1e9 u de l'œil (Terre : 38 268 km = 3,8e9 u), soit ~1000 fois
		// au-dessus de RENDER_MAX_UU : la matrice monde passe en format GPU avec
		// une perte de précision et la géométrie sort DÉCHIRÉE — pans manquants,
		// fragments détachés, arcs concentriques. Constaté en capture sur la Terre,
		// la Lune et Io le 2026-07-27 ; ce n'est PAS propre aux lunes, elles n'ont
		// fait que le révéler (la capture qui « validait » la Terre lui est
		// antérieure et n'avait pas été refaite — même mécanique de vérification
		// périmée que le terminateur, piège n°29).
		// LA BORNE EST UNE HOMOTHÉTIE DE CENTRE L'ŒIL, de facteur COMMUN à tous les
		// corps (voir HomoK ci-dessus) : on contracte la position ET le rayon du
		// même facteur. La projection étant invariante par changement d'échelle
		// radial autour de l'œil, position écran et taille angulaire sont
		// EXACTEMENT conservées ; le facteur étant unique, l'ordre des profondeurs
		// l'est aussi. On ne déplace rien : on ramène l'échelle dans le domaine où
		// le GPU est juste.
		// Homothétie commune, puis COURBURE des lointains (voir RemapDist) : le
		// facteur effectif de CE corps est le rapport des deux distances, et il
		// s'applique à la position ET au rayon — la taille angulaire reste exacte.
		const double DistUU = P.Size() * HomoK;
		const double FacCorps = (DistUU > 1.0e-12) ? RemapDist(DistUU) / DistUU : 1.0;
		P *= HomoK * FacCorps;
		Fac *= HomoK * FacCorps;
		// LES CORPS NE PASSENT PAS PAR `R()` (ils ont leur propre homothétie, qui
		// doit rester COMMUNE à tous) : le changement de repère de rendu leur est
		// donc appliqué ICI, explicitement. L'oublier était le premier bug du rendu
		// à bord — la géométrie sortait juste, mais dans le repère inertiel : par la
		// cupola on voyait les étoiles, et la Terre était ailleurs dans le ciel.
		P = RenderRot.RotateVector(P);
		C->SetWorldLocation(P);

		// ═══ OÙ EST LA NUIT ═══ — la direction du Soleil, passée au matériau.
		// Le Soleil est à l'origine du monde : la lumière qui frappe ce corps voyage
		// donc dans la direction de sa propre position monde. C'est EXACT et propre
		// à chaque corps (mieux que la lumière directionnelle unique, qui prend une
		// seule direction pour toute la scène). Le matériau en tire le masque de
		// nuit et y allume la carte des lumières de villes.
		if (BodyMids.IsValidIndex(i) && BodyMids[i])
		{
			// Direction dans le repère de RENDU (le matériau la compare à la normale
			// du maillage, qui y vit aussi) : elle subit `RenderRot` comme le reste,
			// sinon le terminateur se décollerait de l'éclairage à bord.
			const FVector SunDir =
				RenderRot.RotateVector(BodyWorldKm(Def, EpochTdb).GetSafeNormal());
			if (!SunDir.IsNearlyZero())
				BodyMids[i]->SetVectorParameterValue(
					TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0f));
			// ═══ L'ATMOSPHÈRE TOURNE PLUS VITE QUE LE SOL ═══ (2026-07-27)
			// La période vient du VENT ZONAL réel (`ephem::cloud_deck_period_s`) : le
			// rendu ne choisit rien, il convertit un temps en décalage d'UV. Signe : la
			// longitude sous un U donné DÉCROÎT quand le décalage croît (le matériau
			// échantillonne en U + δ), donc un vent d'est (v > 0) demande δ décroissant
			// — d'où le moins. Vénus, rétrograde, part naturellement dans l'autre sens.
			// `Frac` garde le paramètre dans [0,1) pour ne pas perdre la précision d'un
			// float après des années d'époque ; c'est sans effet visible, le décalage
			// étant constant sur tout le maillage (la texture boucle en U).
			const double CloudT = fen::ephem::cloud_deck_period_s(Def.B);
			if (CloudT != 0.0)
				BodyMids[i]->SetScalarParameterValue(
					TEXT("CloudSpin"), static_cast<float>(FMath::Frac(-EpochTdb / CloudT)));
		}
		const double MeshR = BodyMeshRadius.IsValidIndex(i) ? BodyMeshRadius[i] : 50.0;
		C->SetWorldScale3D(FVector(BodyRadiusKm(Def.B) * Fac / MeshR));
	}

	// --- le Soleil éclaire depuis sa position (rebasée) ----------------------
	// PIÈGE PAYÉ (2026-07-26) : cette position passait par `R`, donc était BORNÉE à
	// RENDER_MAX_UU. Ce plafond vaut 1e6 u, soit 10 km à l'échelle réelle (1 u =
	// 1 cm) — alors que le corps regardé est à des milliers de km, donc des
	// milliards d'u. La lumière se retrouvait ~4000 fois plus PRÈS que la planète,
	// pratiquement sur l'œil : l'hémisphère visible était intégralement éclairé et
	// LE TERMINATEUR DISPARAISSAIT. Régression silencieuse du passage à l'échelle
	// réelle (avant, à 1 u = 1 km, le plafond valait 1e6 km et la direction restait
	// bonne) : la capture qui « validait » le jour/nuit lui est ANTÉRIEURE — son HUD
	// dit encore « ECHELLE VRAIE 1 u = 1 km ».
	//
	// SUITE (2026-07-27) : le placement à 1e12 u était lui-même hors du domaine de
	// précision du GPU. La lumière est maintenant DIRECTIONNELLE (voir BuildScene) :
	// il n'y a plus de position à poser, seulement une DIRECTION — celle qui va du
	// Soleil vers ce qu'on regarde. Aucune distance n'entre plus en jeu, donc aucun
	// plafond ne peut plus la corrompre.
	// APPROXIMATION DÉCLARÉE [GDD 6.8] : une seule direction éclaire toute la scène,
	// prise du Soleil vers l'ŒIL. Deux corps très écartés recevraient en toute
	// rigueur des directions différentes ; à la distance où l'on rend de la
	// géométrie (un corps cadré, éventuellement ses lunes), l'écart est négligeable.
	if (MapActor->SunLight)
	{
		// CamWorldKm est la position de l'œil, le Soleil est à l'origine du monde :
		// la lumière voyage donc du Soleil vers l'œil, direction CamWorldKm. Elle
		// passe par le MÊME changement de repère que les positions (`RenderRot`),
		// sans quoi à bord la Terre serait éclairée depuis un ailleurs inertiel et
		// son terminateur ne collerait plus à ce qu'on voit par le hublot.
		const FVector Dir = RenderRot.RotateVector(CamWorldKm.GetSafeNormal());
		if (!Dir.IsNearlyZero())
			MapActor->SunLight->SetWorldRotation(Dir.Rotation());
	}

	// --- vaisseau en vol interplanétaire : position ESTIMÉE [GDD 7.5] --------
	const bool bVehicle = Bridge.vehicle.valid.load() && !bBord;   // symbole : pas à bord
	if (MapActor->VehicleMarker)
	{
		MapActor->VehicleMarker->SetVisibility(bVehicle);
		if (bVehicle)
		{
			const FVector P = R(EclToUeKmd(Bridge.vehicle.pos_m[0], Bridge.vehicle.pos_m[1],
			                               Bridge.vehicle.pos_m[2]));
			MapActor->VehicleMarker->SetWorldLocation(P);
			MapActor->VehicleMarker->SetWorldScale3D(FVector(MarkerScale(P)));
		}
	}

	// --- NOVELLUS dans le monde [GDD v1.2 11.1, 17.3, 17.4] : LOD PAR TAILLE
	// APPARENTE, en TROIS crans. De loin, un MARQUEUR (comme la flotte) ; de près,
	// le vrai MODÈLE EXTÉRIEUR à l'échelle réelle (109 m) ; dans l'enveloppe, la
	// géométrie INTÉRIEURE (portée par SPStation). Les trois à la position LEO
	// réelle (Terre + offset publié), en caméra-relatif.
	{
		const auto& St = Bridge.station;
		const bool bStation = St.valid.load();
		const FBodyDef* Terre = FindDef(Body::EarthBary);
		const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
		const FVector NovWorld = TerrePos + EclToUeKmd(St.rel_m[0], St.rel_m[1], St.rel_m[2]);
		const FVector Pnov = R(NovWorld);
		// bascule marqueur -> modèle quand l'envergure (109 m) dépasse ~quelques px
		// (taille angulaire = envergure / distance de vue).
		const double DistNov = FMath::Max(1.0e-6, (NovWorld - CamWorldKm).Size());
		// COEXISTENCE (incr. 3c-3) : l'œil est DANS l'enveloppe de la station, et
		// c'est la géométrie INTÉRIEURE qui rend (SPStation la pose sur ce même
		// Pnov). Le modèle extérieur s'efface alors : deux modèles du même objet ne
		// peuvent pas s'interpénétrer à l'écran. La bascule tombe au franchissement
		// de la coque, là où elle est le moins visible.
		// À BORD, l'œil est DANS la station de plein droit (plus seulement en
		// transit) : c'est le même cas que la coexistence, en permanent. Ni modèle
		// extérieur (il envelopperait la caméra), ni marqueur (une sphère émissive
		// collée à l'œil) — seule la géométrie intérieure rend, et c'est SPStation
		// qui la porte, dans son repère canonique.
		const bool bCoexiste = Bridge.interieur_coexiste.load() || bBord;
		const bool bModel = bStation && !bCoexiste &&
		                    ((St.envergure_m * 0.001) / DistNov > 3.0e-3);

		// Position de rendu publiée à SPStation : le rebasage caméra-relatif vit
		// ICI, on ne le duplique pas de l'autre côté.
		NovellusRenderUU = Pnov;
		bNovellusRenderValid = bStation;

		// Le marqueur ne sert QUE de loin : ni pendant la coexistence (on serait
		// dans une sphère émissive collée à l'œil), ni quand le modèle rend.
		const bool bMarqueur = bStation && !bModel && !bCoexiste;
		if (MapActor->StationMarker)
		{
			MapActor->StationMarker->SetVisibility(bMarqueur);
			if (bMarqueur)
			{
				MapActor->StationMarker->SetWorldLocation(Pnov);
				MapActor->StationMarker->SetWorldScale3D(FVector(MarkerScale(Pnov)));
			}
		}

		// L'ATTITUDE : cupola au nadir, axe de vol dans la vitesse. Posée en tête de
		// Tick (la caméra du handoff en a besoin AVANT le placement des objets), et
		// lue telle quelle par la géométrie INTÉRIEURE — sans quoi la bascule de LOD
		// à la traversée de la coque ferait pivoter la station d'un coup, en plein
		// milieu du vol [M].
		const FQuat AttNov = NovellusAttitude;

		// Le modèle est construit tôt (Tick, dès la carte active) pour que ses
		// shaders soient chauds ; ici on ne fait que le MONTRER/cacher (LOD) et le
		// placer, caméra-relatif.
		if (ExtRoot)
		{
			ExtRoot->SetVisibility(bModel, true);   // propage aux pièces
			if (bModel)
			{
				// centre du modèle amené sur Novellus ; échelle réelle (km/u).
				// LE RECENTRAGE PASSE PAR L'ATTITUDE : le centre du modèle doit tomber
				// sur Novellus APRÈS rotation, sinon la station décrirait un petit
				// cercle parasite (du rayon de son décentrage) en tournant sur elle-même.
				ExtRoot->SetWorldScale3D(FVector(ExtScaleKm));
				ExtRoot->SetWorldRotation(AttNov);
				ExtRoot->SetWorldLocation(Pnov - AttNov.RotateVector(ExtCentreUU * ExtScaleKm));
			}
		}
		if (ExtLight)
		{
			ExtLight->SetVisibility(bModel);
			if (bModel) ExtLight->SetWorldLocation(Pnov);
		}
	}

	// --- la flotte en service [GDD 8.3] : position VRAIE de chaque engin ------
	// À l'échelle vraie, plus besoin d'anneaux symboliques ni de rayon plancher :
	// un relais GEO est à 42 164 km de la Terre, et il y EST.
	{
		const auto& F = Bridge.fleet;
		// À BORD, aucun marqueur : ce sont des SYMBOLES de carte (taille écran
		// constante, sphères émissives), et un symbole n'a rien à faire dans une
		// scène vécue à la première personne. Un relais GEO vu du hublot serait un
		// point mobile de la taille d'une planète.
		const int32 NCraft = bBord ? 0
			: FMath::Min(F.n.load(), fen::app::RenderBridge::FleetSnap::MAX);
		const int32 Base[3] = {FLEET_GEO0, FLEET_MARS0, FLEET_PROBE0};
		int32 Used[3] = {0, 0, 0};
		for (int32 i = 0; i < NCraft; ++i)
		{
			const auto& Craft = F.craft[i];
			const int32 Cat = FMath::Clamp(Craft.type, 0, 2);
			if (Used[Cat] >= FLEET_PER_CAT) continue;
			UStaticMeshComponent* M = MapActor->FleetMarkers[Base[Cat] + Used[Cat]++];
			const FBodyDef* Parent =
				(Craft.parent >= 0 && Craft.parent < static_cast<int>(Body::COUNT))
					? FindDef(static_cast<Body>(Craft.parent)) : nullptr;
			const FVector ParentPos =
				Parent ? BodyWorldKm(*Parent, EpochTdb) : FVector::ZeroVector;
			const FVector P = R(ParentPos + EclToUeKmd(Craft.rel_m[0], Craft.rel_m[1],
			                                           Craft.rel_m[2]));
			M->SetVisibility(true);
			M->SetWorldLocation(P);
			M->SetWorldScale3D(FVector(MarkerScale(P)));
		}
		for (int32 Cat = 0; Cat < 3; ++Cat)
			for (int32 k = Used[Cat]; k < FLEET_PER_CAT; ++k)
				MapActor->FleetMarkers[Base[Cat] + k]->SetVisibility(false);

		// vol GEO en cours : la trace publiée est géocentrique (km) — l'estimé
		// est placé à sa VRAIE position autour de la Terre.
		UStaticMeshComponent* GeoM = MapActor->FleetMarkers[FLEET_GEOFLIGHT];
		const bool bGeo = Bridge.geo.valid.load() && !bBord;   // symbole : pas à bord
		GeoM->SetVisibility(bGeo);
		if (bGeo)
		{
			const FBodyDef* Terre = FindDef(Body::EarthBary);
			const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
			const FVector P = R(TerrePos + FVector(Bridge.geo.pos_km[0], -Bridge.geo.pos_km[1],
			                                       Bridge.geo.pos_km[2]));
			GeoM->SetWorldLocation(P);
			GeoM->SetWorldScale3D(FVector(MarkerScale(P)));
		}
	}
}

// Échantillonne les orbites via l'ÉPHÉMÉRIDE (la vérité), en km monde absolus.
// Coûteux -> appelé seulement quand l'époque dérive ou que les options changent.
void USPSolarSystemSubsystem::RebuildOrbitCache(double EpochTdb)
{
	OrbitCache.Reset();
	for (const FBodyDef& Def : GBodies)
	{
		if (Def.B == Body::Sun) continue;
		const double T = TracePeriodS(Def, EpochTdb);
		if (T <= 0.0) continue;

		// ═══ LA FENÊTRE DE TEMPS ÉCHANTILLONNÉE ═══
		// Un modèle EXACTEMENT périodique (les lunes de `Satellites.hpp` : cercles
		// de moyen mouvement constant) se boucle sans couture — on lui donne une
		// période pile, et l'indice de tête boucle modulo. Tout le reste (planètes,
		// Lune, grosses lunes) n'est PAS périodique : les éléments de Standish
		// dérivent, la série lunaire encore plus. On échantillonne alors le PASSÉ
		// réel — la traînée à allumer — plus la péremption du cache (le corps
		// avance dans la fenêtre entre deux reconstructions). Aucune de ces traces
		// n'est rebouclée : aucune cassure ne peut donc apparaître.
		const bool bCycle = Def.bMoon && T <= ORBIT_CYCLE_MAX_S;
		const double Trail = ORBIT_TRAIL_FRAC * T;
		const double Marge = FMath::Min(ORBIT_REDRAW_DAYS * 86400.0, 0.30 * T);
		const double WinStart = bCycle ? EpochTdb : (EpochTdb - Trail);
		const double WinSpan  = bCycle ? T : (Trail + Marge);

		// Trace = la trajectoire RÉELLE échantillonnée via l'éphéméride (pas une
		// ellipse idéalisée). Pour une LUNE on garde la trace RELATIVE à son
		// parent : c'est l'émission qui la recentre chaque frame.
		FOrbitCache Entry;
		Entry.Color = FLinearColor(Def.Color.R, Def.Color.G, Def.Color.B, 1.0f);
		Entry.bMoon = Def.bMoon;
		Entry.Body = static_cast<int>(Def.B);
		Entry.ParentBody = static_cast<int>(Def.Parent);
		Entry.WindowStartS = WinStart;
		Entry.WindowS = WinSpan;
		Entry.TrailS = Trail;
		Entry.bCycle = bCycle;
		Entry.PointsKm.Reserve(ORBIT_SAMPLES + 1);
		for (int32 k = 0; k <= ORBIT_SAMPLES; ++k)
		{
			const double Tk = WinStart + (WinSpan * k) / ORBIT_SAMPLES;
			const fen::Vec3 Rel = GEph.state(Def.B, Def.bMoon ? Def.Parent : Body::Sun,
			                                 fen::Epoch{Tk}).r;
			const FVector P = EclToUeKm(Rel);
			Entry.PointsKm.Add(P);
			if (Def.bMoon) Entry.RadiusKm = FMath::Max(Entry.RadiusKm, P.Size());
		}
		OrbitCache.Add(MoveTemp(Entry));
	}
}

// Ré-émet toutes les polylignes dans le repère rebasé sur l'œil (chaque frame).
void USPSolarSystemSubsystem::EmitOrbits(const FVector& CamWorldKm, double CamDistKm,
                                         double EpochTdb)
{
	UWorld* W = GetWorld();
	ULineBatchComponent* LB =
		W ? W->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr;
	if (!LB) return;
	LB->Flush();

	// Même compression que les corps : les traits passent donc exactement par eux
	// à l'écran (la compression est radiale, elle ne change aucune direction).
	auto R = [&CamWorldKm](const FVector& WorldKm) {
		return CompressKm(WorldKm - CamWorldKm);
	};
	// Épaisseur ANGULAIRE : à l'échelle vraie, une épaisseur fixe en km serait
	// soit invisible (vue système) soit énorme (vue rapprochée). On la lie à la
	// distance de l'œil pour garder ~2 px — dans l'espace COMPRIMÉ, où vivent
	// les traits.
	const double CamDistRender = CamDistKm * UU_PER_KM;   // échelle réelle (cm)
	// TOUS LES TRACÉS SONT DES LIGNES FINES. Une épaisseur en unités MONDE fait
	// dessiner un quad face caméra par segment, et les jointures se voient (cf. le
	// bloc des orbites plus bas). Épaisseur 0 = chemin « ligne fine » du batcher :
	// un pixel, continu, à toute distance. Les tracés de mission (trajectoire,
	// corridor, nœuds) suivent la même règle que les orbites — une seule doctrine
	// de trait pour toute la carte.
	const float Thick = 0.0f;
	(void)CamDistRender;

	// DÉCOR DU MENU : les mêmes orbites, mais en retrait — dans la référence
	// (ref_menu.png) ce sont de simples cercles à la limite du visible.
	const bool bDecor = fen::app::g_render_bridge.menu_backdrop.load() &&
	                    !fen::app::g_render_bridge.carte3d_active.load();
	// ═══ UN TRAIT DU BATCHER N'A PAS D'ALPHA ═══ (trouvé dans le moteur, 2026-07-27)
	// Les lignes et les points de `ULineBatchComponent` sont dessinés dans la passe
	// de base avec le filtre `OpaqueAndMasked` et le nuanceur `SE_BLEND_Opaque`
	// (Engine/Private/BatchedElements.cpp) : l'état de mélange est `TStaticBlendState<>`
	// et le pixel rendu vaut `Source.rgb` — LE CANAL ALPHA EST IGNORÉ, purement et
	// simplement. Tout ce fichier atténuait pourtant ses tracés par l'alpha : le
	// fondu rasant, la mise en avant au survol et l'opacité 0,35 des orbites
	// n'avaient donc AUCUN effet, sauf le seuil qui coupe le trait. C'est aussi
	// l'explication des « paliers d'opacité » constatés à la première tentative de
	// traînée : le dégradé ne passait pas, seule sa coupure se voyait.
	// RÈGLE DE CE FICHIER : sur un tracé du batcher, on module le RVB. Le fond est
	// noir, le rendu est donc le même qu'un fondu — sans dépendre d'un mélange que
	// le moteur ne fait pas.
	//
	// FONDU EN VUE RASANTE. Les orbites sont ~coplanaires (écliptique) : vues par la
	// tranche, elles s'écrasent en UNE ligne dure traversant tout l'écran et le corps
	// focalisé. On atténue leur opacité quand l'élévation de l'œil au-dessus du plan
	// tombe vers zéro (façon Eyes on the Solar System). Élévation ~ |sin(pitch)| :
	// pitch = 0 -> œil DANS le plan -> fondu quasi total ; pitch fort -> vue de dessus
	// -> plein. Bande de transition ~1°..9° d'élévation, courbe smoothstep.
	const double Pitch = fen::app::g_render_bridge.cam.pitch.load();
	const double Elev  = FMath::Abs(FMath::Sin(Pitch));
	const double GrazeT = FMath::Clamp((Elev - 0.02) / 0.14, 0.0, 1.0);
	const float  Graze  = static_cast<float>(GrazeT * GrazeT * (3.0 - 2.0 * GrazeT));
	const float Att = (bDecor ? 0.40f : 1.0f) * Graze;

	const int Survol = fen::app::g_render_bridge.hover_body.load();
	const int Focus  = fen::app::g_render_bridge.focus_body.load();
	for (const FOrbitCache& O : OrbitCache)
	{
		// Une orbite de lune est RELATIVE : on la recentre sur la position COURANTE
		// de sa planète, sinon elle dériverait entre deux reconstructions du cache.
		FVector Centre = FVector::ZeroVector;
		if (O.bMoon)
		{
			const FBodyDef* Parent = FindDef(static_cast<Body>(O.ParentBody));
			if (!Parent) continue;
			Centre = BodyWorldKm(*Parent, EpochTdb);
			// LOD : sous la taille d'un marqueur, l'orbite est un POINT — des
			// segments dégénérés qui empâtent la planète. On la saute.
			const double DistKm = (Centre - CamWorldKm).Size();
			if (DistKm > 1.0 && (O.RadiusKm / DistKm) < MARKER_ANG * 2.0) continue;
		}

		// ═══ TRAIT FIN, PAS UN RUBAN ═══ (2026-07-27, sur retour d'essai)
		// L'épaisseur était donnée en unités MONDE : `DrawLine` construit alors un
		// QUAD face caméra par segment, et les quads consécutifs ne se raccordent
		// pas — d'où la chaîne de tuiles visible en jeu, prise pour un défaut
		// d'alignement des corps. Plus l'épaisseur montait, plus les jointures se
		// voyaient : les « corrections » d'épaisseur aggravaient le mal.
		// Épaisseur 0 fait passer le batcher sur son chemin de LIGNES FINES : un
		// trait d'un pixel, continu, antialiasé, indépendant de la distance et de
		// la résolution. C'est exactement ce que fait Eyes on the Solar System.
		const float ThickO = 0.0f;

		// MISE EN AVANT : l'orbite du corps survolé ou focalisé est plus franche.
		// C'est ce qui rend le survol lisible sans rien écrire à l'écran.
		const bool bVedette = (O.Body == Survol) || (O.Body == Focus);
		const double Gain = Att * (bVedette ? 1.7 : 1.0);
		if (Gain <= 0.004) continue;

		// ═══ LA TRAÎNÉE ═══ (2026-07-27, demande utilisateur)
		// L'anneau fermé est remplacé par la portion que le corps VIENT de
		// parcourir, franche à la tête et éteinte à la queue — la lecture d'Eyes
		// on the Solar System : on voit d'un coup d'œil où va le corps.
		//
		// La tête n'est PAS un point du cache : c'est la position COURANTE du
		// corps, celle-là même qui place son marqueur. Le trait part donc
		// exactement du corps à chaque frame, quelle que soit la péremption du
		// cache — c'est ce qui garantit qu'une trace ne peut plus se « décaler »
		// de son corps.
		//
		// Le cache étant échantillonné UNIFORMÉMENT EN TEMPS sur une fenêtre
		// connue, l'indice de la tête se lit sans aucun calcul d'éphéméride.
		const int32 N = O.PointsKm.Num() - 1;         // nombre d'intervalles
		if (N < 8 || O.WindowS <= 0.0) continue;
		const double PasS = O.WindowS / N;
		double Tete = (EpochTdb - O.WindowStartS) / PasS;
		if (O.bCycle)
		{
			Tete = FMath::Fmod(Tete, static_cast<double>(N));
			if (Tete < 0.0) Tete += N;
		}
		else Tete = FMath::Clamp(Tete, 0.0, static_cast<double>(N));
		const int32 NbSeg = FMath::Clamp(FMath::RoundToInt(O.TrailS / PasS), 2, N);

		// POSITION COURANTE DU CORPS dans le repère de la trace (absolu pour une
		// planète, relatif au parent pour une lune — même convention que le cache).
		const FBodyDef* DefO = FindDef(static_cast<Body>(O.Body));
		if (!DefO) continue;
		const FVector TeteKm = BodyWorldKm(*DefO, EpochTdb) - Centre;

		// LE DÉGRADÉ PASSE PAR LE RVB, PAS PAR L'ALPHA (voir plus haut : le batcher
		// dessine opaque). Et il est étalé sur ~370 segments : un cran de couleur
		// couvre donc deux segments, soit une fraction de degré d'arc — la marche
		// d'escalier qui avait fait rejeter la première traînée ne peut pas
		// réapparaître, elle venait de l'alpha ignoré, pas de la quantification.
		FVector Prec = Centre + TeteKm;
		int32 k = FMath::FloorToInt(Tete);
		for (int32 j = 0; j < NbSeg; ++j, --k)
		{
			const int32 Idx = O.bCycle ? ((k % N) + N) % N : k;
			if (Idx < 0) break;                        // fenêtre passée épuisée
			const double s = static_cast<double>(j) / NbSeg;      // 0 tête -> 1 queue
			const float F = static_cast<float>(Gain * FMath::Pow(1.0 - s, ORBIT_TRAIL_GAMMA));
			const FVector Suiv = Centre + O.PointsKm[Idx];
			if (F > 0.004f)
				LB->DrawLine(R(Prec), R(Suiv),
				             FLinearColor(O.Color.R * F, O.Color.G * F, O.Color.B * F, 1.0f),
				             SDPG_World, ThickO, 0.0f);
			Prec = Suiv;
		}
	}
	if (bDecor) return;   // pas de trajectoire ni de flotte derrière le menu

	// Vol interplanétaire [GDD 8.3] : le rendu TRACE ce que l'écran publie —
	// trajectoire NOMINALE, corridor d'incertitude 3σ autour de la position
	// ESTIMÉE (échelle VRAIE : aucune inflation), nœuds de manœuvre TCM.
	const auto& V = fen::app::g_render_bridge.vehicle;
	if (V.valid.load() && V.n >= 2)
	{
		// Fondu rasant par le RVB (le batcher ignore l'alpha, cf. plus haut).
		const FLinearColor Jaune(1.0f * Graze, 0.85f * Graze, 0.2f * Graze, 1.0f);
		for (int32 k = 1; k < V.n; ++k)
			LB->DrawLine(R(EclToUeKmd(V.traj_m[k - 1][0], V.traj_m[k - 1][1], V.traj_m[k - 1][2])),
			             R(EclToUeKmd(V.traj_m[k][0], V.traj_m[k][1], V.traj_m[k][2])),
			             Jaune, SDPG_World, Thick * 1.4f, 0.0f);

		// corridor 3σ : cercle dans le plan écliptique autour de l'estimé.
		// Le cercle se construit en km MONDE puis passe par R, comme tout le reste :
		// il était bâti en ajoutant des km à un point DÉJÀ converti en unités de
		// rendu, soit un rayon 100 000 fois trop petit (jamais vu — ce tracé dort
		// tant que la boucle de mission vécue ne le réveille pas).
		const FVector CentreKm = EclToUeKmd(V.pos_m[0], V.pos_m[1], V.pos_m[2]);
		const double RayonKm = V.corridor_3s_m * KM_PER_M;
		if (RayonKm > 1.0)
		{
			const FLinearColor Orange(1.0f * Graze, 0.6f * Graze, 0.2f * Graze, 1.0f);
			constexpr int32 SEG = 48;
			FVector Prev = R(CentreKm + FVector(RayonKm, 0, 0));
			for (int32 k = 1; k <= SEG; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / SEG;
				const FVector P = R(CentreKm +
					FVector(RayonKm * FMath::Cos(A), RayonKm * FMath::Sin(A), 0.0));
				LB->DrawLine(Prev, P, Orange, SDPG_World, Thick, 0.0f);
				Prev = P;
			}
		}

		// nœuds de manœuvre : croix (orange = à faire, grise = faite)
		for (int32 k = 0; k < V.n_nodes && k < 2; ++k)
		{
			const FVector N = R(EclToUeKmd(V.nodes_m[k][0], V.nodes_m[k][1], V.nodes_m[k][2]));
			const double S = FMath::Max(1.0, N.Size() * MARKER_ANG * 2.0);
			const FLinearColor Col = V.node_done[k] ? FLinearColor(0.6f, 0.6f, 0.6f, 0.8f)
			                                        : FLinearColor(1.0f, 0.55f, 0.15f, 1.0f);
			LB->DrawLine(N - FVector(S, 0, 0), N + FVector(S, 0, 0), Col, SDPG_World, Thick * 1.6f, 0.0f);
			LB->DrawLine(N - FVector(0, S, 0), N + FVector(0, S, 0), Col, SDPG_World, Thick * 1.6f, 0.0f);
			LB->DrawLine(N - FVector(0, 0, S), N + FVector(0, 0, S), Col, SDPG_World, Thick * 1.6f, 0.0f);
		}
	}

	// Vol GEO [GDD 8.3] : orbite CIBLE (nominale) + trace ESTIMÉE, à l'échelle
	// vraie autour de la Terre — plus besoin d'une scène rapprochée séparée.
	const auto& G = fen::app::g_render_bridge.geo;
	if (G.valid.load())
	{
		const FBodyDef* Terre = FindDef(Body::EarthBary);
		const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
		auto GeoToWorld = [&TerrePos](double Xkm, double Ykm, double Zkm) {
			return TerrePos + FVector(Xkm, -Ykm, Zkm);
		};
		// anneau VERT = orbite cible du contrat (la référence nominale)
		const double Sma = G.target_sma_km;
		if (Sma > 1.0)
		{
			const FLinearColor Vert(0.35f * Graze, 0.95f * Graze, 0.45f * Graze, 1.0f);
			constexpr int32 SEG = 96;
			FVector Prev = R(GeoToWorld(Sma, 0.0, 0.0));
			for (int32 k = 1; k <= SEG; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / SEG;
				const FVector P = R(GeoToWorld(Sma * FMath::Cos(A), Sma * FMath::Sin(A), 0.0));
				LB->DrawLine(Prev, P, Vert, SDPG_World, Thick, 0.0f);
				Prev = P;
			}
		}
		// trace CYAN = solution de navigation (ESTIMÉE, jamais la vérité)
		if (G.n >= 2)
		{
			const FLinearColor Cyan(0.3f * Graze, 0.9f * Graze, 1.0f * Graze, 1.0f);
			FVector Prev = R(GeoToWorld(G.traj_km[0][0], G.traj_km[0][1], G.traj_km[0][2]));
			for (int32 k = 1; k < G.n; ++k)
			{
				const FVector P = R(GeoToWorld(G.traj_km[k][0], G.traj_km[k][1], G.traj_km[k][2]));
				LB->DrawLine(Prev, P, Cyan, SDPG_World, Thick, 0.0f);
				Prev = P;
			}
		}
	}
}

// Projette les corps à l'écran pour le HUD (marqueurs, libellés, picking).
// Coordonnées NORMALISÉES : le HUD ne dépend pas de la taille du viewport.
void USPSolarSystemSubsystem::PublishScreen(const FVector& CamWorldKm, const FRotator& CamRot,
                                            double FovDeg)
{
	auto& S = fen::app::g_render_bridge.screen;
	const double Aspect = ViewportAspect(GetWorld());
	const double TanH = FMath::Tan(FMath::DegreesToRadians(FovDeg) * 0.5);   // FOV horizontal
	const double TanV = TanH / Aspect;
	const double Epoch = fen::app::g_render_bridge.epoch_tdb.load();

	// ═══ LE HUD SE CONFRONTE AU RENDU ═══ (2026-07-27)
	// Un marqueur posé par le HUD et un tracé posé par le renderer ne peuvent
	// coïncider que si les deux projections sont la MÊME. Elles ne l'étaient pas
	// (voir Tick, `SetAspectRatio`), et rien ne le disait : le défaut n'apparaissait
	// qu'à l'œil, hors 16:9, et j'ai cherché du côté des éphémérides. On compare
	// donc une fois notre demi-champ à celui de la matrice de projection que le
	// moteur va réellement employer. Si l'écart dépasse 1 %, la carte le DIT.
	{
		// Pas avant que la caméra de la carte ne soit posée ET prise par le
		// gestionnaire de caméra : aux toutes premières frames la vue est encore
		// celle du contrôleur (champ par défaut), et comparer là n'aurait aucun sens.
		static int32 NVerif = 0;
		const bool bVerifie = (++NVerif != 120);
		if (!bVerifie)
			if (const UWorld* Wd = GetWorld())
				if (const APlayerController* PC = Wd->GetFirstPlayerController())
					if (const ULocalPlayer* LP = PC->GetLocalPlayer())
						if (LP->ViewportClient && LP->ViewportClient->Viewport)
						{
							FSceneViewProjectionData PD;
							if (LP->GetProjectionData(LP->ViewportClient->Viewport, PD))
							{
								const double M00 = PD.ProjectionMatrix.M[0][0];
								const double M11 = PD.ProjectionMatrix.M[1][1];
								if (M00 > 1.0e-6 && M11 > 1.0e-6)
								{
									const double EH = FMath::Abs(1.0 / M00 - TanH) / TanH;
									const double EV = FMath::Abs(1.0 / M11 - TanV) / TanV;
									if (EH > 0.01 || EV > 0.01)
										UE_LOG(LogTemp, Warning,
										       TEXT("[SPSolarSystem] projection HUD != rendu : "
										            "tanH %.6f vs %.6f (%.1f %%), tanV %.6f vs %.6f (%.1f %%). "
										            "Les marqueurs se decaleront des traces."),
										       TanH, 1.0 / M00, EH * 100.0, TanV, 1.0 / M11, EV * 100.0);
								}
							}
						}
	}

	// ═══ NON SÉPARABLE DE SON PARENT = PAS DÉSIGNABLE ═══ (piège n°41)
	// Vue du système, une lune tombe sur le MÊME pixel que sa planète : les
	// libellés s'impriment l'un sur l'autre et le picking tire au sort. On ne
	// déclare donc une lune à l'écran que si elle se DÉTACHE de son parent d'au
	// moins la taille d'un marqueur. C'est la règle déjà payée pour Novellus, ici
	// généralisée — et c'est elle qui REMPLACE le défunt drapeau `show_moons` :
	// ce n'est plus une option, c'est du LOD, donc ça ne peut pas rester éteint.
	constexpr double SEP_MIN_NORM = 0.013;   // ~17 px de large sur 1280
	auto SeparableDuParent = [&](const fen::app::RenderBridge::ScreenBodies& Sc,
	                             int32 Count, int ParentBody, double Nx, double Ny) {
		for (int32 i = 0; i < Count; ++i)
		{
			if (Sc.items[i].body != ParentBody) continue;
			if (!Sc.items[i].on_screen) return true;   // parent hors champ : rien à confondre
			const double dx = Nx - Sc.items[i].nx;
			// Écran non carré : l'écart vertical se ramène en fraction de LARGEUR,
			// l'unité dans laquelle SEP_MIN_NORM est exprimé.
			const double dy = (Ny - Sc.items[i].ny) / Aspect;
			return dx * dx + dy * dy >= SEP_MIN_NORM * SEP_MIN_NORM;
		}
		return true;   // parent non publié
	};

	int32 N = 0;
	for (const FBodyDef& Def : GBodies)
	{
		if (N >= fen::app::RenderBridge::ScreenBodies::MAX) break;
		const FVector Rel = BodyWorldKm(Def, Epoch) - CamWorldKm;
		const FVector Local = CamRot.UnrotateVector(Rel);   // X avant, Y droite, Z haut
		auto& It = S.items[N];
		It.body = static_cast<int>(Def.B);
		It.dist_km = Rel.Size();
		if (Local.X <= 1.0)
		{
			It.on_screen = 0; It.nx = It.ny = -1.0f; It.r_norm = 0.0f;
			++N; continue;
		}
		const double Nx = 0.5 + 0.5 * (Local.Y / Local.X) / TanH;
		const double Ny = 0.5 - 0.5 * (Local.Z / Local.X) / TanV;
		It.nx = static_cast<float>(Nx);
		It.ny = static_cast<float>(Ny);
		// rayon apparent en fraction de la LARGEUR d'écran
		It.r_norm = static_cast<float>(0.5 * (BodyRadiusKm(Def.B) / Local.X) / TanH);
		It.on_screen = (Nx > -0.1 && Nx < 1.1 && Ny > -0.1 && Ny < 1.1) ? 1 : 0;
		if (It.on_screen && Def.bMoon &&
		    !SeparableDuParent(S, N, static_cast<int>(Def.Parent), Nx, Ny))
			It.on_screen = 0;
		++N;
	}

	// NOVELLUS dans la liste écran : focalisable/cliquable comme un corps
	// [GDD v1.2 11.1]. Rayon apparent tiré de l'envergure (109 m) -> minuscule de
	// loin, le HUD le désigne alors au marqueur (comme un satellite).
	{
		const auto& St = fen::app::g_render_bridge.station;
		if (St.valid.load() && N < fen::app::RenderBridge::ScreenBodies::MAX)
		{
			const FBodyDef* Terre = FindDef(Body::EarthBary);
			const FVector TerrePos = Terre ? BodyWorldKm(*Terre, Epoch) : FVector::ZeroVector;
			const FVector Rel =
				(TerrePos + EclToUeKmd(St.rel_m[0], St.rel_m[1], St.rel_m[2])) - CamWorldKm;
			const FVector Local = CamRot.UnrotateVector(Rel);
			auto& It = S.items[N];
			It.body = fen::app::FOCUS_STATION;
			It.dist_km = Rel.Size();
			if (Local.X <= 1.0)
			{
				It.on_screen = 0; It.nx = It.ny = -1.0f; It.r_norm = 0.0f;
			}
			else
			{
				const double Nx = 0.5 + 0.5 * (Local.Y / Local.X) / TanH;
				const double Ny = 0.5 - 0.5 * (Local.Z / Local.X) / TanV;
				It.nx = static_cast<float>(Nx);
				It.ny = static_cast<float>(Ny);
				It.r_norm = static_cast<float>(0.5 * ((St.envergure_m * 0.001) / Local.X) / TanH);
				It.on_screen = (Nx > -0.1 && Nx < 1.1 && Ny > -0.1 && Ny < 1.1) ? 1 : 0;

				// Novellus orbite à 418 km de la Terre : au plan système, les deux
				// tombent sur le MÊME pixel (« DAYRTHLUS », vu en capture). Elle
				// passe par la MÊME règle que les lunes — c'est le même problème, il
				// ne doit pas en exister deux versions qui divergeront.
				if (It.on_screen &&
				    !SeparableDuParent(S, N, static_cast<int>(Body::EarthBary), Nx, Ny))
					It.on_screen = 0;
			}
			++N;
		}
	}
	S.n = N;
}

void USPSolarSystemSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	auto& Bridge = fen::app::g_render_bridge;
	// La carte sert AUSSI de décor au menu (ciel étoilé + orbites ténues,
	// cf. ref_menu.png) : elle s'active donc dans les deux cas, la scène Titre
	// n'étant qu'une version en retrait de la même vue.
	// ═══ LE MONDE REND AUSSI À BORD ═══ (2026-07-27)
	// Il ne rendait qu'au plan système et au menu : à bord, on était enfermé dans
	// une boîte noire — la Terre n'existait pas par les hublots, et l'attitude
	// « cupola au nadir » n'y avait donc aucun témoin. C'était le dernier endroit
	// où Novellus restait un MONDE À PART [GDD v1.2 17.3, ch.18].
	// À bord, la caméra reste celle du PAWN : la carte rend, mais n'a pas l'œil.
	const bool bMonde =
		Bridge.scene.load() == static_cast<int>(fen::app::SceneJeu::Monde);
	bBord = bMonde && !Bridge.carte3d_active.load();
	const bool bActive = Bridge.carte3d_active.load() || Bridge.menu_backdrop.load() || bBord;

	if (bActive && !bBuilt) BuildScene();
	// Novellus vu de près : on charge son modèle extérieur DÈS que la carte est
	// active (pas à l'approche), pour que ses shaders soient chauds au moment où
	// le LOD le montre. Reste caché tant qu'on n'est pas assez proche. bBuilt =>
	// MapActor existe (ExtRoot s'y rattache).
	if (bActive && bBuilt && !bExtBuilt) BuildExteriorStation();
	if (bActive != bWasActive) { SetMapActive(bActive); bWasActive = bActive; }
	const bool bEyeOnMap = bActive && !bBord;
	if (bEyeOnMap != bWasEyeOnMap) { SetMapHasEye(bEyeOnMap); bWasEyeOnMap = bEyeOnMap; }
	if (!bActive || !bBuilt) return;

	const double Epoch = Bridge.epoch_tdb.load();

	// L'ATTITUDE DE NOVELLUS, POSÉE EN TÊTE DE FRAME. Elle est lue par tout ce qui
	// suit — la caméra du handoff juste en dessous, le modèle extérieur dans
	// UpdateScene, et la géométrie intérieure via GetNovellusAttitude — et ces
	// consommateurs se relaient à l'écran : les faire lire des frames différentes
	// ferait pivoter la station à la bascule. Le jeu la publie, on la convertit.
	NovellusAttitude = StationAttitude(Bridge.station);
	// LE REPÈRE DE RENDU (voir GetRenderRot) : celui de la STATION à bord, où la
	// géométrie intérieure reste canonique et où c'est donc le monde qui tourne ;
	// l'inertiel partout ailleurs.
	RenderRot = bBord ? NovellusAttitude.Inverse() : FQuat::Identity;

	// ═══ LE VOL VERS UN CORPS [plan §6.4] ═══
	// Cliquer un corps le focalise ET demande un nouveau cadrage. Si l'œil s'y
	// téléportait, on ne comprendrait pas où l'on va : on approche donc la
	// distance de vue en douceur. L'interpolation est LOGARITHMIQUE — du rayon
	// d'une planète à la ceinture de Kuiper il y a neuf ordres de grandeur, et
	// un lissage linéaire passerait presque tout le trajet sans rien changer de
	// visible. Constante de temps unique : un pas de molette paraît immédiat,
	// un changement de focus devient un vol.
	//
	// PLANCHER : 1 mm. À l'échelle réelle, le zoom descend jusqu'à l'INTÉRIEUR de
	// Novellus (l'amarrage du handoff est à ~20 m) — un plancher à 1 km, hérité de
	// l'avant-échelle-réelle, aurait bloqué la caméra à 50 fois trop loin.
	constexpr double DIST_MIN_KM = 1.0e-6;
	// VOL SCRIPTÉ (`Session::vol_cam`) : la pose publiée EST déjà une trajectoire
	// lissée. La re-lisser la retarderait, et le vol [M] n'arriverait pas sur l'œil
	// du pawn — la coupure du handoff reviendrait, de plusieurs mètres.
	const bool bVolScripte = Bridge.cam.vol_camera.load();
	{
		const double Cible = FMath::Max(DIST_MIN_KM, Bridge.cam.dist_km.load());
		if (bVolScripte || SmoothDistKm <= 0.0) SmoothDistKm = Cible;
		else
		{
			constexpr double TAU = 0.35;                       // s
			const double K = 1.0 - FMath::Exp(-DeltaTime / TAU);
			const double L = FMath::Loge(SmoothDistKm);
			SmoothDistKm = FMath::Exp(L + (FMath::Loge(Cible) - L) * K);
		}
	}

	// --- la caméra : commandée par le HUD, appliquée ici ---------------------
	// Le focus « vole » vers sa cible (façon NASA Eyes) : on lisse le point visé,
	// puis on colle dès que l'écart est négligeable devant la distance de vue —
	// sinon le suivi d'un corps en mouvement traînerait indéfiniment.
	const int FocusId = Bridge.focus_body.load();
	const FVector TargetFocus = FocusWorldKm(Epoch);
	if (!bFocusPrimed || bVolScripte) { SmoothFocusKm = TargetFocus; bFocusPrimed = true; }
	else
	{
		// SUIVI SANS RETARD D'UNE CIBLE EN MOUVEMENT [GDD 14.2 : le temps coule].
		// On ADVECTE d'abord le point lissé du déplacement PROPRE de la cible, puis
		// on n'amortit que l'écart restant — c'est-à-dire uniquement celui d'un
		// CHANGEMENT de focus, la seule chose que ce lissage doit adoucir.
		// Sans cela, un amortisseur de premier ordre garde un retard permanent
		// v/vitesse ≈ 29,8/3,5 ≈ 8,5 km sur la Terre : invisible sur une planète
		// cadrée à 38 000 km, mais Novellus cadrée à 1 km sortait de l'écran.
		// L'advection ne s'applique QUE si la cible est la même qu'à la frame
		// précédente : sur un changement de focus, l'écart EST le vol à lisser.
		if (FocusId == LastFocusId) SmoothFocusKm += TargetFocus - LastTargetFocusKm;
		const double Gap = FVector::Dist(SmoothFocusKm, TargetFocus);
		if (Gap < SmoothDistKm * 1.0e-4) SmoothFocusKm = TargetFocus;
		else SmoothFocusKm = FMath::VInterpTo(SmoothFocusKm, TargetFocus, DeltaTime, 3.5f);
	}
	LastFocusId = FocusId;
	LastTargetFocusKm = TargetFocus;

	const double DistKm = FMath::Max(DIST_MIN_KM, SmoothDistKm);
	// ORIENTATION : cible publiée par l'entrée, suivie en douceur (voir le .h).
	const double YawCible = Bridge.cam.yaw.load();
	const double PitchCible = FMath::Clamp(Bridge.cam.pitch.load(), -1.55, 1.55);
	if (!bOrientPrimed || Bridge.cam.vol_camera.load())
	{
		SmoothYaw = YawCible;
		SmoothPitch = PitchCible;
		bOrientPrimed = true;
	}
	else
	{
		// Le yaw se rattrape par le PLUS COURT CHEMIN : sans repli dans ±π, passer
		// par 0 faisait faire un tour complet à la caméra.
		double d = YawCible - SmoothYaw;
		while (d >  UE_DOUBLE_PI) d -= UE_DOUBLE_TWO_PI;
		while (d < -UE_DOUBLE_PI) d += UE_DOUBLE_TWO_PI;
		const double a = 1.0 - FMath::Exp(-DeltaTime / 0.09);   // τ = 90 ms : vif, pas mou
		SmoothYaw += d * a;
		SmoothPitch += (PitchCible - SmoothPitch) * a;
	}
	const double Yaw = SmoothYaw;
	const double Pitch = FMath::Clamp(SmoothPitch, -1.55, 1.55);
	double FovDeg = FMath::Clamp(Bridge.cam.fov_deg.load(), 10.0, 100.0);
	const FVector Offset(DistKm * FMath::Cos(Pitch) * FMath::Cos(Yaw),
	                     DistKm * FMath::Cos(Pitch) * FMath::Sin(Yaw),
	                     DistKm * FMath::Sin(Pitch));
	FVector CamWorldKm = SmoothFocusKm + Offset;
	// la caméra regarde le point visé : depuis l'origine de rendu, c'est −Offset.
	FRotator CamRot = (-Offset).Rotation();

	// ═══ À BORD, L'ŒIL EST CELUI DU PAWN ═══
	// Tout ce qui précède décrit l'orbite de la caméra de CARTE ; à bord elle n'a
	// pas l'œil, et le monde doit être rebasé sur la tête du joueur, sinon la Terre
	// serait rendue autour d'un point qui n'est plus le sien (parallaxe fausse de
	// plusieurs dizaines de mètres — mesurable à 418 km, et surtout la station
	// n'occulterait plus ce qu'elle doit occulter).
	// L'œil est publié par SPStation en repère STATION (m) ; on repasse en repère
	// du modèle (miroir en y, ×100 = u), puis dans le monde par l'ATTITUDE — c'est
	// exactement la composition que fait `Session::pose_bord` pour l'amarrage, et
	// c'est ce qui rend les deux descriptions superposables à la reprise.
	if (bBord)
	{
		const auto& St = Bridge.station;
		const FBodyDef* Terre = FindDef(Body::EarthBary);
		const FVector TerrePos = Terre ? BodyWorldKm(*Terre, Epoch) : FVector::ZeroVector;
		const FVector NovWorldKm = TerrePos + EclToUeKmd(St.rel_m[0], St.rel_m[1], St.rel_m[2]);
		FVector OeilUU = FVector::ZeroVector;
		if (Bridge.station_out.ready.load())
			OeilUU = FVector( Bridge.station_out.eye_m[0].load() * 100.0,
			                 -Bridge.station_out.eye_m[1].load() * 100.0,
			                  Bridge.station_out.eye_m[2].load() * 100.0);
		CamWorldKm = NovWorldKm + NovellusAttitude.RotateVector(OeilUU) / UU_PER_KM;
	}

	// HANDOFF VERS L'AMBULATION (incr. 3c-3) [GDD v1.2 17.4] : sur la dernière
	// portion du vol [M], l'ORIENTATION et le CHAMP DE VISION glissent de ceux de
	// la carte vers ceux DU PAWN (publiés par SPStation dans `station_out`). À
	// l'arrivée les deux caméras sont CONFONDUES — position (Session::pose_bord),
	// orientation et champ — donc la reprise en 1re personne ne se voit pas.
	//
	// Le champ compte autant que le reste : la carte cadre à 45°, la caméra de
	// bord à 90°, soit un saut de grossissement de tan(45°)/tan(22,5°) = 2,4.
	// Vérifié par capture : sans cette convergence, `-sphandoff` rendait la même
	// cloison que `-spscene=iss`, mais 2,4 fois plus grosse.
	// L'interpolation est en TANGENTE de demi-champ, la grandeur linéaire à
	// l'écran — interpoler les degrés ferait « respirer » le zoom en milieu de
	// course.
	{
		const double Melange = FMath::Clamp(Bridge.cam.look_to_bord.load(), 0.0, 1.0);
		if (Melange > 0.0 && Bridge.station_out.ready.load())
		{
			// LE REGARD DU PAWN EST PUBLIÉ DANS LE REPÈRE DU MODÈLE, pas dans le monde :
			// à bord, la station rend dans son repère canonique (celui où le joueur
			// marche et où vit la collision des 310 corps). Pendant la coexistence elle
			// est posée TOURNÉE de son attitude — la caméra doit donc composer la MÊME
			// rotation, faute de quoi elle regarderait une cloison qui n'est plus là.
			// C'est cette composition qui garde le handoff invisible : caméra et
			// géométrie subissent la même rotation rigide, donc l'image est inchangée.
			const FRotator LookBord =
				(NovellusAttitude * FRotator(Bridge.station_out.pitch.load(),
				                             Bridge.station_out.yaw.load(), 0.0).Quaternion())
					.Rotator();
			// FMath::Lerp(FRotator,...) normalise le delta : passage par le plus
			// court chemin, jamais un tour complet.
			CamRot = FMath::Lerp(CamRot, LookBord, static_cast<float>(Melange));

			const double FovBord =
				FMath::Clamp(static_cast<double>(Bridge.station_out.fov_deg.load()), 10.0, 160.0);
			const double TanA = FMath::Tan(FMath::DegreesToRadians(FovDeg) * 0.5);
			const double TanB = FMath::Tan(FMath::DegreesToRadians(FovBord) * 0.5);
			const double Tan = TanA + (TanB - TanA) * Melange;
			FovDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(Tan));
		}
	}

	// PLAN DE CLIPPING PROCHE : celui du MOTEUR (`UEngine::NearClipPlane` = 10 u,
	// soit 10 cm), le même de bout en bout du zoom.
	//
	// Il y avait ici un near-clip « adaptatif » (~1/2000 de la distance de vue)
	// posé via `IConsoleManager::FindConsoleVariable("r.SetNearClipPlane")`.
	// Ce code N'A JAMAIS RIEN FAIT : `r.SetNearClipPlane` est un FAutoConsoleCommand
	// (UnrealEngine.cpp), pas une variable ; `FindConsoleVariable` appelle
	// `AsVariable()` sur l'objet trouvé et rend donc nullptr. Tout ce qui a été
	// vérifié en capture l'a été avec le plan par défaut — voir piège n°28.
	// On garde ce défaut, et c'est le bon choix : la profondeur d'UE5 est en
	// reversed-Z flottant (précision maximale PRÈS de l'œil, où sont justement les
	// seuls objets géométriques — les corps lointains sont des marqueurs HUD), et
	// surtout l'intérieur ambulable rend avec CE plan : un plan adaptatif rendrait
	// le handoff visible, puisque les deux côtés de la reprise ne clipperaient pas
	// au même endroit.

	UpdateScene(Epoch, CamWorldKm);

	// À BORD, RIEN DE CE QUI SUIT : la caméra est celle du pawn (y toucher volerait
	// la vue au joueur), les traits d'orbite n'ont pas de sens tendus en travers
	// d'un module, et les marqueurs/labels de la carte non plus — on est DANS le
	// monde, on ne le survole pas. Ne restent que les corps et le ciel.
	if (bBord) return;

	if (MapCamera)
	{
		MapCamera->SetActorLocationAndRotation(FVector::ZeroVector, CamRot);
		if (UCameraComponent* Cam = MapCamera->GetCameraComponent())
		{
			// ═══ PIÈGE PAYÉ (2026-07-27) : « 45° » N'ÉTAIT PAS 45° ═══
			// Le moteur ne prend PAS le champ posé pour un champ horizontal sur le
			// viewport courant. `AspectRatioAxisConstraint` vaut MaintainYFOV pour
			// tout projet (BaseEngine.ini), et dans ce mode
			// `FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle`
			// (Camera/CameraStackTypes.cpp:324) convertit d'abord le champ posé en
			// champ VERTICAL avec l'aspect DE LA CAMÉRA :
			//     halfY = atan( tan(halfFOV) / UCameraComponent::AspectRatio )
			// — cet aspect valant 16:9 par défaut, quelle que soit la fenêtre — puis
			// maintient CE champ vertical. Résultat : le champ horizontal rendu suit
			// la largeur de la fenêtre, et ne vaut les 45° demandés QUE si la fenêtre
			// est exactement en 16:9.
			// Le HUD, lui, projette les marqueurs avec « 45° horizontal sur l'aspect
			// réel ». Les deux ne coïncidaient donc qu'en 16:9 : mes captures de
			// contrôle étaient en 1600x900 et sortaient au pixel près, pendant qu'un
			// viewport PIE de 1600x975 rendait 8,3 % plus zoomé — d'où des marqueurs
			// qui s'écartent de leur tracé d'autant plus qu'ils sont loin du centre,
			// et une orbite lunaire « décalée ». Mesuré : nous TanH=0,414214 contre
			// 0,382351 pour le moteur, soit exactement 975/900.
			// EN POSANT L'ASPECT DE LA CAMÉRA = celui du viewport, les deux branches
			// du moteur (MaintainY comme MaintainX) redonnent EXACTEMENT
			// tanH = tan(champ/2) et tanV = tanH/aspect : la convention du HUD
			// devient la vérité du rendu, quelle que soit la forme de la fenêtre.
			Cam->SetAspectRatio(static_cast<float>(ViewportAspect(GetWorld())));
			Cam->SetFieldOfView(static_cast<float>(FovDeg));
		}

		// RÉ-ASSURE LA CIBLE DE VUE CHAQUE FRAME. SetMapActive ne la pose qu'à la
		// TRANSITION ; or en PIE il n'y a pas de pawn par défaut (DefaultPawnClass =
		// nullptr), et le PlayerController peut reprendre la vue sur un défaut (lui-
		// même : mauvaise rotation/FOV/near). Le rendu se fait alors via une caméra
		// aberrante -> système solaire minuscule et décalé + dôme de ciel plein écran
		// (« nébuleuse »), alors que le HUD, lui, projette bien sur MapCamera (labels
		// au bon endroit). Symptôme visible en PIE, pas en -game (timing différent).
		if (UWorld* W = GetWorld())
			if (APlayerController* PC = W->GetFirstPlayerController())
				if (PC->GetViewTarget() != MapCamera)
				{
					if (!bViewTargetReasserted)
					{
						UE_LOG(LogTemp, Warning,
						       TEXT("[SPSolarSystem] vue rendue via '%s' (pas MapCamera) -> ré-assignation. "
						            "PIE sans pawn : la carte impose sa caméra chaque frame."),
						       *GetNameSafe(PC->GetViewTarget()));
						bViewTargetReasserted = true;
					}
					PC->SetViewTarget(MapCamera);
				}
	}

	// Orbites : l'échantillonnage (éphéméride) est mis en cache et ne se refait
	// qu'en cas de dérive d'époque ou de changement d'option ; l'ÉMISSION, elle,
	// a lieu chaque frame — les traits vivent dans le repère rebasé sur l'œil.
	if (FMath::Abs(Epoch - LastOrbitEpoch) > ORBIT_REDRAW_DAYS * 86400.0 ||
	    OrbitCache.Num() == 0)
	{
		RebuildOrbitCache(Epoch);
		LastOrbitEpoch = Epoch;
	}
	EmitOrbits(CamWorldKm, DistKm, Epoch);

	PublishScreen(CamWorldKm, CamRot, FovDeg);
}

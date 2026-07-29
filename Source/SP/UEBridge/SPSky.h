// SPSky.h — LE FOND ÉTOILÉ (Voie lactée), rendu par UE.
//
// La référence a des étoiles PARTOUT : derrière le menu, par les hublots de
// l'ISS et autour de la carte (ref_menu.png, ref_iss.png, ref_systeme.png).
// C'est donc un objet de monde, indépendant de la scène courante, et pas un
// fond peint par le HUD — c'était précisément l'erreur du portage ImGui.
//
// GÉOMÉTRIE : une sphère de 1e8 unités de rayon, centrée à l'origine, à
// l'ENVERS (échelle négative -> on en voit l'intérieur). Les deux scènes
// gardent la caméra au voisinage de l'origine — la carte est rebasée sur l'œil,
// la station tient dans ~5 000 unités — donc aucun recentrage n'est nécessaire.
// Tous les objets rendus (compression de profondeur comprise) restent à moins
// de ~4e6 unités : la sphère les englobe tous.
//
// TEXTURE : /Game/SP/T_Starfield si elle a été importée, sinon décodage direct
// de Space Program/assets/textures/8k_stars_milky_way.jpg au lancement — le
// projet reste jouable sans passe d'import.
//
// ═══ POURQUOI LE CIEL EST EN DEUX COUCHES ═══ (2026-07-27)
//   . la VOÛTE porte `8k_stars_milky_way.jpg`, la carte 8192x4096 du dépôt. Le
//     dépôt en livre une seconde, `8k_stars.jpg` : vérifié pixel par pixel, c'est
//     LE MÊME champ d'étoiles, aux mêmes positions, simplement SANS la nébulosité
//     (luminance moyenne 0,22 contre 1,22, même proportion de pixels brillants).
//     Elle n'apporte donc aucun contenu que la première n'ait déjà. Essayée en
//     seconde couche additive puis RETIRÉE : mesuré au pixel, l'écart était nul
//     (les étoiles vives sont déjà proches de la saturation et le tonemapper
//     écrase ce qu'on empile au-dessus) ;
//   . par-dessus, un champ d'ÉTOILES PONCTUELLES, comme le faisait le moteur
//     Vulkan (pipeline `Star`, POINT_LIST, `make_starfield`,
//     render/shaders/star.vert) et comme le fait Eyes on the Solar System. Un
//     point a sa taille donnée À L'ÉCRAN : il est net par construction, à tout
//     zoom, là où une texture finit toujours par être agrandie.
//
// LE « FOND PIXELLISÉ » N'ÉTAIT PAS UN PROBLÈME DE GÉOMÉTRIE (piège payé, même
// jour). On a d'abord cru à l'argument d'échantillonnage — 8192 px sur 360° font
// 23 px/degré quand l'écran en affiche ~36 à 45° de champ, donc un agrandissement
// de 1,5x. Le calcul est juste et n'expliquait RIEN : ce n'était pas 8192 px qui
// s'affichaient mais 32. Voir ChargerTextureEtoiles.
//
// ═══ CE QUE LA VOÛTE MONTRE, ET DANS QUEL REPÈRE ═══ (mesuré le 2026-07-28)
//
// ~~la sphère est retournée par une échelle négative, le ciel est vu en MIROIR~~
// **CORRIGÉ** : l'échelle est positive (voir `BuildSky`). Le défaut n'était
// d'ailleurs pas un miroir mais une INVERSION CENTRALE — tout le ciel à son
// antipode. Invisible parce qu'un grand cercle (la Voie lactée) est invariant
// par cette inversion : seule sa garniture était retournée.
//
// LE REPÈRE DE LA CARTE EST **GALACTIQUE**, et c'est une MESURE, pas une
// supposition : le centroïde des 1 % de pixels les plus brillants de
// `8k_stars_milky_way.jpg` (8192x4096) tombe à **U = 0,5133, V = 0,4962**.
// C'est le V qui tranche — en galactique la bande suit exactement b = 0, donc
// V = 0,5 ; en équatorial elle balaierait V de 0,16 à 0,84 (grand cercle incliné
// de 62,9°) et son centroïde serait tiré vers le bulbe, à V = 0,661.
//
// ═══ CE QUE TROIS MESURES ONT DIT, ET POURQUOI ON S'ARRÊTE LÀ ═══
//
// La convention UV du maillage n'est PAS un obstacle : `Tools/diag_body_uv.py`
// l'a mesurée sur l'asset livré le 2026-07-27 —
//     U = 0,5 + lon/360   V = (90 − lat)/180   (−X = lon 0, +Z = pôle nord)
// Restait UN bit : le SENS de la longitude galactique dans la texture. Se
// tromper mirore le ciel en longitude, soit exactement la faute qu'on vient de
// corriger — on ne pouvait donc pas le deviner.
//
// TROIS MESURES, DEUX INSTRUMENTS, AUCUNE RÉPONSE (diagnostic archivé sous
// `#if 0` dans SPSky.cpp) :
//   1. brillance absolue hors plan (V > 0,60) -> maxima à U ≈ 0,50 : le HALO DU
//      BULBE, pas les Nuages ;
//   2. fenêtre resserrée (b de −29° à −54°)   -> encore U ≈ 0,50, luminance 2-4
//      sur 255. *Deux mesures qui donnent la même mauvaise réponse accusent
//      l'instrument, pas la fenêtre* ;
//   3. CONTRASTE LOCAL (case moins couronne), le bon détecteur pour une tache
//      compacte sur fond lisse -> trois candidats à contraste 1,58 / 1,65 / 1,67
//      sur 255, indiscernables entre eux, et AUCUN aux positions attendues
//      (Grand Nuage V = 0,683, Petit Nuage V = 0,746).
//
// CONCLUSION, ET ELLE VAUT MIEUX QUE LA MESURE MANQUÉE : les Nuages de Magellan
// ne sont pas détectables dans cette image. Joint au centroïde suspectement
// symétrique (U = 0,5133, V = 0,4962), cela dit que `8k_stars_milky_way.jpg` est
// un panorama STYLISÉ, pas une carte photométrique du ciel. **Le caler sur J2000
// serait une fausse précision** — habiller un décor en instrument, ce que
// [GDD 12.5 / 19.6] refuse explicitement : « une approximation identifiée est
// autorisée ; une approximation déguisée en certitude ne l'est pas. »
//
// LE VRAI REMÈDE EST AILLEURS, et le §6 point 10 le disait déjà : des étoiles en
// POINTS depuis un CATALOGUE réel (Hipparcos). Les points sont nets à tout zoom
// (taille donnée à l'écran), portent de vraies constellations, et un catalogue
// EST daté en J2000 — l'alignement devient alors exact par construction au lieu
// d'être ajusté sur une image. La voûte resterait ce qu'elle est : une nébulosité
// de fond. Les étoiles ponctuelles actuelles sont PROCÉDURALES (graine fixe).
//
// D'ici là, DÉCLARÉ [GDD 6.8] : la voûte n'est pas orientée sur le ciel réel.
// C'est un décor — aucune mesure du jeu n'en dépend.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SPSky.generated.h"

class UStaticMeshComponent;
class ULineBatchComponent;

UCLASS(NotBlueprintable)
class SP_API ASPSkyActor : public AActor
{
	GENERATED_BODY()

public:
	ASPSkyActor();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Dome;
	// Les étoiles ponctuelles. Le batcher de lignes sait dessiner des POINTS de
	// taille constante À L'ÉCRAN (`FBatchedElements::DrawPointElements` construit
	// le quad à partir de la taille en pixels et du w du sommet) : c'est
	// exactement le `gl_PointSize` du nuanceur de la référence, sans avoir à
	// créer ni matériau ni maillage. Elles sont posées UNE FOIS — l'œil étant
	// toujours à l'origine du rendu, le champ n'a aucune raison de bouger.
	UPROPERTY() TObjectPtr<ULineBatchComponent> Stars;
};

UCLASS()
class SP_API USPSkySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

private:
	void BuildSky();
	void BuildStars();                  // tire le champ (une fois) puis l'émet
	// (ré)émet les points pour ce champ de vision ET ce repère de rendu. Le repère
	// n'est l'identité qu'au plan système : À BORD le monde est rendu dans le
	// repère de la STATION (cf. `USPSolarSystemSubsystem::GetRenderRot`), et un
	// ciel qui ne suivrait pas serait COLLÉ au hublot — le contraire même de ce
	// qu'on doit voir, puisque Novellus fait un tour sur elle-même par orbite.
	void EmitStars(float FovDeg, const FQuat& Rot);
	// Une carte du ciel : l'asset importé s'il existe, sinon le JPEG du dépôt.
	UTexture2D* ChargerCarteCiel(const TCHAR* CheminAsset, const TCHAR* Fichier);

	// Une étoile : direction sur la sphère céleste, couleur déjà prémultipliée par
	// sa brillance, et taille VOULUE EN PIXELS. La taille passée au batcher n'est
	// pas celle-là (voir EmitStars) : elle dépend du champ de vision.
	struct FStar
	{
		FVector      Dir;
		FLinearColor Col;
		float        Px = 1.0f;
	};
	TArray<FStar> StarTable;
	float LastStarFov = -1.0f;          // champ pour lequel les points sont posés
	// Repère pour lequel ils sont posés. Les POINTS ne peuvent pas suivre l'acteur :
	// `FLineBatcherSceneProxy` dessine `Point.Position` telle quelle, sans la
	// matrice du composant (LineBatchComponent.cpp) — vérifié dans la source du
	// moteur, pas supposé. Il faut donc les RÉÉMETTRE, ce qui coûte 6 000 points :
	// on ne le fait qu'au-delà d'un seuil angulaire (voir Tick), pas chaque frame.
	FQuat LastStarRot = FQuat::Identity;

	UPROPERTY() TObjectPtr<ASPSkyActor> SkyActor;
	bool bBuilt = false;
	int32 DiagTick = 0;
};

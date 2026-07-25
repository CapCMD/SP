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
// APPROXIMATION DÉCLARÉE [GDD 6.8] : la sphère est retournée par une échelle
// négative uniforme, donc le ciel est vu en MIROIR, et son orientation n'est
// pas calée sur le repère équatorial J2000. C'est un décor, pas une carte du
// ciel : aucune mesure du jeu n'en dépend. À lever avec la passe IAU.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SPSky.generated.h"

class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class SP_API ASPSkyActor : public AActor
{
	GENERATED_BODY()

public:
	ASPSkyActor();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Dome;
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
	UTexture2D* ChargerTextureEtoiles();

	UPROPERTY() TObjectPtr<ASPSkyActor> SkyActor;
	bool bBuilt = false;
	int32 DiagTick = 0;
};

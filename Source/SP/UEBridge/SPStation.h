// SPStation.h — L'ACCUEIL DU JEU : l'intérieur de l'ISS (module Novellus).
//
// C'est ICI qu'on arrive quand une partie démarre — pas sur la carte. Le joueur
// est en PREMIÈRE PERSONNE à bord, se déplace en apesanteur dans la station,
// s'approche d'un poste et l'ouvre. La carte du système solaire est un MODE
// qu'on atteint depuis l'ISS (touche M). Référence visuelle :
// Space Program/docs/reference_solar_system_map/ref_iss.png
//
// Le modèle GLB s'importe en ~310 StaticMesh SANS hiérarchie d'acteurs, mais
// avec les transformations de nœuds CUITES dans les sommets (vérifié par
// Tools/diag_iss_bounds.py) : ils vivent donc dans un repère commun et un SEUL
// acteur porte tout, exactement comme le jeu de référence qui applique une
// transformation unique à tous ses sous-maillages.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/WorldSubsystem.h"
#include "SPStation.generated.h"

class UStaticMeshComponent;
class UCameraComponent;
class UCapsuleComponent;
class UFloatingPawnMovement;
class UPointLightComponent;

// L'acteur porteur de la géométrie de la station (un composant par mesh).
UCLASS(NotBlueprintable)
class SP_API ASPStationActor : public AActor
{
	GENERATED_BODY()

public:
	ASPStationActor();
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
};

// Le joueur à bord : capsule + caméra. Apesanteur -> vol libre, mais la
// géométrie de la station est SOLIDE (collision sur les triangles réels).
UCLASS(NotBlueprintable)
class SP_API ASPStationPawn : public APawn
{
	GENERATED_BODY()

public:
	ASPStationPawn();
	UPROPERTY() TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<UFloatingPawnMovement> Movement;
};

UCLASS()
class SP_API USPStationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

private:
	void BuildScene();                 // assemble les meshes + éclairage
	// Les positions de postes viennent du C++ pur ; UE possède la vraie collision
	// et les VÉRIFIE (un poste dans une cloison serait injoignable en silence).
	void VerifierPostes();
	// UN PAS D'IMPESANTEUR. La loi est en C++ pur et sous oracle
	// (`app/impesanteur.hpp`) ; ce qui se passe ici est ce qu'UE seul peut faire :
	// MESURER s'il y a une paroi à portée de bras, et POUSSER la capsule à travers
	// la géométrie (balayage + glissement du moteur).
	void AvancerEnImpesanteur(float DeltaTime, const FRotator& Look);
	void SetStationVisible(bool bVisible);     // la géométrie rend-elle ?
	void SetStationInControl(bool bControl);   // le joueur est-il aux commandes ?
	// HANDOFF (incr. 3c-3) : hors du repère canonique, la station est rebasée sur
	// la POSE de rendu de Novellus (position ET attitude, que la carte publie),
	// pour coexister avec le plan système pendant le vol [M].
	void AppliquerPose(bool bCoexiste);

	UPROPERTY() TObjectPtr<ASPStationActor> StationActor;
	UPROPERTY() TObjectPtr<ASPStationPawn> Pawn;
	UPROPERTY() TObjectPtr<AActor> PreviousViewTarget;
	// Plafonniers du couloir : sans eux la station est noire (l'émissif du
	// modèle n'éclaire pas les autres surfaces). Portés par un acteur unique, pour
	// qu'un seul déplacement les emmène tous.
	UPROPERTY() TObjectPtr<AActor> LightsHolder;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> StationLights;

	bool bBuilt = false;
	bool bWasVisible = false;
	bool bWasInControl = false;
	double Yaw = 0.0, Pitch = 0.0;
	// LA VITESSE DU JOUEUR (u/s, repère de la station). Elle vit ICI et non dans le
	// composant de mouvement, parce que c'est nous qui l'intégrons : en impesanteur
	// elle PERSISTE d'une frame à l'autre sans entrée — c'est toute la sensation, et
	// c'est exactement ce que `UFloatingPawnMovement` annulait.
	FVector Vitesse = FVector::ZeroVector;

	// Repère CANONIQUE (celui où le pawn marche et où vit la collision) et pose
	// courante appliquée par-dessus : un point p du modèle va en
	// `Decalage + Attitude·p`. Identité en dehors de la coexistence — c'est le
	// repère canonique lui-même.
	FVector CanonStationLoc = FVector::ZeroVector;
	FVector CanonLightsLoc = FVector::ZeroVector;
	FVector CanonPawnLoc = FVector::ZeroVector;
	FVector Decalage = FVector::ZeroVector;
	// L'attitude LVLH de Novellus (cupola au nadir). Lue à la carte, jamais
	// recalculée : le modèle extérieur et l'intérieur se relaient à la traversée
	// de la coque et doivent porter la MÊME.
	FQuat Attitude = FQuat::Identity;
};

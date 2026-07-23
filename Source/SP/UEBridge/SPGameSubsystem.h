// SPGameSubsystem.h - injecte SPACE PROGRAM dans le viewport de jeu.
// Des qu'un monde de jeu demarre (PIE ou -game), le widget ImGui est pose en
// overlay plein ecran : le jeu apparait, identique a fenetre_jeu.exe.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SPGameSubsystem.generated.h"

class SSpaceProgramWidget;

UCLASS()
class SP_API USPGameSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	TSharedPtr<SSpaceProgramWidget> Overlay;
};

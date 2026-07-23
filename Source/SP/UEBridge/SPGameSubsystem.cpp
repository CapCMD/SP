// SPGameSubsystem.cpp
#include "SPGameSubsystem.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "SPImGuiOverlay.h"

bool USPGameSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) return false;
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

void USPGameSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UGameViewportClient* Viewport = InWorld.GetGameViewport();
	if (!Viewport) return;

	SAssignNew(Overlay, SSpaceProgramWidget).World(&InWorld);
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 1000);

	// souris visible, entrees vers le widget (le jeu est 100 % UI)
	if (APlayerController* PC = InWorld.GetFirstPlayerController())
	{
		PC->bShowMouseCursor = true;
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Overlay);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Overlay);
	}
}

void USPGameSubsystem::Deinitialize()
{
	if (Overlay.IsValid())
	{
		if (const UWorld* W = GetWorld())
		{
			if (UGameViewportClient* Viewport = W->GetGameViewport())
			{
				Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
			}
		}
		Overlay.Reset();
	}
	Super::Deinitialize();
}

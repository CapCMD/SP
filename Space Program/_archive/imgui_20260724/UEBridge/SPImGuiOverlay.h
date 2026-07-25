// SPImGuiOverlay.h - le widget Slate qui heberge SPACE PROGRAM dans UE5.
// Remplace ui/jeu_main.cpp (GLFW + OpenGL) : entrees Slate -> ImGui, rendu
// ImDrawData -> FSlateDrawElement. Le jeu (fen::ui::Interface) est inchange.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Texture2D.h"

struct FSPGameState;   // pimpl : contextes ImGui/ImPlot + fen::ui::Interface (defini dans le .cpp)

class SSpaceProgramWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpaceProgramWidget) : _World(nullptr) {}
		SLATE_ARGUMENT(UWorld*, World)
	SLATE_END_ARGS()

	SSpaceProgramWidget();               // hors-ligne : FSPGameState est incomplet ici (pimpl)
	void Construct(const FArguments& InArgs);
	virtual ~SSpaceProgramWidget() override;

	// --- boucle de jeu : Tick = NewFrame + dessiner + Render ; OnPaint = ImDrawData -> Slate
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	                      FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	                      bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1360.0, 880.0); }

	// --- entrees
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void   OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;

private:
	void RebuildFonts(float Scale);          // reconstruit police + atlas (equivalent appliquer_echelle)
	void ApplyDisplayRequests();             // resolution / plein ecran demandes par l'ecran Reglages
	void SetImGuiCurrent() const;            // rend nos contextes ImGui/ImPlot courants
	FReply HandleMouseButton(const FPointerEvent& MouseEvent, bool bDown);

	TUniquePtr<FSPGameState> State;
	TStrongObjectPtr<UTexture2D> FontTexture;
	FSlateBrush FontBrush;
	TWeakObjectPtr<UWorld> World;
	bool bFrameReady = false;
	bool bCaptureArmed = false;   // capture headless (-spcapture) : scene ouverte
};

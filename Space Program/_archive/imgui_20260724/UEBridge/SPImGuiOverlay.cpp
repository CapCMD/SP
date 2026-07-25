// SPImGuiOverlay.cpp - SPACE PROGRAM dans une fenetre UE5 (Slate).
// Reprend fidelement ui/jeu_main.cpp : meme style, meme police (Segoe UI 17px
// sur-echantillonnee), meme fond (0.05,0.06,0.08), meme boucle dessiner(w,h,dt).
//
// ORDRE D'INCLUSION CRITIQUE : les headers du jeu AVANT tout header UE, sinon
// les macros UE (PI, check, ...) casseraient astro_core (cst::PI, etc.).
#include "imgui.h"
#include "implot.h"
#include "app/jeu.hpp"
#include "ui/jeu_ecrans.hpp"

#include <cstdio>
#include <string>

#include "SPImGuiOverlay.h"
#include "SPCapture.h"

#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "TextureResource.h"

// ---------------------------------------------------------------------------
// L'etat du jeu : contextes ImGui/ImPlot + l'interface du jeu, inchangee.
struct FSPGameState
{
	ImGuiContext*  Ctx = nullptr;
	ImPlotContext* PlotCtx = nullptr;
	fen::ui::Interface Uif;
	ImGuiStyle BaseStyle;              // style de reference avant mise a l'echelle
	std::string IniPath;               // imgui.ini -> Saved/ du projet
	std::string SavePath;              // agence.sauvegarde.txt -> Saved/ du projet
};

void SSpaceProgramWidget::SetImGuiCurrent() const
{
	if (State.IsValid())
	{
		ImGui::SetCurrentContext(State->Ctx);
		ImPlot::SetCurrentContext(State->PlotCtx);
	}
}

SSpaceProgramWidget::SSpaceProgramWidget() = default;

// ---------------------------------------------------------------------------
void SSpaceProgramWidget::Construct(const FArguments& InArgs)
{
	World = InArgs._World;
	State = MakeUnique<FSPGameState>();

	IMGUI_CHECKVERSION();
	State->Ctx = ImGui::CreateContext();
	ImGui::SetCurrentContext(State->Ctx);
	State->PlotCtx = ImPlot::CreateContext();
	ImPlot::SetCurrentContext(State->PlotCtx);

	ImGuiIO& io = ImGui::GetIO();
	io.BackendPlatformName = "unreal_slate";
	io.BackendRendererName = "unreal_slate";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

	// fichiers persistants dans Saved/ (l'exe d'origine ecrivait a cote du binaire)
	State->IniPath  = TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("imgui.ini")));
	State->SavePath = TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("agence.sauvegarde.txt")));
	io.IniFilename = State->IniPath.c_str();
	State->Uif.chemin_sauvegarde = State->SavePath;

	// meme style que jeu_main.cpp
	ImGui::StyleColorsDark();
	ImGui::GetStyle().WindowRounding = 2.0f;
	ImGui::GetStyle().FrameRounding  = 3.0f;
	State->BaseStyle = ImGui::GetStyle();

	// echelle par defaut deduite du DPI du moniteur (meme table que l'original)
	const float DpiScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(0.0f, 0.0f);
	{
		const float S[fen::ui::Interface::NB_SCALE] = {0.85f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
		int Best = 1; float BestDist = 1e9f;
		for (int i = 0; i < fen::ui::Interface::NB_SCALE; ++i)
		{
			const float D = FMath::Abs(S[i] - DpiScale);
			if (D < BestDist) { BestDist = D; Best = i; }
		}
		State->Uif.scale_choix = Best;
	}
	RebuildFonts(State->Uif.ui_scale());
}

SSpaceProgramWidget::~SSpaceProgramWidget()
{
	if (State.IsValid())
	{
		ImGui::SetCurrentContext(State->Ctx);
		ImPlot::SetCurrentContext(State->PlotCtx);
		ImPlot::DestroyContext(State->PlotCtx);
		ImGui::DestroyContext(State->Ctx);
		State.Reset();
	}
}

// ---------------------------------------------------------------------------
// Equivalent de appliquer_echelle() : vraie TTF systeme + sur-echantillonnage,
// puis atlas -> UTexture2D (a la place de la texture OpenGL).
void SSpaceProgramWidget::RebuildFonts(float Scale)
{
	SetImGuiCurrent();
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();

	ImFontConfig Cfg;
	Cfg.OversampleH = 2; Cfg.OversampleV = 2;
	bool bLoaded = false;
	const char* Ttf[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/tahoma.ttf",
	                     "C:/Windows/Fonts/arial.ttf"};
	for (const char* F : Ttf)
	{
		if (FILE* T = std::fopen(F, "rb"))
		{
			std::fclose(T);
			io.Fonts->AddFontFromFileTTF(F, 17.0f * Scale, &Cfg);
			bLoaded = true;
			break;
		}
	}
	if (!bLoaded) { Cfg.SizePixels = 16.0f * Scale; io.Fonts->AddFontDefault(&Cfg); }
	io.Fonts->Build();

	unsigned char* Pixels = nullptr; int W = 0, H = 0;
	io.Fonts->GetTexDataAsRGBA32(&Pixels, &W, &H);

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_R8G8B8A8);
	Tex->SRGB = false;
	Tex->Filter = TF_Bilinear;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels, (SIZE_T)W * H * 4);
	Mip.BulkData.Unlock();
	Tex->UpdateResource();

	FontTexture.Reset(Tex);
	FontBrush.SetResourceObject(Tex);
	FontBrush.ImageSize = FVector2D(W, H);
	FontBrush.DrawAs = ESlateBrushDrawType::Image;
	FontBrush.Tiling = ESlateBrushTileType::NoTile;
	io.Fonts->SetTexID((ImTextureID)&FontBrush);

	ImGui::GetStyle() = State->BaseStyle;
	ImGui::GetStyle().ScaleAllSizes(Scale);
}

// Reglages d'affichage : n'a de sens qu'en jeu autonome ; en PIE la fenetre
// appartient a l'editeur, on acquitte simplement la demande.
void SSpaceProgramWidget::ApplyDisplayRequests()
{
	fen::ui::Interface& Uif = State->Uif;
	Uif.appliquer_affichage = false;
	const UWorld* W = World.Get();
	if (!W || W->WorldType != EWorldType::Game) return;
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetScreenResolution(FIntPoint(Uif.res_w(), Uif.res_h()));
		Settings->SetFullscreenMode(Uif.plein_ecran ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		Settings->ApplySettings(false);
	}
}

// ---------------------------------------------------------------------------
void SSpaceProgramWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SetImGuiCurrent();
	fen::ui::Interface& Uif = State->Uif;

	if (Uif.appliquer_scale)     { Uif.appliquer_scale = false; RebuildFonts(Uif.ui_scale()); }
	if (Uif.appliquer_affichage) { ApplyDisplayRequests(); }

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)Size.X, (float)Size.Y);
	io.DeltaTime = FMath::Max(InDeltaTime, 1.e-4f);

	// CAPTURE HEADLESS (-spcapture) : ouvre d'emblee la scene demandee sur une
	// partie de test, exactement comme les drapeaux du binaire de reference.
	// Sans -spcapture, ce bloc ne fait rien.
	if (SPCapture::IsRequested() && !bCaptureArmed)
	{
		bCaptureArmed = true;
		const int Scene = SPCapture::RequestedScene();
		if (Scene >= 1)
		{
			Uif.jeu.creer_agence("CAPTURE", fen::app::ModeAide::Normal);
			Uif.ecran = (Scene == 2) ? fen::ui::Ecran::Carte3D : fen::ui::Ecran::Station;
		}
	}

	ImGui::NewFrame();
	Uif.dessiner((float)Size.X, (float)Size.Y, InDeltaTime);
	ImGui::Render();
	bFrameReady = true;
	SPCapture::Tick();

	if (Uif.quitter)
	{
		Uif.quitter = false;
		if (UWorld* W = World.Get())
		{
			UKismetSystemLibrary::QuitGame(W, W->GetFirstPlayerController(), EQuitPreference::Quit, false);
		}
	}
}

// ---------------------------------------------------------------------------
int32 SSpaceProgramWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                   const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                                   int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Le fond opaque (glClearColor(0.05,0.06,0.08) du jeu 2D d'origine) n'a de
	// sens que pour les écrans PUREMENT UI. Dès qu'une scène 3D est active —
	// l'intérieur de l'ISS ou la carte — ce rectangle plein écran MASQUERAIT
	// tout le monde UE : l'overlay doit alors être transparent.
	if (fen::app::g_render_bridge.scene.load() == static_cast<int>(fen::app::SceneJeu::Titre))
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
		                           FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
		                           FLinearColor(0.05f, 0.06f, 0.08f, 1.0f));
	}
	if (!bFrameReady || !State.IsValid()) return LayerId;

	SetImGuiCurrent();
	const ImDrawData* DrawData = ImGui::GetDrawData();
	if (!DrawData || DrawData->CmdListsCount == 0) return LayerId;

	const FSlateRenderTransform& RT = AllottedGeometry.ToPaintGeometry().GetAccumulatedRenderTransform();
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(FontBrush);
	const int32 Layer = LayerId + 1;

	TArray<FSlateVertex> Verts;
	TArray<SlateIndex>   Indexes;
	for (int L = 0; L < DrawData->CmdListsCount; ++L)
	{
		const ImDrawList* List = DrawData->CmdLists[L];

		Verts.Reset(List->VtxBuffer.Size);
		for (int V = 0; V < List->VtxBuffer.Size; ++V)
		{
			const ImDrawVert& Src = List->VtxBuffer.Data[V];
			const FColor Col((uint8)(Src.col & 0xFF), (uint8)((Src.col >> 8) & 0xFF),
			                 (uint8)((Src.col >> 16) & 0xFF), (uint8)((Src.col >> 24) & 0xFF));
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				RT, FVector2f(Src.pos.x, Src.pos.y), FVector2f(Src.uv.x, Src.uv.y), Col));
		}

		for (int C = 0; C < List->CmdBuffer.Size; ++C)
		{
			const ImDrawCmd& Cmd = List->CmdBuffer.Data[C];
			if (Cmd.UserCallback || Cmd.ElemCount == 0) continue;

			const FVector2f ClipMin = TransformPoint(RT, FVector2f(Cmd.ClipRect.x, Cmd.ClipRect.y));
			const FVector2f ClipMax = TransformPoint(RT, FVector2f(Cmd.ClipRect.z, Cmd.ClipRect.w));
			OutDrawElements.PushClip(FSlateClippingZone(FSlateRect(ClipMin.X, ClipMin.Y, ClipMax.X, ClipMax.Y)));

			Indexes.Reset((int32)Cmd.ElemCount);
			const ImDrawIdx* Base = List->IdxBuffer.Data + Cmd.IdxOffset;
			for (unsigned int I = 0; I < Cmd.ElemCount; ++I) Indexes.Add((SlateIndex)Base[I]);

			FSlateDrawElement::MakeCustomVerts(OutDrawElements, Layer, Handle, Verts, Indexes, nullptr, 0, 0);
			OutDrawElements.PopClip();
		}
	}
	return Layer;
}

// ---------------------------------------------------------------------------
// Entrees : Slate -> ImGui (API evenements 1.90).
static int ToImGuiMouseButton(const FKey& Key)
{
	if (Key == EKeys::LeftMouseButton)   return 0;
	if (Key == EKeys::RightMouseButton)  return 1;
	if (Key == EKeys::MiddleMouseButton) return 2;
	if (Key == EKeys::ThumbMouseButton)  return 3;
	if (Key == EKeys::ThumbMouseButton2) return 4;
	return -1;
}

static ImGuiKey ToImGuiKey(const FKey& Key)
{
	struct FPair { const FKey* K; ImGuiKey V; };
	static const TMap<FKey, ImGuiKey> Map = []()
	{
		TMap<FKey, ImGuiKey> M;
		M.Add(EKeys::A, ImGuiKey_A); M.Add(EKeys::B, ImGuiKey_B); M.Add(EKeys::C, ImGuiKey_C);
		M.Add(EKeys::D, ImGuiKey_D); M.Add(EKeys::E, ImGuiKey_E); M.Add(EKeys::F, ImGuiKey_F);
		M.Add(EKeys::G, ImGuiKey_G); M.Add(EKeys::H, ImGuiKey_H); M.Add(EKeys::I, ImGuiKey_I);
		M.Add(EKeys::J, ImGuiKey_J); M.Add(EKeys::K, ImGuiKey_K); M.Add(EKeys::L, ImGuiKey_L);
		M.Add(EKeys::M, ImGuiKey_M); M.Add(EKeys::N, ImGuiKey_N); M.Add(EKeys::O, ImGuiKey_O);
		M.Add(EKeys::P, ImGuiKey_P); M.Add(EKeys::Q, ImGuiKey_Q); M.Add(EKeys::R, ImGuiKey_R);
		M.Add(EKeys::S, ImGuiKey_S); M.Add(EKeys::T, ImGuiKey_T); M.Add(EKeys::U, ImGuiKey_U);
		M.Add(EKeys::V, ImGuiKey_V); M.Add(EKeys::W, ImGuiKey_W); M.Add(EKeys::X, ImGuiKey_X);
		M.Add(EKeys::Y, ImGuiKey_Y); M.Add(EKeys::Z, ImGuiKey_Z);
		M.Add(EKeys::Zero, ImGuiKey_0); M.Add(EKeys::One, ImGuiKey_1); M.Add(EKeys::Two, ImGuiKey_2);
		M.Add(EKeys::Three, ImGuiKey_3); M.Add(EKeys::Four, ImGuiKey_4); M.Add(EKeys::Five, ImGuiKey_5);
		M.Add(EKeys::Six, ImGuiKey_6); M.Add(EKeys::Seven, ImGuiKey_7); M.Add(EKeys::Eight, ImGuiKey_8);
		M.Add(EKeys::Nine, ImGuiKey_9);
		M.Add(EKeys::NumPadZero, ImGuiKey_Keypad0); M.Add(EKeys::NumPadOne, ImGuiKey_Keypad1);
		M.Add(EKeys::NumPadTwo, ImGuiKey_Keypad2); M.Add(EKeys::NumPadThree, ImGuiKey_Keypad3);
		M.Add(EKeys::NumPadFour, ImGuiKey_Keypad4); M.Add(EKeys::NumPadFive, ImGuiKey_Keypad5);
		M.Add(EKeys::NumPadSix, ImGuiKey_Keypad6); M.Add(EKeys::NumPadSeven, ImGuiKey_Keypad7);
		M.Add(EKeys::NumPadEight, ImGuiKey_Keypad8); M.Add(EKeys::NumPadNine, ImGuiKey_Keypad9);
		M.Add(EKeys::Multiply, ImGuiKey_KeypadMultiply); M.Add(EKeys::Add, ImGuiKey_KeypadAdd);
		M.Add(EKeys::Subtract, ImGuiKey_KeypadSubtract); M.Add(EKeys::Decimal, ImGuiKey_KeypadDecimal);
		M.Add(EKeys::Divide, ImGuiKey_KeypadDivide);
		M.Add(EKeys::F1, ImGuiKey_F1); M.Add(EKeys::F2, ImGuiKey_F2); M.Add(EKeys::F3, ImGuiKey_F3);
		M.Add(EKeys::F4, ImGuiKey_F4); M.Add(EKeys::F5, ImGuiKey_F5); M.Add(EKeys::F6, ImGuiKey_F6);
		M.Add(EKeys::F7, ImGuiKey_F7); M.Add(EKeys::F8, ImGuiKey_F8); M.Add(EKeys::F9, ImGuiKey_F9);
		M.Add(EKeys::F10, ImGuiKey_F10); M.Add(EKeys::F11, ImGuiKey_F11); M.Add(EKeys::F12, ImGuiKey_F12);
		M.Add(EKeys::Left, ImGuiKey_LeftArrow); M.Add(EKeys::Right, ImGuiKey_RightArrow);
		M.Add(EKeys::Up, ImGuiKey_UpArrow); M.Add(EKeys::Down, ImGuiKey_DownArrow);
		M.Add(EKeys::Home, ImGuiKey_Home); M.Add(EKeys::End, ImGuiKey_End);
		M.Add(EKeys::PageUp, ImGuiKey_PageUp); M.Add(EKeys::PageDown, ImGuiKey_PageDown);
		M.Add(EKeys::Insert, ImGuiKey_Insert); M.Add(EKeys::Delete, ImGuiKey_Delete);
		M.Add(EKeys::BackSpace, ImGuiKey_Backspace); M.Add(EKeys::SpaceBar, ImGuiKey_Space);
		M.Add(EKeys::Enter, ImGuiKey_Enter); M.Add(EKeys::Escape, ImGuiKey_Escape);
		M.Add(EKeys::Tab, ImGuiKey_Tab); M.Add(EKeys::CapsLock, ImGuiKey_CapsLock);
		M.Add(EKeys::LeftShift, ImGuiKey_LeftShift); M.Add(EKeys::RightShift, ImGuiKey_RightShift);
		M.Add(EKeys::LeftControl, ImGuiKey_LeftCtrl); M.Add(EKeys::RightControl, ImGuiKey_RightCtrl);
		M.Add(EKeys::LeftAlt, ImGuiKey_LeftAlt); M.Add(EKeys::RightAlt, ImGuiKey_RightAlt);
		M.Add(EKeys::Semicolon, ImGuiKey_Semicolon); M.Add(EKeys::Equals, ImGuiKey_Equal);
		M.Add(EKeys::Comma, ImGuiKey_Comma); M.Add(EKeys::Hyphen, ImGuiKey_Minus);
		M.Add(EKeys::Period, ImGuiKey_Period); M.Add(EKeys::Slash, ImGuiKey_Slash);
		M.Add(EKeys::Tilde, ImGuiKey_GraveAccent); M.Add(EKeys::LeftBracket, ImGuiKey_LeftBracket);
		M.Add(EKeys::Backslash, ImGuiKey_Backslash); M.Add(EKeys::RightBracket, ImGuiKey_RightBracket);
		M.Add(EKeys::Apostrophe, ImGuiKey_Apostrophe);
		return M;
	}();
	const ImGuiKey* Found = Map.Find(Key);
	return Found ? *Found : ImGuiKey_None;
}

static void AddModifierEvents(ImGuiIO& io, const FInputEvent& Event)
{
	io.AddKeyEvent(ImGuiMod_Ctrl,  Event.IsControlDown());
	io.AddKeyEvent(ImGuiMod_Shift, Event.IsShiftDown());
	io.AddKeyEvent(ImGuiMod_Alt,   Event.IsAltDown());
	io.AddKeyEvent(ImGuiMod_Super, Event.IsCommandDown());
}

FReply SSpaceProgramWidget::HandleMouseButton(const FPointerEvent& MouseEvent, bool bDown)
{
	SetImGuiCurrent();
	ImGuiIO& io = ImGui::GetIO();
	AddModifierEvents(io, MouseEvent);
	const int Button = ToImGuiMouseButton(MouseEvent.GetEffectingButton());
	if (Button >= 0) io.AddMouseButtonEvent(Button, bDown);

	FReply Reply = FReply::Handled();
	if (bDown)
	{
		Reply.CaptureMouse(AsShared());
		Reply.SetUserFocus(AsShared(), EFocusCause::Mouse);
	}
	else if (HasMouseCapture() && !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton)
	         && !MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton)
	         && !MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton))
	{
		Reply.ReleaseMouseCapture();
	}
	return Reply;
}

FReply SSpaceProgramWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return HandleMouseButton(MouseEvent, true);
}

FReply SSpaceProgramWidget::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return HandleMouseButton(MouseEvent, false);
}

FReply SSpaceProgramWidget::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return HandleMouseButton(MouseEvent, true);
}

FReply SSpaceProgramWidget::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SetImGuiCurrent();
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	ImGui::GetIO().AddMousePosEvent((float)Local.X, (float)Local.Y);
	return FReply::Handled();
}

FReply SSpaceProgramWidget::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SetImGuiCurrent();
	ImGui::GetIO().AddMouseWheelEvent(0.0f, MouseEvent.GetWheelDelta());
	return FReply::Handled();
}

void SSpaceProgramWidget::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SetImGuiCurrent();
	ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

FReply SSpaceProgramWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	SetImGuiCurrent();
	ImGuiIO& io = ImGui::GetIO();
	AddModifierEvents(io, InKeyEvent);
	const ImGuiKey Key = ToImGuiKey(InKeyEvent.GetKey());
	if (Key != ImGuiKey_None) io.AddKeyEvent(Key, true);
	return FReply::Handled();
}

FReply SSpaceProgramWidget::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	SetImGuiCurrent();
	ImGuiIO& io = ImGui::GetIO();
	AddModifierEvents(io, InKeyEvent);
	const ImGuiKey Key = ToImGuiKey(InKeyEvent.GetKey());
	if (Key != ImGuiKey_None) io.AddKeyEvent(Key, false);
	return FReply::Handled();
}

FReply SSpaceProgramWidget::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	SetImGuiCurrent();
	ImGui::GetIO().AddInputCharacterUTF16((ImWchar16)InCharacterEvent.GetCharacter());
	return FReply::Handled();
}

FCursorReply SSpaceProgramWidget::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	SetImGuiCurrent();
	switch (ImGui::GetMouseCursor())
	{
	case ImGuiMouseCursor_TextInput:  return FCursorReply::Cursor(EMouseCursor::TextEditBeam);
	case ImGuiMouseCursor_ResizeAll:  return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	case ImGuiMouseCursor_ResizeNS:   return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
	case ImGuiMouseCursor_ResizeEW:   return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	case ImGuiMouseCursor_ResizeNESW: return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest);
	case ImGuiMouseCursor_ResizeNWSE: return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
	case ImGuiMouseCursor_Hand:       return FCursorReply::Cursor(EMouseCursor::Hand);
	case ImGuiMouseCursor_NotAllowed: return FCursorReply::Cursor(EMouseCursor::SlashedCircle);
	case ImGuiMouseCursor_None:       return FCursorReply::Cursor(EMouseCursor::None);
	default:                          return FCursorReply::Cursor(EMouseCursor::Default);
	}
}

void SSpaceProgramWidget::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	SetImGuiCurrent();
	ImGui::GetIO().AddFocusEvent(false);
}

FReply SSpaceProgramWidget::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	SetImGuiCurrent();
	ImGui::GetIO().AddFocusEvent(true);
	return FReply::Handled();
}

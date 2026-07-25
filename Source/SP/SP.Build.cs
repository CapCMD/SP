// SP.Build.cs - SPACE PROGRAM sur UE5.
// Le module compile VERBATIM le coeur du jeu d'origine (astro_core + modules
// fen/ + jeu.cpp) et y ajoute UEBridge/ : le rendu, les entrees et le HUD sont
// 100 % natifs UE5. ImGui et ImPlot ont ete retires du module au passage en
// rendu total (sources conservees dans Space Program/_archive/imgui_20260724).
using UnrealBuildTool;
using System.IO;

public class SP : ModuleRules
{
	public SP(ReadOnlyTargetRules Target) : base(Target)
	{
		// Pas de PCH force-inclus : les sources du jeu (astro_core, imgui...) doivent
		// compiler telles quelles, sans macros UE injectees (check, PI, ...).
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		CppStandard = CppStandardVersion.Cpp20;

		// DETERMINISME BIT-A-BIT d'astro_core : /fp:precise (jamais fast-math),
		// meme exigence que le CMakeLists d'origine (une graine = un vol).
		FPSemantics = FPSemanticsMode.Precise;

		// le code du jeu compile verbatim : le masquage de variable (C4456) reste
		// un warning, comme dans le build g++ d'origine (-Wall sans -Wshadow).
		ShadowVariableWarningLevel = WarningLevel.Warning;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// ImageWrapper/ImageCore : capture headless (SPCapture), comme le
		// --capture du binaire de reference.
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "ApplicationCore", "AssetRegistry", "ImageCore", "ImageWrapper" });

		PublicIncludePaths.AddRange(new string[] {
			Path.Combine(ModuleDirectory, "SpaceProgram"),                       // "app/..."
			Path.Combine(ModuleDirectory, "SpaceProgram", "astro_core", "include"),
			Path.Combine(ModuleDirectory, "SpaceProgram", "mission", "include"),
		});
	}
}

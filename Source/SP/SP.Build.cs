// SP.Build.cs - SPACE PROGRAM sur UE5.
// Le module compile VERBATIM le coeur du jeu d'origine (astro_core + jeu.cpp + ecrans ImGui)
// et y ajoute un pont Slate (UEBridge/) qui remplace GLFW/OpenGL.
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

		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "ApplicationCore", "AssetRegistry" });

		PublicIncludePaths.AddRange(new string[] {
			Path.Combine(ModuleDirectory, "SpaceProgram"),                       // "app/...", "ui/..."
			Path.Combine(ModuleDirectory, "SpaceProgram", "astro_core", "include"),
			Path.Combine(ModuleDirectory, "SpaceProgram", "mission", "include"),
			Path.Combine(ModuleDirectory, "ThirdParty", "imgui"),
			Path.Combine(ModuleDirectory, "ThirdParty", "implot"),
		});
	}
}

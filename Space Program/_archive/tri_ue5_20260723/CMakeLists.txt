cmake_minimum_required(VERSION 3.16)
project(fenetre CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DETERMINISME BIT-A-BIT : requis par le mecanisme de commit (une graine = un vol,
# sur toutes les machines). -ffast-math est INTERDIT.
if(MSVC)
  # /fp:precise n'interdit la contraction FMA qu'a partir de VS2022 (/fp:contract- implicite).
  add_compile_options(/W4 /fp:precise)
  message(WARNING "MSVC : determinisme bit-a-bit non garanti avant VS2022 - preferer g++/clang")
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -ffp-contract=off)
endif()
if(MINGW)
  # binaires autonomes : pas de DLL libstdc++/libgcc/winpthread a distribuer
  add_link_options(-static)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)   # le Makefile compile en -O2 ; meme defaut ici
endif()

file(GLOB ASTRO_CORE_SRC astro_core/src/*.cpp)
add_library(astro_core STATIC ${ASTRO_CORE_SRC})
target_include_directories(astro_core PUBLIC astro_core/include mission/include)

add_executable(fenetre app/fenetre_cli.cpp)
target_link_libraries(fenetre PRIVATE astro_core)

# scripts/ : un binaire par outil (meme liste que le Makefile)
set(SCRIPTS
  m00_iterate m00_ops m00_nav m00_program m00_postmortem m00_sobol
  m00_design m00_play
  m01_porkchop m01_economy m01_corridor
  t01_tour t01_veega t01_dsm t01_refine t01_facture)
foreach(s ${SCRIPTS})
  add_executable(${s} scripts/${s}.cpp)
  target_link_libraries(${s} PRIVATE astro_core)
endforeach()

enable_testing()
add_executable(test_astro_core tests/test_astro_core.cpp)
target_include_directories(test_astro_core PRIVATE tests)
target_link_libraries(test_astro_core PRIVATE astro_core)
add_test(NAME oracles COMMAND test_astro_core)

# --- LE JEU v0.4 (fenetre_jeu) : ImGui + ImPlot + GLFW, dans extern/ ----------
# La cible n'existe que si les dependances sont la : le coeur reste sans dependance.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui AND WIN32)
  set(IMGUI ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui)
  set(IMPLOT ${CMAKE_CURRENT_SOURCE_DIR}/extern/implot)
  add_library(imgui_pile STATIC
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_glfw.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp
    ${IMPLOT}/implot.cpp ${IMPLOT}/implot_items.cpp)
  target_include_directories(imgui_pile PUBLIC
    ${IMGUI} ${IMGUI}/backends ${IMPLOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/include)
  add_executable(fenetre_jeu ui/jeu_main.cpp app/jeu.cpp)
  target_include_directories(fenetre_jeu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(fenetre_jeu PRIVATE astro_core imgui_pile
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/lib-mingw-w64/libglfw3.a
    opengl32 gdi32 user32 shell32)
  # -mwindows : sous-systeme GUI, aucune console au lancement (double-clic propre).
  target_link_options(fenetre_jeu PRIVATE -mwindows)
endif()

# --- LE RENDERCORE NATIF VULKAN (space_program_3d) ---------------------------
# Cible NOUVELLE et INDEPENDANTE : elle consomme astro_core en LECTURE SEULE et
# ne touche a aucune cible existante. Conditionnee a la presence du SDK Vulkan et
# de extern/imgui (backend Vulkan + GLFW). Voir docs/RENDER_ARCHITECTURE.md.
if(DEFINED ENV{VULKAN_SDK})
  set(SPR_VULKAN_SDK $ENV{VULKAN_SDK})
else()
  file(GLOB _spr_vk_sdks "C:/VulkanSDK/*")
  if(_spr_vk_sdks)
    list(SORT _spr_vk_sdks ORDER DESCENDING)
    list(GET _spr_vk_sdks 0 SPR_VULKAN_SDK)
  endif()
endif()

if(WIN32 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
        AND SPR_VULKAN_SDK AND EXISTS "${SPR_VULKAN_SDK}/Include/vulkan/vulkan.h")
  message(STATUS "RenderCore Vulkan : SDK = ${SPR_VULKAN_SDK}")
  set(SPR_VK_INC "${SPR_VULKAN_SDK}/Include")
  set(SPR_VK_LIB "${SPR_VULKAN_SDK}/Lib/vulkan-1.lib")
  find_program(SPR_GLSLC NAMES glslc HINTS "${SPR_VULKAN_SDK}/Bin")

  # -- ImGui compile pour Vulkan (copie dediee : n'interfere pas avec imgui_pile) --
  add_library(imgui_vk STATIC
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_glfw.cpp ${IMGUI}/backends/imgui_impl_vulkan.cpp)
  target_include_directories(imgui_vk PUBLIC
    ${IMGUI} ${IMGUI}/backends ${SPR_VK_INC}
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/include)

  # -- compilation des shaders GLSL -> SPIR-V (glslc) --------------------------
  # common.glsl est un include partage (bloc Frame) : il n'est pas compile seul
  # mais chaque shader en depend (recompilation si modifie). glslc resout
  # #include "common.glsl" relativement au dossier du shader.
  set(SPR_SHADER_OUT "${CMAKE_BINARY_DIR}/render_shaders")
  set(SPR_SHADER_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/common.glsl")
  set(SPR_SPV "")
  foreach(sh scene.vert scene.frag planet.vert planet.frag star.vert star.frag
             shell.vert shell.frag ring.vert ring.frag mesh.vert mesh.frag
             shadow.vert
             fullscreen.vert composite.frag bright.frag blur.frag)
    add_custom_command(
      OUTPUT "${SPR_SHADER_OUT}/${sh}.spv"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${SPR_SHADER_OUT}"
      COMMAND ${SPR_GLSLC} --target-env=vulkan1.2
              "${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/${sh}"
              -o "${SPR_SHADER_OUT}/${sh}.spv"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/${sh}" "${SPR_SHADER_COMMON}"
      COMMENT "glslc ${sh} -> ${sh}.spv" VERBATIM)
    list(APPEND SPR_SPV "${SPR_SHADER_OUT}/${sh}.spv")
  endforeach()
  add_custom_target(spr_shaders DEPENDS ${SPR_SPV})

  # -- la bibliotheque de rendu (spr) -----------------------------------------
  file(GLOB_RECURSE SPR_RENDER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/render/src/*.cpp)
  add_library(spr_render STATIC ${SPR_RENDER_SRC})
  target_include_directories(spr_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/render/include
    astro_core/include mission/include
    ${IMGUI} ${IMGUI}/backends ${SPR_VK_INC}
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/include)
  target_compile_definitions(spr_render PRIVATE SPR_SHADER_DIR="${SPR_SHADER_OUT}")

  add_executable(space_program_3d render/app/space_program_3d.cpp)
  target_link_libraries(space_program_3d PRIVATE
    spr_render astro_core imgui_vk
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/lib-mingw-w64/libglfw3.a
    ${SPR_VK_LIB} gdi32 user32 shell32)
  add_dependencies(space_program_3d spr_shaders)

  # --- LA MAP DU SYSTEME SOLAIRE (ecran principal de gameplay) ---------------
  # Consomme la MEME chaine (astro_core -> DataBridge -> RenderCore) ; ajoute la
  # vue carte (orbites, controle du temps, textures GLB). ole32 = COM/WIC pour le
  # decodage des textures embarquees des .glb (GlbTexture.cpp). SPR_ASSET_DIR pointe
  # sur le dossier des modeles GLB (chemin absolu du depot, robuste au cwd).
  # app/jeu.cpp = LE MODELE DE JEU 2D (fen::app::Jeu), modele PUR (astro_core seul,
  # aucune dependance GL/ImGui). On le compile ici pour brancher les panneaux 3D de
  # l'ISS sur les VRAIES donnees de l'agence (migration 2D->3D). La physique/maths
  # (astro_core) reste inchangee : le 3D ne fait que LIRE le modele.
  add_executable(solar_system_map render/app/solar_system_map.cpp app/jeu.cpp)
  target_include_directories(solar_system_map PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(solar_system_map PRIVATE
    SPR_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  target_link_libraries(solar_system_map PRIVATE
    spr_render astro_core imgui_vk
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/lib-mingw-w64/libglfw3.a
    ${SPR_VK_LIB} gdi32 user32 shell32 ole32)
  add_dependencies(solar_system_map spr_shaders)
endif()

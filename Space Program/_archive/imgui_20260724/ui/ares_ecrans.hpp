// ui/ares_ecrans.hpp — l'écran ARES (couche GDD v1.1). ZERO PHYSIQUE ni règle
// ici : tout est lu dans app::AresLayer ; les ACHATS passent par jeu.payer()
// (l'économie stricte de l'agence tranche). Style : poste de bureau d'études.
#pragma once
#include <cstdio>
#include "imgui.h"
#include "app/jeu.hpp"

namespace fen::ui {

namespace ares_ui {
inline ImVec4 vert()   { return ImVec4(0.55f, 0.85f, 0.55f, 1); }
inline ImVec4 orange() { return ImVec4(0.95f, 0.65f, 0.30f, 1); }
inline ImVec4 rouge()  { return ImVec4(0.95f, 0.35f, 0.30f, 1); }
inline ImVec4 gris()   { return ImVec4(0.55f, 0.58f, 0.62f, 1); }

inline const char* alerte_nom(economy::AlertLevel a) {
  switch (a) {
    case economy::AlertLevel::Normal:  return "NORMAL";
    case economy::AlertLevel::Watch:   return "SURVEILLANCE";
    case economy::AlertLevel::Delays:  return "RETARDS SECONDAIRES";
    case economy::AlertLevel::Tension: return "TENSION";
    case economy::AlertLevel::Crisis:  return "CRISE : GEL";
    default:                           return "ASPHYXIE";
  }
}
inline ImVec4 alerte_couleur(economy::AlertLevel a) {
  if (a == economy::AlertLevel::Normal) return vert();
  if (a <= economy::AlertLevel::Tension) return orange();
  return rouge();
}
inline const char* confiance_lettre(reliability::Confidence c) {
  switch (c) {
    case reliability::Confidence::A: return "A";
    case reliability::Confidence::B: return "B";
    case reliability::Confidence::C: return "C";
    default: return "D";
  }
}
} // namespace ares_ui

// ---------------------------------------------------------------------------
inline void ecran_ares(app::Jeu& jeu) {
  using namespace ares_ui;
  auto& L = jeu.ares;
  if (!L.initialisee()) { ImGui::TextDisabled("ARES : initialisation..."); return; }
  auto& G = *L.etat;

  // --- bandeau : qui je suis, ce que je vaux, ou en est l'agence -------------
  ImGui::TextColored(vert(), "ARES");
  ImGui::SameLine(0, 14);
  ImGui::Text("%s", career::rank_name(G.career.rank));
  ImGui::SameLine(0, 14);
  if (G.career.rank != career::Rank::Directeur) {
    const double seuil = career::PROMOTION_THRESHOLDS[static_cast<int>(G.career.rank)];
    ImGui::Text("score %.0f / %.0f", G.career.score, seuil);
  } else ImGui::Text("score %.0f", G.career.score);
  ImGui::SameLine(0, 14);
  ImGui::Text("age %.1f ans", G.character.age_bio_years());
  ImGui::SameLine(0, 14);
  const auto niveau = G.treasury.level();
  ImGui::TextColored(alerte_couleur(niveau), "tresorerie: %s", alerte_nom(niveau));
  if (G.career.promotion_frozen) {
    ImGui::SameLine(0, 14);
    ImGui::TextColored(orange(), "PROMOTION GELEE");
  }
  for (const auto& n : L.notifications) ImGui::TextColored(vert(), "> %s", n.c_str());
  ImGui::Separator();

  if (!ImGui::BeginTabBar("##ares_tabs")) return;

  // --- PROGRAMME -------------------------------------------------------------
  if (ImGui::BeginTabItem("PROGRAMME")) {
    ImGui::TextUnformatted("CARRIERE [GDD 3] — le rang est un filtre institutionnel,");
    ImGui::TextUnformatted("jamais un substitut a la science (regle du verrou le plus fort).");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Text("Rang      : %s", career::rank_name(G.career.rank));
    ImGui::Text("Confiance : %.0f / 100", G.career.confidence_ares);
    ImGui::Text("Recherches simultanees autorisees : %d",
                career::max_parallel_research(G.career.rank));
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted("ECONOMIE [GDD 13] — miroir de la tresorerie agence ;");
    ImGui::TextUnformatted("le fonds de reserve n'est pas engageable librement.");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Text("Solde       : %.1f M$", G.treasury.balance_musd);
    ImGui::Text("Reserve     : %.1f M$ (obligatoire)", G.treasury.reserve_musd);
    ImGui::Text("Engageable  : %.1f M$", G.treasury.available_musd());
    ImGui::TextColored(alerte_couleur(niveau), "Palier      : %s", alerte_nom(niveau));
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextUnformatted("SITES DE LANCEMENT [GDD 13.3] — la latitude borne l'inclinaison :");
    if (ImGui::BeginTable("sites", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("site"); ImGui::TableSetupColumn("latitude");
      ImGui::TableSetupColumn("inc. min"); ImGui::TableSetupColumn("cout");
      ImGui::TableHeadersRow();
      for (const auto& s : economy::launch_sites()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(s.name.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%.2f deg", s.latitude_deg);
        ImGui::TableNextColumn(); ImGui::Text("%.2f deg", s.min_inclination_deg());
        ImGui::TableNextColumn(); ImGui::Text("x%.2f", s.cost_factor);
      }
      ImGui::EndTable();
    }
    ImGui::EndTabItem();
  }

  // --- ARBRE TECHNOLOGIQUE ---------------------------------------------------
  if (ImGui::BeginTabItem("ARBRE TECHNO")) {
    const int cap = career::max_parallel_research(G.career.rank);
    const int actives = static_cast<int>(G.research.active().size());
    ImGui::Text("Recherches actives : %d / %d (capacite institutionnelle du rang)",
                actives, cap);
    ImGui::Dummy(ImVec2(0, 4));
    for (int b = 0; b < 6; ++b) {
      const auto branche = static_cast<tech::Branch>(b);
      if (!ImGui::CollapsingHeader(tech::branch_name(branche))) continue;
      for (const auto& n : G.tree.all()) {
        if (n.branch != branche) continue;
        ImGui::PushID(n.id.c_str());
        ImGui::Text("TRL %d/9", n.trl);
        ImGui::SameLine(0, 10);
        ImGui::TextUnformatted(n.name.c_str());
        if (n.transverse) { ImGui::SameLine(); ImGui::TextDisabled("[transverse]"); }
        ImGui::SameLine(ImGui::GetWindowWidth() - 340);
        // en cours ?
        const tech::ResearchProject* projet = nullptr;
        for (const auto& p : G.research.active())
          if (p.node_id == n.id) projet = &p;
        if (n.operational()) {
          ImGui::TextColored(vert(), "OPERATIONNEL");
        } else if (projet) {
          ImGui::ProgressBar(static_cast<float>(projet->progress()),
                             ImVec2(200, 0));
        } else if (!G.tree.researchable(n.id)) {
          ImGui::TextColored(gris(), "prerequis manquants");
        } else if (G.career.rank < n.min_rank) {
          ImGui::TextColored(orange(), "rang requis : %s",
                             career::rank_name(n.min_rank));
        } else {
          char lbl[96];
          std::snprintf(lbl, sizeof lbl, "RECHERCHER  %.0f M$ / %.0f j",
                        n.research_cost_musd, n.research_days);
          if (ImGui::SmallButton(lbl)) {
            if (actives >= cap) {
              jeu.erreur = "ARES : capacite de recherche du rang atteinte.";
            } else if (jeu.payer(n.research_cost_musd, "recherche ARES " + n.name)) {
              G.research.start(G.tree, n.id, G.career.rank);
            }
          }
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTabItem();
  }

  // --- CATALOGUE DE MISSIONS -------------------------------------------------
  if (ImGui::BeginTabItem("CATALOGUE")) {
    ImGui::TextUnformatted("Missions planifiees de longue date [GDD 4.2] : visibles,");
    ImGui::TextUnformatted("mais VERROUILLEES tant que les 4 axes ne sont pas reunis.");
    ImGui::Dummy(ImVec2(0, 6));
    const auto& entrees = G.catalog.entries();
    for (std::size_t i = 0; i < entrees.size(); ++i) {
      const auto& e = entrees[i];
      const auto dispo = G.catalog.check(i, G.career, G.tree,
                                         G.treasury.available_musd(),
                                         &G.station, G.clock.now_days());
      ImGui::PushID(static_cast<int>(i));
      ImGui::TextUnformatted(e.contract.id.c_str());
      ImGui::SameLine(0, 10);
      ImGui::TextUnformatted(e.contract.title.c_str());
      if (e.contract.crewed) { ImGui::SameLine(); ImGui::TextDisabled("[habitee]"); }
      ImGui::SameLine(ImGui::GetWindowWidth() - 300);
      if (dispo.playable) {
        ImGui::TextColored(vert(), "PREREQUIS REUNIS");
      } else if (dispo.suspended) {
        ImGui::TextColored(rouge(), "FAMILLE SUSPENDUE");
      } else {
        ImGui::TextColored(orange(), "VERROU : %s",
                           tech::lock_name(dispo.verdict.dominant));
      }
      for (const auto& raison : dispo.verdict.reasons)
        ImGui::TextDisabled("      - %s", raison.c_str());
      ImGui::Dummy(ImVec2(0, 4));
      ImGui::PopID();
    }
    ImGui::EndTabItem();
  }

  // --- STATION NOVELLUS ------------------------------------------------------
  if (ImGui::BeginTabItem("NOVELLUS")) {
    const auto fx = station::effects(G.station);
    ImGui::Text("Palier %d / 4  [GDD 11.3]", G.station.tier());
    ImGui::SameLine(0, 16);
    ImGui::Text("puissance %.0f kW", G.station.power_kw());
    ImGui::SameLine(0, 16);
    ImGui::Text("rejet thermique %.0f kW", G.station.thermal_kw());
    ImGui::Text("Effets : recherche x%.2f, maintenance x%.2f, equipage %.0f, EVA %s",
                fx.research_speed, fx.maintenance_quality, fx.crew_capacity,
                fx.eva_ops ? "oui" : "non");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextUnformatted("MODULES INSTALLES :");
    for (const auto& m : G.station.modules) {
      ImGui::BulletText("%s (gen %d)%s", station::module_name(m.type), m.generation,
                        m.operational ? "" : "  [HORS SERVICE]");
    }
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextUnformatted("EXTENSIONS DISPONIBLES (payees sur la tresorerie agence) :");
    for (const auto& o : app::offres_modules()) {
      const bool deja = (o.type != station::ModuleType::Power) && G.station.has(o.type);
      ImGui::PushID(station::module_name(o.type));
      if (deja) {
        ImGui::TextDisabled("%-26s installe", station::module_name(o.type));
      } else {
        char lbl[96];
        std::snprintf(lbl, sizeof lbl, "CONSTRUIRE  %.0f M$", o.cout_musd);
        if (ImGui::SmallButton(lbl)) {
          if (jeu.payer(o.cout_musd, std::string("module Novellus ") +
                                         station::module_name(o.type))) {
            station::StationModule m;
            m.type = o.type;
            m.generation = (o.type == station::ModuleType::Power) ? 2 : 1;
            m.power_supply_kw = o.puissance_kw;
            m.thermal_reject_kw = o.thermique_kw;
            G.station.modules.push_back(m);
            G.station.topology.push_back({1, static_cast<int>(G.station.modules.size()) - 1});
          }
        }
        ImGui::SameLine(0, 12);
        ImGui::TextUnformatted(station::module_name(o.type));
        ImGui::SameLine(0, 10);
        ImGui::TextDisabled("- %s", o.pourquoi);
      }
      ImGui::PopID();
    }
    ImGui::EndTabItem();
  }

  // --- BASE DE FIABILITE -----------------------------------------------------
  if (ImGui::BeginTabItem("FIABILITE")) {
    ImGui::TextUnformatted("Referentiel de surete [GDD 12.3] : traçable, contextuel,");
    ImGui::TextUnformatted("conservateur. La valeur nominale n'est JAMAIS utilisee brute.");
    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::BeginTable("fiab", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("id"); ImGui::TableSetupColumn("famille");
      ImGui::TableSetupColumn("fiabilite [basse..haute]");
      ImGui::TableSetupColumn("conf."); ImGui::TableSetupColumn("source");
      ImGui::TableSetupColumn("revisions");
      ImGui::TableHeadersRow();
      for (const auto& r : G.reliability_db.all()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.id.c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.family.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.4f  [%.4f .. %.4f]", r.nominal, r.lo, r.hi);
        ImGui::TableNextColumn();
        const auto conf = r.confidence;
        ImGui::TextColored(conf == reliability::Confidence::A ? vert()
                           : conf == reliability::Confidence::D ? rouge() : orange(),
                           "%s", confiance_lettre(conf));
        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.source.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%d", static_cast<int>(r.history.size()));
      }
      ImGui::EndTable();
    }
    ImGui::EndTabItem();
  }

  ImGui::EndTabBar();
}

} // namespace fen::ui

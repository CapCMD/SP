// tests/test_contenu_gdd.cpp — ORACLES DU CONTENU NOMMÉ PAR LE GDD
//
// Décision de cadrage : on remplit tout ce que le CORPS du GDD nomme, et rien
// de ce que son chapitre 20 renvoie explicitement à une version ultérieure
// (coûts et durées de recherche unitaires, matrice mission × technos, table
// TRL par rang). Ces oracles vérifient donc la CONFORMITÉ AUX TABLES DU GDD,
// pas des valeurs de game design inventées.
//
//   . filières de propulsion  -> tableau 6.4, à la ligne près
//   . sources d'énergie pures -> paliers 1, 2 et 4 de 5.12.3
//   . modules Novellus        -> les dix de 11.2 et leurs effets de 11.6
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS.
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <set>
#include <string>

#include "app/ares.hpp"
#include "app/vehicle_design.hpp"
#include "fen/rel/Relativity.hpp"
#include "fen/reliability/AdvancedFilieres.hpp"
#include "fen/station/Novellus.hpp"
#include "fen/vehicle/Geometry.hpp"
#include "fen/vehicle/PartsCatalog.hpp"
#include "fen/vehicle/Propulsion.hpp"

using namespace fen;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)
#define CHECK_NEAR(a, b, tol, nom)                                           \
  do {                                                                       \
    const double d_ = std::fabs((a) - (b));                                   \
    const double s_ = std::fabs(b) > 1e-30 ? d_ / std::fabs(b) : d_;          \
    if (s_ <= (tol)) { ++g_ok; }                                              \
    else { ++g_ko; std::printf("ECHEC : %s — %.6g vs %.6g (ligne %d)\n",       \
                               nom, (double)(a), (double)(b), __LINE__); }    \
  } while (0)

int main() {
  // ═══════════ FILIÈRES DE PROPULSION — TABLEAU 6.4 À LA LIGNE PRÈS ═══════════
  {
    using vehicle::PropFamily;
    using vehicle::prop_family;
    const int n = static_cast<int>(std::size(vehicle::PROP_FAMILIES));
    CHECK(n == 8, "6.4 : les huit lignes du tableau sont presentes");

    // Chaque ligne, avec ses bornes EXACTES telles que le GDD les écrit.
    struct Attendu { PropFamily f; double isp_lo, isp_hi; bool continu; };
    const Attendu table[] = {
        {PropFamily::ChemicalSolid,    250.0,   280.0, false},
        {PropFamily::ChemicalLiquid,   300.0,   460.0, false},
        {PropFamily::ElectricHall,    1500.0,  3000.0, true},
        {PropFamily::ElectricGridded, 3000.0, 10000.0, true},
        {PropFamily::Ntp,              850.0,  1000.0, false},
        {PropFamily::Nep,             2000.0, 10000.0, true},
        {PropFamily::Fusion,          1.0e4,   1.0e6,  true},
        {PropFamily::Antimatter,      1.0e5,   1.0e7,  true},
    };
    for (const auto& a : table) {
      const auto* c = prop_family(a.f);
      CHECK(c != nullptr, "6.4 : filiere presente");
      if (!c) continue;
      CHECK_NEAR(c->isp_min_s, a.isp_lo, 1e-12, "6.4 : borne basse d Isp");
      CHECK_NEAR(c->isp_max_s, a.isp_hi, 1e-12, "6.4 : borne haute d Isp");
      CHECK((c->regime == vehicle::BurnRegime::Continuous) == a.continu,
            "6.4 : regime impulsionnel ou continu conforme");
      CHECK(c->isp_min_s < c->isp_max_s, "6.4 : fourchette ordonnee");
      CHECK(c->thrust_min_n < c->thrust_max_n, "6.4 : poussee ordonnee");
      CHECK(std::string(c->limiting_factor).size() > 0,
            "6.4 : facteur limitant dominant renseigne");
    }

    // LECTURE DE DESIGN [6.4] : « plus on descend, plus l Isp monte » est une
    // TENDANCE, pas un ordre strict — le tableau du GDD place le NTP APRÈS
    // l electrique alors que son Isp est dix fois moindre. C est voulu : le NTP
    // est classe par maturite, et il est L EXCEPTION qui troque l Isp contre la
    // poussee. Un oracle qui exigerait la monotonie stricte serait donc FAUX.
    // On verifie la progression sur la chaine des filieres a haut rendement...
    const PropFamily chaine[] = {PropFamily::ChemicalSolid, PropFamily::ChemicalLiquid,
                                 PropFamily::ElectricHall, PropFamily::ElectricGridded,
                                 PropFamily::Fusion, PropFamily::Antimatter};
    for (std::size_t i = 1; i < std::size(chaine); ++i)
      CHECK(prop_family(chaine[i])->isp_max_s >= prop_family(chaine[i - 1])->isp_max_s,
            "6.4 : Isp croissant le long de la chaine a haut rendement");
    // ... et on verifie EXPLICITEMENT que le NTP est l exception documentee :
    // Isp bien inferieur a l electrique, mais poussee de quatre ordres au-dessus.
    CHECK(prop_family(PropFamily::Ntp)->isp_max_s <
          prop_family(PropFamily::ElectricHall)->isp_min_s,
          "6.4 : le NTP a un Isp bien inferieur a l electrique");
    CHECK(prop_family(PropFamily::Ntp)->thrust_min_n /
              prop_family(PropFamily::ElectricHall)->thrust_max_n >= 1.0e4,
          "6.4 : ... mais une poussee de quatre ordres au-dessus — c est LE compromis");
    // « Aucune filiere ne cumule Isp eleve, forte poussee, faible masse ET TRL
    // eleve : chacune paie AU MOINS un axe. » Une filiere qui offre les deux
    // premiers doit donc payer sur la masse : c est le cas des nucleaires, qui
    // trainent blindage et radiateurs. La fusion ne fait pas exception.
    for (const auto& c : vehicle::PROP_FAMILIES)
      CHECK(!(c.isp_min_s > 1500.0 && c.thrust_max_n > 1.0e3) || c.nuclear,
            "6.4 : haut Isp + forte poussee => paye en masse (nucleaire)");

    // INVARIANT 5.12.1 : seules les filieres ALIMENTEES ont besoin de puissance,
    // et le nucleaire traine blindage et radiateurs.
    CHECK(!prop_family(PropFamily::ChemicalLiquid)->needs_power,
          "5.12.1 : le chimique n a pas de source separee");
    CHECK(prop_family(PropFamily::ElectricGridded)->needs_power,
          "5.12.1 : l electrique est plafonne par la puissance");
    CHECK(!prop_family(PropFamily::Ntp)->needs_power,
          "5.12.9 : le NTP chauffe un ergol, il n est pas alimente electriquement");
    CHECK(prop_family(PropFamily::Ntp)->nuclear && prop_family(PropFamily::Nep)->nuclear,
          "5.12 : NTP et NEP sont nucleaires");
    CHECK(!prop_family(PropFamily::ElectricHall)->nuclear,
          "5.12.7 : l electrique seul n est pas nucleaire");

    // 6.3 : SEUL l impulsionnel peut decoller, atterrir ou inserer brutalement.
    CHECK(prop_family(PropFamily::ChemicalSolid)->regime == vehicle::BurnRegime::Impulsive &&
          prop_family(PropFamily::ChemicalLiquid)->regime == vehicle::BurnRegime::Impulsive &&
          prop_family(PropFamily::Ntp)->regime == vehicle::BurnRegime::Impulsive,
          "6.3 : chimique et NTP sont impulsionnels");
    CHECK(prop_family(PropFamily::Nep)->regime == vehicle::BurnRegime::Continuous,
          "6.3 : la NEP est continue — incapable d inserer brutalement");

    // 6.2 : LE COMPROMIS. À puissance fixée, monter l Isp FAIT BAISSER la
    // poussée. C est une identite, on la verifie comme telle.
    const double P = 100e3, eta = 0.6;
    const double f_hall = vehicle::powered_thrust(eta, P, 2000.0 * cst::G0);
    const double f_ion  = vehicle::powered_thrust(eta, P, 6000.0 * cst::G0);
    CHECK_NEAR(f_hall / f_ion, 3.0, 1e-12,
               "6.2 : F = 2.eta.P/ve — tripler l Isp divise la poussee par trois");
  }

  // ═══════════ SOURCES D'ÉNERGIE PURES — PALIERS 1, 2, 4 de 5.12.3 ═══════════
  {
    using vehicle::PropTier;
    CHECK(std::size(vehicle::ENERGY_SOURCES) == 3,
          "5.12.3 : les trois sources pures (solaire, RTG, fission)");
    // 5.12.1 : ces paliers ne POUSSENT pas. `prop_class` doit le refuser.
    CHECK(vehicle::prop_class(PropTier::Solar) == nullptr &&
          vehicle::prop_class(PropTier::Rtg) == nullptr &&
          vehicle::prop_class(PropTier::Fission) == nullptr,
          "5.12.1 : une source d energie n est pas un propulseur");
    CHECK(vehicle::energy_source(PropTier::Solar) != nullptr &&
          vehicle::energy_source(PropTier::Rtg) != nullptr &&
          vehicle::energy_source(PropTier::Fission) != nullptr,
          "5.12.3 : les trois sources sont decrites");
    CHECK(vehicle::energy_source(PropTier::Chemical) == nullptr,
          "5.12.1 : le chimique n est pas une source d energie");

    // 5.12.5 : le solaire DÉCROÎT en 1/d². C est ce qui le rend inutilisable
    // dans le systeme externe.
    const auto* sol = vehicle::energy_source(PropTier::Solar);
    CHECK(sol->falls_off_with_distance, "5.12.5 : le solaire depend de la distance");
    CHECK_NEAR(vehicle::solar_power_at(1000.0, cst::AU), 1000.0, 1e-12,
               "5.12.5 : puissance de reference a 1 UA");
    CHECK_NEAR(vehicle::solar_power_at(1000.0, 2.0 * cst::AU), 250.0, 1e-12,
               "5.12.5 : loi en 1/d2");
    // À Jupiter (5,2 UA) il ne reste que ~3,7 % : d'où le RTG.
    CHECK(vehicle::solar_power_at(1000.0, 5.2 * cst::AU) < 40.0,
          "5.12.5 : marginal au-dela de la ceinture principale");

    // 5.12.6 : le RTG NE décroît PAS avec la distance mais avec le TEMPS.
    const auto* rtg = vehicle::energy_source(PropTier::Rtg);
    CHECK(!rtg->falls_off_with_distance, "5.12.6 : le RTG ignore la distance au Soleil");
    CHECK_NEAR(rtg->half_life_years, 87.7, 1e-12, "5.12.6 : demi-vie du Pu-238");
    CHECK_NEAR(vehicle::rtg_power_after(100.0, 87.7), 50.0, 1e-12,
               "5.12.6 : moitie de la puissance apres une demi-vie");
    CHECK_NEAR(vehicle::rtg_power_after(100.0, 0.0), 100.0, 1e-12,
               "5.12.6 : pas de perte a t=0");
    CHECK(vehicle::rtg_power_after(100.0, 14.0) > 88.0,
          "5.12.6 : ~89 % apres 14 ans (ordre de grandeur Voyager)");

    // 5.12.2 : la MASSE de la source entre au budget. Elle n est jamais gratuite.
    const double m_rtg = vehicle::source_mass_kg(PropTier::Rtg, 1000.0);
    const double m_fis = vehicle::source_mass_kg(PropTier::Fission, 1000.0);
    CHECK(m_rtg > 0.0 && m_fis > 0.0, "5.12.2 : toute source a une masse");
    CHECK(m_rtg > m_fis,
          "5.12.6 : a puissance egale, le RTG est bien plus lourd qu un reacteur");
    CHECK_NEAR(vehicle::source_mass_kg(PropTier::Fission, 2000.0), 2.0 * m_fis,
               1e-12, "5.12.2 : masse proportionnelle a la puissance");
    CHECK(vehicle::source_mass_kg(PropTier::Fission, 1000.0, true) < m_fis,
          "5.12.2 : la fourchette optimiste allege, elle ne supprime pas");
    CHECK(vehicle::source_mass_kg(PropTier::Chemical, 1000.0) == 0.0,
          "5.12.1 : pas de masse de source pour une filiere non alimentee");
    CHECK(rtg->nuclear && vehicle::energy_source(PropTier::Fission)->nuclear &&
          !sol->nuclear, "5.12 : nature nucleaire correctement declaree");
  }

  // ═══════════ NOVELLUS — LES DIX MODULES DE 11.2 ET LEURS EFFETS DE 11.6 ════
  {
    using station::ModuleType;
    using station::Station;
    using station::StationModule;

    // 11.2 : DIX modules, chacun une fonction DISTINCTE — aucun doublon de nom.
    std::set<std::string> noms;
    for (int i = 0; i < 10; ++i)
      noms.insert(station::module_name(static_cast<ModuleType>(i)));
    CHECK(noms.size() == 10, "11.2 : dix modules aux fonctions distinctes");

    // 11.4 : la catégorisation exacte du GDD.
    const ModuleType obligatoires[] = {ModuleType::CommandCore, ModuleType::DockingNode,
                                       ModuleType::LifeSupport, ModuleType::Power,
                                       ModuleType::CrewHabitat, ModuleType::Storage};
    for (auto m : obligatoires)
      CHECK(station::module_category(m) == station::ModuleCategory::Mandatory,
            "11.4 : module obligatoire correctement categorise");
    CHECK(station::module_category(ModuleType::ScienceLab) == station::ModuleCategory::Advanced &&
          station::module_category(ModuleType::Workshop) == station::ModuleCategory::Advanced,
          "11.4 : laboratoire et atelier sont operationnels avances");
    CHECK(station::module_category(ModuleType::Medical) == station::ModuleCategory::Robustness &&
          station::module_category(ModuleType::EvaAirlock) == station::ModuleCategory::Robustness,
          "11.4 : medical et sas EVA relevent de la robustesse");

    // 11.2 : SANS NOYAU, NOVELLUS N EST PAS OPÉRATIONNELLE. C est une
    // definition, pas un equilibrage.
    {
      Station st;
      st.modules.push_back({ModuleType::ScienceLab, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Power, true, 1, 100.0, 0.0});
      const auto e = station::effects(st);
      CHECK(!e.operational, "11.2 : pas de noyau = station non operationnelle");
      CHECK(e.research_speed == 1.0,
            "11.2 : sans noyau, le laboratoire n apporte RIEN");
    }

    // 11.6 : chaque module produit un effet CONCRET. On les verifie un a un.
    {
      Station st;
      st.modules.push_back({ModuleType::CommandCore, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Power, true, 1, 100.0, 0.0});
      auto e = station::effects(st);
      CHECK(e.operational, "11.6 : le noyau rend la station operationnelle");
      CHECK(e.docking_slots == 0 && !e.can_expand,
            "11.6 : sans noeud d amarrage, aucune extension possible");

      st.modules.push_back({ModuleType::DockingNode, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.docking_slots > 0 && e.can_expand,
            "11.6 : le noeud d amarrage ouvre accueil et extension");

      st.modules.push_back({ModuleType::ScienceLab, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.research_speed > 1.0, "11.6 : le laboratoire accelere la recherche");

      st.modules.push_back({ModuleType::Workshop, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.maintenance_quality < 1.0,
            "11.6 : l atelier ameliore la qualite de maintenance");

      st.modules.push_back({ModuleType::CrewHabitat, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::LifeSupport, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.crew_capacity > 0.0, "11.6 : l habitat porte la capacite d equipage");
      CHECK(e.sustainable_days > 0.0, "11.6 : le support-vie porte la duree soutenable");

      const double avant = e.sustainable_days;
      st.modules.push_back({ModuleType::Storage, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.sustainable_days > avant, "11.6 : le stockage prolonge le sejour");

      st.modules.push_back({ModuleType::Medical, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.medical_risk_factor < 1.0, "11.6 : le medical reduit le risque sanitaire");

      st.modules.push_back({ModuleType::EvaAirlock, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.eva_ops, "11.6 : le sas ouvre les operations externes");
      CHECK(st.tier() == 4, "11.3 : les dix modules donnent le palier 4");
    }

    // 11.6 : LE MODULE ÉNERGÉTIQUE CONDITIONNE LES FONCTIONS ÉNERGIVORES.
    {
      Station st;
      st.modules.push_back({ModuleType::CommandCore, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::ScienceLab, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Workshop, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Power, true, 1, 5.0, 0.0});   // trop peu
      auto e = station::effects(st);
      CHECK(st.power_demand_kw() > 0.0, "11.6 : les modules consomment");
      CHECK(!e.power_sufficient, "11.6 : puissance insuffisante detectee");
      CHECK(e.research_speed == 1.0 && e.maintenance_quality == 1.0,
            "11.6 : sans marge, labo et atelier ne tournent pas");

      st.modules.back().power_supply_kw = 200.0;                       // de quoi tout tenir
      e = station::effects(st);
      CHECK(e.power_sufficient && e.power_margin_kw > 0.0,
            "11.6 : une generation superieure debloque les fonctions");
      CHECK(e.research_speed > 1.0 && e.maintenance_quality < 1.0,
            "11.6 : labo et atelier reprennent");
    }

    // 11.3 : les quatre paliers, dans l ordre exact du GDD.
    {
      Station st;
      CHECK(st.tier() == 0, "11.3 : station vide = palier 0");
      st.modules.push_back({ModuleType::CommandCore, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::DockingNode, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Power, true, 1, 100.0, 0.0});
      CHECK(st.tier() == 1, "11.3 : palier 1 = fondation structurelle et energetique");
      st.modules.push_back({ModuleType::LifeSupport, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::CrewHabitat, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Storage, true, 1, 0.0, 0.0});
      CHECK(st.tier() == 2, "11.3 : palier 2 = mise en habitabilite");
      st.modules.push_back({ModuleType::ScienceLab, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Workshop, true, 1, 0.0, 0.0});
      CHECK(st.tier() == 3, "11.3 : palier 3 = exploitation avancee");
      st.modules.push_back({ModuleType::Medical, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::EvaAirlock, true, 1, 0.0, 0.0});
      CHECK(st.tier() == 4, "11.3 : palier 4 = autonomie et robustesse");
    }
  }

  // ═══════════ ARBRE TECHNOLOGIQUE — SOUS-BRANCHES DE 5.7–5.13 ═══════════
  {
    tech::TechTree t;
    app::AresLayer::seed_arbre(t);
    const auto& tous = t.all();

    // 5.2 : les SIX branches sont peuplées. Une branche vide serait un chapitre
    // du GDD sans contenu.
    int par_branche[6] = {0, 0, 0, 0, 0, 0};
    int transverses = 0;
    for (const auto& n : tous) {
      par_branche[static_cast<int>(n.branch)]++;
      if (n.transverse) ++transverses;
    }
    for (int b = 0; b < 6; ++b)
      CHECK(par_branche[b] >= 5, "5.2 : chaque branche porte des noeuds");
    CHECK(tous.size() >= 60, "5.7-5.13 : l arbre couvre les sous-branches nommees");

    // 5.13 : les technologies transverses sont DISTRIBUÉES, pas regroupées en
    // une septième branche déguisée.
    CHECK(transverses >= 8, "5.13 : les transverses nommees sont presentes");
    std::set<int> branches_transverses;
    for (const auto& n : tous)
      if (n.transverse) branches_transverses.insert(static_cast<int>(n.branch));
    CHECK(branches_transverses.size() >= 4,
          "5.13 : les transverses sont REPARTIES sur plusieurs branches");

    // Chaque sous-branche que le GDD NOMME doit exister. Liste tirée
    // littéralement de 5.7 à 5.13.
    const char* nommees[] = {
        // 5.7
        "lanceur_leger", "lanceur_moyen", "lanceur_lourd", "lanceur_super_lourd",
        "rentree_capsule", "reutilisation", "rdv_automatise", "transfert_ergols",
        "cadence_industrielle",
        // 5.8
        "sondes", "orbiteurs_cartographie", "edl_robotique", "rovers",
        "prelevement", "retour_echantillons", "robotique_orbitale",
        // 5.9
        "capsule_habitee", "eva", "amarrage_habite", "sauvetage_habite",
        "station_modulaire", "logistique_leo",
        // 5.10
        "recyclage_partiel", "eclss_habite", "medecine_embarquee",
        "facteurs_humains", "maintenance_locale",
        // 5.11
        "hohmann_ops", "gravity_assist", "multi_survols", "nav_profonde",
        "capture_orbitale", "aerofreinage", "aerocapture", "prepositionnement",
        // 5.12
        "solaire", "rtg", "electrique_avancee", "fission_spatiale", "ntp",
        "nep_megawatt", "fusion", "antimatiere",
        // 5.13
        "materiaux_ht", "radioprotection", "thermique_radiateurs", "avionique",
        "informatique_bord", "capteurs_navigation", "communications",
        "robotique", "qualification_essais", "fabrication_metrologie",
    };
    for (const char* id : nommees)
      CHECK(t.find(id) != nullptr, "5.7-5.13 : sous-branche nommee par le GDD presente");

    // 5.1 : « une mission avancée ne dépend jamais d'une technologie isolée mais
    // de la CONVERGENCE de plusieurs maturités. » Les nœuds de fin d'arbre
    // doivent donc avoir plusieurs prérequis.
    CHECK(t.find("nep_megawatt")->prereqs.size() >= 3,
          "5.1 : la NEP megawatt exige la convergence de plusieurs filieres");
    CHECK(t.find("fission_spatiale")->prereqs.size() >= 3,
          "5.12.8 : le reacteur exige thermique, materiaux, blindage, essais");

    // 5.13, exemple LITTÉRAL du GDD : « une NEP mégawatt reste inaccessible tant
    // que les filières thermique/radiateurs et matériaux haute température ne
    // sont pas mûres, même si le réacteur lui-même l'est. »
    {
      const auto& p = t.find("nep_megawatt")->prereqs;
      const bool thermique = std::find(p.begin(), p.end(), "thermique_radiateurs") != p.end();
      const bool materiaux = std::find(p.begin(), p.end(), "materiaux_ht") != p.end();
      CHECK(thermique && materiaux, "5.13 : l exemple du GDD est cable tel quel");
    }

    // 5.3 : le DAG est cohérent — tout prérequis existe, et aucun cycle.
    for (const auto& n : tous)
      for (const auto& p : n.prereqs)
        CHECK(t.find(p) != nullptr, "5.3 : tout prerequis designe un noeud existant");

    // 19.2 : le rang ne remplace jamais la science — un nœud ne peut pas exiger
    // un rang INFÉRIEUR à celui de ses prérequis (ce serait un contournement).
    for (const auto& n : tous)
      for (const auto& p : n.prereqs) {
        const tech::TechNode* pre = t.find(p);
        if (pre) CHECK(n.min_rank >= pre->min_rank,
                       "19.2 : un noeud n abaisse jamais le rang de ses prerequis");
      }

    // 5.6 : ce que le monde sait DÉJÀ faire est opérationnel au départ ; ce qui
    // reste à chercher ne l'est pas.
    CHECK(t.find("lanceur_moyen")->operational(), "5.6 : lanceur moyen disponible au depart");
    CHECK(t.find("sondes")->operational(), "5.6 : sondes disponibles au depart");
    CHECK(t.find("capsule_habitee")->operational(), "5.6 : capsule habitee au depart");
    CHECK(!t.find("fusion")->operational(), "5.6 : la fusion reste a chercher");
    CHECK(!t.find("antimatiere")->operational(), "5.6 : l antimatiere reste a chercher");
    CHECK(t.find("antimatiere")->min_rank == career::Rank::Directeur,
          "5.12.12 : l antimatiere est reservee au Directeur, fin d arbre");

    // GDD 20 : les coûts et durées sont DÉCLARÉS PROVISOIRES, jamais présentés
    // comme du design validé.
    int chiffres = 0, provisoires = 0;
    for (const auto& n : tous) {
      if (n.research_days > 0.0 || n.research_cost_musd > 0.0) {
        ++chiffres;
        if (n.costs_provisional) ++provisoires;
      }
    }
    CHECK(chiffres > 0 && chiffres == provisoires,
          "GDD 20 : tout cout ou duree chiffre est marque PROVISOIRE");

    // 4.3 : plus la percée est lointaine, plus elle est longue. On le vérifie
    // sur la chaîne que le GDD ordonne lui-même en 5.12.3.
    CHECK(t.find("antimatiere")->research_days > t.find("fusion")->research_days,
          "4.3 : l antimatiere est plus longue que la fusion");
    CHECK(t.find("fusion")->research_days > t.find("nep_megawatt")->research_days,
          "4.3 : la fusion est plus longue que la NEP");
    CHECK(t.find("nep_megawatt")->research_days > t.find("electrique_avancee")->research_days,
          "4.3 : la NEP est plus longue que l electrique");
  }

  // ═══════════ CATALOGUE DE MISSIONS — LES TYPES DE 10.1 ═══════════
  {
    mission::MissionCatalog c;
    app::AresLayer::seed_catalogue(c);
    const auto& e = c.entries();
    CHECK(e.size() >= 10, "10.1 : les grands types de mission sont couverts");

    // 10.2 : un contrat ne peut PAS exister sans son corps de mail, puisque
    // c'est le seul canal par lequel il entre dans la partie.
    for (const auto& x : e) {
      CHECK(!x.contract.mail_body.empty(), "10.2 : tout contrat porte son mail");
      CHECK(!x.contract.id.empty() && !x.contract.title.empty(),
            "10.2 : tout contrat est identifie et titre");
      CHECK(!x.contract.family.empty(),
            "10.4 : tout contrat a une famille (suspension par filiere)");
      CHECK(!x.contract.prerequisites.required_tech.empty(),
            "4.2 : aucune mission n est jouable sans prerequis techniques");
    }

    // 4.2 : « une mission avancee ne depend JAMAIS d une seule technologie mais
    // d un ENSEMBLE coherent de maturites ». L exemple litteral du GDD est la
    // mission habitee martienne : lanceur lourd, propulsion adaptee, support-vie
    // longue duree, protection radiative, systeme de rentree ou d insertion.
    const mission::CatalogEntry* mars = nullptr;
    for (const auto& x : e) if (x.contract.id == "CAT-09") mars = &x;
    CHECK(mars != nullptr, "10.1 : la mission habitee martienne est au catalogue");
    if (mars) {
      const auto& t = mars->contract.prerequisites.required_tech;
      CHECK(t.size() >= 5, "4.2 : la mission martienne exige une CONVERGENCE");
      auto exige = [&t](const char* id) {
        return std::find(t.begin(), t.end(), std::string(id)) != t.end();
      };
      CHECK(exige("lanceur_super_lourd"), "4.2 : ... un lanceur lourd");
      CHECK(exige("ntp"), "4.2 : ... une propulsion adaptee");
      CHECK(exige("sejour_long"), "4.2 : ... un support-vie longue duree");
      CHECK(exige("radioprotection"), "4.2 : ... une protection radiative credible");
      CHECK(exige("aerocapture"), "4.2 : ... un systeme d insertion approprie");
      CHECK(mars->contract.crewed, "10.1 : elle est habitee");
      CHECK(mars->contract.prerequisites.min_rank == career::Rank::Directeur,
            "3.2 : reservee au Directeur de Programme");
    }

    // 19.3 : la mission relativiste est en FIN d arbre et n existe que par
    // l antimatiere.
    const mission::CatalogEntry* rel = nullptr;
    for (const auto& x : e) if (x.contract.id == "CAT-11") rel = &x;
    CHECK(rel != nullptr, "10.1 : la mission a propulsion extreme est au catalogue");
    if (rel) {
      const auto& t = rel->contract.prerequisites.required_tech;
      CHECK(std::find(t.begin(), t.end(), std::string("antimatiere")) != t.end(),
            "19.3/19.4 : seule l antimatiere ouvre le regime relativiste");
      CHECK(rel->contract.prerequisites.infra.station_tier == 4,
            "5.4 : elle exige l infrastructure la plus complete");
    }

    // 3.2 : les rangs sont ETAGES — il existe des missions a chaque niveau.
    std::set<int> rangs;
    for (const auto& x : e)
      rangs.insert(static_cast<int>(x.contract.prerequisites.min_rank));
    CHECK(rangs.size() >= 4, "3.2 : le catalogue s etage sur les rangs de carriere");
    CHECK(rangs.count(static_cast<int>(career::Rank::Stagiaire)) > 0,
          "3.2 : un Stagiaire a de quoi commencer");
  }

  // ═══════════ CATALOGUE DE PIÈCES — [GDD 12.1] ═══════════
  {
    const auto& moteurs = vehicle::engine_catalog();
    CHECK(moteurs.size() >= 15, "12.1 : le catalogue de moteurs est fourni");

    for (const auto& p : moteurs) {
      // 12.3.1 : aucune donnee sans provenance.
      CHECK(std::string(p.source).size() > 0, "12.1 : toute piece porte sa source");
      CHECK(std::string(p.lineage).size() > 0, "12.1 : toute piece porte sa LIGNEE reelle");
      // 6.4 : Isp = fourchette DURE ; poussee = ORDRE de grandeur (une decade).
      CHECK(vehicle::engine_within_family_envelope(p),
            "6.4 : la piece reste dans l enveloppe de sa filiere");
      const vehicle::PropFamilyClass* fam = vehicle::prop_family(p.family);
      CHECK(fam && p.isp_vac_s >= fam->isp_min_s && p.isp_vac_s <= fam->isp_max_s,
            "6.4 : l Isp respecte STRICTEMENT la fourchette du tableau");
      // 12.5 : pas de fausse precision sur du speculatif.
      CHECK(vehicle::part_confidence_consistent(p),
            "12.5 : confiance, TRL et incertitude coherents");
      // Physique elementaire : Isp et poussee positifs, masse positive.
      CHECK(p.isp_vac_s > 0.0 && p.thrust_vac_n > 0.0 && p.mass_kg > 0.0,
            "12.1 : performances physiquement valides");
      if (p.isp_sl_s > 0.0)
        CHECK(p.isp_sl_s < p.isp_vac_s,
              "physique : l Isp au sol est TOUJOURS inferieur a l Isp dans le vide");
    }

    // 12.1 : « les composants speculatifs ne sont introduits que TARDIVEMENT ».
    int voles = 0, speculatifs = 0;
    for (const auto& p : moteurs) {
      if (p.status == vehicle::QualStatus::Flown) ++voles;
      if (p.status == vehicle::QualStatus::Speculative) ++speculatifs;
    }
    CHECK(voles > speculatifs,
          "12.1 : le catalogue est domine par des pieces REELLES, pas des concepts");
    CHECK(speculatifs > 0, "12.1 : le speculatif existe, mais marque comme tel");

    // Ancrages sur des valeurs publiques verifiables.
    const auto* rs25 = vehicle::find_engine("RS-25");
    CHECK(rs25 != nullptr, "12.1 : le RS-25 est au catalogue");
    if (rs25) {
      CHECK_NEAR(rs25->isp_vac_s, 452.3, 1e-9, "RS-25 : Isp vide publie");
      CHECK(rs25->confidence == vehicle::PartConfidence::A,
            "RS-25 : donnee de niveau A (135 vols)");
    }
    const auto* nstar = vehicle::find_engine("NSTAR");
    CHECK(nstar != nullptr && nstar->thrust_vac_n < 1.0,
          "6.4 : un ion a grilles pousse moins d un newton");
    const auto* f1 = vehicle::find_engine("F-1");
    CHECK(f1 != nullptr && f1->thrust_vac_n > 7.0e6,
          "12.1 : le F-1 reste le plus puissant du catalogue");

    // LE MATÉRIEL RÉEL DÉBORDE PARFOIS DU TABLEAU, et on doit le savoir.
    // Le SPT-100 pousse 83 mN quand 6.4 ecrit 0,1 N pour le Hall ; le NERVA NRX
    // poussait 334 kN quand 6.4 ecrit 10-100 kN pour le NTP. Entre une donnee
    // mesuree et un ordre de grandeur redactionnel, la donnee gagne [12.3.1].
    const auto* spt = vehicle::find_engine("SPT-100");
    const auto* nerva = vehicle::find_engine("NERVA-NRX");
    CHECK(spt && vehicle::engine_outside_literal_thrust_band(*spt),
          "12.3.1 : le SPT-100 sort de la bande litterale, et c est signale");
    CHECK(nerva && vehicle::engine_outside_literal_thrust_band(*nerva),
          "12.3.1 : le NERVA NRX sort de la bande litterale, et c est signale");
    CHECK(spt && vehicle::engine_within_family_envelope(*spt),
          "6.4 : ... mais il reste dans l ORDRE de grandeur");
    // La majorite des pieces, elle, doit tomber dans la bande litterale : si ce
    // n etait pas le cas, ce serait la CLASSIFICATION qui serait fausse.
    int hors_bande = 0;
    for (const auto& p : moteurs)
      if (vehicle::engine_outside_literal_thrust_band(p)) ++hors_bande;
    CHECK(hors_bande * 4 < static_cast<int>(moteurs.size()),
          "6.4 : les debordements restent des exceptions, pas la regle");

    // Le catalogue DECRIT, Vehicle.hpp CALCULE : la conversion doit conserver
    // la physique, pas la reinventer.
    if (rs25) {
      const vehicle::Engine e = vehicle::to_engine(*rs25);
      CHECK_NEAR(e.ve(), rs25->isp_vac_s * cst::G0, 1e-9,
                 "conversion : ve = Isp.g0, aucune valeur reinventee");
      CHECK_NEAR(e.mdot(), e.thrust_vac / e.ve(), 1e-9,
                 "conversion : mdot = F/ve");
      CHECK(e.heritage > 0.9, "conversion : une piece volee a un heritage fort");
    }
    const auto* fus = vehicle::find_engine("FUSION-DD");
    if (fus) CHECK(vehicle::to_engine(*fus).heritage < 0.2,
                   "12.5 : une piece speculative n herite d aucune confiance");

    // Reservoirs et capsules : memes exigences.
    CHECK(vehicle::tank_catalog().size() >= 5, "12.1 : reservoirs au catalogue");
    for (const auto& t : vehicle::tank_catalog()) {
      CHECK(t.dry_fraction > 0.0 && t.dry_fraction < 0.5,
            "12.1 : fraction seche physiquement plausible");
      CHECK(t.density_kg_m3 > 0.0, "12.1 : densite renseignee");
      CHECK(std::string(t.source).size() > 0, "12.1 : reservoir source");
    }
    CHECK(vehicle::capsule_catalog().size() >= 4, "12.1 : capsules au catalogue");
    for (const auto& c : vehicle::capsule_catalog()) {
      CHECK(c.nose_radius_m > 0.0 && c.cd_hypersonic > 0.0 && c.area_m2 > 0.0,
            "7.6 : une capsule est un CORPS DE RENTREE parametre");
      CHECK(c.max_entry_g > 0.0, "8.5 : chaque capsule porte sa limite structurale");
      CHECK(std::string(c.lineage).size() > 0, "12.1 : capsule de lignee reelle");
    }
    // Le module Apollo doit redonner le coefficient balistique attendu.
    const auto* apollo = vehicle::find_capsule("APOLLO-CM");
    if (apollo) {
      const double B = apollo->dry_mass_kg / (apollo->cd_hypersonic * apollo->area_m2);
      CHECK(B > 300.0 && B < 360.0, "12.1 : B du module Apollo ~ 340 kg/m2");
    }
  }

  // ═══════════ SITES DE LANCEMENT — GÉOMÉTRIE RÉELLE [GDD 13.3] ═══════════
  {
    using economy::launch_sites;
    const auto& sites = launch_sites();
    CHECK(sites.size() >= 3, "13.3 : Kourou, Cape Canaveral, Baikonour");

    const economy::LaunchSite* kourou = nullptr;
    const economy::LaunchSite* cap = nullptr;
    const economy::LaunchSite* baik = nullptr;
    for (const auto& s : sites) {
      if (s.name == "Kourou") kourou = &s;
      if (s.name == "Cape Canaveral") cap = &s;
      if (s.name == "Baikonour") baik = &s;
    }
    CHECK(kourou && cap && baik, "13.3 : les trois sites nommes sont presents");

    // 1) L INCLINAISON MINIMALE EST LA LATITUDE. On ne descend jamais dessous
    // sans dog-leg — c est de la trigonometrie spherique, pas un reglage.
    CHECK(!cap->reachable(20.0), "13.3 : Cap Canaveral (28,5 deg) ne peut PAS viser 20 deg");
    CHECK(cap->reachable(28.5), "13.3 : ... mais atteint sa propre latitude");
    CHECK(!baik->reachable(40.0), "13.3 : Baikonour (46 deg) ne descend pas a 40 deg");
    CHECK(baik->reachable(51.6), "13.3 : ... et sert bien l ISS a 51,6 deg");

    // 2) L AZIMUT REQUIS SUIT cos(i) = sin(beta).cos(phi).
    // A i = phi, le tir est PLEIN EST (90 deg).
    const auto az_eq = kourou->required_azimuth_deg(kourou->latitude_deg);
    CHECK(az_eq.has_value(), "13.3 : azimut defini a l inclinaison minimale");
    if (az_eq) CHECK(std::fabs(*az_eq - 90.0) < 1e-6,
                     "13.3 : i = latitude => tir plein est (azimut 90 deg)");
    // Une orbite polaire (i = 90) se tire plein NORD (azimut 0), depuis tout site.
    const auto az_pol = cap->required_azimuth_deg(90.0);
    CHECK(az_pol.has_value() && std::fabs(*az_pol) < 1e-6,
          "13.3 : orbite polaire => tir plein nord (azimut 0)");
    // Sous la latitude : AUCUN azimut ne convient.
    CHECK(!baik->required_azimuth_deg(30.0).has_value(),
          "13.3 : pas d azimut sous la latitude du site");

    // 3) LE COULOIR D AZIMUT BORNE LE RESTE. Cape Canaveral, couloir 35-120 deg,
    // ne peut pas tirer plein nord : pas de polaire depuis la Floride (survol
    // des cotes habitees) — contrainte geographique reelle.
    CHECK(!cap->azimuth_allowed(0.0),
          "13.3 : pas de tir plein nord depuis Cape Canaveral (couloir 35-120)");
    CHECK(cap->azimuth_allowed(90.0), "13.3 : tir est autorise depuis la Floride");
    // Kourou, couloir large chevauchant le nord, atteint le polaire.
    CHECK(kourou->reachable(90.0), "13.3 : Kourou peut viser une orbite polaire");

    // 4) LA ROTATION TERRESTRE : « GTO roi » pour Kourou. Le gain plein est a
    // l equateur vaut omega.R = 465 m/s ; a 5 deg de latitude, ~463 m/s.
    const double g_kourou = kourou->rotation_velocity_gain(kourou->latitude_deg);
    CHECK(g_kourou > 455.0 && g_kourou < 466.0,
          "13.3 : Kourou tire ~463 m/s de la rotation terrestre (GTO roi)");
    // Plus la latitude monte, moins la rotation aide : c est ce qui degrade
    // Baikonour pour le GTO malgre son cout logistique plus bas.
    const double g_baik = baik->rotation_velocity_gain(baik->latitude_deg);
    CHECK(g_baik < g_kourou,
          "13.3 : un site plus haut en latitude tire MOINS de la rotation");
    // Le gain plein est a l equateur est le maximum theorique.
    CHECK_NEAR(cst::OMEGA_EARTH * cst::R_EARTH, 465.1, 0.01,
               "13.3 : gain equatorial plein est = omega.R ~ 465 m/s");
    // Viser plus haut que la latitude reduit le gain (l azimut s ecarte de l est).
    CHECK(kourou->rotation_velocity_gain(60.0) <
          kourou->rotation_velocity_gain(kourou->latitude_deg),
          "13.3 : viser une inclinaison plus haute reduit l aide de la rotation");

    // 5) COUTS RELATIFS — arbitrage technique ET budgetaire [GDD 13.3].
    CHECK(baik->cost_factor < kourou->cost_factor,
          "13.3 : Baikonour moins cher, mais moins bien place");
  }

  // ═══════════ DÉGRADATION DES FILIÈRES AVANCÉES [GDD 12.4] ═══════════
  {
    using namespace fen::reliability;

    // 1) CŒURS NUCLÉAIRES : la fiabilite decroit avec le burnup et le temps.
    CHECK_NEAR(nuclear_core_reliability(0.0, 0.0), 1.0, 1e-12,
               "12.4 : un coeur neuf part de 1");
    CHECK(nuclear_core_reliability(0.5, 0.0) < nuclear_core_reliability(0.1, 0.0),
          "12.4 : plus de burnup = moins fiable");
    CHECK(nuclear_core_reliability(0.3, 10.0) < nuclear_core_reliability(0.3, 0.0),
          "12.4 : le vieillissement calendaire degrade aussi");
    // Le NTP se degrade PLUS VITE : cyclage thermique de l ergol dans le coeur.
    CHECK(nuclear_core_reliability(0.5, 0.0, true) <
          nuclear_core_reliability(0.5, 0.0, false),
          "12.4 : le NTP fatigue plus vite que la fission de puissance");
    CHECK(nuclear_core_reliability(2.0, 0.0) > 0.0,
          "12.4 : la fiabilite reste positive, jamais un verdict binaire");

    // 2) RADIATEURS : perces par les debris, ils perdent leur capacite. On
    // BRANCHE sur l environnement de debris deja modelise.
    env::DebrisEnvironment env_prop;               // couloir propre
    const env::Corridor leo{600.0, 1000.0, "LEO haute"};
    RadiatorWear rad{200.0, 0.15};                 // grand radiateur NEP
    CHECK_NEAR(radiator_capacity_fraction(rad, env_prop, leo, 365.0), 1.0, 1e-9,
               "12.4 : un couloir propre n erode pas le radiateur");
    // Un couloir pollue erode : la capacite chute avec le temps d exposition.
    env::DebrisEnvironment env_sale;
    env_sale.add_breakup("VOL-X", 800.0, 8000.0, env::BreakupKind::Collision, 0.0);
    const double c1 = radiator_capacity_fraction(rad, env_sale, leo, 100.0);
    const double c2 = radiator_capacity_fraction(rad, env_sale, leo, 400.0);
    CHECK(c1 < 1.0 && c2 < c1, "12.4 : le radiateur s erode avec l exposition");
    CHECK(c2 > 0.0, "12.4 : erosion continue, jamais un mur");
    // Un GRAND radiateur (NEP/fusion) est PLUS vulnerable qu un petit : c est le
    // point precis du GDD (facteur dominant de vulnerabilite).
    RadiatorWear petit{20.0, 0.15};
    CHECK(radiator_capacity_fraction(petit, env_sale, leo, 400.0) >
          radiator_capacity_fraction(rad, env_sale, leo, 400.0),
          "12.4 : un grand radiateur est plus vulnerable — critique pour NEP/fusion");
    // Verrou thermique : si la capacite tombe sous la charge, la mission est bloquee.
    CHECK(thermal_still_ok(1.0, 1000.0, 800.0), "12.4 : radiateur neuf couvre la charge");
    CHECK(!thermal_still_ok(0.5, 1000.0, 800.0),
          "12.4 : radiateur a moitie erode ne couvre plus 800 W — mission bloquee");

    // 3) CONFINEMENT ANTIMATIERE : perte = CATASTROPHE, donc on modelise la
    // survie, pas une performance qui grignote.
    CHECK_NEAR(antimatter_confinement_survival(0.0, 1.0), 1.0, 1e-12,
               "12.4 : survie certaine a t=0");
    CHECK(antimatter_confinement_survival(100.0, 1.0) < 1.0,
          "12.4 : le risque de perte croit avec la duree");
    // Un confinement de MAUVAISE qualite echoue bien plus vite.
    CHECK(antimatter_confinement_survival(100.0, 0.2) <
          antimatter_confinement_survival(100.0, 1.0),
          "12.4 : un confinement immature perd bien plus souvent");
    // Survie + perte = 1 (c est un complement, pas deux modeles independants).
    CHECK_NEAR(antimatter_confinement_survival(200.0, 0.5) +
               antimatter_confinement_loss_prob(200.0, 0.5), 1.0, 1e-12,
               "12.4 : survie et perte sont complementaires");
    // Sur une mission relativiste de plusieurs annees, meme un bon confinement
    // porte un risque NON negligeable : c est ce qui fait de l antimatiere un
    // changement de REGIME, pas un meilleur moteur [GDD 19.3].
    CHECK(antimatter_confinement_loss_prob(3.0 * 365.0, 1.0) > 0.5,
          "19.3 : sur 3 ans, la perte de confinement est un risque majeur");

    // 4) PERFORATION SUB-MILLIMÉTRIQUE — le mécanisme qui était DÉCLARÉ MANQUANT
    // « faute d un modele de flux que rien dans le depot ne porte » [GDD 12.4].
    // Il en porte un : Grun 1985 + Cour-Palais (env/Micrometeoroid.hpp). Ici on
    // ne verifie plus la physique (test_gdd_manques le fait) mais la CONSEQUENCE
    // DE MISSION : un radiateur paye pour une endurance, et ce que coute d aller
    // au-dela.
    const double aire = 1000.0;                    // aile de NEP megawatt
    const double paroi = 1.5;
    // A l endurance exacte, la survie doit etre HAUTE — c est tout le sens du 3
    // sigma de dimensionnement. Un radiateur taille sur la MOYENNE aurait une
    // chance sur deux, ce qui serait un dimensionnement absurde.
    const double s_endurance = radiator_load_survival(aire, 900.0, 900.0, paroi);
    CHECK(s_endurance > 0.99,
          "12.4 : a l endurance exacte, le radiateur tient (3 sigma de marge)");
    // Voler MOINS longtemps ne rembourse pas la marge : elle est deja construite.
    CHECK(radiator_load_survival(aire, 300.0, 900.0, paroi) >= s_endurance,
          "12.4 : voler plus court ne coute rien");
    // Voler NETTEMENT plus longtemps que l endurance achetee : la sanction tombe,
    // et elle est severe parce que la queue de Poisson est raide.
    const double s_double = radiator_load_survival(aire, 1826.0, 900.0, paroi);
    std::printf("     PERFORATION [12.4] : aile de %.0f m2, paroi %.1f mm, endurance"
                " 900 j -> survie a 900 j = %.5f, a 1826 j = %.3e\n",
                aire, paroi, s_endurance, s_double);
    CHECK(s_double < 0.01,
          "12.4 : voler deux fois l endurance du radiateur = echec thermique");
    CHECK(s_double < s_endurance, "12.4 : la sanction est monotone en duree");
    // Une paroi plus epaisse rachete l endurance — c est le levier du joueur.
    CHECK(radiator_load_survival(aire, 1826.0, 900.0, 3.0) >
          radiator_load_survival(aire, 1826.0, 900.0, 1.5),
          "12.4 : blinder rachete le depassement d endurance");
    // Pas de radiateur, pas de sanction : un chimique ne paie rien de tout ceci.
    CHECK(radiator_load_survival(0.0, 1826.0, 900.0, paroi) == 1.0,
          "12.4 : sans radiateur, aucune penalite de perforation");
    // La queue de Poisson est une VRAIE loi, pas un seuil deguise.
    CHECK_NEAR(poisson_cdf(0.0, 0.0), 1.0, 1e-12, "Poisson : moyenne nulle = certitude");
    CHECK(poisson_cdf(5.0, 0.0) > 0.0 && poisson_cdf(5.0, 0.0) < 0.01,
          "Poisson : P(K=0) pour moyenne 5 vaut e^-5");
    CHECK_NEAR(poisson_cdf(3.0, 0.0), std::exp(-3.0), 1e-12,
               "Poisson : P(K<=0) = e^-moyenne, exactement");
    CHECK(poisson_cdf(10.0, 100.0) > 0.9999, "Poisson : la queue haute sature a 1");
    CHECK(poisson_cdf(1000.0, 1000.0) > 0.4 && poisson_cdf(1000.0, 1000.0) < 0.6,
          "Poisson : a la moyenne, la CDF vaut ~0,5 (approximation normale)");
  }

  // ═══════════ CE QUE LA PERFORATION CHANGE AU VÉHICULE [GDD 12.4, 6.5] ═══════
  // La marge de surface du radiateur n'est plus un forfait de 1,15 : elle est
  // DÉRIVÉE du flux de Grün. La question qui décide si le travail valait quelque
  // chose n'est pas « le modèle est-il joli » mais « de combien la calibration
  // bouge-t-elle ». On la mesure au lieu de l'espérer.
  {
    using namespace fen::vehicle;
    // Le forfait d'avant : endurance nulle => on retombe sur redundancy_margin
    // = 1,15, et pas de blindage a payer (paroi de reference).
    env::RadiatorSpec forfait{};
    forfait.endurance_days = 0.0;
    forfait.wall_mm = env::RADIATOR_WALL_BASELINE_MM;

    // ON MESURE SUR LES DEUX BOUTS DE L'ÉCHELLE, parce que c'est exactement là que
    // le forfait unique se casse : la marge derivee suit 1/racine(N), donc elle
    // depend de la TAILLE de l aile. Un propulseur ionique de quelques kW et une
    // NEP megawatt ne sont pas le meme objet statistique.
    const EnginePart* petit = nullptr;   // radiateur de quelques m2
    const EnginePart* gros = nullptr;    // aile de plusieurs centaines de m2
    for (const auto& p : engine_catalog()) {
      const PowerPlant pp = power_plant_for(p, PropTier::Fission);
      if (pp.radiator_area_m2 <= 0.0) continue;
      if (!petit || pp.radiator_area_m2 < power_plant_for(*petit, PropTier::Fission).radiator_area_m2)
        petit = &p;
      if (!gros || pp.radiator_area_m2 > power_plant_for(*gros, PropTier::Fission).radiator_area_m2)
        gros = &p;
    }
    CHECK(petit != nullptr && gros != nullptr,
          "12.4 : le catalogue porte des filieres alimentees de deux echelles");
    if (petit && gros) {
      double ecart_gros = 1.0, delta_petit_kg = 0.0;
      for (const EnginePart* p : {petit, gros}) {
        const PowerPlant der = power_plant_for(*p, PropTier::Fission);
        const PowerPlant frf = power_plant_for(*p, PropTier::Fission, forfait);
        const double ecart = der.radiator_mass_kg / frf.radiator_mass_kg;
        std::printf("     CALIBRATION [12.4] : %-9s aile %8.1f m2 (%5.1f circuits) :"
                    " forfait 1,15 = %8.1f kg -> derive + blindage = %8.1f kg"
                    " (x%.3f, %+.1f kg)\n",
                    p->id, der.radiator_area_m2,
                    env::radiator_segment_count(der.radiator_area_m2),
                    frf.radiator_mass_kg, der.radiator_mass_kg, ecart,
                    der.radiator_mass_kg - frf.radiator_mass_kg);
        CHECK(ecart > 1.0, "12.4 : deriver la marge ne rend jamais le radiateur gratuit");
        if (p == gros) ecart_gros = ecart;
        else delta_petit_kg = der.radiator_mass_kg - frf.radiator_mass_kg;
      }
      // LE RÉSULTAT QUI DÉCIDE SI LE TRAVAIL ÉTAIT ACCEPTABLE : sur l aile
      // DOMINANTE — celle qui pese des TONNES et qui fixe la calibration du jeu —
      // le remplacement du forfait par le calcul ne deplace presque rien. La
      // marge derivee y est PLUS BASSE que 1,15 (grande aile, pertes moyennees) et
      // le blindage a payer reprend a peu pres ce qu elle rend.
      CHECK(ecart_gros < 1.10,
            "12.4 : sur l aile dominante, deriver la marge deplace moins de 10 %");
      // SUR UNE PETITE AILE, L'ÉCART RELATIF EST GROS ET NE VEUT RIEN DIRE. Un
      // radiateur de 2 m² n'a que quatre circuits pour moyenner ses pertes, donc le
      // 3σ y exige +40 % de surface : c'est de la statistique juste, mais appliquee
      // a un pas de tube (0,40 m²) mesure sur un PANNEAU DE 8,8 m². C'est la sortie
      // la MOINS fiable du modele, et elle porte sur quelques kilos — pas de quoi
      // deplacer une calibration. On verrouille donc l ABSOLU, pas le ratio.
      CHECK(std::fabs(delta_petit_kg) < 10.0,
            "12.4 : sur une petite aile l ecart pese quelques kilos, pas une tonne");
      // Le blindage est de la masse SÈCHE : Tsiolkovsky le paie.
      const PowerPlant der_gros = power_plant_for(*gros, PropTier::Fission);
      CHECK(der_gros.radiator_mass_kg > 0.0 && der_gros.radiator_area_m2 > 0.0,
            "6.5 : la filiere alimentee traine toujours ses radiateurs");
      // Le point [GDD 6.5] : plus d endurance = plus de surface = plus de masse.
      env::RadiatorSpec longue{};
      longue.endurance_days = 3650.0;
      CHECK(power_plant_for(*gros, PropTier::Fission, longue).radiator_mass_kg
                > der_gros.radiator_mass_kg,
            "6.5 : construire pour dix ans coute plus de radiateur que pour trois");
    }
  }

  // ═══════════ ATELIER D'ASSEMBLAGE — TSIOLKOVSKY [GDD 12.2] ═══════════
  {
    using namespace fen::app;
    // La conception de départ : deux étages chimiques, doit être faisable.
    VehicleDesign d = VehicleDesign::starter();
    DesignSummary s = evaluate_design(d);
    CHECK(s.valid, "12.2 : la conception de depart est valide");
    CHECK(s.converged, "12.2 : le sizing converge");
    CHECK(s.liftoff_capable, "6.3 : l etage du bas (chimique) peut decoller");
    CHECK(s.warning.empty(), "12.2 : aucune alerte sur une conception saine");
    CHECK(s.stages.size() == 2, "12.2 : deux etages lus dans le tableau");

    // Le Δv total est la SOMME des Δv confiés — pas une valeur inventee.
    CHECK_NEAR(s.total_dv_ms, d.stages[0].dv_target_ms + d.stages[1].dv_target_ms,
               1e-9, "12.2 : Delta-v total = somme des etages");

    // TSIOLKOVSKY : demander PLUS de Δv coute EXPONENTIELLEMENT plus d ergols.
    VehicleDesign d2 = d;
    d2.stages[1].dv_target_ms += 2000.0;
    DesignSummary s2 = evaluate_design(d2);
    CHECK(s2.liftoff_mass_kg > s.liftoff_mass_kg,
          "6.1 : plus de Delta-v = plus de masse au decollage");
    // La croissance est SUR-lineaire (exponentielle) : doubler la marge ne
    // double pas la masse, il l'exponentie.
    const double ratio_dv = s2.total_dv_ms / s.total_dv_ms;
    const double ratio_m = s2.liftoff_mass_kg / s.liftoff_mass_kg;
    CHECK(ratio_m > ratio_dv, "6.1 : la masse croit plus vite que le Delta-v");

    // Une charge utile plus lourde alourdit tout le lanceur (effet cascade).
    VehicleDesign d3 = d;
    d3.payload_kg += 2000.0;
    CHECK(evaluate_design(d3).liftoff_mass_kg > s.liftoff_mass_kg,
          "6.1 : une charge utile plus lourde alourdit tout le lanceur");

    // Une capsule AJOUTE sa masse seche a la charge utile.
    VehicleDesign d4 = d;
    d4.capsule = 0;   // module Apollo
    CHECK(evaluate_design(d4).payload_kg > s.payload_kg,
          "12.1 : la capsule s ajoute a la charge utile");

    // VALIDATION 6.3 : un etage du bas ELECTRIQUE ne peut pas decoller. Le
    // modele le DIT (il ne bloque pas — c est une validation, pas un mur).
    VehicleDesign elec = d;
    elec.stages[0].engine = VehicleDesign::index_moteur("NSTAR");  // ion a grilles
    DesignSummary se = evaluate_design(elec);
    CHECK(!se.liftoff_capable, "6.3 : un ion a grilles ne decolle pas");
    CHECK(!se.warning.empty(), "6.3 : l infaisabilite au decollage est signalee");

    // Retirer tous les etages sauf un reste valide ; zero etage est invalide.
    VehicleDesign vide;
    CHECK(!evaluate_design(vide).valid, "12.2 : aucun etage = conception invalide");

    // Le partage du Δv est au JOUEUR : le modele n optimise pas. Deux partages
    // differents du meme Δv total donnent des masses differentes (l etagement
    // compte), et c est a lui de trancher [anti-feature 1.5].
    VehicleDesign a = d, b = d;
    a.stages[0].dv_target_ms = 2000.0; a.stages[1].dv_target_ms = 6100.0;
    b.stages[0].dv_target_ms = 6100.0; b.stages[1].dv_target_ms = 2000.0;
    const double ma = evaluate_design(a).liftoff_mass_kg;
    const double mb = evaluate_design(b).liftoff_mass_kg;
    CHECK(std::fabs(ma - mb) > 1.0, "1.5 : le partage du Delta-v change la masse (choix du joueur)");
  }

  // ═══════════ UNE FILIÈRE ALIMENTÉE TRAÎNE SA CENTRALE [GDD 5.12.1, 6.2, 6.5] ═
  // Le modèle DISAIT « energie != propulsion » et ne l'appliquait nulle part :
  // `source_mass_kg`, `PoweredPropulsion` et tout `env/Thermal.hpp` n'avaient
  // AUCUN appelant vivant. Un etage NEP-1MW coutait 900 kg de tuyere et rendait
  // Isp 5000 s — toute la branche 6 etait une amelioration STRICTE, ce que
  // [GDD 6.2] interdit en une ligne.
  {
    using namespace fen::vehicle;
    using namespace fen::app;

    // --- 1) LA PUISSANCE NE SE SAISIT PAS, ELLE SE DEDUIT [GDD 6.2] ----------
    // F = 2.eta.P/ve se retourne. Le test n'est PAS que la formule s'execute :
    // c'est qu'appliquee aux poussees et Isp du catalogue, elle RETROUVE la
    // puissance d'entree PUBLIEE de propulseurs reels. Trois pieces volees, et
    // une quatrieme dont le NOM porte la reponse.
    auto p_of = [](const char* id) {
      const auto& v = engine_catalog();
      for (const auto& e : v)
        if (std::string(e.id) == id)
          return power_required_w(e.thrust_vac_n, e.isp_vac_s * cst::G0,
                                  jet_efficiency(e.family));
      return 0.0;
    };
    struct Recoupe { const char* id; double publie_w; const char* mission; };
    const Recoupe recoupes[] = {
      {"NSTAR",   2300.0,    "Deep Space 1 / Dawn"},
      {"NEXT-C",  7400.0,    "DART"},
      {"SPT-100", 1350.0,    "plateformes GEO"},
      {"NEP-1MW", 1000000.0, "le nom de la piece"},
    };
    for (const auto& r : recoupes) {
      const double p = p_of(r.id);
      const double ecart = std::fabs(p - r.publie_w) / r.publie_w;
      std::printf("     puissance : %-8s deduite %9.0f W  publiee %9.0f W  (%.0f %%) — %s\n",
                  r.id, p, r.publie_w, 100.0 * ecart, r.mission);
      CHECK(ecart < 0.15,
            "6.2 : la puissance deduite retrouve la puissance d entree publiee");
    }

    // --- 2) LA PUISSANCE SPECIFIQUE EST UNE ECHELLE, PAS UN INTERVALLE -------
    // Appliquer les 5 W/kg d'un Kilopower de 10 kWe a un reacteur de 1 MWe est
    // une erreur de CATEGORIE — elle rendrait la NEP absurdement lourde, donc
    // refusee pour une mauvaise raison (piege n°77).
    CHECK_NEAR(specific_power_w_per_kg(PropTier::Fission, SPECIFIC_POWER_REF_LO_W),
               energy_source(PropTier::Fission)->specific_power_min_w_per_kg, 1e-9,
               "5.12.8 : a l echelle du Kilopower, le bas de la fourchette");
    CHECK_NEAR(specific_power_w_per_kg(PropTier::Fission, SPECIFIC_POWER_REF_HI_W),
               energy_source(PropTier::Fission)->specific_power_max_w_per_kg, 1e-9,
               "5.12.8 : a l echelle du multi-megawatt, le haut de la fourchette");
    CHECK(specific_power_w_per_kg(PropTier::Fission, 1.0e6) >
          specific_power_w_per_kg(PropTier::Fission, 1.0e5),
          "5.12.8 : la puissance specifique s ameliore avec l echelle");
    CHECK(specific_power_w_per_kg(PropTier::Fission, 1.0e12) <=
          energy_source(PropTier::Fission)->specific_power_max_w_per_kg,
          "12.5 : ... et reste bornee par la fourchette declaree");
    // Le solaire de Deep Space 1 : ~2 kW, donc le bas de l echelle. Le panneau
    // SCARLET pesait ~50 kg pour 2,5 kW — l ordre de grandeur est retrouve.
    const double m_sol = power_plant_mass_kg(PropTier::Solar, p_of("NSTAR"));
    std::printf("     centrale : solaire pour NSTAR -> %.0f kg (SCARLET de DS1 : ~50 kg)\n", m_sol);
    CHECK(m_sol > 20.0 && m_sol < 80.0,
          "5.12.5 : la centrale solaire d une sonde ionique pese quelques dizaines de kg");

    // --- 3) LE RECOUPEMENT QUI COMPTE : alpha en kg/kWe ---------------------
    // C'est sous cette forme que la litterature publie ces systemes. Une NEP
    // megawatt de l etat de l art se situe entre 20 et 45 kg/kWe ; si le modele
    // en sortait, c'est le modele qui aurait tort, pas la litterature.
    const EnginePart* nep = nullptr;
    for (const auto& e : engine_catalog()) if (std::string(e.id) == "NEP-1MW") nep = &e;
    CHECK(nep != nullptr, "12.1 : la piece NEP-1MW existe au catalogue");
    const PowerPlant pp_nep = power_plant_for(*nep, PropTier::Fission);
    std::printf("     centrale : NEP 1 MWe -> reacteur %.0f kg + radiateurs %.0f kg "
                "(%.0f m2) = %.1f kg/kWe\n",
                pp_nep.source_mass_kg, pp_nep.radiator_mass_kg,
                pp_nep.radiator_area_m2, pp_nep.alpha_kg_per_kwe());
    CHECK(pp_nep.alpha_kg_per_kwe() > 20.0 && pp_nep.alpha_kg_per_kwe() < 45.0,
          "5.12.10 : la centrale NEP tombe dans la bande publiee 20-45 kg/kWe");
    CHECK(pp_nep.total_mass_kg() > 20.0 * nep->mass_kg,
          "5.12.1 : la centrale pese vingt fois la tuyere — energie != propulsion");

    // --- 4) SEUL UN CYCLE THERMIQUE EXIGE DES RADIATEURS DEDIES [GDD 6.5] ---
    // Un panneau et un RTG rejettent leur chaleur par leur propre surface ; un
    // reacteur a 30 % de rendement jette 2,33 fois ce qu il produit.
    // MEME propulseur, deux sources : seule la part REACTEUR change, et elle
    // suffit a multiplier le radiateur par pres de sept.
    const PowerPlant pp_sol = power_plant_for(*nep, PropTier::Solar);
    CHECK_NEAR(pp_sol.waste_heat_w,
               (1.0 - jet_efficiency(nep->family)) * pp_sol.p_electric_w, 1e-6,
               "6.5 : sur panneau, il ne reste a rejeter que les pertes du jet");
    CHECK_NEAR(pp_nep.waste_heat_w - pp_sol.waste_heat_w,
               fen::env::reactor_waste_heat(pp_nep.p_electric_w,
                                            energy_source(PropTier::Fission)->eta_thermal),
               1e-6, "6.5 : tout l ecart est la chaleur residuelle du reacteur");
    std::printf("     centrale : meme tuyere, radiateurs %.0f kg sur panneau "
                "contre %.0f kg sur reacteur\n",
                pp_sol.radiator_mass_kg, pp_nep.radiator_mass_kg);
    CHECK(pp_nep.radiator_mass_kg > 5.0 * pp_sol.radiator_mass_kg,
          "6.5 : le rejet thermique du reacteur ecrase celui d un panneau");
    CHECK(pp_nep.waste_heat_w > 2.0 * pp_nep.p_electric_w,
          "6.5 : a 30 % de rendement, un reacteur jette plus qu il ne produit");

    // --- 5) LES FILIERES NON ALIMENTEES NE PAIENT RIEN (non-regression) -----
    for (const char* id : {"RL10C-1", "RD-180", "SRB-P80", "NERVA-NRX"}) {
      const EnginePart* e = nullptr;
      for (const auto& x : engine_catalog()) if (std::string(x.id) == id) e = &x;
      CHECK(e && !power_plant_for(*e, PropTier::Fission).needs_power,
            "6.2 : chimique et NTP ne reclament aucune puissance electrique");
      CHECK(e && power_plant_for(*e, PropTier::Fission).total_mass_kg() == 0.0,
            "6.2 : ... donc aucune centrale a porter");
    }

    // --- 6) LA FUSION PRODUIT SA PUISSANCE, PAS SA THERMIQUE [GDD 6.4] ------
    // « bilan net, confinement, MATERIAUX » : le facteur limitant que le GDD lui
    // attribue est celui qui sort du calcul, sans qu on l y ait mis.
    const EnginePart* fus = nullptr;
    for (const auto& e : engine_catalog()) if (std::string(e.id) == "FUSION-DD") fus = &e;
    const PowerPlant pp_fus = power_plant_for(*fus, PropTier::Chemical);
    CHECK(pp_fus.self_powered && pp_fus.source_mass_kg == 0.0,
          "5.12.1 : la fusion EST sa propre source — pas de reacteur en plus");
    CHECK(!pp_fus.source_missing,
          "5.12.1 : ... elle ne reclame donc aucun choix de source");
    std::printf("     centrale : fusion D-D -> %.0f MW rejetes, radiateurs %.0f t (%.0f m2)\n",
                pp_fus.waste_heat_w / 1e6, pp_fus.radiator_mass_kg / 1000.0,
                pp_fus.radiator_area_m2);
    CHECK(pp_fus.radiator_mass_kg > fus->mass_kg,
          "6.5 : ses radiateurs pesent plus que sa tuyere — le mur est thermique");

    // --- 7) ET DANS L ATELIER, TSIOLKOVSKY LA PAIE [GDD 6.1, 12.2] ----------
    VehicleDesign nepd = VehicleDesign::starter();
    nepd.stages[1].engine = VehicleDesign::index_moteur("NEP-1MW");
    nepd.stages[1].dv_target_ms = 4600.0;
    const DesignSummary sans = evaluate_design(nepd);      // aucune source choisie
    CHECK(!sans.power_ok && !sans.warning.empty(),
          "5.12.1 : une filiere alimentee sans source est SIGNALEE, pas ignoree");
    CHECK(sans.powerplant_mass_kg == 0.0,
          "5.12.1 : ... et la masse affichee ne ment pas en pesant une centrale absente");

    nepd.stages[1].source = PropTier::Fission;
    const DesignSummary avec = evaluate_design(nepd);
    CHECK(avec.power_ok, "5.12.1 : la source choisie leve le motif");
    CHECK_NEAR(avec.powerplant_mass_kg, pp_nep.total_mass_kg(), 1e-6,
               "6.1 : la centrale de l etage est celle que la filiere reclame");
    std::printf("     atelier : etage NEP -> +%.0f t de centrale, decollage %.0f t "
                "(sans centrale : %.0f t)\n",
                avec.powerplant_mass_kg / 1000.0, avec.liftoff_mass_kg / 1000.0,
                sans.liftoff_mass_kg / 1000.0);
    CHECK(avec.liftoff_mass_kg > sans.liftoff_mass_kg,
          "6.1 : la centrale est de la masse seche, donc Tsiolkovsky la paie");
    CHECK(avec.stages[1].power.p_electric_w > 1.0e6,
          "6.2 : l etage lit la puissance que sa filiere reclame");
    // ET LE CHIMIQUE EST INCHANGE : la passe n a pas deplace ce qui marchait.
    const DesignSummary chim = evaluate_design(VehicleDesign::starter());
    CHECK(chim.powerplant_mass_kg == 0.0 && chim.warning.empty(),
          "12.2 : une pile chimique ne porte aucune centrale et reste saine");
  }

  // ═══════════ UNE SEULE SOURCE DE VERITE PAR MOTEUR [GDD 12.1] ═══════════
  // La couche gestion REECRIVAIT la physique de moteurs qui existent deja au
  // catalogue de pieces. Deux tables pour un meme objet ne restent pas d accord,
  // et celles-ci avaient DEJA diverge : l Aestus poussait 29 400 N cote gestion
  // et 29 600 N cote catalogue. Le test ne verifie pas que la valeur est bonne —
  // il verifie qu il n y a plus qu UN endroit ou elle puisse etre fausse.
  {
    using namespace fen::mission;
    using namespace fen::vehicle;
    int apparies = 0;
    for (const auto& o : engines()) {
      const EnginePart* p = find_engine(o.eng.id);
      if (!p) continue;                       // MTX-1 : poste de depense, pas une piece
      ++apparies;
      CHECK(o.eng.thrust_vac == p->thrust_vac_n && o.eng.isp_vac == p->isp_vac_s &&
            o.eng.mass == p->mass_kg && o.eng.max_restarts == p->max_restarts,
            "12.1 : la physique du moteur de programme EST celle du catalogue");
    }
    CHECK(apparies >= 2, "12.1 : au moins deux moteurs de programme sont des pieces reelles");
    // Le cas qui avait DEJA diverge, nomme.
    const EngineOption* aestus = nullptr;
    for (const auto& o : engines()) if (std::string(o.eng.id) == "AESTUS") aestus = &o;
    CHECK(aestus && aestus->eng.thrust_vac == find_engine("AESTUS")->thrust_vac_n,
          "12.1 : l Aestus ne pousse plus deux valeurs differentes selon la couche");
    // ...et ce que la fusion NE fait PAS : le prix, le delai et la courbe
    // d essais restent cote gestion, parce que le catalogue ne les porte pas.
    CHECK(aestus->unit_cost_musd > 0.0 && aestus->lead_months > 0.0 &&
          aestus->Rmax > aestus->R0,
          "12.1 : l approvisionnement reste ou il est — le catalogue ne le porte pas");
  }

  // ═══════════ TOUTES LES PIECES, SANS EXCEPTION [GDD 12.1, 5.4, 12.5] ═══════
  // « Assemblage a partir de pieces REELLES ou extrapolees de lignees reelles,
  // JAMAIS GENERIQUES. » La couche gestion n'offrait que TROIS moteurs, dont un
  // (« MTX-1 neuf ») sans aucune lignee. Les dix-huit pieces du catalogue sont
  // desormais commandables, et rien d autre ne l est.
  {
    using namespace fen::mission;
    using namespace fen::vehicle;

    CHECK(engines().size() == engine_catalog().size(),
          "12.1 : toutes les pieces du catalogue sont commandables, sans exception");
    for (const auto& o : engines())
      CHECK(find_engine(o.eng.id) != nullptr,
            "12.1 : ... et aucun moteur generique ne s y est glisse");

    // --- (a) LE TRIPLET DE PRIX EST OBLIGATOIRE [GDD 12.3.2, 12.3.4] --------
    // « Pas de precision artificielle » : la plupart des prix unitaires de
    // moteurs-fusees ne sont PAS publies, et un nombre nu le cacherait.
    int publies = 0, non_publies = 0;
    for (const auto& p : engine_catalog()) {
      CHECK(p.cost_lo_musd > 0.0 && p.cost_lo_musd <= p.cost_musd &&
            p.cost_musd <= p.cost_hi_musd,
            "12.3.2 : le triplet de prix est renseigne et ordonne");
      CHECK(std::string(p.cost_source).size() > 10,
            "12.3.1 : une valeur sans provenance est refusee");
      if (p.cost_confidence <= PartConfidence::B) ++publies; else ++non_publies;
    }
    std::printf("     prix : %d pieces sur %d ont une source publiee (A/B), %d sont des estimations declarees\n",
                publies, (int)engine_catalog().size(), non_publies);
    CHECK(publies >= 4, "12.1 : plusieurs prix viennent de contrats ou de prix constructeur reels");

    // Les ancrages REELS, nommes. Si un jour quelqu un les « arrondit », ceci tombe.
    CHECK_NEAR(find_engine("RS-25")->cost_musd, 146.0, 1e-9,
               "12.1 : RS-25 — 3,5 Md$ pour 24 moteurs, contrat NASA/Aerojet");
    CHECK_NEAR(find_engine("RS-25")->cost_lo_musd, 99.4, 1e-9,
               "12.1 : ... et 1,79 Md$ pour 18 en production seule");
    CHECK_NEAR(find_engine("F-1")->cost_musd, 21.0, 1e-9,
               "12.1 : F-1 — 76 moteurs pour 158,4 M$ en 1964, soit ~21 M$ 2026");
    CHECK(find_engine("RD-180")->cost_lo_musd < 10.0 &&
          find_engine("RD-180")->cost_hi_musd > 60.0,
          "12.3.4 : RD-180 — les sources publiques s etalent de 9,9 a 70 M$, et le triplet le DIT");

    // --- (b) LE PRINCIPE CONSERVATEUR JOUE SUR LE PRIX [GDD 12.5] ----------
    // Miroir exact de `reliability::evaluate` : une confiance basse tire vers la
    // borne PESSIMISTE, qui est le HAUT pour un prix. Une estimation floue ne
    // doit jamais rendre un programme moins cher qu une donnee mesuree.
    CHECK_NEAR(effective_cost_musd(*find_engine("RS-25")),
               find_engine("RS-25")->cost_musd, 1e-9,
               "12.5 : un prix de contrat (A) est pris a sa valeur");
    const EnginePart* nep2 = find_engine("NEP-1MW");
    CHECK_NEAR(effective_cost_musd(*nep2), nep2->cost_hi_musd, 1e-9,
               "12.5 : un prix inexistant (D) est pris a sa borne HAUTE");
    for (const auto& p : engine_catalog())
      CHECK(effective_cost_musd(p) >= p.cost_musd - 1e-12,
            "12.5 : le prix retenu ne descend JAMAIS sous le nominal");

    // --- (c) LA FIABILITE EST DERIVEE, ET ELLE REPRODUIT L ECRIT A LA MAIN --
    // C est ce qui rend la derivation legitime plutot que commode : elle rend
    // EXACTEMENT les deux triplets qu elle remplace.
    const EngineReliabilityCurve rl10 = reliability_curve_for(*find_engine("RL10C-1"));
    CHECK_NEAR(rl10.R0, 0.9980, 1e-12, "12.3 : RL10C-1 retrouve son R0 ecrit a la main");
    CHECK_NEAR(rl10.Rmax, 0.9995, 1e-12, "12.3 : ... et son asymptote");
    CHECK_NEAR(rl10.h_char, 500.0, 1e-12, "12.3 : ... et ses heures caracteristiques");
    const EngineReliabilityCurve aest = reliability_curve_for(*find_engine("AESTUS"));
    CHECK_NEAR(aest.R0, 0.9950, 1e-12, "12.3 : Aestus retrouve son R0 ecrit a la main");
    CHECK_NEAR(aest.Rmax, 0.9990, 1e-12, "12.3 : ... et son asymptote");
    // [GDD 12.5] : l absence de donnee n est JAMAIS une bonne fiabilite.
    CHECK(reliability_curve_for(*find_engine("FUSION-DD")).R0 <
          reliability_curve_for(*find_engine("NERVA-NRX")).R0,
          "12.5 : un concept est moins fiable qu un moteur qualifie au banc");
    CHECK(reliability_curve_for(*find_engine("NERVA-NRX")).R0 < aest.R0,
          "12.5 : ... et un moteur qualifie au banc, moins qu un moteur VOLE");

    // --- (d) LE DELAI EST DERIVE DU TRL, qui est deja sur la piece ----------
    CHECK(lead_months_for(*find_engine("RL10C-1")) < lead_months_for(*find_engine("BHT-6000")),
          "5.3 : un moteur en production s obtient plus vite qu un qualifie au banc");
    CHECK(lead_months_for(*find_engine("BHT-6000")) < lead_months_for(*find_engine("FUSION-DD")),
          "5.3 : ... et un concept TRL 1 n est pas un achat, c est un programme");

    // --- (e) LE DEVELOPPEMENT EST PAYE PAR L ARBRE, PAS PAR LA MISSION -----
    for (const auto& o : engines())
      CHECK(o.dev_cost_musd == 0.0,
            "5.4 : aucun cout de developpement sur la piece — l arbre le paie deja");

    // --- (f) LE RESERVOIR EST CELUI QUE LES ERGOLS IMPOSENT ----------------
    CHECK_NEAR(option_from_part(*find_engine("RL10C-1")).tank_dry_fraction,
               find_tank("TANK-LOX-LH2")->dry_fraction, 1e-12,
               "12.1 : un LOX/LH2 porte la fraction seche du reservoir cryogenique");
    CHECK_NEAR(option_from_part(*find_engine("NSTAR")).tank_dry_fraction,
               find_tank("TANK-XE")->dry_fraction, 1e-12,
               "12.1 : un ionique porte celle d un reservoir de xenon");
    CHECK(option_from_part(*find_engine("SRB-P80")).tank_dry_fraction == 0.0,
          "12.1 : un SOLIDE n a pas de reservoir — son enveloppe est deja dans sa masse");

    // --- (g) LA BRANCHE 6 NE S OUVRE PAS TOUTE SEULE [GDD 5.4] -------------
    // Meme defaut que les quatre nœuds « lanceur » qui ne gardaient rien : sans
    // ce filtre, NEP et fusion seraient au catalogue des la premiere mission.
    int gardes = 0;
    for (const auto& o : engines()) if (!o.tech_id.empty()) ++gardes;
    std::printf("     arbre : %d moteurs sur %d exigent un noeud de la branche 6\n",
                gardes, (int)engines().size());
    CHECK(gardes >= 6, "5.4 : electrique, NTP, NEP et fusion sont gardes par l arbre");
    CHECK(option_from_part(*find_engine("RL10C-1")).tech_id.empty(),
          "5.4 : ... et l etat de l art chimique reste disponible au depart");
    CHECK(option_from_part(*find_engine("NEP-1MW")).tech_id == "nep_megawatt" &&
          option_from_part(*find_engine("FUSION-DD")).tech_id == "fusion" &&
          option_from_part(*find_engine("NERVA-NRX")).tech_id == "ntp",
          "5.4 : chaque piece nomme le noeud qui la qualifie");
    // Et le refus DIT la direction au lieu de constater une impasse (piege n°42).
    {
      Contract c{}; c.payload_kg = 1000.0; c.budget_musd = 1e6;
      c.deadline_months = 600.0; c.min_success_prob = 0.5;
      Program pr; pr.engine_index = 0;
      for (std::size_t i = 0; i < engines().size(); ++i)
        if (engines()[i].eng.id == "NEP-1MW") pr.engine_index = (int)i;
      EngineFilter rien = [](const EngineOption& E) { return E.tech_id.empty(); };
      const Assessment a = assess_multistage(c, pr, 1, 3000.0, 150.0, 1, nullptr, {}, &rien);
      CHECK(!a.ok && a.why.find("nep_megawatt") != std::string::npos,
            "5.4 : le refus NOMME le noeud a rechercher");
    }
  }

  // ═══════════ LE VEHICULE CONCU EST CELUI QUI VOLE [GDD 4.1, 12.2] ═════════
  // Le poste CONCEPTION empilait des pieces dans son coin et la mission volait
  // avec autre chose : le joueur choisissait un moteur DEUX FOIS, dans deux
  // postes, et seul l autre comptait. C est le piege n°84 a l echelle d un poste.
  {
    using namespace fen::mission;
    using namespace fen::vehicle;
    using namespace fen::app;

    Contract c{}; c.payload_kg = 2000.0; c.budget_musd = 1e6;
    c.deadline_months = 600.0; c.min_success_prob = 0.5;
    Program pr; pr.engine_index = 0; pr.dv_margin = 0.0;
    auto eval = [&](const std::vector<StageChoice>* p, int ns) {
      return assess_multistage(c, pr, 2, 4000.0, 150.0, ns, nullptr, {}, nullptr, p);
    };

    // --- (a) LA PILE DE DEPART REPRODUIT LE MODE MODELE, AU BIT PRES --------
    // C est la garantie de non-regression, et elle n est pas une opinion : la
    // conception de depart EST le vehicule que la mission dimensionnait (deux
    // etages RL10C-1 a parts egales, structure 150 kg).
    const Assessment sans = eval(nullptr, 2);
    std::vector<StageChoice> depart = VehicleDesign::starter().stages;
    const Assessment avec = eval(&depart, 2);
    CHECK_NEAR(avec.m0_kg, sans.m0_kg, 1e-6,
               "4.1 : brancher l atelier ne deplace RIEN — meme masse au kg pres");
    CHECK_NEAR(avec.cost_total, sans.cost_total, 1e-6, "4.1 : ... ni le cout");
    CHECK_NEAR(avec.p_engine, sans.p_engine, 1e-12, "4.1 : ... ni la fiabilite");
    CHECK_NEAR(avec.schedule_months, sans.schedule_months, 1e-9, "4.1 : ... ni le calendrier");

    // --- (b) MAIS CHANGER LA CONCEPTION CHANGE LA MISSION -------------------
    // Sans quoi la liaison serait decorative — c est tout l objet de la passe.
    std::vector<StageChoice> pauvre = depart;
    for (auto& st : pauvre) {
      st.engine = VehicleDesign::index_moteur("AESTUS");   // Isp 324 au lieu de 449,7
      st.tank   = VehicleDesign::index_reservoir("TANK-STOCK");
    }
    const Assessment aest = eval(&pauvre, 2);
    std::printf("     vehicule concu : 2x RL10 -> %.1f t | 2x Aestus -> %.1f t "
                "(+%.0f %%, Isp 449,7 contre 324)\n",
                avec.m0_kg / 1000.0, aest.m0_kg / 1000.0,
                100.0 * (aest.m0_kg / avec.m0_kg - 1.0));
    CHECK(aest.m0_kg > 1.25 * avec.m0_kg,
          "6.1 : un Isp plus faible alourdit la mission — la conception MORD");
    CHECK(aest.cost_engine < avec.cost_engine,
          "12.1 : ... et l Aestus coute moins cher que le RL10, etage par etage");

    // --- (c) C EST L ARCHITECTURE QUI SE TRANSMET, PAS LE DELTA-V ABSOLU ----
    // Le Δv tombe de l objectif et de la geometrie de la fenetre, que l atelier
    // ignore. Ce que l architecte decide, c est le PARTAGE.
    std::vector<StageChoice> double_dv = depart;
    for (auto& st : double_dv) st.dv_target_ms *= 2.0;    // meme partage, valeurs doublees
    CHECK_NEAR(eval(&double_dv, 2).m0_kg, avec.m0_kg, 1e-6,
               "4.1 : doubler les deux Delta-v ne change rien — seul le PARTAGE compte");
    std::vector<StageChoice> asym = depart;
    asym[0].dv_target_ms = 6000.0; asym[1].dv_target_ms = 2000.0;
    CHECK(std::fabs(eval(&asym, 2).m0_kg - avec.m0_kg) > 1.0,
          "1.5 : ... mais changer le partage change la masse — c est la decision du joueur");

    // --- (d) UN ETAGE ALIMENTE FAIT PORTER SA CENTRALE A LA MISSION --------
    // Le meme chemin que l atelier (`power_plant_for`), pas une seconde formule.
    std::vector<StageChoice> nepp = depart;
    nepp[1].engine = VehicleDesign::index_moteur("NEP-1MW");
    nepp[1].tank   = VehicleDesign::index_reservoir("TANK-XE");
    const Assessment sans_src = eval(&nepp, 2);
    CHECK(!sans_src.ok && sans_src.why.find("SOURCE") != std::string::npos,
          "5.12.1 : un etage alimente sans source est refuse, et le refus le DIT");
    nepp[1].source = PropTier::Fission;
    const Assessment avec_src = eval(&nepp, 2);
    CHECK(avec_src.fits_mass && avec_src.dry_kg > sans.dry_kg + 10000.0,
          "6.5 : la centrale du NEP entre dans la masse seche de la mission");

    // --- (e) UNE PILE HETEROGENE SE PAIE ETAGE PAR ETAGE [GDD 12.1] --------
    std::vector<StageChoice> mixte = depart;
    mixte[0].engine = VehicleDesign::index_moteur("RS-25");   // 146 M$
    const double attendu = effective_cost_musd(*find_engine("RS-25"))
                         + effective_cost_musd(*find_engine("RL10C-1"));
    CHECK_NEAR(eval(&mixte, 2).cost_engine, attendu, 1e-9,
               "12.1 : le prix est la somme des moteurs REELLEMENT montes");

    // --- (f) LES ALLUMAGES SE REPARTISSENT, ILS NE S ADDITIONNENT PAS ------
    // [GDD 12.3.5] Serie sur les etages ; chaque etage inferieur fait UNE
    // manoeuvre puis est largue, le dernier fait celles qui restent. Une
    // premiere redaction imputait n_burns au dernier EN PLUS d un allumage a
    // chacun des autres : trois allumages pour deux manoeuvres, un risque que le
    // vehicule ne court pas. C est l oracle de non-regression (a) qui l a
    // attrape, en comparant a un mode modele que personne n avait touche.
    {
      const double R = engine_reliability(option_from_part(*find_engine("RL10C-1")), 0.0);
      const double R_sep = 0.99;
      CHECK_NEAR(avec.p_engine, R * R * R_sep, 1e-12,
                 "12.3.5 : deux etages, deux manoeuvres = DEUX allumages, pas trois");
      // Et quand il y a plus de manoeuvres que d etages, le dernier les porte.
      const Assessment quatre =
          assess_multistage(c, pr, 4, 4000.0, 150.0, 2, nullptr, {}, nullptr, &depart);
      CHECK_NEAR(quatre.p_engine, std::pow(R, 4) * R_sep, 1e-12,
                 "12.3.5 : quatre manoeuvres sur deux etages = 1 + 3");
    }

    // --- (g) LA PORTE DE L ARBRE PORTE SUR CHAQUE ETAGE [GDD 5.4] ---------
    {
      EngineFilter rien = [](const EngineOption& E) { return E.tech_id.empty(); };
      std::vector<StageChoice> avec_nep = depart;
      avec_nep[1].engine = VehicleDesign::index_moteur("NEP-1MW");
      avec_nep[1].source = PropTier::Fission;
      const Assessment r = assess_multistage(c, pr, 2, 4000.0, 150.0, 2, nullptr, {},
                                             &rien, &avec_nep);
      CHECK(!r.ok && r.why.find("nep_megawatt") != std::string::npos,
            "5.4 : un SEUL etage non qualifie suffit a refuser, et il est nomme");
    }

    // ═══════ LES SOUS-SYSTEMES AVANCES ONT LEUR PROPRE FIABILITE [GDD 12.4] ══
    // « Radiateurs, reacteurs, confinement : chacun a la sienne, SOUVENT
    // DIMENSIONNANTE. » Le module qui la modelisait n avait AUCUN appelant :
    // choisir un NEP ne coutait rien de plus qu un chimique, une fois sa
    // centrale payee en masse.
    {
      std::vector<StageChoice> nucleaire = depart;
      nucleaire[1].engine = VehicleDesign::index_moteur("NEP-1MW");
      nucleaire[1].tank   = VehicleDesign::index_reservoir("TANK-XE");
      nucleaire[1].source = PropTier::Fission;
      auto ev = [&](const std::vector<StageChoice>& p, const EnvironnementMission& em) {
        return assess_multistage(c, pr, 2, 4000.0, 150.0, 2, nullptr, {}, nullptr, &p, &em);
      };

      // --- (a) UN CHIMIQUE NE PAIE RIEN DE PLUS -------------------------
      EnvironnementMission deux_ans; deux_ans.duree_vol_jours = 730.0;
      CHECK_NEAR(ev(depart, deux_ans).p_filieres, 1.0, 1e-12,
                 "12.4 : une architecture chimique ne porte aucun sous-systeme avance");

      // --- (b) LE CŒUR VIEILLIT, ET LA DUREE DECIDE ---------------------
      // Une NEP pousse en CONTINU : deux ans de croisiere, c est deux ans de vie
      // de coeur consommes sur les sept qu il a.
      EnvironnementMission court; court.duree_vol_jours = 30.0;
      const Assessment n_court = ev(nucleaire, court);
      const Assessment n_long  = ev(nucleaire, deux_ans);
      std::printf("     filieres 12.4 : coeur NEP -> %.3f a 30 j, %.3f a 730 j (%s)\n",
                  n_court.p_filieres, n_long.p_filieres, n_long.cause_filieres.c_str());
      CHECK(n_long.p_filieres < n_court.p_filieres,
            "12.4 : le coeur d une NEP se consomme avec la duree du vol");
      CHECK(n_court.p_filieres < 1.0,
            "12.4 : ... et il vieillit deja au calendrier, avant meme de partir");
      CHECK(n_long.cause_filieres.find("coeur") != std::string::npos,
            "12.4 : la cause dominante est NOMMEE, pas noyee dans un chiffre");
      // « SOUVENT dimensionnante » — souvent, pas toujours, et la mesure dit
      // QUAND. Sur deux ans, le NEP etant speculatif (R0 = 0,75), c est encore
      // le MOTEUR qui domine ; sur un aller-retour relativiste, le coeur a brule
      // toute sa vie nominale et c est lui qui decide.
      EnvironnementMission relativiste; relativiste.duree_vol_jours = 8365.0;  // 22,9 ans
      const Assessment n_rel = ev(nucleaire, relativiste);
      // LE LIBELLÉ A DÛ ÊTRE CORRIGÉ : cette ligne annonçait « risque du cœur » aux
      // deux durées. C'était vrai tant que la perforation n'était pas modélisée ;
      // à 8 365 jours c'est elle qui décide maintenant, et la cause le dit.
      std::printf("     filieres 12.4 : risque des filieres %.1f %% a 730 j (%s),"
                  " %.1f %% a 8365 j (%s) — moteur : %.1f %%\n",
                  100.0 * (1.0 - n_long.p_filieres), n_long.cause_filieres.c_str(),
                  100.0 * (1.0 - n_rel.p_filieres), n_rel.cause_filieres.c_str(),
                  100.0 * (1.0 - n_long.p_engine));
      CHECK(1.0 - n_long.p_filieres < 1.0 - n_long.p_engine,
            "12.4 : sur deux ans, un moteur SPECULATIF domine encore son reacteur");
      CHECK(1.0 - n_rel.p_filieres > 1.0 - n_rel.p_engine,
            "12.4 : « souvent dimensionnante » — sur un aller-retour, le coeur decide");
      // Et un NTP ne brule PAS son coeur : il tire quelques minutes.
      std::vector<StageChoice> ntp = depart;
      ntp[1].engine = VehicleDesign::index_moteur("NERVA-NRX");
      CHECK(ev(ntp, deux_ans).p_filieres > n_long.p_filieres,
            "12.4 : un NTP ne consomme pas son coeur — il n y a que le calendrier");

      // --- (c) UNE AILE DE RADIATEUR EST UNE CIBLE [GDD 7.8, 10.5] ------
      // Le couloir traverse est celui que l agence a POLLUE elle-meme, et le
      // temps qu on y passe est celui de la CAMPAGNE D ASSEMBLAGE — deja
      // calcule, jamais pose a la main. Un tir unique injecte sans trainer : il
      // n a donc pas d exposition, et c est vrai, pas une omission.
      // LA DENSITE EST CELLE D UNE VRAIE LEO, PAS UN NOMBRE COMMODE : le couloir
      // 200-600 km fait 2,3e20 m3, donc ~50 000 objets y font 2e-16 /m3. Une
      // premiere redaction avait pris 1e-10 — SEPT ORDRES trop haut — et le
      // radiateur y etait detruit a coup sur, ce qui aurait valide le mecanisme
      // pour une raison fausse.
      EnvironnementMission pollue = deux_ans;
      pollue.densite_debris_m3 =
          50000.0 / env::standard_corridors()[0].volume_m3();
      CHECK_NEAR(ev(nucleaire, pollue).p_filieres, n_long.p_filieres, 1e-12,
                 "7.8 : un tir unique ne traine pas dans le couloir — aucune exposition");
      // Une CAMPAGNE, elle, attend. On force une charge que le super-lourd ne
      // passe pas d un coup.
      Contract lourd = c; lourd.payload_kg = 90000.0;
      CapaciteAssemblage capa; capa.rdv_automatise = true; capa.transfert_ergols = true;
      auto ev2 = [&](const std::vector<StageChoice>& p, const EnvironnementMission& em) {
        return assess_multistage(lourd, pr, 2, 1500.0, 150.0, 2, nullptr, capa,
                                 nullptr, &p, &em);
      };
      const Assessment propre = ev2(nucleaire, deux_ans);
      const Assessment sale   = ev2(nucleaire, pollue);
      std::printf("     filieres 12.4 : campagne de %.0f j en couloir a 50 000 objets "
                  "(%.1e /m3) -> risque de collision des radiateurs %.2f %%\n",
                  sale.assemblage.duree_jours, pollue.densite_debris_m3,
                  100.0 * (1.0 - sale.p_filieres / propre.p_filieres));
      CHECK(sale.assemblage.duree_jours > 0.0, "5.2 : la campagne dure bien quelque chose");
      CHECK(sale.p_filieres < propre.p_filieres,
            "7.8 : mille metres carres de radiateur, c est une section de collision");
      // LA CAUSE DOMINANTE EST CELLE QUI DOMINE VRAIMENT : sur deux ans, c est le
      // coeur (17 %) et non les radiateurs (3,5 %). Sur un vol COURT, le coeur
      // est neuf et c est la pollution qui prend la tete — et le verdict suit.
      CHECK(sale.cause_filieres.find("coeur") != std::string::npos,
            "12.4 : sur deux ans, le coeur domine — le verdict ne se trompe pas de cause");
      EnvironnementMission bref = pollue; bref.duree_vol_jours = 30.0;
      const Assessment court_sale = ev2(nucleaire, bref);
      CHECK(court_sale.cause_filieres.find("radiateur") != std::string::npos,
            "7.8 : sur un vol court en couloir pollue, ce sont les radiateurs");
      // Et le chimique, qui n a pas de radiateur, ne paie PAS la pollution.
      CHECK_NEAR(ev2(depart, pollue).p_filieres, ev2(depart, deux_ans).p_filieres, 1e-12,
                 "7.8 : sans radiateur, la meme pollution ne coute rien");

      // --- (d) LA PERFORATION SUB-MILLIMETRIQUE [GDD 12.4, 6.5] ---------
      // Le troisieme mecanisme de 12.4, celui qui etait DECLARE NON MODELISE
      // « faute d un modele de flux ». Deux populations distinctes, et il faut le
      // verifier : (c) c est une COLLISION avec un objet catalogue, donc la
      // pollution que l agence a produite ; (d) c est le fond naturel PERMANENT,
      // qui ne depend d aucun couloir et frappe meme en croisiere interplanetaire.
      // Le premier a besoin d une densite de debris ; le second, de rien.
      EnvironnementMission fond = deux_ans;         // densite_debris_m3 = 0
      CHECK(fond.densite_debris_m3 == 0.0, "12.4 : aucun debris catalogue ici");
      // A l endurance du radiateur (900 j) la marge 3 sigma couvre : rien ne doit
      // s effondrer. C est le NON-EFFET qu il faut verrouiller, sinon le mecanisme
      // punirait toutes les missions du jeu au passage.
      EnvironnementMission a_endurance = deux_ans;
      a_endurance.duree_vol_jours = env::RadiatorSpec{}.endurance_days;
      const Assessment n_end = ev(nucleaire, a_endurance);
      CHECK(n_end.cause_filieres.find("perforation") == std::string::npos,
            "12.4 : a l endurance prevue, la perforation n est PAS la cause dominante");
      // Au-dela, elle prend la tete — et elle le DIT. C est le seul mecanisme qui
      // reste quand le vol dure des annees : le coeur plafonne (burnup borne a 2),
      // la perforation non.
      std::printf("     filieres 12.4 : perforation -> cause a 8365 j = \"%s\","
                  " p_filieres = %.3e (a 900 j : %.4f, cause \"%s\")\n",
                  n_rel.cause_filieres.c_str(), n_rel.p_filieres,
                  n_end.p_filieres, n_end.cause_filieres.c_str());
      CHECK(n_rel.cause_filieres.find("perforation") != std::string::npos,
            "12.4 : sur un vol de 23 ans, la perforation des radiateurs decide");
      CHECK(n_rel.p_filieres < n_end.p_filieres,
            "12.4 : le verdict suit la duree, sans discontinuite decretee");
      // UNE PROBABILITE NULLE SERAIT UN VERDICT BINAIRE. Une premiere redaction en
      // rendait un — `0,5·(1 + erf)` s annule des que |z| > 6 — alors que la vraie
      // valeur (~1e-115) est largement representable. [GDD 12.4] l interdit
      // explicitement : « jamais un verdict binaire decrete ».
      CHECK(n_rel.p_filieres > 0.0,
            "12.4 : meme desesperee, la probabilite reste un nombre, pas un decret");
      // OU EST LA VRAIE LIMITE : erfc encaisse jusqu a z ~ -37 (soit ~1e-300),
      // au-dela c est le DOUBLE qui sature, pas le modele. Le vol de 23 ans est a
      // z = -22,8 : dans le domaine, et il rend bien un nombre. On verrouille la
      // ou ca compte, et on nomme le plancher au lieu de le pretendre absent.
      const double queue = reliability::poisson_cdf(1.0e4, 7000.0);   // z = -30
      CHECK(queue > 0.0 && queue < 1.0e-150,
            "Poisson : la queue basse extreme reste un nombre (erfc, pas 1 + erf)");
      CHECK(reliability::poisson_cdf(1.0e4, 100.0) == 0.0,
            "Poisson : au-dela de z ~ -37 c est le double qui sature, pas le modele");
      // Et un chimique, qui n a pas de radiateur, ne paie toujours RIEN — meme sur
      // vingt-trois ans. Le mecanisme est propre a la filiere alimentee [GDD 5.12.1].
      CHECK_NEAR(ev(depart, relativiste).p_filieres, 1.0, 1e-12,
                 "12.4 : sans radiateur, aucune perforation a payer");
    }
  }

  // ═══════════ LA GÉOMÉTRIE DU VÉHICULE [GDD 12.2, 17.2, 17.4] ═══════════════
  // « L'éditeur en coupe fournit LA GÉOMÉTRIE DU VÉHICULE, réutilisée
  //   directement au rendu » [12.2] — « un véhicule assemblé par le joueur doit
  //   être RENDU, pas modélisé » [17.2].
  // Aucune pièce ne portait de dimension : le vaisseau du joueur était un point
  // émissif de taille écran constante, à dix mètres comme à dix UA. Ces oracles
  // tiennent les RECOUPEMENTS de la dérivation — pas des chiffres posés.
  {
    using namespace fen::vehicle;
    using namespace fen::app;

    // --- (a) LA ROUTE EXACTE : trois cotes publiques, sans un paramètre ------
    // A_sortie = F_vide (1 − Isp_sol/Isp_vide) / p0, identité de la poussée.
    const EnginePart* rs25 = find_engine("RS-25");
    const EnginePart* f1   = find_engine("F-1");
    const EnginePart* rl10 = find_engine("RL10C-1");
    const EnginePart* vul  = find_engine("VULCAIN-2");
    CHECK(rs25 && f1 && rl10 && vul, "17.2 : les moteurs de recoupement existent");
    if (rs25 && f1 && rl10 && vul) {
      // RS-25 : 2,30 m de diamètre de sortie publié.
      CHECK_NEAR(exit_diameter_m(*rs25), 2.30, 0.05,
                 "17.2 : RS-25 — diametre de sortie retrouve a 5 % pres");
      // F-1 : 3,72 m publiés.
      CHECK_NEAR(exit_diameter_m(*f1), 3.72, 0.05,
                 "17.2 : F-1 — diametre de sortie retrouve a 5 % pres");
      // Longueurs publiées : RS-25 4,24 m, F-1 5,79 m, RL10C-1 2,22 m.
      CHECK_NEAR(engine_length_m(*rs25), 4.24, 0.10,
                 "17.2 : RS-25 — longueur retrouvee a 10 % pres");
      CHECK_NEAR(engine_length_m(*f1), 5.79, 0.10,
                 "17.2 : F-1 — longueur retrouvee a 10 % pres");
      CHECK_NEAR(engine_length_m(*rl10), 2.22, 0.10,
                 "17.2 : RL10C-1 — longueur retrouvee a 10 % pres, moteur A VIDE");
      // RL10C-1 : 1,45 m de sortie publiés — et c'est la ROUTE DE CLASSE qui le
      // rend, puisque ce moteur ne s'allume pas au sol. C'est elle qu'on vérifie.
      CHECK(exit_area_exact_m2(*rl10) == 0.0,
            "17.2 : un moteur a vide n a pas de route exacte");
      CHECK_NEAR(exit_diameter_m(*rl10), 1.45, 0.05,
                 "17.2 : RL10C-1 — la route de classe retrouve la cote publiee");
      // Le RD-180 a DEUX tuyères : la dérivation en rend l'équivalent unique,
      // soit √2 × 1,43 m = 2,02 m. Le vérifier nomme l'approximation.
      const EnginePart* rd = find_engine("RD-180");
      CHECK(rd && std::fabs(exit_diameter_m(*rd) - 2.02) < 0.10,
            "17.2 : RD-180 — tuyere EQUIVALENTE unique (deux cloches de 1,43 m)");
    }

    // --- (b) L'ERREUR DE LA ROUTE DE CLASSE EST MESURÉE, PAS ESPÉRÉE --------
    // Là où les deux routes s'appliquent, on compare. C'est la borne déclarée
    // [GDD 6.8] de tout ce que la route exacte ne couvre pas.
    double pire = 0.0;
    int n_exacts = 0;
    for (const auto& e : engine_catalog()) {
      const double a_ex = exit_area_exact_m2(e);
      if (a_ex <= 0.0) continue;
      ++n_exacts;
      const double d_ex = 2.0 * std::sqrt(a_ex / cst::PI);
      const double d_cl = 2.0 * std::sqrt(e.thrust_vac_n * nozzle_class_sea_level().k() / cst::PI);
      pire = std::max(pire, std::fabs(d_cl / d_ex - 1.0));
    }
    CHECK(n_exacts >= 5, "17.2 : cinq moteurs au moins ont une route exacte");
    CHECK(pire < 0.35, "17.2 : la route de classe reste sous 35 % sur le diametre");
    CHECK(pire > 0.10, "17.2 : ... et elle n est PAS exacte — la borne est reelle");
    if (rs25 && f1 && rl10)
      std::printf("     GEOMETRIE : RS-25 %.2f m x %.2f m (publie 2,30 x 4,24) | F-1 %.2f x %.2f "
                  "(3,72 x 5,79) | RL10C-1 %.2f x %.2f (1,45 x 2,22)\n",
                  exit_diameter_m(*rs25), engine_length_m(*rs25), exit_diameter_m(*f1),
                  engine_length_m(*f1), exit_diameter_m(*rl10), engine_length_m(*rl10));
    std::printf("     GEOMETRIE : route de classe a +/- %.0f %% du diametre exact — BORNE "
                "DECLAREE de ce que la route exacte ne couvre pas [GDD 6.8]\n", 100.0 * pire);

    // --- (c) LA CAPSULE : sa cote sort de sa section de rentree --------------
    // Les quatre lignées citent leur diamètre dans leur propre source.
    struct Cote { const char* id; double d; };
    const Cote cotes[] = {{"APOLLO-CM", 3.91}, {"ORION-CM", 5.00},
                          {"DRAGON-2", 3.70},  {"MARS-AEROSHELL", 4.50},
                          {"SOYUZ-SA", 2.20}};
    for (const auto& c : cotes) {
      const CapsulePart* p = find_capsule(c.id);
      CHECK(p != nullptr, "17.2 : la capsule de recoupement existe");
      if (p) CHECK_NEAR(capsule_diameter_m(*p), c.d, 0.01,
                        "17.2 : le diametre de bouclier sort de la section de rentree");
    }

    // --- (d) LA COUPE EST UNE CONSÉQUENCE DU DIMENSIONNEMENT ----------------
    VehicleDesign d = VehicleDesign::starter();
    DesignSummary s = evaluate_design(d);
    std::vector<double> prop;
    for (const auto& st : s.stages) prop.push_back(st.propellant_kg);
    const VehicleHull h = build_hull(d.stages, prop, nullptr, d.payload_kg);
    CHECK(h.valid, "12.2 : la conception de depart a une coupe");
    CHECK(h.segments.size() >= 5, "12.2 : ajutages, reservoirs, interetage, charge");
    // Le vaisseau de départ : ~21 m pour ~2 m. Un étage Centaur (20,8 t d'ergols
    // pour 12,7 m) est le voisin réel ; on tient l'ordre de grandeur, pas un
    // chiffre posé.
    CHECK(h.length_m > 10.0 && h.length_m < 40.0,
          "12.2 : longueur du vaisseau de depart dans l ordre de grandeur reel");
    CHECK(h.max_diameter_m > 1.4 && h.max_diameter_m < 6.0,
          "12.2 : diametre dans l ordre de grandeur reel");
    // LES SEGMENTS SONT JOINTIFS ET ORDONNÉS — c'est ce qui fait une coupe, et
    // c'est ce dont le rendu a besoin. L'interétage est la seule exception : il
    // ENVELOPPE le moteur du dessus au lieu de s'ajouter à la pile.
    double z = 0.0;
    for (const auto& g : h.segments) {
      CHECK(g.z1_m > g.z0_m, "12.2 : tout segment a une longueur positive");
      if (g.role == HullRole::Interstage) continue;
      CHECK(std::fabs(g.z0_m - z) < 1e-9, "12.2 : les segments sont JOINTIFS");
      z = g.z1_m;
    }
    CHECK_NEAR(z, h.length_m, 1e-12, "12.2 : la longueur est celle de la pile");

    // --- (e) LA GÉOMÉTRIE SUIT LA CONCEPTION, elle ne la précède pas --------
    // Doubler le Δv confié à un étage, c'est plus d'ergols, donc un réservoir
    // plus long. Rien n'est tabulé : la coupe est une SORTIE du sizing.
    VehicleDesign gros = d;
    gros.stages[0].dv_target_ms *= 2.0;
    DesignSummary sg = evaluate_design(gros);
    std::vector<double> pg;
    for (const auto& st : sg.stages) pg.push_back(st.propellant_kg);
    const VehicleHull hg = build_hull(gros.stages, pg, nullptr, gros.payload_kg);
    CHECK(hg.length_m > h.length_m,
          "12.2 : plus de Delta-v confie => vaisseau plus long");

    // Et une capsule impose SON diamètre à toute la pile — comme Orion impose
    // 5 m à ce qui la porte.
    const CapsulePart* orion = find_capsule("ORION-CM");
    CHECK(orion != nullptr, "12.2 : ORION-CM au catalogue");
    if (orion) {
      const VehicleHull ho = build_hull(d.stages, prop, orion, d.payload_kg);
      CHECK_NEAR(ho.max_diameter_m, capsule_diameter_m(*orion), 1e-12,
                 "12.2 : la capsule impose son diametre a la pile");
      CHECK(ho.segments.back().role == HullRole::Capsule,
            "12.2 : la capsule est en tete de pile");
    }

    // --- (f) UN SOLIDE N'A PAS DE RÉSERVOIR, mais il a un volume ------------
    // Le catalogue le dit (`tank_id` vide) ; la coupe doit quand même enfermer
    // la poudre, à la densité d'un APCP.
    VehicleDesign sol;
    const int p80 = VehicleDesign::index_moteur("SRB-P80");
    CHECK(p80 >= 0, "12.2 : le P80 est au catalogue");
    if (p80 >= 0) {
      sol.stages.push_back({p80, 0, 2000.0, 300.0});
      sol.payload_kg = 1000.0;
      DesignSummary ss = evaluate_design(sol);
      std::vector<double> ps{ss.stages[0].propellant_kg};
      const VehicleHull hs = build_hull(sol.stages, ps, nullptr, sol.payload_kg);
      CHECK(hs.valid && hs.length_m > 3.0,
            "12.2 : un etage solide a un corps, sans reservoir au catalogue");
      // La poudre est TROIS FOIS plus dense que le LOX/LH2 : à masse égale, le
      // corps d'un solide est bien plus court qu'un réservoir cryogénique.
      const double v_solide = stage_propellant_volume_m3(sol.stages[0], 10000.0);
      const double v_cryo = tank_volume_m3(*find_tank("TANK-LOX-LH2"), 10000.0);
      CHECK(v_solide < 0.4 * v_cryo,
            "12.2 : la poudre est bien plus dense que le cryogenique");
    }

    // --- (g) CETTE GÉOMÉTRIE NE NOURRIT AUCUNE PHYSIQUE ---------------------
    // La frontière est l'argument qui autorise les approximations ci-dessus :
    // une cote fausse déplace un dessin, jamais un budget de masse. On le tient
    // en vérifiant que le sizing ignore tout de la coupe.
    CHECK_NEAR(evaluate_design(d).liftoff_mass_kg, s.liftoff_mass_kg, 1e-12,
               "17.2 : la coupe ne deplace aucune masse");

    // --- (h) CE QUE LE FEU VERT FIGE VIENT DU DIMENSIONNEMENT DE MISSION -----
    // La coupe d'un vol ne se reconstruit pas depuis l'atelier (que le joueur
    // retouche) mais depuis les ergols que CETTE mission a exigés. Ils sont
    // publiés par `assess_multistage`, à un seul endroit.
    {
      using namespace fen::mission;
      Contract c{}; c.payload_kg = 2000.0; c.budget_musd = 1e6;
      c.deadline_months = 600.0; c.min_success_prob = 0.5;
      Program pr; pr.engine_index = 0; pr.dv_margin = 0.0;
      std::vector<StageChoice> pile = VehicleDesign::starter().stages;
      const Assessment a = assess_multistage(c, pr, 2, 4000.0, 150.0,
                                             (int)pile.size(), nullptr, {}, nullptr, &pile);
      CHECK(a.propellant_par_etage.size() == pile.size(),
            "12.2 : un ergol publie par etage, pour la coupe du vol");
      double somme = 0.0;
      for (double p : a.propellant_par_etage) somme += p;
      CHECK_NEAR(somme, a.propellant_kg, 1e-9,
                 "12.2 : le detail par etage somme au total — une seule verite");
      const VehicleHull hv = build_hull(pile, a.propellant_par_etage, nullptr,
                                        c.payload_kg);
      CHECK(hv.valid && hv.length_m > 5.0,
            "17.2 : le vaisseau qui VOLE a une coupe, tiree de son dimensionnement");
    }
    std::printf("     GEOMETRIE : vaisseau de depart %.1f m x %.2f m (%zu segments, %.1f t "
                "d ergols) — la coupe SORT du dimensionnement\n",
                h.length_m, h.max_diameter_m, h.segments.size(),
                (prop.empty() ? 0.0 : (prop[0] + (prop.size() > 1 ? prop[1] : 0.0))) / 1000.0);
  }

  // ═══════════ RELATIVITÉ v1.2 : verrou aller-retour + antimatière [GDD 6.7] ══
  {
    using namespace fen::rel;
    const double ve = VE_ANTIMATTER_EFF;   // c/3

    // Le seuil narratif est bien plus haut que le seuil mesurable.
    CHECK(BETA_NARRATIVE > BETA_THRESHOLD, "6.7.2 : seuil narratif > seuil mesurable");
    CHECK(BETA_NARRATIVE >= 0.7 - 1e-9, "6.7.2 : perceptible vers beta >= 0,7");

    // VERROU DE L'ALLER-RETOUR [6.7.4] : ratio unitaire puissance 4.
    const double r1 = mass_ratio(0.5, ve);
    const double r4 = round_trip_mass_ratio(0.5, ve);
    CHECK_NEAR(r4, r1 * r1 * r1 * r1, 1e-6 * r4, "6.7.4 : aller-retour = ratio unitaire^4");
    // Ancrages du tableau du GDD : beta=0,5 -> unitaire ~5,2, aller-retour ~730.
    CHECK(r1 > 4.5 && r1 < 6.0, "6.7.4 : ratio unitaire ~5,2 a beta=0,5");
    CHECK(r4 > 500.0 && r4 < 1100.0, "6.7.4 : aller-retour ~730 a beta=0,5");
    // beta=0,9 : l'aller-retour explose (~4,7e7) — hors de portee sans ravito.
    CHECK(round_trip_mass_ratio(0.9, ve) > 1.0e7,
          "6.7.4 : aller-retour a beta=0,9 physiquement hors de portee");
    // Monotonie : plus vite = plus cher, et l'aller-retour toujours pire que l'aller.
    CHECK(round_trip_mass_ratio(0.6, ve) > round_trip_mass_ratio(0.5, ve),
          "6.7.4 : plus de beta = ratio aller-retour plus grand");
    CHECK(round_trip_mass_ratio(0.5, ve) > mass_ratio(0.5, ve),
          "6.7.4 : l aller-retour coute toujours plus que l aller simple");

    // CHAÎNE ANTIMATIÈRE [5.12.12] : masse <-> beta, inversion coherente.
    const double mdry = 10000.0;   // 10 tonnes de vehicule sec
    const double a_needed = antimatter_needed_g(mdry, 0.3);
    CHECK(a_needed > 0.0, "5.12.12 : atteindre beta demande de l antimatiere");
    // Inversion : cette masse redonne bien ~beta=0,3.
    const double b_back = beta_from_antimatter(mdry, a_needed);
    CHECK_NEAR(b_back, 0.3, 0.02, "5.12.12 : masse -> beta -> masse coherent");
    // LE POINT DU GDD : quelques grammes ne donnent qu'un beta derisoire.
    CHECK(beta_from_antimatter(mdry, 1.0) < 1.0e-4,
          "19.3 : 1 g d antimatiere sur 10 t = beta minuscule");
    // ... et viser beta=0,3 demande des TONNES d'antimatiere (hors echelle).
    CHECK(a_needed > 1.0e6, "19.3 : viser beta=0,3 demande des tonnes d antimatiere");

    // LE MODÈLE DE PRODUCTION est le levier : debit faible => accumulation
    // pluriannuelle ; confinement plafonne le stock utile.
    AntimatterProduction prod = AntimatterProduction::for_tier(AntimatterTier::Fission);
    CHECK(prod.years_to_accumulate(1.0) >= 1.0,
          "5.12.12 : produire un gramme est pluriannuel");
    CHECK(prod.max_usable_stock_g() <= prod.confinement_capacity_g + 1e-9,
          "5.12.12 : le confinement plafonne le stock utile");
    CHECK(prod.cost_to_produce_me(1.0) > 1.0e5,
          "5.12.12 : le cout par gramme est hors echelle");
    CHECK(prod.stock_survival(0.0) == 1.0 && prod.stock_survival(1e9) < 1.0,
          "5.12.12 : le confinement perd du stock avec le temps (risque permanent)");
    // Un meilleur debit reduit le temps d'accumulation (le vrai levier). Le debit
    // n'est plus un champ qu'on ecrit : il decoule du PALIER DE BRANCHE 6, ce que
    // [GDD 5.12.12] demande (« le rendement couple la production a la branche
    // energie »). Ameliorer le levier, c'est donc monter d'un palier.
    AntimatterProduction fast = AntimatterProduction::for_tier(AntimatterTier::Mature);
    CHECK(fast.years_to_accumulate(1.0) < prod.years_to_accumulate(1.0),
          "5.12.12 : ameliorer le debit raccourcit l accumulation");
    CHECK(fast.production_efficiency > prod.production_efficiency
          && fast.plant_power_w > prod.plant_power_w,
          "5.12.12 : ... et le debit ne progresse que par le rendement ou la puissance");
  }

  std::printf("\nCONTENU GDD (6.4, 5.12, 11, arbre, 10.1, 12.1, 13.3, 12.4, 12.2, 6.7) : %d OK, %d en echec.\n",
              g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS

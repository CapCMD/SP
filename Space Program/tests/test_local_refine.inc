// tests/test_local_refine.inc — ORACLES DU RAFFINEUR LOCAL.
//
// Un optimiseur est difficile à tester : il rend "un point", et un point ne se
// vérifie pas contre une intuition. Les six oracles ci-dessous sont donc bâtis
// sur des choses qu'on SAIT exactement, ou sur des INVARIANTS qu'une
// implémentation fausse ne peut pas satisfaire par hasard.
// ---------------------------------------------------------------------------
static void test_local_refine() {
  section("RAFFINEUR LOCAL : descente projetee quasi-Newton");

  // -------------------------------------------------------------------------
  // 1. LE GRADIENT LUI-MÊME, contre le gradient ANALYTIQUE.
  // Avant de croire une descente, on vérifie la pente. f = sum w_i (x_i-c_i)^3
  // -> gradient analytique connu, non trivial (non symétrique, change de signe).
  // Un signe faux, un dénominateur faux, un décalage d'indice : tout est pris.
  // -------------------------------------------------------------------------
  {
    const int D = 8;
    std::vector<double> lo(D), hi(D), c(D), w(D), x(D);
    for (int j = 0; j < D; ++j) {
      lo[j] = -2.0 - 0.1 * j; hi[j] = 3.0 + 0.2 * j;
      c[j] = 0.3 * j - 1.0;  w[j] = 0.5 + 0.7 * j;
      x[j] = 0.1 * j + 0.25;
    }
    auto f = [&](const std::vector<double>& v) {
      double s = 0; for (int j = 0; j < D; ++j) { const double d = v[j] - c[j]; s += w[j] * d * d * d; }
      return s;
    };
    // gradient NORMALISÉ analytique : df/dz_j = 3 w_j (x_j-c_j)^2 * (hi_j-lo_j)
    astro::RefineOptions o;
    std::vector<double> z(D), g(D), zt(D);
    for (int j = 0; j < D; ++j) z[j] = (x[j] - lo[j]) / (hi[j] - lo[j]);
    std::vector<double> xb(D);
    auto F = [&](const std::vector<double>& zz) {
      for (int j = 0; j < D; ++j) xb[j] = lo[j] + zz[j] * (hi[j] - lo[j]);
      return f(xb);
    };
    long long ev = 0;
    astro::fd_gradient(F, z, F(z), o, g, ev, zt);
    double worst = 0.0;
    for (int j = 0; j < D; ++j) {
      const double d = x[j] - c[j];
      const double ga = 3.0 * w[j] * d * d * (hi[j] - lo[j]);
      worst = std::max(worst, std::fabs(g[j] - ga) / std::max(1.0, std::fabs(ga)));
    }
    std::printf("     gradient FD vs ANALYTIQUE (8 var., cubique) : ecart relatif max %.2e\n", worst);
    CHECK(worst < 1e-7, "le gradient par differences finies EST le gradient");
  }

  // -------------------------------------------------------------------------
  // 2. BOÎTE ACTIVE : la réponse est connue EXACTEMENT.
  // Pour une quadratique SÉPARABLE, le minimum dans la boîte est le minimum
  // libre RABOTÉ sur la boîte, composante par composante. On place la moitié des
  // cibles HORS de la boîte : la contrainte MORD.
  // Le discriminant : à l'optimum, le vrai gradient est GRAND (il pousse dehors)
  // et le gradient PROJETÉ est nul. Une méthode qui ne projette pas ne converge
  // jamais ; une méthode qui projette mal converge au mauvais point.
  // -------------------------------------------------------------------------
  {
    const int D = 10;
    std::vector<double> lo(D, 0.0), hi(D, 1.0), c(D), w(D), x0(D);
    for (int j = 0; j < D; ++j) {
      w[j] = 1.0 + 3.0 * j;                    // conditionnement 1:28
      c[j] = (j % 2 == 0) ? (0.15 + 0.06 * j)  // dedans
                          : (j < 5 ? -0.7 : 1.9);  // DEHORS -> borne active
      x0[j] = 0.5;
    }
    auto f = [&](const std::vector<double>& v) {
      double s = 0; for (int j = 0; j < D; ++j) { const double d = v[j] - c[j]; s += w[j] * d * d; }
      return s;
    };
    astro::RefineOptions o; o.max_iter = 300;
    auto R = astro::refine_local(f, lo, hi, x0, o);
    double worst = 0.0;
    int actives = 0;
    for (int j = 0; j < D; ++j) {
      const double xstar = std::clamp(c[j], lo[j], hi[j]);
      if (xstar != c[j]) ++actives;
      worst = std::max(worst, std::fabs(R.x[j] - xstar));
    }
    std::printf("     quadratique 10-D, %d bornes ACTIVES : ecart max au minimum exact %.2e\n",
                actives, worst);
    std::printf("     residu KKT (||gradient projete||) = %.2e   (arret : %s)\n", R.gnorm, R.stop);
    CHECK(actives == 5, "la moitie des bornes sont bien actives");
    CHECK(worst < 1e-7, "le minimum PROJETE exact est atteint");
    CHECK(R.gnorm < 1e-8, "residu KKT nul : c'est un point stationnaire de la BOITE");
    CHECK(R.f <= R.f0, "descente monotone");
  }

  // -------------------------------------------------------------------------
  // 3. ROSENBROCK : la vallée courbe. Minimum global connu : (1,1), f = 0.
  // POURQUOI CET ORACLE NE PEUT PAS PASSER PAR ACCIDENT : la plus forte pente
  // met ~1e5 iterations a descendre cette vallee. Exiger f < 1e-10 en MOINS DE
  // 200 iterations, c'est exiger que BFGS FONCTIONNE VRAIMENT — pas qu'il soit
  // silencieusement retombe sur la plus forte pente a chaque tour.
  // -------------------------------------------------------------------------
  {
    auto f = [](const std::vector<double>& v) {
      const double a = 1.0 - v[0], b = v[1] - v[0] * v[0];
      return a * a + 100.0 * b * b;
    };
    std::vector<double> lo{-2.0, -1.0}, hi{2.0, 3.0}, x0{-1.2, 1.0};
    astro::RefineOptions o; o.max_iter = 200;
    auto R = astro::refine_local(f, lo, hi, x0, o);
    std::printf("     Rosenbrock depuis (-1.2, 1.0) : f = %.3e en %d iterations, %lld evals\n",
                R.f, R.iters, R.evals);
    std::printf("     x = (%.9f, %.9f)   (exact : 1, 1)\n", R.x[0], R.x[1]);
    CHECK(R.f < 1e-10, "Rosenbrock : le minimum global est atteint");
    CHECK(std::fabs(R.x[0] - 1.0) < 1e-5 && std::fabs(R.x[1] - 1.0) < 1e-5, "et il est AU BON ENDROIT");
    CHECK(R.iters < 200, "en moins de 200 iterations -> BFGS travaille (la pente seule en met 1e5)");
  }

  // -------------------------------------------------------------------------
  // 4. LA FALAISE. C'EST L'ORACLE QUI COMPTE.
  // f = (x0-0.8)^2 + (x1-0.5)^2  pour x0 < 0.5 ; +inf au-dela (echec de Lambert).
  // Le minimum LIBRE (0.8, 0.5) est DANS la falaise. La reponse correcte est donc
  // la BORDURE : x0 -> 0.5 par la gauche, x1 = 0.5, f -> 0.09.
  //
  // POURQUOI ELLE NE PEUT PAS PASSER PAR ACCIDENT : une difference centree naive
  // calcule, a x0 = 0.5 - 1e-7, la pente (1e300 - f)/(2e-6) = 5e305. La direction
  // deborde, le pas devient NaN, et le raffineur rend n'importe quoi. Il n'y a
  // AUCUN moyen d'obtenir 0.0900 ici sans detecter la discontinuite.
  // -------------------------------------------------------------------------
  {
    auto f = [](const std::vector<double>& v) {
      if (v[0] >= 0.5) return 1e300;                       // LA FALAISE
      const double a = v[0] - 0.8, b = v[1] - 0.5;
      return a * a + b * b;
    };
    std::vector<double> lo{0.0, 0.0}, hi{1.0, 1.0}, x0{0.2, 0.2};
    astro::RefineOptions o; o.max_iter = 400;
    auto R = astro::refine_local(f, lo, hi, x0, o);
    std::printf("     falaise : f = %.6f (borne inf theorique 0.09) | x0 = %.9f (mur a 0.5)\n",
                R.f, R.x[0]);
    std::printf("     discontinuite detectee %d fois | arret : %s\n", R.cliff_hits, R.stop);
    CHECK(std::isfinite(R.f) && R.f < 1e298, "la falaise ne fait pas exploser le raffineur");
    CHECK(R.x[0] < 0.5, "le mur n'est JAMAIS franchi");
    CHECK(R.x[0] > 0.4977, "et il est APPROCHE : la descente ne s'arrete pas avant");
    CHECK(std::fabs(R.x[1] - 0.5) < 1e-5, "la direction LIBRE, elle, est resolue exactement");
    CHECK(R.f < 0.0902 && R.f > 0.0899, "la valeur est celle de la BORDURE, pas une autre");
    CHECK(R.cliff_hits > 0, "la difference finie a bien DETECTE la falaise (elle ne l'a pas subie)");
  }

  // -------------------------------------------------------------------------
  // 5. INVARIANT DE MONOTONIE, sur 300 departs aleatoires d'une fonction sale
  // (vallee de Rosenbrock + falaise + bornes). UNE seule violation est un bug :
  // le pas n'est accepte que si f baisse STRICTEMENT. Rien de statistique ici.
  // -------------------------------------------------------------------------
  {
    auto f = [](const std::vector<double>& v) {
      if (v[0] + v[1] > 1.6) return 1e300;                  // falaise DIAGONALE (pas une borne !)
      const double a = 1.0 - v[0], b = v[1] - v[0] * v[0];
      return a * a + 100.0 * b * b + 0.3 * std::sin(9.0 * v[0]) * std::sin(9.0 * v[1]);
    };
    std::vector<double> lo{-1.5, -1.0}, hi{1.5, 2.0};
    astro::RefineOptions o; o.max_iter = 120;
    Rng rng(20260714ull);
    int viol = 0, nan_ = 0, n = 0;
    double gain = 0.0;
    for (int i = 0; i < 300; ++i) {
      std::vector<double> x0{rng.uniform(lo[0], hi[0]), rng.uniform(lo[1], hi[1])};
      auto R = astro::refine_local(f, lo, hi, x0, o);
      if (!R.ok) continue;
      ++n;
      if (!(std::isfinite(R.f))) ++nan_;
      if (R.f > R.f0) ++viol;
      gain += R.f0 - R.f;
    }
    std::printf("     %d departs faisables : %d remontees, %d NaN, gain moyen %.3f\n",
                n, viol, nan_, gain / std::max(1, n));
    CHECK(viol == 0, "AUCUNE remontee : la decroissance est monotone PAR CONSTRUCTION");
    CHECK(nan_ == 0, "AUCUN NaN, malgre la falaise diagonale");
    CHECK(n > 200, "la falaise diagonale laisse bien passer la plupart des departs");
  }

  // -------------------------------------------------------------------------
  // 6. L'ÉCHELLE. C'est l'oracle qui justifie la NORMALISATION.
  // Quadratique dont les variables couvrent 12 ordres de grandeur (1e-3 .. 1e9),
  // exactement comme le vrai probleme (eta ~ 0,4 ; t0 ~ 1e9 s).
  // Un raffineur a pas de DF ABSOLU unique est INCAPABLE de bouger la variable de
  // portee 1e9 (h=1e-6 y est du bruit d'arrondi) : elle resterait a son depart.
  // Exiger qu'elle atteigne sa cible a 1e-9 pres EN RELATIF, c'est exiger la
  // normalisation. On ne peut pas passer cet oracle sans elle.
  // -------------------------------------------------------------------------
  {
    const int D = 6;
    const double sc[6] = {1e-3, 1e0, 1e3, 1e6, 1e8, 1e9};
    std::vector<double> lo(D), hi(D), c(D), x0(D);
    for (int j = 0; j < D; ++j) { lo[j] = -sc[j]; hi[j] = sc[j]; c[j] = 0.37 * sc[j]; x0[j] = -0.8 * sc[j]; }
    auto f = [&](const std::vector<double>& v) {
      double s = 0;
      for (int j = 0; j < D; ++j) { const double d = (v[j] - c[j]) / sc[j]; s += (1.0 + j) * d * d; }
      return s;
    };
    astro::RefineOptions o; o.max_iter = 200;
    auto R = astro::refine_local(f, lo, hi, x0, o);
    double worst = 0.0;
    for (int j = 0; j < D; ++j) worst = std::max(worst, std::fabs(R.x[j] - c[j]) / sc[j]);
    std::printf("     12 ordres de grandeur (1e-3 .. 1e9) : ecart relatif max a la cible %.2e\n", worst);
    std::printf("     variable de portee 1e9 : depart %.4e -> arrivee %.9e (cible %.9e)\n",
                x0[5], R.x[5], c[5]);
    CHECK(worst < 1e-9, "toutes les variables convergent -> la NORMALISATION fait son travail");
  }
}

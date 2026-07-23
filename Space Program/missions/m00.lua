-- missions/m00.lua — MISSION JOUABLE : LEO 200 km -> GEO.
--
-- LE CHOIX DU JOUEUR : combien de POURSUITE acheter (niveau 0..6). C'est le
-- coeur du jeu. Le joueur ne voit jamais la position vraie ; il achete un
-- ESTIME, corrige dessus, et vit avec. Trop peu -> l'orbite diverge. Trop ->
-- il paie pour rien.
--
--   arg[1] = graine  (le tirage des aleas de vol : rejouer change tout)
--   arg[2] = niveau de poursuite achete, 0..6
--
-- Renvoie un bloc lisible + une derniere ligne machine : RESULT|ok|a|e|i|dv|cout

local seed  = tonumber(arg[1]) or 4071
local level = tonumber(arg[2]) or 3
local doc   = fen.load_fpl("missions/m00_geo_solution.fpl")
local S     = fen.session(doc, seed)
local t0    = doc.epoch0
local MU    = fen.MU_EARTH

-- ---------------------------------------------------------------------------
-- LES 7 NIVEAUX DE POURSUITE. Ce sont EXACTEMENT les scenarios mesures du
-- moteur (m00_nav.cpp), avec leur taux de reussite observe. Le joueur achete
-- un compromis argent / heures / risque.
-- Chaque passe : {station, debut, fin} en secondes depuis le lancement.
-- ---------------------------------------------------------------------------
local NIVEAUX = {
  [0] = { nom="AVEUGLE (rien)",            reussite="~6 %",  passes={} },
  [1] = { nom="30 min, 1 station",         reussite="~70 %", passes={ {0,4000,5800} } },
  [2] = { nom="3h10, 1 station",           reussite="~60 %", passes={ {0,3600,15000} } },
  [3] = { nom="+ 1 arc apres chaque manoeuvre", reussite="~85 %",
          passes={ {0,3600,15000}, {2,21000,36000}, {2,64000,78000} } },
  [4] = { nom="3 stations, arcs courts",   reussite="~90 %",
          passes={ {0,3600,15000},{1,3600,15000},{2,3600,15000},
                   {0,21000,36000},{1,21000,36000},{2,21000,36000},
                   {0,64000,78000},{1,64000,78000},{2,64000,78000} } },
  [5] = { nom="3 stations, arcs COMPLETS", reussite="~96 %",
          passes={ {0,3600,15000},{1,3600,15000},{2,3600,15000},
                   {0,21000,58000},{1,21000,58000},{2,21000,58000},
                   {0,65000,103000},{1,65000,103000},{2,65000,103000} } },
  [6] = { nom="tout + 2 rev. avant le TRIM", reussite="~97 %",
          passes={ {0,3600,15000},{1,3600,15000},{2,3600,15000},
                   {0,21000,58000},{1,21000,58000},{2,21000,58000},
                   {0,65000,280000},{1,65000,280000},{2,65000,280000} } },
}
local N = NIVEAUX[level] or NIVEAUX[3]
local extra_revs = (level == 6) and 2 or 0

print(("=========================================================="))
print((" M00 — LEO -> GEO   |   graine %d"):format(seed))
print((" Poursuite achetee : niveau %d — %s"):format(level, N.nom))
print((" (ce niveau reussit %s des vols)"):format(N.reussite))
print(("=========================================================="))

for _, p in ipairs(N.passes) do
  S:schedule_pass{ station=p[1], t_start=t0+p[2], t_end=t0+p[3], sample_dt=60 }
end

local G = fen.forces(); G:central(MU); G:third_body("Sun","Earth"); G:third_body("Moon","Earth")

local function predict(o, want, skip)
  local el = fen.rv_to_elements(o.r, o.v, MU)
  local T  = fen.orbital_period(el.a, MU)
  if not (T > 0) then return nil end
  local mid = fen.propagate(G, o.t, o.r, o.v, o.m, o.t + skip*T)
  local r   = fen.propagate(G, o.t + skip*T, mid.r, mid.v, mid.m, o.t + (skip+1.1)*T,
                            { {kind="periapsis",mu=MU}, {kind="apoapsis",mu=MU} })
  for _, ev in ipairs(r.events) do
    if want == nil or ev.name == want then return ev end
  end
  return nil
end
local function target_v(r, r_other)
  local a = 0.5*(r:norm() + r_other)
  return fen.unit(fen.cross(fen.vec3(0,0,1), r)) * fen.vis_viva(r:norm(), a, MU)
end
local function burn(id, ev, vt)
  local rep = S:commit_burn{ id=id, t=ev.t, dv=vt-ev.v, frame="inertial", stage=0 }
  print(("  [%-5s] t0+%7.0f s | dv %8.2f m/s | ergols %6.1f kg")
        :format(id, ev.t-t0, rep.dv_commanded, rep.propellant))
  return rep.dv_commanded
end

-- verdict d'echec propre (au lieu d'un plantage) si le vol est perdu
local function echec(pourquoi, a, e, i)
  print("")
  print(("  *** MISSION RATEE : %s"):format(pourquoi))
  print(("RESULT|0|%.1f|%.6f|%.4f|%.1f|%.1f")
        :format(a or 0, e or 9, i or 9, 0, S:tracking_cost_musd()))
  os.exit(0)
end

S:commit_burn(fen.plan_burn(doc, 1))
if not S:alive() then echec("perte a l'injection") end

local dv_corr = 0
local steps = { {"AMF",15200,0.00,0}, {"AMF2",nil,0.02,0}, {"TRIM",nil,0.02,extra_revs} }
for k, st in ipairs(steps) do
  local id, wait, skip, revs = st[1], st[2], st[3], st[4]
  if wait then S:advance_to(t0+wait)
  else
    local o = S:observe()
    local el = fen.rv_to_elements(o.r, o.v, MU)
    if not (el.a > 0) then echec("orbite hyperbolique : l'estime a diverge", el.a, el.e, el.i/fen.DEG) end
    S:advance_to(S:t() + (0.38+revs)*fen.orbital_period(el.a, MU))
  end
  local o = S:observe()
  print(("  [OBS %d] %-30s sigma_pos = %8.1f m"):format(k, o.source, o.sigma_pos))
  local ev = predict(o, (k==1) and "APOAPSIS" or nil, skip)
  if not ev then echec("navigation insuffisante : aucune apside predite") end
  local vt = (k<3) and target_v(ev.r, fen.R_GEO)
                    or fen.unit(fen.cross(fen.vec3(0,0,1), ev.r))*fen.v_circular(ev.r:norm(), MU)
  local d = burn(id, ev, vt)
  if not S:alive() then echec("reservoir vide pendant "..id) end
  if k>1 then dv_corr = dv_corr + d end
end

-- LE JURY. Il note sur la VERITE (que le joueur ne voit pas). Cible GEO.
S:advance_to(S:t() + 2000)
local v = S:score(fen.R_GEO, 50000, 0.002, 0.25)   -- a +/-50 km, e<0.002, i<0.25 deg

-- ce que le JOUEUR croyait (son estime) — pour montrer l'ecart avec la realite
local o  = S:observe()
local ec = fen.rv_to_elements(o.r, o.v, MU)
print("")
print(("  TON ESTIME  : a = %9.1f km   e = %.6f"):format(ec.a/1000, ec.e))
print(("  LA REALITE  : a = %9.1f km   e = %.6f   i = %.4f deg")
      :format(v.a/1000, v.e, v.i_deg))
print(("  ecart reel a la cible : %+.1f km"):format(v.a_err/1000))
print(("  correction payee   : %6.1f m/s"):format(dv_corr))
print(("  navigation achetee : %6.1f M$"):format(S:tracking_cost_musd()))
print(("  Delta-v en soute   : %6.1f m/s"):format(S:dv_remaining(0)))
print("")
print(("  >>> %s"):format(v.ok and "MISSION REUSSIE" or "MISSION RATEE"))
print(("RESULT|%d|%.1f|%.6f|%.4f|%.1f|%.1f")
      :format(v.ok and 1 or 0, v.a/1000, v.e, v.i_deg, dv_corr, S:tracking_cost_musd()))

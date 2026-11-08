<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Map overlays on the HUD shape renderer

Identified in issue #33 part 33d-i (SDD 33, D39). The map's route legs and arrows, waypoint stems, the destination pick
(disc, height guide, marker) and the grid stems are drawn as Canvas lines and triangles in the `Map/` overlays
(`SOLMapRouteOverlay`, `SOLMapPickOverlay`, `SOLMapBodyOverlay`, `SOLMapHologramOverlay`), not through the glow-shape
layer the flight HUD uses. Because of that the Help glossary cannot draw them from shared shape code (D5), and those
rows show screenshot crops instead (D39).

If picked up: move the overlays' geometry into pure builders (the `UI/SOLHudSymbols` pattern) drawn through an
`SSOLHudShapeLayer`, so the map gets the HUD's glow look and the glossary can draw the real symbols; then replace the
crops with drawn symbols. Orbit lines are a 3D mesh with a material and would stay a crop. Grill the look (glow on the
map, Canvas fallback) and the per-frame cost (the map draws many legs and stems) when picked up.

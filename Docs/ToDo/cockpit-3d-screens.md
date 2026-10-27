<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Cockpit 3D hologram screens

Requested while grilling #20: the flight HUD elements (target, info, boost, radar) should be holograms, and when the
camera is zoomed in on the ship they could live on 3D screens inside the cockpit so it feels like sitting in the ship,
as in Limit Theory.

Notes: the LT research (`Docs/Research/LT_features_for_SOL.md` rows 89-103) found no cockpit 3D screen in LT itself; its
3D-in-UI is a 3D mesh rendered into a 2D widget rectangle. The hologram style for the HUD is #21; this item is the
step after it (render the HUD into a render target shown on in-world planes attached to the ship).

When picked up: grill the camera modes (first person versus zoomed third person), render-target cost, readability and
the interaction with the existing HUD.

<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Predicted ship path under gravity

The P key was reserved for it in SDD 2 section 6 and does nothing today. Issue #20 repurposes P in flight to toggle the
route overlay, so this item needs a new key (or a sub-option of the overlay).

Wanted: a line predicting where the current velocity takes the ship under the bodies' gravity, redrawn every frame as
the ship thrusts. Needs a cheap integrator (the Newtonian core already has gravity-aware pieces), a bounded horizon and
a batched line draw.

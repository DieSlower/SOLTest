<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Damage and pilot death

Deferred from issue #18 (SDD 18 Amendment 7, user decision H2). The player ship has no damage model and the pilot
cannot die yet.

- **Prolonged G drain is lethal.** Under a load above the sustained limit that the pilot cannot escape (for example
  resting on a body heavier than 1.5 G, or a manual burn left on), the G meter drains, the pilot blacks out, recovers
  slowly while unconscious (`UnconsciousRecoverPerS`), wakes and drains again. Today this only raises the HUD's
  "LETHAL G" warning once the meter has been depleted for `LethalWarningS` (30 s; `FSOLGMeterState::DepletedS`). With a
  damage system, a depleted meter held this long should injure and eventually kill the pilot.
- **Impacts.** Collision impacts do not count as G-load (SDD 18 decision 22); a damage system should turn hard
  impacts into hull damage and pilot injury.
- **Death and respawn.** What happens on death (respawn point, loss) is a design question for that ticket; a respawn
  already clears the autopilot order and the G meter (`USOLShipSubsystem::ResetPilotState`).

<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Star Field: CelestialVault plugin vs. AT-HYG — Research Notes

**Status:** research supporting SDD 1's star-field decision. **Compiled:** 2026-09-27 from the UE 5.8 install at `C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Runtime\CelestialVault`. Re-check if the engine version changes.

## What CelestialVault is

- Engine plugin, marked **beta** (`IsBetaVersion: true`). Description: "A DaySequence implementation of a Celestial Vault for Earth using ephemeris". Depends on the DaySequence plugin.
- Earth-observer-centric: the main actor (`ACelestialVaultDaySequenceActor`) works from latitude/longitude, local time, daylight-savings rules and topocentric maths (`UCelestialMaths`, VSOP87 ephemeris, aberration from Earth's velocity). It is not usable as-is from an arbitrary point in the solar system.
- Stars are rendered as an **instanced static mesh of quads** (`UInstancedStaticMeshComponent`, one instance per star row) using a plane mesh (`SM_Plane_FacingX`) and the material `MI_StarsStar_EnergyConservative`, with 4 custom data floats per instance. Stars sit on a vault sphere (`StarsVaultPercentage`, default 99% of the vault distance).
- Star input is a `UDataTable` whose row struct is `FCelestialStarInputData` (RA in hours, DEC, distance in parsecs, magnitude, and more). Shipped tables: `DT_HYGCatalog_10K`, `DT_HYGCatalog_Full`, plus fictional stars. Stars above `MaxVisibleMagnitude` are skipped at build time.
- Reusable assets under `Content/`: `T_MilkyWay`, `T_Constellations`, star and planet materials (`M_Stars`, `M_Stars_EnergyConservative`, `M_Planets`, `M_Moon`), `MF_BillboardSizeByPixelUnits`, `MF_ScalePlaneToMinScreenPixels`, `MF_DirectionToLatLong`, `MPC_CelestialVault`.

## Why not use it directly with AT-HYG

- AT-HYG has about 2.55 million stars. A DataTable of that size is slow to import, and an ISM quad per star means a very large instance buffer and culling cost, while almost every star is sub-pixel at 1080p/4K.
- The Earth/DaySequence dependency does not fit a ship flying anywhere in SOL.

## Decision (SDD 1)

Own ship-centric camera-locked star actor:

1. Offline conversion of AT-HYG into (a) a compact binary of bright stars (~mag < 8) drawn as GPU sprites with proper size, colour and diffraction spikes, and (b) an HDR cubemap for the faint remainder, built by splatting each star's flux onto the texels it covers so faint stars merge into a physically correct glow.
2. Reuse CelestialVault's Milky Way texture and material functions as assets; do not use its actor.
3. Render at effectively infinite distance, behind everything else.

Open detail for the star-field part's own SDD: exact magnitude cutoff, cubemap resolution, and colour handling (B-V or temperature to colour).

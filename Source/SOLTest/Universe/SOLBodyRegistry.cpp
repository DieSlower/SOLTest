/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLBodyRegistry.h"

#include "SOLConstants.h"
#include "Universe/SOLBodyRotation.h"

namespace
{
    // One row of the built-in solar-system table: name, mean radius (m), GM (m^3/s^2), Standish elements, then
    // sidereal rotation period (h), axial tilt (deg, 0-180) and rotation angle at J2000 (deg)
    struct FSOLSolarSystemRow
    {
        const TCHAR* Name;
        double RadiusM;
        double GM;
        FSOLSecularElements Elements;
        double RotationPeriodH;
        double AxialTiltDeg;
        double W0Deg;
    };

    // Sun and planets; elements from JPL Standish "Keplerian elements for approximate positions", Table 1 (1800-2050);
    // rotation periods and obliquities from the NASA/JPL Planetary Fact Sheet (Sun: equatorial period), W0 = 0 (SDD 12)
    const FSOLSolarSystemRow REGISTRY_SOLAR_SYSTEM[] =
    {
        { TEXT("Sun"), 6.957e8, 1.32712440018e20, {}, 601.2, 7.25, 0.0 },
        { TEXT("Mercury"), 2.4397e6, 2.2032e13,
            { 0.38709927, 0.00000037, 0.20563593, 0.00001906, 7.00497902, -0.00594749,
              252.25032350, 149472.67411175, 77.45779628, 0.16047689, 48.33076593, -0.12534081 },
            1407.6, 0.034, 0.0 },
        { TEXT("Venus"), 6.0518e6, 3.24859e14,
            { 0.72333566, 0.00000390, 0.00677672, -0.00004107, 3.39467605, -0.00078890,
              181.97909950, 58517.81538729, 131.60246718, 0.00268329, 76.67984255, -0.27769418 },
            5832.6, 177.36, 0.0 },
        { TEXT("Earth"), 6.371e6, 3.986004418e14,
            { 1.00000261, 0.00000562, 0.01671123, -0.00004392, -0.00001531, -0.01294668,
              100.46457166, 35999.37244981, 102.93768193, 0.32327364, 0.0, 0.0 },
            23.9345, 23.44, 0.0 },
        { TEXT("Mars"), 3.3895e6, 4.282837e13,
            { 1.52371034, 0.00001847, 0.09339410, 0.00007882, 1.84969142, -0.00813131,
              -4.55343205, 19140.30268499, -23.94362959, 0.44441088, 49.55953891, -0.29257343 },
            24.6229, 25.19, 0.0 },
        { TEXT("Jupiter"), 6.9911e7, 1.26686534e17,
            { 5.20288700, -0.00011607, 0.04838624, -0.00013253, 1.30439695, -0.00183714,
              34.39644051, 3034.74612775, 14.72847983, 0.21252668, 100.47390909, 0.20469106 },
            9.9250, 3.13, 0.0 },
        { TEXT("Saturn"), 5.8232e7, 3.7931187e16,
            { 9.53667594, -0.00125060, 0.05386179, -0.00050991, 2.48599187, 0.00193609,
              49.95424423, 1222.49362201, 92.59887831, -0.41897216, 113.66242448, -0.28867794 },
            10.656, 26.73, 0.0 },
        { TEXT("Uranus"), 2.5362e7, 5.793939e15,
            { 19.18916464, -0.00196176, 0.04725744, -0.00004397, 0.77263783, -0.00242939,
              313.23810451, 428.48202785, 170.95427630, 0.40805281, 74.01692503, 0.04240589 },
            17.24, 97.77, 0.0 },
        { TEXT("Neptune"), 2.4622e7, 6.836529e15,
            { 30.06992276, 0.00026291, 0.00859048, 0.00005105, 1.77004347, 0.00035372,
              -55.12002969, 218.45945325, 44.96476227, -0.32241464, 131.78422574, -0.00508664 },
            16.11, 28.32, 0.0 },
    };

    // Registry indices of the bodies above that host children (fixed by REGISTRY_SOLAR_SYSTEM's order)
    constexpr int32 REGISTRY_INDEX_SUN = 0;
    constexpr int32 REGISTRY_INDEX_EARTH = 3;
    constexpr int32 REGISTRY_INDEX_MARS = 4;
    constexpr int32 REGISTRY_INDEX_JUPITER = 5;
    constexpr int32 REGISTRY_INDEX_SATURN = 6;
    constexpr int32 REGISTRY_INDEX_URANUS = 7;
    constexpr int32 REGISTRY_INDEX_NEPTUNE = 8;

    // Reference plane a child row's elements are written in
    enum class ESOLElementFrame : uint8
    {
        Ecliptic,       // J2000 ecliptic, like the planets; used as-is
        HostEquator,    // the host's equator, with i measured from the host's spin pole; tilted by the host's
                        // AxialTiltDeg (SDD 12 convention) at populate time so the orbit lies in the rendered equator
    };

    // One child row: parent registry index, the frame its elements are written in, then the usual body columns
    struct FSOLChildBodyRow
    {
        int32 ParentIndex;
        ESOLElementFrame Frame;
        FSOLSolarSystemRow Body;
    };

    // Dwarf planets (parented to the Sun), then major moons (parented to their planet); issue #6 / SDD 6 Section 3.1.
    //
    // Precise catalog data:
    //  - Moon radii and GMs: JPL SSD "Planetary Satellite Physical Parameters" (ssd.jpl.nasa.gov/sats/phys_par).
    //  - Moon a (km) from JPL SSD "Planetary Satellite Mean Elements" (epoch 2000-01-01.5), converted with
    //    1 AU = 149,597,870.7 km. Eccentricities are the commonly tabulated mean values (NASA/JPL).
    //  - Moon sidereal orbital periods P (days) are the commonly tabulated (NASA/JPL) values.
    //  - Moons other than the Moon: i, node, longitude of periapsis and mean longitude are JPL Horizons osculating
    //    elements at J2000 (JD 2451545.0 TDB) in the host's IAU body-equator frame (node = OM, LongPeri = OM + w,
    //    L = OM + w + MA). Uranus's IAU north pole is opposite its spin, so Horizons gives its moons i ~ 180; those
    //    rows are flipped to the spin-pole frame (i' = 180 - i, OM' = 180 - OM, w' = w + 180).
    //  - The Moon: Meeus/Simon mean ecliptic elements at J2000 (L, perigee, node), mean i = 5.145396 deg.
    //  - Dwarf planets: JPL Horizons osculating ecliptic elements at J2000 (Ceres heliocentric; Pluto, Eris, Makemake
    //    and Haumea solar-system-barycentric, which tracks their long-term mean orbit far better than heliocentric
    //    osculating elements do). Pluto's row is the Pluto-Charon barycenter's orbit (Charon is not modeled).
    //  - Dwarf-planet radii, GMs, rotation: Ceres (Dawn: R 469.7 km, GM 62.62736 km^3/s^2, P 9.074170 h, obliquity
    //    ~4 deg); Pluto (New Horizons: R 1188.3 km, GM 869.6 km^3/s^2, P 6.387230 d, obliquity 122.53 deg); Eris
    //    (R 1163 km, mass 1.6466e22 kg (Holler et al. 2021), P 15.786 d); Makemake (R 715 km (Ortiz et al. 2012),
    //    P 22.8266 h); Haumea (P 3.915341 h, mass 4.006e21 kg (Ragozzine & Brown 2009)).
    //
    // Derived or approximated (check these first):
    //  - LDotDegPerCy = 360 * 36525 / P(days) = 13,149,000 / P for every moon (mean motion in deg per Julian century).
    //    Dwarf planets use Horizons' osculating mean motion N (deg/day) * 36525.
    //  - Every rate term other than LDotDegPerCy is 0: moons' nodal/apsidal precession and dwarf planets' secular
    //    drift are out of scope (SDD 1's simplification beyond the planets), so these are fixed ellipses.
    //  - Moon rows other than the Moon mix mean a/e/P with osculating epoch angles; the epoch angles of near-circular
    //    orbits (e < 0.005) are poorly defined. Also, every host-equator row is tilted into the host's rendered
    //    equator using SDD 12's fixed/fake pole-azimuth convention (the host's pole always tips toward ecliptic +X,
    //    not its real sky direction), so the resulting node is referenced to that fake azimuth, not the real sky:
    //    relative phases between sibling moons of the same host ARE preserved (e.g. which Galilean moon leads
    //    another), but each host's moons share one common, not-astronomically-real phase offset from the real sky
    //    (which moon is sunward of its host on a real date will not match reality).
    //  - The "Earth" row above (REGISTRY_SOLAR_SYSTEM) is the Earth-Moon barycenter, not Earth's literal center
    //    (standard Standish/Horizons practice), so the Moon's row orbits that barycenter, about 4,670 km from
    //    Earth's true center.
    //  - GM from G * mass (G = 6.6743e-11): Eris 1.09899e12; Makemake 2.07e11 from a poorly constrained ~3.1e21 kg
    //    estimate (Parker et al. 2018); Haumea 2.67372e11.
    //  - Haumea R 798 km is the volume-equivalent radius of the Ortiz et al. (2017) 2322 x 1704 x 1026 km ellipsoid.
    //  - Phobos's I0Deg is 1.08 (commonly tabulated inclination to Mars's equator/Laplace plane), corrected from an
    //    earlier 0.653 found in adversarial review.
    //  - Every moon is tidally locked, so its RotationPeriodH = P(days) * 24.
    //  - Moon spin tilts: each moon's pole is taken parallel to its host's (AxialTiltDeg = host's), exact for a moon
    //    with zero obliquity in its host's equator. The Moon uses its real 1.5424 deg from ecliptic-north. Triton's
    //    synchronous spin follows its retrograde orbit, which SDD 12's fixed pole azimuth (poles tip toward ecliptic
    //    +X only) cannot represent; 180 (ecliptic-south) is the closest representable pole. Triton's real orbit
    //    normal and this forced spin pole differ by roughly 35 deg, so once textured, a synchronously-locked Triton
    //    will visibly nod/wobble rather than holding steady like a correctly tidally-locked body: the point facing
    //    Neptune will swing roughly +-35 deg over Triton's ~5.9-day orbit instead of staying fixed.
    //  - Eris, Makemake and Haumea pole directions are unknown: AxialTiltDeg = 0 with their measured periods.
    //  - W0Deg = 0 everywhere, matching the planets (SDD 12); a locked moon's face toward its host is not phased.
    //
    // Saturn's ring shepherd moons Pan and Daphnis (issue #6 Part 5d, added so SOLPlanetRing's moon-shepherd gap
    // mechanism has real data; they sit inside the Encke and Keeler gaps of Saturn's A ring):
    //  - a (Pan 133,584 km, Daphnis 136,505 km) is the JPL SSD satellite mean-element value. P (Pan 0.575050718 d,
    //    Daphnis 0.5940798 d) is Jacobson, R.A., et al. (2008), "Revised orbits of Saturn's small inner satellites",
    //    Astronomical Journal 135(1), 261-263 -- the source JPL SSD's own mean-element page cites for both periods --
    //    matching the other moons' 6-7 significant figure precision (replacing this part's earlier 3-sig-fig estimate,
    //    which was imprecise enough to drift LDotDegPerCy's derived mean longitude by tens of degrees per sim-year).
    //  - i, node, longitude of periapsis and mean longitude are fetched from JPL Horizons (horizons.api, ELEMENTS,
    //    CENTER 500@699, REF_PLANE BODY, TLIST 2451545.0) the same way as the other Saturn moons: Pan OM 247.4223,
    //    w 259.2285, MA 359.9341, i 0.00086; Daphnis OM 121.2292, w 32.2294, MA 0.1175, i 0.00171. i is rounded to
    //    4 decimals like the other rows (both are essentially in Saturn's equator/ring plane).
    //  - e = 0 for both (derived/approximated): Horizons' osculating e (~0.005 for each) is dominated by Saturn's J2,
    //    not a real free eccentricity; their mean eccentricities are tabulated as ~0 (Daphnis's real ~3e-5 eccentricity
    //    and ~0.004 deg inclination drive the Keeler Gap's wavy edges, which this simplification does not reproduce).
    //    With e = 0 the longitude of periapsis is kept from Horizons only for traceability; it has no effect.
    //  - Mean radii are representative values for irregular shapes (Pan ~14.1 km, Daphnis ~3.8 km; Cassini).
    //  - GM from G * mass with poorly constrained Cassini-era masses (Pan ~4.95e15 kg, Daphnis ~7.7e13 kg; Porco et
    //    al. 2007, Weiss et al. 2009): Pan 3.30378e5, Daphnis 5.139211e3 m^3/s^2.
    const FSOLChildBodyRow REGISTRY_CHILD_BODIES[] =
    {
        // Dwarf planets (ecliptic, Sun-relative)
        { REGISTRY_INDEX_SUN, ESOLElementFrame::Ecliptic, { TEXT("Ceres"), 4.697e5, 6.262736e10,
            { 2.76649602, 0.0, 0.07837563, 0.0, 10.58336046, 0.0,
              160.59387500, 7823.46643900, 154.41722000, 0.0, 80.49435747, 0.0 },
            9.074170, 4.0, 0.0 } },
        { REGISTRY_INDEX_SUN, ESOLElementFrame::Ecliptic, { TEXT("Pluto"), 1.1883e6, 8.696e11,
            { 39.48741550, 0.0, 0.24897636, 0.0, 17.14055931, 0.0,
              238.92746900, 145.17686400, 224.07872000, 0.0, 110.30125386, 0.0 },
            153.29352, 122.53, 0.0 } },
        { REGISTRY_INDEX_SUN, ESOLElementFrame::Ecliptic, { TEXT("Eris"), 1.163e6, 1.09899e12,
            { 67.83513506, 0.0, 0.43843000, 0.0, 43.99285596, 0.0,
              21.04072200, 64.47679500, 187.19772500, 0.0, 35.97652907, 0.0 },
            378.864, 0.0, 0.0 } },
        { REGISTRY_INDEX_SUN, ESOLElementFrame::Ecliptic, { TEXT("Makemake"), 7.15e5, 2.07e11,
            { 45.49889310, 0.0, 0.16038663, 0.0, 29.00199284, 0.0,
              155.61583100, 117.37733200, 15.51162100, 0.0, 79.44279338, 0.0 },
            22.8266, 0.0, 0.0 } },
        { REGISTRY_INDEX_SUN, ESOLElementFrame::Ecliptic, { TEXT("Haumea"), 7.98e5, 2.67372e11,
            { 43.10286750, 0.0, 0.19500944, 0.0, 28.20492617, 0.0,
              192.31995600, 127.29936400, 1.89488900, 0.0, 121.94764449, 0.0 },
            3.915341, 0.0, 0.0 } },

        // Earth: a 384,400 km, P 27.321661 d -> LDot 481,266.4940
        { REGISTRY_INDEX_EARTH, ESOLElementFrame::Ecliptic, { TEXT("Moon"), 1.7374e6, 4.9028e12,
            { 0.0025695553, 0.0, 0.0549, 0.0, 5.145396, 0.0,
              218.3164477, 481266.4940, 83.3532465, 0.0, 125.0445479, 0.0 },
            655.719864, 1.5424, 0.0 } },

        // Mars: Phobos a 9,375 km, P 0.31891023 d; Deimos a 23,457 km, P 1.26244 d
        { REGISTRY_INDEX_MARS, ESOLElementFrame::HostEquator, { TEXT("Phobos"), 1.108e4, 7.087e5,
            { 0.0000626680, 0.0, 0.0151, 0.0, 1.0800, 0.0, 215.5110, 41231038.5904, 25.6886, 0.0, 41.8325, 0.0 },
            7.653846, 25.19, 0.0 } },
        { REGISTRY_INDEX_MARS, ESOLElementFrame::HostEquator, { TEXT("Deimos"), 6.2e3, 9.62e4,
            { 0.0001568004, 0.0, 0.00033, 0.0, 2.2279, 0.0, 258.8668, 10415544.5011, 253.7731, 0.0, 28.8065, 0.0 },
            30.298560, 25.19, 0.0 } },

        // Jupiter (Galilean): a 421,800 / 671,100 / 1,070,400 / 1,882,700 km;
        // P 1.769137786 / 3.551181 / 7.15455296 / 16.6890184 d
        { REGISTRY_INDEX_JUPITER, ESOLElementFrame::HostEquator, { TEXT("Io"), 1.82149e6, 5.95991547e12,
            { 0.0028195588, 0.0, 0.0041, 0.0, 0.0375, 0.0, 19.9404, 7432434.0953, 44.7872, 0.0, 243.1625, 0.0 },
            42.459307, 3.13, 0.0 } },
        { REGISTRY_INDEX_JUPITER, ESOLElementFrame::HostEquator, { TEXT("Europa"), 1.5608e6, 3.2027121e12,
            { 0.0044860264, 0.0, 0.009, 0.0, 0.4622, 0.0, 214.4592, 3702711.8584, 229.0481, 0.0, 180.0999, 0.0 },
            85.228344, 3.13, 0.0 } },
        { REGISTRY_INDEX_JUPITER, ESOLElementFrame::HostEquator, { TEXT("Ganymede"), 2.6312e6, 9.88783275e12,
            { 0.0071551821, 0.0, 0.0013, 0.0, 0.2069, 0.0, 221.7946, 1837850.6768, 304.7455, 0.0, 72.9129, 0.0 },
            171.709271, 3.13, 0.0 } },
        { REGISTRY_INDEX_JUPITER, ESOLElementFrame::HostEquator, { TEXT("Callisto"), 2.4103e6, 7.1792834e12,
            { 0.0125850722, 0.0, 0.0074, 0.0, 0.1996, 0.0, 80.9574, 787883.3665, 355.8387, 0.0, 158.3252, 0.0 },
            400.536442, 3.13, 0.0 } },

        // Saturn: a 186,000 / 238,400 / 295,000 / 377,700 / 527,200 / 1,221,900 / 3,561,700 km;
        // P 0.942422 / 1.370218 / 1.887802 / 2.736915 / 4.518212 / 15.945421 / 79.3215 d
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Mimas"), 1.982e5, 2.50349e9,
            { 0.0012433332, 0.0, 0.0196, 0.0, 1.5708, 0.0, 188.3322, 13952348.3111, 150.9342, 0.0, 172.9984, 0.0 },
            22.618128, 26.73, 0.0 } },
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Enceladus"), 2.521e5, 7.21037e9,
            { 0.0015936056, 0.0, 0.0047, 0.0, 0.0098, 0.0, 182.3835, 9596283.2192, 175.4301, 0.0, 309.0790, 0.0 },
            32.885232, 26.73, 0.0 } },
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Tethys"), 5.311e5, 4.121353e10,
            { 0.0019719532, 0.0, 0.0001, 0.0, 1.0930, 0.0, 187.0501, 6965243.1770, 196.6672, 0.0, 259.7693, 0.0 },
            45.307248, 26.73, 0.0 } },
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Dione"), 5.614e5, 7.311607e10,
            { 0.0025247686, 0.0, 0.0022, 0.0, 0.0290, 0.0, 176.9069, 4804314.3466, 204.8502, 0.0, 288.1410, 0.0 },
            65.685960, 26.73, 0.0 } },
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Rhea"), 7.635e5, 1.5394175e11,
            { 0.0035241143, 0.0, 0.0012583, 0.0, 0.3186, 0.0, 52.1704, 2910222.0082, 205.2675, 0.0, 346.1670, 0.0 },
            108.437088, 26.73, 0.0 } },
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Titan"), 2.57476e6, 8.9781371e12,
            { 0.0081678970, 0.0, 0.0288, 0.0, 0.3600, 0.0, 7.5560, 824625.4520, 204.1198, 0.0, 241.8359, 0.0 },
            382.690104, 26.73, 0.0 } },
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Iapetus"), 7.343e5, 1.2051511e11,
            { 0.0238084940, 0.0, 0.0276812, 0.0, 15.4701, 0.0, 89.8956, 165768.4234, 241.8776, 0.0, 253.5208, 0.0 },
            1903.716000, 26.73, 0.0 } },

        // Saturn's ring shepherds: Pan a 133,584 km, P 0.575050718 d (Encke Gap); Daphnis a 136,505 km, P 0.5940798 d
        // (Keeler Gap); LDotDegPerCy = 13,149,000 / period_days
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Pan"), 1.41e4, 3.30378e5,
            { 0.0008929539, 0.0, 0.0, 0.0, 0.0009, 0.0, 146.5849, 22865809.2033, 146.6508, 0.0, 247.4223, 0.0 },
            13.800000, 26.73, 0.0 } },
        { REGISTRY_INDEX_SATURN, ESOLElementFrame::HostEquator, { TEXT("Daphnis"), 3.8e3, 5.139211e3,
            { 0.0009124796, 0.0, 0.0, 0.0, 0.0017, 0.0, 153.5761, 22133390.1607, 153.4587, 0.0, 121.2292, 0.0 },
            14.256000, 26.73, 0.0 } },

        // Uranus: a 129,846 / 190,929 / 265,986 / 436,298 / 583,511 km;
        // P 1.413479 / 2.520379 / 4.144177 / 8.705869 / 13.463234 d
        { REGISTRY_INDEX_URANUS, ESOLElementFrame::HostEquator, { TEXT("Miranda"), 2.358e5, 4.3e9,
            { 0.0008679669, 0.0, 0.0013, 0.0, 4.4278, 0.0, 147.5636, 9302578.9559, 85.5017, 0.0, 280.8287, 0.0 },
            33.923496, 97.77, 0.0 } },
        { REGISTRY_INDEX_URANUS, ESOLElementFrame::HostEquator, { TEXT("Ariel"), 5.789e5, 8.35e10,
            { 0.0012762815, 0.0, 0.0012, 0.0, 0.0027, 0.0, 23.2099, 5217072.5117, 230.4155, 0.0, 29.3786, 0.0 },
            60.489096, 97.77, 0.0 } },
        { REGISTRY_INDEX_URANUS, ESOLElementFrame::HostEquator, { TEXT("Umbriel"), 5.847e5, 8.51e10,
            { 0.0017780066, 0.0, 0.0039, 0.0, 0.0564, 0.0, 71.2348, 3172885.7141, 160.0114, 0.0, 13.6513, 0.0 },
            99.460248, 97.77, 0.0 } },
        { REGISTRY_INDEX_URANUS, ESOLElementFrame::HostEquator, { TEXT("Titania"), 7.889e5, 2.269e11,
            { 0.0029164720, 0.0, 0.0011, 0.0, 0.1007, 0.0, 101.5961, 1510360.4247, 27.1793, 0.0, 168.5636, 0.0 },
            208.940856, 97.77, 0.0 } },
        { REGISTRY_INDEX_URANUS, ESOLElementFrame::HostEquator, { TEXT("Oberon"), 7.614e5, 2.053e11,
            { 0.0039005301, 0.0, 0.0014, 0.0, 0.1880, 0.0, 172.5469, 976659.8426, 79.0506, 0.0, 220.0438, 0.0 },
            323.117616, 97.77, 0.0 } },

        // Neptune: Triton a 354,800 km, P 5.876854 d, retrograde (i ~ 157 deg from Neptune's spin pole)
        { REGISTRY_INDEX_NEPTUNE, ESOLElementFrame::HostEquator, { TEXT("Triton"), 1.3526e6, 1.42849546e12,
            { 0.0023716915, 0.0, 0.000016, 0.0, 156.8279, 0.0, 236.4627, 2237421.5864, 253.0020, 0.0, 177.8669, 0.0 },
            141.044496, 180.0, 0.0 } },
    };

    //////////////////////////////////////////////////////////////////////////
    // Re-expresses host-equator elements in the ecliptic frame by tilting them with the host's axial tilt (SDD 12),
    // keeping the same physical ellipse and orbital phase; only epoch values convert, so the source rates must be 0
    FSOLSecularElements HostEquatorToEcliptic(const FSOLSecularElements& equatorial, const double hostTiltRad)
    {
        checkf(equatorial.ADotAUPerCy == 0.0 && equatorial.EDotPerCy == 0.0 && equatorial.IDotDegPerCy == 0.0
            && equatorial.LongPeriDotDegPerCy == 0.0 && equatorial.LongNodeDotDegPerCy == 0.0,
            TEXT("HostEquatorToEcliptic: only the mean-longitude rate carries over a frame change"));

        // Orbit-plane basis in the host-equator frame: +X toward periapsis, +Z along the orbit normal
        const double nodeRad = FMath::DegreesToRadians(equatorial.LongNode0Deg);
        const double argPeriRad = FMath::DegreesToRadians(equatorial.LongPeri0Deg - equatorial.LongNode0Deg);
        const double incRad = FMath::DegreesToRadians(equatorial.I0Deg);
        const FQuat4d orbitToEquator = FQuat4d(FVector3d::ZAxisVector, nodeRad)
            * FQuat4d(FVector3d::XAxisVector, incRad) * FQuat4d(FVector3d::ZAxisVector, argPeriRad);
        const FQuat4d orbitToEcliptic = SOLBodyRotation::TiltRotation(hostTiltRad) * orbitToEquator;
        const FVector3d normal = orbitToEcliptic.RotateVector(FVector3d::ZAxisVector);
        const FVector3d periapsis = orbitToEcliptic.RotateVector(FVector3d::XAxisVector);

        // Ascending node on the ecliptic is +Z x normal; an orbit lying in the ecliptic falls back to +X
        FVector3d node(-normal.Y, normal.X, 0.0);
        node = node.SizeSquared() > UE_DOUBLE_SMALL_NUMBER ? node.GetUnsafeNormal() : FVector3d::XAxisVector;
        const double nodeDeg = FMath::RadiansToDegrees(FMath::Atan2(node.Y, node.X));
        const double argPeriDeg = FMath::RadiansToDegrees(FMath::Atan2((node ^ periapsis) | normal, node | periapsis));

        // Mean anomaly at epoch (L - LongPeri) is frame-independent, so it is preserved exactly
        FSOLSecularElements ecliptic = equatorial;
        ecliptic.I0Deg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(normal.Z, -1.0, 1.0)));
        ecliptic.LongNode0Deg = nodeDeg;
        ecliptic.LongPeri0Deg = nodeDeg + argPeriDeg;
        ecliptic.L0Deg = ecliptic.LongPeri0Deg + (equatorial.L0Deg - equatorial.LongPeri0Deg);
        return ecliptic;
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds a body definition from a table row, its parent index and its ecliptic-frame elements
    FSOLBodyDef MakeBodyDef(const FSOLSolarSystemRow& row, const int32 parentIndex, const FSOLSecularElements& elements)
    {
        FSOLBodyDef def;
        def.Name = FName(row.Name);
        def.RadiusM = row.RadiusM;
        def.GM = row.GM;
        def.ParentIndex = parentIndex;
        def.Elements = elements;
        def.RotationPeriodH = row.RotationPeriodH;
        def.AxialTiltDeg = row.AxialTiltDeg;
        def.W0Deg = row.W0Deg;
        return def;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the GM that reproduces these elements' own mean motion exactly via Kepler's third law (mu = n^2 a^3);
    // used so a child body's orbital velocity is consistent with its own tabulated period even when that differs from
    // what its parent's measured GM would predict (e.g. a close moon shaped by the parent's oblateness)
    double ChildOrbitGM(const FSOLSecularElements& elements)
    {
        const double meanMotionRadPerSec = FMath::DegreesToRadians(elements.LDotDegPerCy) / SOL::SECONDS_PER_JULIAN_CENTURY;
        const double semiMajorAxisM = elements.A0AU * SOL::AU_M;
        return FMath::Square(meanMotionRadPerSec) * (semiMajorAxisM * semiMajorAxisM * semiMajorAxisM);
    }
}

//////////////////////////////////////////////////////////////////////////
// Adds a body and returns its new index
int32 FSOLBodyRegistry::AddBody(const FSOLBodyDef& def)
{
    checkf(def.ParentIndex == INDEX_NONE || (def.ParentIndex >= 0 && def.ParentIndex < Num()),
        TEXT("FSOLBodyRegistry::AddBody: parent %d of %s must be added before the child"), def.ParentIndex,
        *def.Name.ToString());

    mNames.Add(def.Name);
    mRadiiM.Add(def.RadiusM);
    mGMs.Add(def.GM);

    // A non-zero OrbitGM overrides the parent's own GM for this body's own orbital integration (see FSOLBodyDef)
    const double orbitGM = def.OrbitGM > 0.0 ? def.OrbitGM : (def.ParentIndex != INDEX_NONE ? mGMs[def.ParentIndex] : 0.0);
    mOrbitGMs.Add(orbitGM);

    mParents.Add(def.ParentIndex);
    mElements.Add(def.Elements);
    mPositionsM.Add(FVector3d::ZeroVector);
    mVelocitiesMps.Add(FVector3d::ZeroVector);
    mRotationPeriodsH.Add(def.RotationPeriodH);
    mAxialTiltsRad.Add(FMath::DegreesToRadians(def.AxialTiltDeg));
    mW0sRad.Add(FMath::DegreesToRadians(def.W0Deg));
    return mOrientations.Add(FQuat4d::Identity);
}

//////////////////////////////////////////////////////////////////////////
// Clears, then adds the Sun and the eight planets (Sun=0 ... Neptune=8), then the dwarf planets and major moons
void FSOLBodyRegistry::PopulateSolarSystem()
{
    const int32 planetCount = UE_ARRAY_COUNT(REGISTRY_SOLAR_SYSTEM);
    const int32 bodyCount = planetCount + UE_ARRAY_COUNT(REGISTRY_CHILD_BODIES);
    mNames.Reset(bodyCount);
    mRadiiM.Reset(bodyCount);
    mGMs.Reset(bodyCount);
    mOrbitGMs.Reset(bodyCount);
    mParents.Reset(bodyCount);
    mElements.Reset(bodyCount);
    mPositionsM.Reset(bodyCount);
    mVelocitiesMps.Reset(bodyCount);
    mRotationPeriodsH.Reset(bodyCount);
    mAxialTiltsRad.Reset(bodyCount);
    mW0sRad.Reset(bodyCount);
    mOrientations.Reset(bodyCount);

    // Row 0 is the Sun (the root); every planet orbits it
    for (int32 index = 0; index < planetCount; ++index)
    {
        const FSOLSolarSystemRow& row = REGISTRY_SOLAR_SYSTEM[index];
        AddBody(MakeBodyDef(row, index == 0 ? INDEX_NONE : REGISTRY_INDEX_SUN, row.Elements));
    }

    // Children follow every planet, so each host is already added; host-equator rows are tilted by the host's tilt
    for (const FSOLChildBodyRow& child : REGISTRY_CHILD_BODIES)
    {
        const FSOLSecularElements elements = child.Frame == ESOLElementFrame::HostEquator
            ? HostEquatorToEcliptic(child.Body.Elements, mAxialTiltsRad[child.ParentIndex])
            : child.Body.Elements;
        FSOLBodyDef def = MakeBodyDef(child.Body, child.ParentIndex, elements);

        // Every child row's own tabulated mean motion drives its orbit GM, not its parent's bare point-mass GM: this
        // keeps the vis-viva velocity consistent with the body's real period even where the two diverge (e.g. a close
        // moon shaped by its parent's oblateness)
        def.OrbitGM = ChildOrbitGM(elements);
        AddBody(def);
    }
}

//////////////////////////////////////////////////////////////////////////
// Computes absolute (Sun-frame) positions and velocities; root bodies stay fixed at the origin
void FSOLBodyRegistry::Update(const double secondsSinceJ2000)
{
    const double centuries = secondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;
    const int32 bodyCount = mNames.Num();

    // Parents precede children, so one forward pass sees every parent's absolute state already computed
    for (int32 index = 0; index < bodyCount; ++index)
    {
        // Every body spins, root or not
        mOrientations[index] = SOLBodyRotation::ComputeOrientation(mAxialTiltsRad[index], mW0sRad[index],
            mRotationPeriodsH[index], secondsSinceJ2000);

        const int32 parent = mParents[index];
        if (parent == INDEX_NONE)
        {
            mPositionsM[index] = FVector3d::ZeroVector;
            mVelocitiesMps[index] = FVector3d::ZeroVector;
            continue;
        }
        const FSOLOrbitState relative = SOLKepler::ElementsToState(mElements[index].AtCenturies(centuries),
            mOrbitGMs[index], 0.0);
        mPositionsM[index] = mPositionsM[parent] + relative.PositionM;
        mVelocitiesMps[index] = mVelocitiesMps[parent] + relative.VelocityMps;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of bodies
int32 FSOLBodyRegistry::Num() const
{
    return mNames.Num();
}

//////////////////////////////////////////////////////////////////////////
// Returns the index of the named body, or INDEX_NONE if absent
int32 FSOLBodyRegistry::FindByName(const FName name) const
{
    return mNames.IndexOfByKey(name);
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's name
FName FSOLBodyRegistry::GetName(const int32 index) const
{
    return mNames[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's parent index
int32 FSOLBodyRegistry::GetParent(const int32 index) const
{
    return mParents[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's mean radius in meters
double FSOLBodyRegistry::GetRadiusM(const int32 index) const
{
    return mRadiiM[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's gravitational parameter in m^3/s^2
double FSOLBodyRegistry::GetGM(const int32 index) const
{
    return mGMs[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's Sun-frame position in meters
const FVector3d& FSOLBodyRegistry::GetPositionM(const int32 index) const
{
    return mPositionsM[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's Sun-frame velocity in m/s
const FVector3d& FSOLBodyRegistry::GetVelocityMps(const int32 index) const
{
    return mVelocitiesMps[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's secular orbital elements relative to its parent (unused for the Sun)
const FSOLSecularElements& FSOLBodyRegistry::GetElements(const int32 index) const
{
    return mElements[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns all Sun-frame positions, indexed like the bodies
TConstArrayView<FVector3d> FSOLBodyRegistry::GetPositionsM() const
{
    return mPositionsM;
}

//////////////////////////////////////////////////////////////////////////
// Returns all Sun-frame velocities, indexed like the bodies
TConstArrayView<FVector3d> FSOLBodyRegistry::GetVelocitiesMps() const
{
    return mVelocitiesMps;
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's orientation (ecliptic frame): fixed axial tilt composed with the current spin
const FQuat4d& FSOLBodyRegistry::GetOrientation(const int32 index) const
{
    return mOrientations[index];
}

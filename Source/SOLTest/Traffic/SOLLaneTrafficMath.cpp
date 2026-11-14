/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneTrafficMath.h"

#include "SOLConstants.h"

#include <cmath>

namespace
{
    constexpr double TRAFFIC_PI = UE_DOUBLE_PI;
    constexpr double TRAFFIC_BELOW_ONE = 0.99999999999999989;      // Largest double below 1 (phase upper limit)
    constexpr double TRAFFIC_LEG_ROUNDING_FRACTION = 1.0e-9;       // A nominal leg below this fraction of L0 is the
                                                                   // rounding of the end-ring subtraction (no leg)
    constexpr double TRAFFIC_INVERSE_TOLERANCE = 1.0e-14;          // Map inverse step tolerance, fraction of l0
    constexpr int32 TRAFFIC_INVERSE_MAX_STEPS = 64;                // Map inverse step limit (Newton or bisection)
    constexpr double TRAFFIC_SPEED_BOUND_ROUNDING = 1.0e-6;        // The speed bound is tight (a coasting peak ship
                                                                   // mid-lane reaches it), so it adds this fraction
                                                                   // for rounding: the float rail's dv (about 6e-8
                                                                   // relative) and the evaluated positions
    constexpr double TRAFFIC_MAP_MIN_LEG_ZONES = 1.001;            // The metric map folds over for l <= Z, so a
                                                                   // shorter current leg reads as this many zones

    // A leg evaluated at a time: the arc from each end ring (each computed directly near its own end) and the speed
    struct FTrafficLegArc
    {
        double FromStartM = 0.0;
        double FromEndM = 0.0;
        double SpeedMps = 0.0;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of a vector is finite
    bool TrafficIsFinite(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rising smoothstep s^2 (3 - 2 s), s = (x - e0) / (e1 - e0) clamped to [0, 1]
    double TrafficSmoothstep(const double e0, const double e1, const double x)
    {
        const double s = FMath::Clamp((x - e0) / (e1 - e0), 0.0, 1.0);
        return s * s * (3.0 - 2.0 * s);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the descending smoothstep of a value: 1 at and below low, 0 at and above high
    double TrafficSmoothFall(const double value, const double low, const double high)
    {
        return 1.0 - TrafficSmoothstep(low, high, value);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a phase wrapped into [0, 1); a non-finite phase reads as 0
    double TrafficWrapPhase(const double phase)
    {
        if (!FMath::IsFinite(phase))
        {
            return 0.0;
        }
        const double fraction = phase - FMath::FloorToDouble(phase);
        return fraction >= 1.0 ? 0.0 : fraction;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a value that is finite and positive, else 0
    double TrafficPositiveOrZero(const double value)
    {
        return FMath::IsFinite(value) && value > 0.0 ? value : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when the metric map can be evaluated: a finite, positive nominal leg and a finite current leg
    bool TrafficMapInputsValid(const double l0, const double l)
    {
        return FMath::IsFinite(l0) && l0 > 0.0 && FMath::IsFinite(l);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the current leg the metric map uses for a zone z: l, or TRAFFIC_MAP_MIN_LEG_ZONES x z when l is shorter
    // (X' = (l - Z) / (l0 - Z) mid-leg, so the map stays strictly monotone)
    double TrafficMapLeg(const double l, const double z)
    {
        return FMath::Max(l, TRAFFIC_MAP_MIN_LEG_ZONES * z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the end-zone ramp J(u) of SDD 50 section 4.5 (P(s) = s^3 - s^4 / 2 inside each zone, linear between)
    double TrafficRampJ(const double u, const double l0, const double z)
    {
        if (u < z)
        {
            const double s = u / z;
            return z * s * s * s * (1.0 - 0.5 * s);
        }
        if (u <= l0 - z)
        {
            return 0.5 * z + (u - z);
        }
        const double s = (l0 - u) / z;
        return (l0 - z) - z * s * s * s * (1.0 - 0.5 * s);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ramp's slope J'(u): the smoothstep 3 s^2 - 2 s^3 inside each zone, 1 between
    double TrafficRampSlope(const double u, const double l0, const double z)
    {
        if (u < z)
        {
            const double s = u / z;
            return s * s * (3.0 - 2.0 * s);
        }
        if (u <= l0 - z)
        {
            return 1.0;
        }
        const double s = (l0 - u) / z;
        return s * s * (3.0 - 2.0 * s);
    }

    //////////////////////////////////////////////////////////////////////////
    // Evaluates a valid leg at time tl (clamped to [0, LegS], NaN 0) from the nearer end in time: s = min(tl, LegS -
    // tl) runs accelerate-then-coast from that end's ring, so each half is exact near its own end and the halves meet
    // at LegS / 2 whenever 2 AccelM + PeakMps CoastS = l0 (exact for a MakeLegProfile leg and for TrafficShipLeg)
    FTrafficLegArc TrafficEvaluateLeg(const FSOLLaneLeg& leg, const double legM, const double tl)
    {
        const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
        const double t = FMath::IsNaN(tl) ? 0.0 : FMath::Clamp(tl, 0.0, leg.LegS);
        const bool bFirstHalf = t <= 0.5 * leg.LegS;
        const double s = bFirstHalf ? t : leg.LegS - t;

        // Arc and speed from the nearer end ring: accelerating (or braking, seen backwards), then coasting
        double nearM = 0.0;
        FTrafficLegArc arc;
        if (s <= leg.AccelS)
        {
            nearM = w * s + 0.5 * leg.Alpha * s * s;
            arc.SpeedMps = w + leg.Alpha * s;
        }
        else
        {
            nearM = leg.AccelM + leg.PeakMps * (s - leg.AccelS);
            arc.SpeedMps = leg.PeakMps;
        }
        arc.FromStartM = bFirstHalf ? nearM : legM - nearM;
        arc.FromEndM = bFirstHalf ? legM - nearM : nearM;
        return arc;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the leg a ship's stored constants (dv, t_a) describe on a nominal leg l0, the derivation the rail and
    // the shader share: Alpha = dv / t_a, peak w + dv, d_a = t_a (w + dv / 2), and the coast derived from l0 rather
    // than stored, t_c = (l0 - 2 d_a) / (w + dv), so 2 d_a + v_p t_c = l0 holds for float-rounded constants too and
    // the two halves meet at T_leg / 2; invalid for a bad l0 or a non-positive leg time
    FSOLLaneLeg TrafficShipLeg(const FSOLLaneShip& ship, const double legM)
    {
        FSOLLaneLeg leg;
        if (!FMath::IsFinite(legM) || !(legM > 0.0))
        {
            return leg;
        }
        const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
        leg.DvMps = TrafficPositiveOrZero(ship.DvMps);
        leg.AccelS = TrafficPositiveOrZero(ship.AccelS);
        leg.Alpha = leg.AccelS > 0.0 ? leg.DvMps / leg.AccelS : 0.0;
        leg.PeakMps = w + leg.DvMps;
        leg.AccelM = leg.AccelS * (w + 0.5 * leg.DvMps);

        // The coast may come out slightly negative on an uncapped leg whose rounded t_a overshoots l0 / 2: the halves
        // then meet inside the accelerate phase, off by Alpha (t_c / 2)^2 (below a micrometre for float rounding)
        leg.CoastS = (legM - 2.0 * leg.AccelM) / leg.PeakMps;
        leg.LegS = 2.0 * leg.AccelS + leg.CoastS;
        if (!FMath::IsFinite(leg.LegS) || !(leg.LegS > 0.0))
        {
            return FSOLLaneLeg();
        }
        leg.bValid = true;
        return leg;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the lane's basis: outbound axis, up from the ecliptic north pole (or +X near the pole), right
FSOLLaneBasis SOLLaneTraffic::LaneBasis(const FVector3d& aM, const FVector3d& bM)
{
    FSOLLaneBasis basis;
    if (!TrafficIsFinite(aM) || !TrafficIsFinite(bM))
    {
        return basis;
    }
    const FVector3d delta = bM - aM;
    const double lengthSq = delta.SizeSquared();
    if (!(lengthSq > 0.0) || !FMath::IsFinite(lengthSq))
    {
        return basis;
    }

    // Up from north projected off the axis; within the fallback angle of the pole it is +X projected, and between the
    // fallback and blend angles it turns smoothly about the axis from the one to the other
    const FVector3d axis = delta / FMath::Sqrt(lengthSq);
    const double poleAngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Min(FMath::Abs(axis.Z), 1.0)));
    const double fallbackWeight = TrafficSmoothFall(poleAngleDeg, SOL::NPC_TUNNEL_UP_FALLBACK_DEG,
        SOL::NPC_TUNNEL_UP_BLEND_DEG);
    FVector3d up;
    if (fallbackWeight <= 0.0)
    {
        const FVector3d projected = FVector3d::UnitZ() - axis * axis.Z;
        up = projected / projected.Size();
    }
    else
    {
        const FVector3d projectedX = FVector3d::UnitX() - axis * axis.X;
        const FVector3d upX = projectedX / projectedX.Size();
        if (fallbackWeight >= 1.0)
        {
            up = upX;
        }
        else
        {
            // Rotate the north up about the axis toward the +X up by the weighted signed angle between them
            const FVector3d projectedZ = FVector3d::UnitZ() - axis * axis.Z;
            const FVector3d upZ = projectedZ / projectedZ.Size();
            const FVector3d side = FVector3d::CrossProduct(axis, upZ);
            const double angle = FMath::Atan2(FVector3d::DotProduct(upX, side), FVector3d::DotProduct(upX, upZ));
            const double turned = fallbackWeight * angle;
            up = upZ * FMath::Cos(turned) + side * FMath::Sin(turned);
        }
    }
    basis.Axis = axis;
    basis.Up = up;
    basis.Right = FVector3d::CrossProduct(axis, up);
    basis.bValid = true;
    return basis;
}

//////////////////////////////////////////////////////////////////////////
// Returns the radial factor: the pipe-flow parabola inside the wall, the ramped halo plateau beyond it
double SOLLaneTraffic::RadialFactor(const double rho)
{
    if (FMath::IsNaN(rho))
    {
        return 0.0;
    }
    const double r = FMath::Abs(rho);
    if (r <= 1.0)
    {
        return 1.0 - r * r;
    }
    return SOL::NPC_LANE_HALO_KAPPA * TrafficSmoothstep(1.0, 1.0 + SOL::NPC_LANE_HALO_RAMP_WALL_RADII, r);
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's flip-and-burn leg; v_p^2 - w^2 = min(Alpha l0, kappa (c^2 - w^2)) gives dv without cancellation
FSOLLaneLeg SOLLaneTraffic::MakeLegProfile(const double kappa, const double effort, const double legM)
{
    FSOLLaneLeg leg;
    if (!FMath::IsFinite(legM) || !(legM > 0.0))
    {
        return leg;
    }
    const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
    const double c = SOL::NPC_LANE_SPEED_CAP_MPS;
    const double k = FMath::IsNaN(kappa) ? 0.0 : FMath::Clamp(kappa, 0.0, 1.0);
    const double e = FMath::IsNaN(effort) ? SOL::NPC_LANE_EFFORT_MIN
        : FMath::Clamp(effort, SOL::NPC_LANE_EFFORT_MIN, SOL::NPC_LANE_EFFORT_MAX);

    // Peak: the unbraked mid-leg speed or the blended cap c_k, whichever is lower
    const double alpha = k * e * SOL::NPC_LANE_PEAK_ACCEL_MPS2;
    const double rampExcessSq = alpha * legM;
    const double capExcessSq = k * (c * c - w * w);
    const bool bCapped = rampExcessSq > capExcessSq;
    const double excessSq = bCapped ? capExcessSq : rampExcessSq;
    const double peak = FMath::Sqrt(w * w + excessSq);
    leg.Alpha = alpha;
    leg.PeakMps = peak;
    leg.DvMps = excessSq / (peak + w);

    // Accelerate, coast, brake (the wall speed throughout when there is no acceleration)
    if (alpha > 0.0)
    {
        leg.AccelS = leg.DvMps / alpha;
        if (bCapped)
        {
            leg.AccelM = excessSq / (2.0 * alpha);
            leg.CoastS = FMath::Max(0.0, (legM - 2.0 * leg.AccelM) / peak);
        }
        else
        {
            leg.AccelM = 0.5 * legM;
        }
    }
    else
    {
        leg.CoastS = legM / peak;
    }
    leg.LegS = 2.0 * leg.AccelS + leg.CoastS;
    leg.bValid = true;
    return leg;
}

//////////////////////////////////////////////////////////////////////////
// Returns the arc from the departure ring at leg time tl
double SOLLaneTraffic::LegArcM(const FSOLLaneLeg& leg, const double legM, const double tl)
{
    return leg.bValid ? TrafficEvaluateLeg(leg, legM, tl).FromStartM : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the speed at leg time tl
double SOLLaneTraffic::LegSpeedMps(const FSOLLaneLeg& leg, const double legM, const double tl)
{
    return leg.bValid ? TrafficEvaluateLeg(leg, legM, tl).SpeedMps : SOL::NPC_LANE_WALL_SPEED_MPS;
}

//////////////////////////////////////////////////////////////////////////
// Returns the speed at leg arc u from the squared-speed law
double SOLLaneTraffic::LegSpeedAtArcMps(const FSOLLaneLeg& leg, const double legM, const double u)
{
    const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
    if (!leg.bValid)
    {
        return w;
    }
    const double arc = FMath::IsNaN(u) ? 0.0 : FMath::Clamp(u, 0.0, legM);
    const double nearestEndM = FMath::Min(arc, legM - arc);
    return FMath::Sqrt(FMath::Min(w * w + 2.0 * leg.Alpha * nearestEndM, leg.PeakMps * leg.PeakMps));
}

//////////////////////////////////////////////////////////////////////////
// Returns the mean-effort speed at arc u and radius rho
double SOLLaneTraffic::MeanSpeedMps(const double u, const double rho, const double legM)
{
    return LegSpeedAtArcMps(MakeLegProfile(RadialFactor(rho), SOL::NPC_LANE_EFFORT_MEAN, legM), legM, u);
}

//////////////////////////////////////////////////////////////////////////
// Returns the metric zone of a nominal leg
double SOLLaneTraffic::MetricZoneM(const double l0)
{
    if (!FMath::IsFinite(l0) || !(l0 > 0.0))
    {
        return 0.0;
    }
    return FMath::Min(SOL::NPC_LANE_METRIC_ZONE_FRACTION * l0, 0.5 * l0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the metric map X(u) from the nominal leg onto the current leg
double SOLLaneTraffic::MetricMapM(const double u, const double l0, const double l)
{
    if (!TrafficMapInputsValid(l0, l))
    {
        return 0.0;
    }
    const double z = MetricZoneM(l0);
    const double arc = FMath::IsNaN(u) ? 0.0 : FMath::Clamp(u, 0.0, l0);
    return arc + (TrafficMapLeg(l, z) - l0) * TrafficRampJ(arc, l0, z) / (l0 - z);
}

//////////////////////////////////////////////////////////////////////////
// Returns the metric map's slope X'(u)
double SOLLaneTraffic::MetricMapSlope(const double u, const double l0, const double l)
{
    if (!TrafficMapInputsValid(l0, l))
    {
        return 1.0;
    }
    const double z = MetricZoneM(l0);
    const double arc = FMath::IsNaN(u) ? 0.0 : FMath::Clamp(u, 0.0, l0);
    return 1.0 + (TrafficMapLeg(l, z) - l0) * TrafficRampSlope(arc, l0, z) / (l0 - z);
}

//////////////////////////////////////////////////////////////////////////
// Returns the nominal arc of a current-leg distance x: Newton steps kept inside a bisection bracket, solved from the
// nearer end (X is symmetric, so x past l / 2 solves X(v) = l - x and returns l0 - v)
double SOLLaneTraffic::MetricMapInverseM(const double x, const double l0, const double l)
{
    if (!TrafficMapInputsValid(l0, l))
    {
        return 0.0;
    }
    const double mapLeg = TrafficMapLeg(l, MetricZoneM(l0));
    const double distance = FMath::IsNaN(x) ? 0.0 : FMath::Clamp(x, 0.0, mapLeg);
    const bool bFromEnd = distance > 0.5 * mapLeg;
    const double target = bFromEnd ? mapLeg - distance : distance;

    // Safeguarded Newton iteration from the proportional guess
    double lo = 0.0;
    double hi = l0;
    double v = FMath::Clamp(target * l0 / mapLeg, 0.0, l0);
    for (int32 step = 0; step < TRAFFIC_INVERSE_MAX_STEPS; ++step)
    {
        const double residual = MetricMapM(v, l0, l) - target;
        if (residual == 0.0)
        {
            break;
        }
        if (residual > 0.0)
        {
            hi = v;
        }
        else
        {
            lo = v;
        }
        const double slope = MetricMapSlope(v, l0, l);
        double next = slope > 0.0 ? v - residual / slope : 0.5 * (lo + hi);
        if (!(next > lo && next < hi))
        {
            next = 0.5 * (lo + hi);
        }
        const bool bConverged = FMath::Abs(next - v) <= TRAFFIC_INVERSE_TOLERANCE * l0;
        v = next;
        if (bConverged)
        {
            break;
        }
    }
    return bFromEnd ? l0 - v : v;
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's cycle times
FSOLLaneCycle SOLLaneTraffic::MakeCycle(const double legS, const double uTurnDepthM)
{
    FSOLLaneCycle cycle;
    cycle.LegS = TrafficPositiveOrZero(legS);
    cycle.TurnS = TRAFFIC_PI * TrafficPositiveOrZero(uTurnDepthM) / SOL::NPC_LANE_WALL_SPEED_MPS;
    cycle.CycleS = 2.0 * cycle.LegS + 2.0 * cycle.TurnS;
    return cycle;
}

//////////////////////////////////////////////////////////////////////////
// Returns where a ship is in its cycle; tau = (phase x T_cyc + t) mod T_cyc, exact for phase 0 and t inside a cycle
FSOLLaneCyclePoint SOLLaneTraffic::CycleSegmentAt(const FSOLLaneCycle& cycle, const double phase, const double timeS)
{
    FSOLLaneCyclePoint point;
    const double wrapped = TrafficWrapPhase(phase);
    point.Phase = wrapped;
    if (!FMath::IsFinite(cycle.CycleS) || !(cycle.CycleS > 0.0))
    {
        return point;
    }

    // Cycle time and phase, both kept inside [0, T_cyc) and [0, 1)
    const double t = FMath::IsFinite(timeS) ? timeS : 0.0;
    double tau = std::fmod(wrapped * cycle.CycleS + t, cycle.CycleS);
    if (tau < 0.0)
    {
        tau += cycle.CycleS;
    }
    if (!(tau >= 0.0 && tau < cycle.CycleS))
    {
        tau = 0.0;
    }
    point.CycleTimeS = tau;
    point.Phase = FMath::Min(tau / cycle.CycleS, TRAFFIC_BELOW_ONE);

    // The half-open segments: outbound, turn at B, inbound, turn at A
    const double turnBEndS = cycle.LegS + cycle.TurnS;
    const double inboundEndS = turnBEndS + cycle.LegS;
    if (tau < cycle.LegS)
    {
        point.Segment = ESOLLaneSegment::Outbound;
        point.SegmentTimeS = tau;
    }
    else if (tau < turnBEndS)
    {
        point.Segment = ESOLLaneSegment::TurnAtB;
        point.SegmentTimeS = tau - cycle.LegS;
    }
    else if (tau < inboundEndS)
    {
        point.Segment = ESOLLaneSegment::Inbound;
        point.SegmentTimeS = tau - turnBEndS;
    }
    else
    {
        point.Segment = ESOLLaneSegment::TurnAtA;
        point.SegmentTimeS = tau - inboundEndS;
    }
    return point;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's frozen profile (SDD 50 section 4.2)
FSOLLaneProfile SOLLaneTraffic::MakeLaneProfile(const double wallRadiusM, const double nominalLengthM,
    const double bodyRadiusAM, const double bodyRadiusBM)
{
    FSOLLaneProfile profile;
    const double rw = wallRadiusM;
    const double marginM = SOL::NPC_TUNNEL_END_MARGIN_RADII * rw;
    profile.WallRadiusM = rw;
    profile.HaloOuterRadiusM = SOL::NPC_LANE_HALO_OUTER_WALL_RADII * rw;
    profile.NominalLengthM = nominalLengthM;
    profile.EndArcAM = bodyRadiusAM + marginM;
    profile.EndArcBM = bodyRadiusBM + marginM;
    profile.NominalLegM = nominalLengthM - profile.EndArcAM - profile.EndArcBM;
    profile.UTurnDepthM = SOL::NPC_LANE_UTURN_DEPTH_WALL_RADII * rw;
    profile.UTurnS = TRAFFIC_PI * profile.UTurnDepthM / SOL::NPC_LANE_WALL_SPEED_MPS;
    profile.MetricZoneM = MetricZoneM(profile.NominalLegM);

    // Drivable: a wall, a finite length and a leg longer than both metric zones (and than the subtraction's rounding)
    profile.bDrivable = FMath::IsFinite(rw) && rw > 0.0 && FMath::IsFinite(nominalLengthM)
        && FMath::IsFinite(profile.NominalLegM) && profile.NominalLegM > 2.0 * profile.MetricZoneM
        && profile.NominalLegM > TRAFFIC_LEG_ROUNDING_FRACTION * FMath::Abs(nominalLengthM);
    return profile;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's current frame
FSOLLaneFrame SOLLaneTraffic::MakeLaneFrame(const FSOLLaneProfile& profile, const FVector3d& aM, const FVector3d& bM)
{
    FSOLLaneFrame frame;
    frame.AM = aM;
    frame.BM = bM;
    const FSOLLaneBasis basis = LaneBasis(aM, bM);
    if (!basis.bValid)
    {
        return frame;
    }
    frame.Axis = basis.Axis;
    frame.Up = basis.Up;
    frame.Right = basis.Right;

    // The current leg, floored where the metric map would fold over (the end rings within a zone of each other or
    // crossed, never on a real lane); B's end moves out by the excess so evaluations from either end agree
    const double rawLegM = (bM - aM).Size() - profile.EndArcAM - profile.EndArcBM;
    frame.CurrentLegM = FMath::Max(0.0, TrafficMapLeg(rawLegM, profile.MetricZoneM));
    frame.EndShiftM = frame.CurrentLegM - rawLegM;
    frame.bValid = true;
    return frame;
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's constants from its dimensionless spawn values
FSOLLaneShip SOLLaneTraffic::MakeLaneShip(const FSOLLaneProfile& profile, const double phase, const double effort,
    const double rho, const double clockRad)
{
    const double radius = FMath::IsNaN(rho) ? 0.0 : FMath::Clamp(rho, 0.0, SOL::NPC_LANE_HALO_OUTER_WALL_RADII);
    const double clock = FMath::IsNaN(clockRad) ? 0.5 * TRAFFIC_PI : FMath::Clamp(clockRad, 0.0, TRAFFIC_PI);
    const FSOLLaneLeg leg = MakeLegProfile(RadialFactor(radius), effort, profile.NominalLegM);
    const double lateralM = radius * profile.WallRadiusM;

    FSOLLaneShip ship;
    ship.Phase = TrafficWrapPhase(phase);
    ship.DvMps = leg.DvMps;
    ship.UpOffsetM = lateralM * FMath::Cos(clock);
    ship.RightOffsetM = FMath::Max(0.0, lateralM * FMath::Sin(clock));
    ship.AccelS = leg.AccelS;
    return ship;
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's leg time, derived from its constants and the lane's nominal leg
double SOLLaneTraffic::LaneShipLegS(const FSOLLaneProfile& profile, const FSOLLaneShip& ship)
{
    return TrafficShipLeg(ship, profile.NominalLegM).LegS;
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's position relative to an origin, from the nearer end of the lane
FVector3d SOLLaneTraffic::LaneShipPositionRelM(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FSOLLaneShip& ship, const double timeS, const FVector3d& originM)
{
    const FVector3d aRelM = frame.AM - originM;
    if (!frame.bValid)
    {
        return aRelM;
    }
    const FVector3d bRelM = (frame.BM - originM) + frame.Axis * frame.EndShiftM;     // B's end (shifted on a crowded
                                                                                     // lane only)
    const FVector3d upM = frame.Up * ship.UpOffsetM;
    const FVector3d rightM = frame.Right * ship.RightOffsetM;

    // A lane that cannot be driven parks its ships on the midpoint disc
    const FSOLLaneLeg leg = TrafficShipLeg(ship, profile.NominalLegM);
    if (!profile.bDrivable || !leg.bValid)
    {
        return aRelM + frame.Axis * (0.5 * (frame.BM - frame.AM).Size()) + upM + rightM;
    }

    const FSOLLaneCyclePoint point = CycleSegmentAt(MakeCycle(leg.LegS, profile.UTurnDepthM), ship.Phase, timeS);
    const double l0 = profile.NominalLegM;
    const double l = frame.CurrentLegM;
    switch (point.Segment)
    {
    case ESOLLaneSegment::Outbound:
    case ESOLLaneSegment::Inbound:
        {
            // The leg from the nearer end ring: outbound departs A on the right, inbound departs B on the left
            const bool bOutbound = point.Segment == ESOLLaneSegment::Outbound;
            const FTrafficLegArc arc = TrafficEvaluateLeg(leg, l0, point.SegmentTimeS);
            const bool bNearDeparture = arc.FromStartM <= arc.FromEndM;
            const double ringDistanceM = MetricMapM(bNearDeparture ? arc.FromStartM : arc.FromEndM, l0, l);
            const FVector3d lateralM = bOutbound ? upM + rightM : upM - rightM;
            if (bOutbound == bNearDeparture)
            {
                return aRelM + frame.Axis * (profile.EndArcAM + ringDistanceM) + lateralM;
            }
            return bRelM - frame.Axis * (profile.EndArcBM + ringDistanceM) + lateralM;
        }
    case ESOLLaneSegment::TurnAtB:
        {
            const double theta = TRAFFIC_PI * point.SegmentTimeS / profile.UTurnS;
            return bRelM + frame.Axis * (profile.UTurnDepthM * FMath::Sin(theta) - profile.EndArcBM) + upM
                + rightM * FMath::Cos(theta);
        }
    case ESOLLaneSegment::TurnAtA:
    default:
        {
            const double theta = TRAFFIC_PI * point.SegmentTimeS / profile.UTurnS;
            return aRelM + frame.Axis * (profile.EndArcAM - profile.UTurnDepthM * FMath::Sin(theta)) + upM
                - rightM * FMath::Cos(theta);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's absolute position
FVector3d SOLLaneTraffic::LaneShipPositionM(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FSOLLaneShip& ship, const double timeS)
{
    return LaneShipPositionRelM(profile, frame, ship, timeS, FVector3d::ZeroVector);
}

//////////////////////////////////////////////////////////////////////////
// Returns the lane ship speed bound relative to body A (the wake check)
double SOLLaneTraffic::LaneShipSpeedBoundMps(const double maxPeakMps, const double nominalLegM,
    const double currentLegM, const double haloOuterRadiusM, const double endpointRelativeSpeedMps)
{
    // The metric map's largest slope (l - Z) / (l0 - Z) on a stretched lane; a shortened one only slows the middle
    double slope = 1.0;
    if (TrafficMapInputsValid(nominalLegM, currentLegM) && currentLegM > nominalLegM)
    {
        const double z = MetricZoneM(nominalLegM);
        slope = (currentLegM - z) / (nominalLegM - z);
    }
    const double turnScale = FMath::IsFinite(currentLegM) && currentLegM > 0.0
        ? 1.0 + FMath::Max(haloOuterRadiusM, 0.0) / currentLegM : 1.0;

    // A non-finite peak reads as the cap (the largest any lane ship reaches), so the bound never turns NaN
    const double peakMps = FMath::IsFinite(maxPeakMps) ? FMath::Abs(maxPeakMps) : SOL::NPC_LANE_SPEED_CAP_MPS;
    return (peakMps * slope + FMath::Abs(endpointRelativeSpeedMps) * turnScale) * (1.0 + TRAFFIC_SPEED_BOUND_ROUNDING);
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's thrust kind, magnitude and direction at a time
FSOLLaneThrustState SOLLaneTraffic::LaneThrustAt(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FSOLLaneShip& ship, const double timeS)
{
    FSOLLaneThrustState state;
    state.Direction = frame.Axis;
    const FSOLLaneLeg leg = TrafficShipLeg(ship, profile.NominalLegM);
    if (!frame.bValid || !profile.bDrivable || !leg.bValid)
    {
        return state;
    }

    const FSOLLaneCyclePoint point = CycleSegmentAt(MakeCycle(leg.LegS, profile.UTurnDepthM), ship.Phase, timeS);
    state.Segment = point.Segment;
    if (point.Segment == ESOLLaneSegment::Outbound || point.Segment == ESOLLaneSegment::Inbound)
    {
        // Legs: accelerate along travel, coast, brake against it (the nominal profile only: the metric map's X'' u'^2
        // term, below a in the end zones of every default lane, is left out, SDD 50 section 4.12)
        const FVector3d travel = point.Segment == ESOLLaneSegment::Outbound ? frame.Axis : -frame.Axis;
        const double tl = point.SegmentTimeS;
        state.Direction = travel;
        if (leg.Alpha > 0.0 && tl < leg.AccelS)
        {
            state.Thrust = ESOLLaneThrust::Accelerate;
            state.AccelMps2 = leg.Alpha;
        }
        else if (leg.Alpha > 0.0 && leg.LegS - tl < leg.AccelS)
        {
            state.Thrust = ESOLLaneThrust::Brake;
            state.AccelMps2 = leg.Alpha;
            state.Direction = -travel;
        }
        return state;
    }

    // U-turns: the half-ellipse's centripetal acceleration (pi / T_U)^2 x (p - c), toward the centre c
    const bool bAtB = point.Segment == ESOLLaneSegment::TurnAtB;
    const double rate = TRAFFIC_PI / profile.UTurnS;
    const double theta = rate * point.SegmentTimeS;
    const double side = bAtB ? 1.0 : -1.0;
    const FVector3d fromCentreM = (frame.Axis * (profile.UTurnDepthM * FMath::Sin(theta))
        + frame.Right * (ship.RightOffsetM * FMath::Cos(theta))) * side;
    const double distanceM = fromCentreM.Size();
    state.Thrust = ESOLLaneThrust::Turn;
    state.AccelMps2 = rate * rate * distanceM;
    state.Direction = distanceM > 0.0 ? -fromCentreM / distanceM : -frame.Axis * side;
    return state;
}

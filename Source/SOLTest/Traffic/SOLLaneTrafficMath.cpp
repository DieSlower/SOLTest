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
    constexpr int32 TRAFFIC_LEAVE_MARCH_STEPS = 16;                // The ETA's leave arc: march samples...
    constexpr int32 TRAFFIC_LEAVE_BISECT_STEPS = 12;               // ...then bisection steps
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

    //////////////////////////////////////////////////////////////////////////
    // Returns the hull flip fraction at a cycle point: f on a leg, in a turn 1 for a ship whose legs brake (the brake
    // attitude is held through the turn) and 0 otherwise
    double TrafficFlipFraction(const FSOLLaneLeg& leg, const FSOLLaneCyclePoint& point)
    {
        if (point.Segment == ESOLLaneSegment::Outbound || point.Segment == ESOLLaneSegment::Inbound)
        {
            return SOLLaneTraffic::LaneHullFlipFraction(leg.LegS, leg.AccelS, point.SegmentTimeS);
        }
        return leg.AccelS > 0.0 ? 1.0 : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the hull facing at a cycle point (SDD 50 section 4.4, D21); the point is ignored for an invalid frame,
    // a non-drivable lane or an invalid leg
    FVector3d TrafficHullForward(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame, const FSOLLaneShip& ship,
        const FSOLLaneLeg& leg, const FSOLLaneCyclePoint& point)
    {
        if (!frame.bValid)
        {
            return frame.Axis.SizeSquared() > 0.5 ? frame.Axis : FVector3d::UnitX();
        }
        if (!profile.bDrivable || !leg.bValid)
        {
            return frame.Axis;
        }

        // Legs: facing travel, turning over about Up in the mid-leg window, facing the brake thrust after it
        if (point.Segment == ESOLLaneSegment::Outbound || point.Segment == ESOLLaneSegment::Inbound)
        {
            const FVector3d travel = point.Segment == ESOLLaneSegment::Outbound ? frame.Axis : -frame.Axis;
            const double angle = TRAFFIC_PI * SOLLaneTraffic::LaneHullFlipFraction(leg.LegS, leg.AccelS,
                point.SegmentTimeS);
            return travel * FMath::Cos(angle) + frame.Up * FMath::Sin(angle);
        }

        // Turns: a braking ship holds its brake attitude in world space (it enters the next leg facing travel)
        const double side = point.Segment == ESOLLaneSegment::TurnAtB ? 1.0 : -1.0;
        if (leg.AccelS > 0.0)
        {
            return -frame.Axis * side;
        }

        // A ship that never brakes faces the turn's velocity, d/dtheta of the half-ellipse (the reversal direction at
        // a zero-speed apex of an axis ship)
        const double theta = TRAFFIC_PI * point.SegmentTimeS / profile.UTurnS;
        const double cosTheta = FMath::Cos(theta);
        const FVector3d velocity = (frame.Axis * (profile.UTurnDepthM * cosTheta)
            - frame.Right * (ship.RightOffsetM * FMath::Sin(theta))) * side;
        const double speed = velocity.Size();
        if (speed > 0.0 && FMath::IsFinite(speed))
        {
            return velocity / speed;
        }
        return frame.Axis * (cosTheta > 0.0 ? side : -side);
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
// Returns a ship's flip-and-burn leg (D21): dv = max(0, min(dv_f, dv_c)), each root written without cancellation, so
// the coast is at least T_f on a burning leg and a leg of at most w T_f has no burn
FSOLLaneLeg SOLLaneTraffic::MakeLegProfile(const double kappa, const double effort, const double legM,
    const double flipS)
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
    const double flipWindowS = FMath::IsNaN(flipS) ? 0.0 : FMath::Max(flipS, 0.0);
    const double alpha = k * e * SOL::NPC_LANE_PEAK_ACCEL_MPS2;

    // Flip-limited excess dv_f: the positive root of dv^2 + (2 w + alpha T_f) dv - alpha (l0 - w T_f) = 0 in its
    // cancellation-free form; 0 when the leg leaves no room to burn (l0 <= w T_f, an infinite window included)
    const double burnRoomM = legM - w * flipWindowS;
    double flipExcessMps = 0.0;
    if (alpha > 0.0 && burnRoomM > 0.0)
    {
        const double linear = 2.0 * w + alpha * flipWindowS;
        flipExcessMps = 2.0 * alpha * burnRoomM / (linear + FMath::Sqrt(linear * linear + 4.0 * alpha * burnRoomM));
    }

    // Cap-limited excess dv_c = kappa (c^2 - w^2) / (c_k + w), c_k^2 = w^2 + kappa (c^2 - w^2)
    const double capExcessSq = k * (c * c - w * w);
    const double capExcessMps = capExcessSq / (FMath::Sqrt(w * w + capExcessSq) + w);
    const double dv = FMath::Max(0.0, FMath::Min(flipExcessMps, capExcessMps));

    // Accelerate, coast, brake (the wall speed throughout when there is no burn)
    leg.Alpha = alpha;
    leg.DvMps = dv;
    leg.PeakMps = w + dv;
    leg.AccelS = alpha > 0.0 ? dv / alpha : 0.0;
    leg.AccelM = leg.AccelS * (w + 0.5 * dv);
    leg.CoastS = FMath::Max(0.0, (legM - 2.0 * leg.AccelM) / leg.PeakMps);
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

// Returns a ship's position relative to an origin from its already derived leg and cycle (defined below)
static FVector3d TrafficPositionRelMImpl(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FSOLLaneShip& ship, const FSOLLaneLeg& leg, const FSOLLaneCycle& cycle, double timeS,
    const FVector3d& originM);

//////////////////////////////////////////////////////////////////////////
// Returns a ship's position relative to an origin, from the nearer end of the lane
FVector3d SOLLaneTraffic::LaneShipPositionRelM(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FSOLLaneShip& ship, const double timeS, const FVector3d& originM)
{
    const FSOLLaneLeg leg = TrafficShipLeg(ship, profile.NominalLegM);
    return TrafficPositionRelMImpl(profile, frame, ship, leg, MakeCycle(leg.LegS, profile.UTurnDepthM), timeS, originM);
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's velocity (central difference over +- stepS) and hull facing, the leg and cycle derived once
FSOLLaneShipMotion SOLLaneTraffic::LaneShipMotion(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FSOLLaneShip& ship, const double timeS, const double stepS)
{
    const FSOLLaneLeg leg = TrafficShipLeg(ship, profile.NominalLegM);
    const FSOLLaneCycle cycle = MakeCycle(leg.LegS, profile.UTurnDepthM);
    FSOLLaneShipMotion motion;
    motion.VelocityMps = (TrafficPositionRelMImpl(profile, frame, ship, leg, cycle, timeS + stepS, frame.AM)
        - TrafficPositionRelMImpl(profile, frame, ship, leg, cycle, timeS - stepS, frame.AM)) / (2.0 * stepS);
    motion.HullForward = (!frame.bValid || !profile.bDrivable || !leg.bValid)
        ? TrafficHullForward(profile, frame, ship, leg, FSOLLaneCyclePoint())
        : TrafficHullForward(profile, frame, ship, leg, CycleSegmentAt(cycle, ship.Phase, timeS));
    return motion;
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's position relative to an origin from its already derived leg and cycle (LaneShipPositionRelM)
static FVector3d TrafficPositionRelMImpl(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FSOLLaneShip& ship, const FSOLLaneLeg& leg, const FSOLLaneCycle& cycle, const double timeS,
    const FVector3d& originM)
{
    using namespace SOLLaneTraffic;
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
    if (!profile.bDrivable || !leg.bValid)
    {
        return aRelM + frame.Axis * (0.5 * (frame.BM - frame.AM).Size()) + upM + rightM;
    }

    const FSOLLaneCyclePoint point = CycleSegmentAt(cycle, ship.Phase, timeS);
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
        state.HullForward = TrafficHullForward(profile, frame, ship, leg, FSOLLaneCyclePoint());
        return state;
    }

    const FSOLLaneCyclePoint point = CycleSegmentAt(MakeCycle(leg.LegS, profile.UTurnDepthM), ship.Phase, timeS);
    state.Segment = point.Segment;
    state.FlipFraction = TrafficFlipFraction(leg, point);
    state.HullForward = TrafficHullForward(profile, frame, ship, leg, point);
    if (point.Segment == ESOLLaneSegment::Outbound || point.Segment == ESOLLaneSegment::Inbound)
    {
        // Legs: accelerate along travel, brake against it, the flip window (no thrust) centred on the leg's midpoint,
        // coast otherwise; the burn kinds win where a float-rounded window overlaps them by a rounding step (the
        // nominal profile only: the metric map's X'' u'^2 term, below a in the end zones of every default lane, is left
        // out, SDD 50 section 4.12)
        const FVector3d travel = point.Segment == ESOLLaneSegment::Outbound ? frame.Axis : -frame.Axis;
        const double tl = point.SegmentTimeS;
        state.Direction = travel;
        if (leg.AccelS > 0.0 && tl < leg.AccelS)
        {
            state.Thrust = ESOLLaneThrust::Accelerate;
            state.AccelMps2 = leg.Alpha;
        }
        else if (leg.AccelS > 0.0 && tl > leg.LegS - leg.AccelS)
        {
            state.Thrust = ESOLLaneThrust::Brake;
            state.AccelMps2 = leg.Alpha;
            state.Direction = -travel;
        }
        else if (leg.AccelS > 0.0 && FMath::Abs(tl - 0.5 * leg.LegS) < 0.5 * SOL::NPC_LANE_FLIP_S)
        {
            state.Thrust = ESOLLaneThrust::Flip;
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

//////////////////////////////////////////////////////////////////////////
// Returns the hull flip fraction at a leg time
double SOLLaneTraffic::LaneHullFlipFraction(const double legS, const double accelS, const double legTimeS)
{
    if (!FMath::IsFinite(legS) || !FMath::IsFinite(accelS) || !FMath::IsFinite(legTimeS) || !(accelS > 0.0)
        || !(legS > 0.0))
    {
        return 0.0;
    }
    const double tl = FMath::Clamp(legTimeS, 0.0, legS);
    const double flipWindowS = SOL::NPC_LANE_FLIP_S;
    if (!(flipWindowS > 0.0))
    {
        return tl >= 0.5 * legS ? 1.0 : 0.0;
    }
    const double x = FMath::Clamp((tl - 0.5 * (legS - flipWindowS)) / flipWindowS, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship's hull facing at a time
FVector3d SOLLaneTraffic::LaneHullForward(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FSOLLaneShip& ship, const double timeS)
{
    const FSOLLaneLeg leg = TrafficShipLeg(ship, profile.NominalLegM);
    if (!frame.bValid || !profile.bDrivable || !leg.bValid)
    {
        return TrafficHullForward(profile, frame, ship, leg, FSOLLaneCyclePoint());
    }
    const FSOLLaneCyclePoint point = CycleSegmentAt(MakeCycle(leg.LegS, profile.UTurnDepthM), ship.Phase, timeS);
    return TrafficHullForward(profile, frame, ship, leg, point);
}

//////////////////////////////////////////////////////////////////////////
// Returns the verdict on a player speed against a traffic speed
FSOLLaneMergeRating SOLLaneTraffic::RateMerge(const double trafficSpeedMps, const double playerSpeedMps)
{
    FSOLLaneMergeRating rating;
    if (!FMath::IsFinite(trafficSpeedMps) || !(trafficSpeedMps > 0.0) || !FMath::IsFinite(playerSpeedMps))
    {
        return rating;
    }
    rating.bValid = true;
    rating.Delta = (playerSpeedMps - trafficSpeedMps) / trafficSpeedMps;
    const double offBy = FMath::Abs(rating.Delta);
    if (offBy <= SOL::NPC_LANE_MERGE_MATCH_FRACTION)
    {
        rating.Verdict = ESOLLaneMergeVerdict::Matched;
        rating.Band = ESOLLaneMergeBand::Green;
        return rating;
    }
    rating.Verdict = rating.Delta > 0.0 ? ESOLLaneMergeVerdict::TooFast : ESOLLaneMergeVerdict::TooSlow;
    rating.Band = offBy <= SOL::NPC_LANE_MERGE_AMBER_FRACTION ? ESOLLaneMergeBand::Amber : ESOLLaneMergeBand::Red;
    return rating;
}

//////////////////////////////////////////////////////////////////////////
// Returns the traffic direction the player would merge into: the velocity, else the heading, else the half
int32 SOLLaneTraffic::MergeDirectionSign(const double alongVelocityMps, const double alongForward,
    const double rightOffsetM)
{
    if (FMath::Abs(alongVelocityMps) >= SOL::NPC_LANE_MERGE_DIRECTION_MIN_MPS)
    {
        return alongVelocityMps > 0.0 ? 1 : -1;
    }
    if (FMath::Abs(alongForward) > SOL::NPC_LANE_MERGE_FORWARD_MIN)
    {
        return alongForward > 0.0 ? 1 : -1;
    }
    return rightOffsetM < 0.0 ? -1 : 1;
}

//////////////////////////////////////////////////////////////////////////
// Returns where a point lies relative to a lane: along-axis distance, leg arc, foot fraction, offsets, rho and flags
FSOLLanePoint SOLLaneTraffic::LanePointOf(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const FVector3d& pointM)
{
    FSOLLanePoint point;
    const double spanM = (frame.BM - frame.AM).Size();
    if (!frame.bValid || !profile.bDrivable || !(spanM > 0.0) || !(profile.WallRadiusM > 0.0)
        || !FMath::IsFinite(pointM.X) || !FMath::IsFinite(pointM.Y) || !FMath::IsFinite(pointM.Z))
    {
        return point;
    }

    // The offsets on the lane basis, then the arc, foot and radius
    const FVector3d relM = pointM - frame.AM;
    point.AlongM = relM.Dot(frame.Axis);
    point.RightOffsetM = relM.Dot(frame.Right);
    point.UpOffsetM = relM.Dot(frame.Up);
    point.LegArcM = point.AlongM - profile.EndArcAM;
    point.FootFraction = FMath::Clamp(point.AlongM / spanM, 0.0, 1.0);
    point.Rho = FMath::Sqrt(point.RightOffsetM * point.RightOffsetM + point.UpOffsetM * point.UpOffsetM)
        / profile.WallRadiusM;
    point.bInLeg = point.LegArcM >= 0.0 && point.LegArcM <= frame.CurrentLegM;
    point.bInHalo = point.bInLeg && point.Rho <= profile.HaloOuterRadiusM / profile.WallRadiusM;
    point.bValid = true;
    return point;
}

//////////////////////////////////////////////////////////////////////////
// Returns the mean traffic speed at a leg arc and rho for a direction, through the metric map
double SOLLaneTraffic::TrafficSpeedMps(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const double legArcM, const double rho, const int32 directionSign)
{
    if (!frame.bValid || !profile.bDrivable || FMath::IsNaN(legArcM) || FMath::IsNaN(rho))
    {
        return 0.0;
    }
    const double l0 = profile.NominalLegM;
    const double l = frame.CurrentLegM;
    const double x = FMath::Clamp(legArcM, 0.0, l);
    const double fromDepartureM = directionSign > 0 ? x : l - x;
    const double u = MetricMapInverseM(fromDepartureM, l0, l);
    return MeanSpeedMps(u, rho, l0) * MetricMapSlope(u, l0, l);
}

//////////////////////////////////////////////////////////////////////////
// Returns the merge readout of the lane whose halo holds the player at the smallest rho (lower lane index on a tie)
FSOLLaneMergeReadout SOLLaneTraffic::MergeReadout(const TConstArrayView<FSOLLaneMergeLane> lanes,
    const FVector3d& playerM, const FVector3d& playerVelocityMps, const FVector3d& playerForward,
    const double timeWarpScale)
{
    FSOLLaneMergeReadout readout;
    if (!FMath::IsFinite(playerVelocityMps.X) || !FMath::IsFinite(playerVelocityMps.Y)
        || !FMath::IsFinite(playerVelocityMps.Z))
    {
        return readout;
    }

    // The lane: the smallest rho among the halos holding the player (strictly smaller, so the lower index on a tie)
    FSOLLanePoint best;
    for (int32 lane = 0; lane < lanes.Num(); ++lane)
    {
        const FSOLLanePoint point = LanePointOf(lanes[lane].Profile, lanes[lane].Frame, playerM);
        if (point.bInHalo && (readout.Lane == INDEX_NONE || point.Rho < best.Rho))
        {
            best = point;
            readout.Lane = lane;
        }
    }
    if (readout.Lane == INDEX_NONE)
    {
        return readout;
    }

    // The lane frame at the foot, the direction from the velocity, heading or half, then the two speeds
    const FSOLLaneMergeLane& lane = lanes[readout.Lane];
    const FVector3d& axis = lane.Frame.Axis;
    readout.FrameVelocityMps = lane.AVelocityMps + (lane.BVelocityMps - lane.AVelocityMps) * best.FootFraction;
    const double alongVelocityMps = (playerVelocityMps - readout.FrameVelocityMps).Dot(axis);
    const double forwardSize = playerForward.Size();
    const double alongForward = FMath::IsFinite(forwardSize) && forwardSize > 0.0
        ? playerForward.Dot(axis) / forwardSize : 0.0;
    readout.DirectionSign = MergeDirectionSign(alongVelocityMps, alongForward, best.RightOffsetM);
    readout.Rho = best.Rho;
    readout.TrafficSpeedMps = TrafficSpeedMps(lane.Profile, lane.Frame, best.LegArcM, best.Rho,
        readout.DirectionSign);
    readout.PlayerSpeedMps = static_cast<double>(readout.DirectionSign) * alongVelocityMps;
    const FSOLLaneMergeRating rating = RateMerge(readout.TrafficSpeedMps, readout.PlayerSpeedMps);
    readout.Delta = rating.Delta;
    readout.Verdict = rating.Verdict;
    readout.Band = rating.Band;
    readout.bWarpNote = timeWarpScale > 1.0;
    readout.bShown = rating.bValid;
    if (!readout.bShown)
    {
        readout.Lane = INDEX_NONE;
    }
    return readout;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns +1 for an outbound direction sign (> 0), else -1
    double TrafficDirection(const int32 directionSign)
    {
        return directionSign > 0 ? 1.0 : -1.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a lane view can be flown: a drivable profile on a valid frame
    bool TrafficLaneUsable(const FSOLLaneMergeLane& lane)
    {
        return lane.Profile.bDrivable && lane.Frame.bValid && lane.Profile.WallRadiusM > 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the lane frame's velocity at a foot fraction, lerp(V_A, V_B, s)
    FVector3d TrafficFrameVelocity(const FSOLLaneMergeLane& lane, const double footFraction)
    {
        return lane.AVelocityMps + (lane.BVelocityMps - lane.AVelocityMps) * footFraction;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the nominal arc u (from the departure ring of a direction) of a leg arc, through the metric map
    double TrafficNominalArc(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame, const double legArcM,
        const int32 directionSign)
    {
        const double l = frame.CurrentLegM;
        const double x = FMath::Clamp(legArcM, 0.0, l);
        return SOLLaneTraffic::MetricMapInverseM(directionSign > 0 ? x : l - x, profile.NominalLegM, l);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the mean-effort leg of a radius on a lane (the speed field's own profile)
    FSOLLaneLeg TrafficMeanLeg(const FSOLLaneProfile& profile, const double rho)
    {
        return SOLLaneTraffic::MakeLegProfile(SOLLaneTraffic::RadialFactor(rho), SOL::NPC_LANE_EFFORT_MEAN,
            profile.NominalLegM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the traffic's acceleration along its travel at a leg arc: +Alpha while the mean leg accelerates, -Alpha
    // while it brakes, 0 at its peak, times the map's slope (the map's curvature term is left out, SDD 50 section 4.12)
    double TrafficAlongAccelMps2(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame, const double legArcM,
        const double rho, const int32 directionSign)
    {
        const FSOLLaneLeg leg = TrafficMeanLeg(profile, rho);
        if (!leg.bValid || !(leg.Alpha > 0.0) || !(leg.DvMps > 0.0))
        {
            return 0.0;
        }
        const double l0 = profile.NominalLegM;
        const double u = TrafficNominalArc(profile, frame, legArcM, directionSign);
        const double alpha = u < leg.AccelM ? leg.Alpha : (u > l0 - leg.AccelM ? -leg.Alpha : 0.0);
        return alpha * SOLLaneTraffic::MetricMapSlope(u, l0, frame.CurrentLegM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the time over a distance accelerating from the wall speed, (sqrt(w^2 + 2 a s) - w) / a without
    // cancellation
    double TrafficBurnTimeS(const double alpha, const double distanceM)
    {
        const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
        return 2.0 * distanceM / (FMath::Sqrt(w * w + 2.0 * alpha * distanceM) + w);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the time a leg takes from its departure ring to nominal arc u (closed form: accelerate, coast, brake)
    double TrafficLegTimeToArcS(const FSOLLaneLeg& leg, const double l0, const double u)
    {
        const double arc = FMath::IsNaN(u) ? 0.0 : FMath::Clamp(u, 0.0, l0);
        if (!leg.bValid || !(leg.PeakMps > 0.0))
        {
            return 0.0;
        }
        if (!(leg.Alpha > 0.0) || !(leg.DvMps > 0.0))
        {
            return arc / leg.PeakMps;
        }
        const double coastM = FMath::Max(0.0, l0 - 2.0 * leg.AccelM);
        if (arc <= leg.AccelM)
        {
            return TrafficBurnTimeS(leg.Alpha, arc);
        }
        if (arc <= leg.AccelM + coastM)
        {
            return leg.AccelS + (arc - leg.AccelM) / leg.PeakMps;
        }

        // Braking: the whole leg minus the time left to the far ring (a mirrored burn)
        return 2.0 * leg.AccelS + coastM / leg.PeakMps - TrafficBurnTimeS(leg.Alpha, FMath::Max(0.0, l0 - arc));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a target's leg arc on a lane, unclamped (beyond the end rings for a body at an end)
    double TrafficTargetArcM(const FSOLLaneMergeLane& lane, const FVector3d& targetM)
    {
        return (targetM - lane.Frame.AM).Dot(lane.Frame.Axis) - lane.Profile.EndArcAM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the leave arc: the nearer (first reached in the travel direction) of the exit arc and the target's own
    // arc less the leave distance
    double TrafficLeaveArcM(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg, const FVector3d& targetM,
        const double leaveDistanceM)
    {
        const double d = TrafficDirection(leg.DirectionSign);
        const double brakeArcM = TrafficTargetArcM(lane, targetM) - d * FMath::Min(leaveDistanceM, 1.0e300);
        return d > 0.0 ? FMath::Min(leg.ExitArcM, brakeArcM) : FMath::Max(leg.ExitArcM, brakeArcM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a ship on the track at a leg arc, at the traffic speed there, is at or past its leave arc
    bool TrafficIsPastLeave(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg, const double arcM,
        const FVector3d& targetM, const double brakeAccelMps2, const double flipS)
    {
        const double speedMps = SOLLaneTraffic::TrafficSpeedMps(lane.Profile, lane.Frame, arcM,
            SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, leg.DirectionSign);
        const double leaveArcM = TrafficLeaveArcM(lane, leg, targetM, SOLLaneTraffic::LaneLeaveDistanceM(speedMps,
            brakeAccelMps2, flipS));
        return (arcM - leaveArcM) * TrafficDirection(leg.DirectionSign) >= 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the first arc from startArcM toward the exit where a ship holding the traffic speed leaves (the exit at
    // the latest): a march of TRAFFIC_LEAVE_MARCH_STEPS samples, then a bisection of the first bracket
    double TrafficFirstLeaveArcM(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg, const double startArcM,
        const FVector3d& targetM, const double brakeAccelMps2, const double flipS)
    {
        const double exitM = leg.ExitArcM;
        if ((exitM - startArcM) * TrafficDirection(leg.DirectionSign) <= 0.0
            || TrafficIsPastLeave(lane, leg, startArcM, targetM, brakeAccelMps2, flipS))
        {
            return startArcM;
        }
        double beforeM = startArcM;
        double afterM = exitM;
        for (int32 step = 1; step <= TRAFFIC_LEAVE_MARCH_STEPS; ++step)
        {
            const double arcM = startArcM + (exitM - startArcM) * step / TRAFFIC_LEAVE_MARCH_STEPS;
            if (TrafficIsPastLeave(lane, leg, arcM, targetM, brakeAccelMps2, flipS))
            {
                afterM = arcM;
                break;
            }
            beforeM = arcM;
        }
        for (int32 step = 0; step < TRAFFIC_LEAVE_BISECT_STEPS; ++step)
        {
            const double midM = 0.5 * (beforeM + afterM);
            (TrafficIsPastLeave(lane, leg, midM, targetM, brakeAccelMps2, flipS) ? afterM : beforeM) = midM;
        }
        return afterM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a view's track leg: the precomputed one, else computed now
    FSOLLaneLeg TrafficTrackLeg(const FSOLLaneMergeLane& lane)
    {
        return lane.TrackLeg.bValid ? lane.TrackLeg : TrafficMeanLeg(lane.Profile, SOL::NPC_LANE_AUTO_TRACK_WALL_RADII);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the point on the keep-right track at a leg arc for a direction
    FVector3d TrafficTrackPointM(const FSOLLaneMergeLane& lane, const double legArcM, const int32 directionSign)
    {
        return lane.Frame.AM + lane.Frame.Axis * (lane.Profile.EndArcAM + legArcM)
            + SOLLaneTraffic::LaneTrackOffsetM(lane.Profile, lane.Frame, directionSign);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the first lane the straight path runs along inside its halo for at least the minimum overlap
FSOLLaneAutoLeg SOLLaneTraffic::FindLaneLeg(const TConstArrayView<FSOLLaneMergeLane> lanes, const FVector3d& shipM,
    const FVector3d& targetM)
{
    FSOLLaneAutoLeg result;
    if (!TrafficIsFinite(shipM) || !TrafficIsFinite(targetM) || shipM == targetM)
    {
        return result;
    }
    for (int32 index = 0; index < lanes.Num(); ++index)
    {
        const FSOLLaneMergeLane& lane = lanes[index];
        if (!TrafficLaneUsable(lane))
        {
            continue;
        }
        const FSOLLaneProfile& profile = lane.Profile;
        const FSOLLaneFrame& frame = lane.Frame;
        const double l = frame.CurrentLegM;
        const double haloM = profile.HaloOuterRadiusM;

        // The path in lane coordinates: leg arc x(t) and the lateral offset r(t) on (Right, Up), t in [0, 1]
        const FVector3d relShipM = shipM - frame.AM;
        const FVector3d relTargetM = targetM - frame.AM;
        const double x0 = relShipM.Dot(frame.Axis) - profile.EndArcAM;
        const double x1 = relTargetM.Dot(frame.Axis) - profile.EndArcAM;
        const FVector2d r0(relShipM.Dot(frame.Right), relShipM.Dot(frame.Up));
        const FVector2d r1(relTargetM.Dot(frame.Right), relTargetM.Dot(frame.Up));
        const double dx = x1 - x0;
        const FVector2d dr = r1 - r0;

        // Between the end rings
        double legEnterT = 0.0;
        double legLeaveT = 1.0;
        if (dx == 0.0)
        {
            if (x0 < 0.0 || x0 > l)
            {
                continue;
            }
        }
        else
        {
            const double ringAT = -x0 / dx;
            const double ringBT = (l - x0) / dx;
            legEnterT = FMath::Min(ringAT, ringBT);
            legLeaveT = FMath::Max(ringAT, ringBT);
        }

        // Inside the halo: |r0 + t dr|^2 <= H^2, roots in the cancellation-free form
        double haloEnterT = 0.0;
        double haloLeaveT = 1.0;
        const double a = dr.SizeSquared();
        const double halfB = r0.Dot(dr);
        const double c = r0.SizeSquared() - haloM * haloM;
        if (a <= UE_DOUBLE_SMALL_NUMBER * haloM * haloM)
        {
            if (c > 0.0)
            {
                continue;
            }
        }
        else
        {
            const double disc = halfB * halfB - a * c;
            if (disc < 0.0)
            {
                continue;
            }
            const double q = -(halfB + (halfB >= 0.0 ? 1.0 : -1.0) * FMath::Sqrt(disc));
            const double rootA = q / a;
            const double rootB = q != 0.0 ? c / q : rootA;
            haloEnterT = FMath::Min(rootA, rootB);
            haloLeaveT = FMath::Max(rootA, rootB);
        }

        // The part inside both, its axis length against the minimum (D20: a crossing is left unchanged)
        const double t0 = FMath::Max3(0.0, legEnterT, haloEnterT);
        const double t1 = FMath::Min3(1.0, legLeaveT, haloLeaveT);
        const double overlapM = FMath::Abs(dx) * (t1 - t0);
        if (!(t1 > t0) || !(overlapM >= SOL::NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII * profile.WallRadiusM))
        {
            continue;
        }

        // Direction, entry (exactly the ring arc when a ring bounds it) and exit
        result.bFound = true;
        result.Lane = index;
        result.DirectionSign = dx > 0.0 ? 1 : -1;
        result.OverlapM = overlapM;
        result.bStartsInside = x0 >= 0.0 && x0 <= l && r0.SizeSquared() <= haloM * haloM;
        if (result.bStartsInside)
        {
            result.EntryArcM = x0;
        }
        else if (t0 == legEnterT)
        {
            result.EntryArcM = dx > 0.0 ? 0.0 : l;
        }
        else
        {
            result.EntryArcM = FMath::Clamp(x0 + t0 * dx, 0.0, l);
        }
        result.ExitArcM = FMath::Clamp(x1, 0.0, l);
        result.BodyA = lane.BodyA;
        result.BodyB = lane.BodyB;
        result.bTargetInside = x1 >= 0.0 && x1 <= l && r1.SizeSquared() <= haloM * haloM;
        return result;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane view of the end bodies' positions and velocities, with the track's mean leg precomputed
FSOLLaneMergeLane SOLLaneTraffic::MakeMergeLane(const FSOLLaneProfile& profile, const int32 bodyA, const int32 bodyB,
    const FVector3d& aM, const FVector3d& bM, const FVector3d& aVelocityMps, const FVector3d& bVelocityMps)
{
    FSOLLaneMergeLane lane;
    lane.Profile = profile;
    lane.Frame = MakeLaneFrame(profile, aM, bM);
    lane.AVelocityMps = aVelocityMps;
    lane.BVelocityMps = bVelocityMps;
    lane.BodyA = bodyA;
    lane.BodyB = bodyB;
    lane.TrackLeg = TrafficMeanLeg(profile, SOL::NPC_LANE_AUTO_TRACK_WALL_RADII);
    return lane;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a lane may be searched: never for War power, only with nothing latched
bool SOLLaneTraffic::IsLaneSearchAllowed(const ESOLLaneAutoMode latched, const bool bIgnoreLanes)
{
    return !bIgnoreLanes && latched == ESOLLaneAutoMode::None;
}

//////////////////////////////////////////////////////////////////////////
// Returns the keep-right track's lateral offset for a direction
FVector3d SOLLaneTraffic::LaneTrackOffsetM(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
    const int32 directionSign)
{
    if (!frame.bValid || !profile.bDrivable)
    {
        return FVector3d::ZeroVector;
    }
    return frame.Right * (TrafficDirection(directionSign) * SOL::NPC_LANE_AUTO_TRACK_WALL_RADII * profile.WallRadiusM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the merge point on the keep-right track at the entry arc with the traffic's velocity and acceleration
FSOLLaneMergeTarget SOLLaneTraffic::LaneMergeTarget(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg)
{
    FSOLLaneMergeTarget merge;
    if (!leg.bFound || !TrafficLaneUsable(lane))
    {
        return merge;
    }
    const double d = TrafficDirection(leg.DirectionSign);
    const double rho = SOL::NPC_LANE_AUTO_TRACK_WALL_RADII;
    merge.PositionM = TrafficTrackPointM(lane, leg.EntryArcM, leg.DirectionSign);
    merge.TrafficSpeedMps = TrafficSpeedMps(lane.Profile, lane.Frame, leg.EntryArcM, rho, leg.DirectionSign);
    const FSOLLanePoint point = LanePointOf(lane.Profile, lane.Frame, merge.PositionM);
    merge.VelocityMps = TrafficFrameVelocity(lane, point.FootFraction) + lane.Frame.Axis * (d * merge.TrafficSpeedMps);
    merge.AccelMps2 = lane.Frame.Axis * (d * TrafficAlongAccelMps2(lane.Profile, lane.Frame, leg.EntryArcM, rho,
        leg.DirectionSign));
    merge.bValid = point.bValid && TrafficIsFinite(merge.VelocityMps) && TrafficIsFinite(merge.AccelMps2);
    return merge;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the ship flies in the halo near the track's radius at the track's traffic speed
bool SOLLaneTraffic::IsLaneMerged(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg, const FVector3d& shipM,
    const FVector3d& shipVelocityMps)
{
    if (!leg.bFound || !TrafficLaneUsable(lane) || !TrafficIsFinite(shipVelocityMps))
    {
        return false;
    }
    const FSOLLanePoint point = LanePointOf(lane.Profile, lane.Frame, shipM);
    if (!point.bInHalo
        || FMath::Abs(point.Rho - SOL::NPC_LANE_AUTO_TRACK_WALL_RADII) > SOL::NPC_LANE_AUTO_HOLD_RADIUS_WALL_RADII)
    {
        return false;
    }
    const double trafficMps = TrafficSpeedMps(lane.Profile, lane.Frame, point.LegArcM,
        SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, leg.DirectionSign);
    const double alongMps = (shipVelocityMps - TrafficFrameVelocity(lane, point.FootFraction)).Dot(lane.Frame.Axis)
        * TrafficDirection(leg.DirectionSign);
    return trafficMps > 0.0 && FMath::Abs(alongMps - trafficMps) <= SOL::NPC_LANE_AUTO_HOLD_SPEED_FRACTION * trafficMps;
}

//////////////////////////////////////////////////////////////////////////
// Returns the AUTO lane mode: Left stays, leave at the leave arc, hold inside (or merged), else approach
ESOLLaneAutoMode SOLLaneTraffic::LaneAutoModeAt(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg,
    const FVector3d& shipM, const FVector3d& shipVelocityMps, const FVector3d& targetM,
    const FVector3d& targetVelocityMps, const double brakeAccelMps2, const double flipS,
    const ESOLLaneAutoMode previous)
{
    if (previous == ESOLLaneAutoMode::Left)
    {
        return ESOLLaneAutoMode::Left;
    }
    if (!leg.bFound || !TrafficLaneUsable(lane))
    {
        return ESOLLaneAutoMode::None;
    }
    const FSOLLanePoint point = LanePointOf(lane.Profile, lane.Frame, shipM);
    if (!point.bValid)
    {
        return ESOLLaneAutoMode::None;
    }
    const double leaveDistanceM = LaneLeaveDistanceM((shipVelocityMps - targetVelocityMps).Size(), brakeAccelMps2,
        flipS);
    if ((point.LegArcM - TrafficLeaveArcM(lane, leg, targetM, leaveDistanceM)) * TrafficDirection(leg.DirectionSign)
        >= 0.0)
    {
        return ESOLLaneAutoMode::Left;
    }
    if ((previous == ESOLLaneAutoMode::None && leg.bStartsInside)
        || (previous == ESOLLaneAutoMode::Hold && point.bInHalo) || IsLaneMerged(lane, leg, shipM, shipVelocityMps))
    {
        return ESOLLaneAutoMode::Hold;
    }
    return ESOLLaneAutoMode::Approach;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the marker shows its LANE tag
bool SOLLaneTraffic::IsLaneMarkerShown(const ESOLLaneAutoMode mode)
{
    return mode == ESOLLaneAutoMode::Hold;
}

//////////////////////////////////////////////////////////////////////////
// Returns the hold guidance on the keep-right track at the traffic speed
FSOLGuidanceOutput SOLLaneTraffic::LaneHoldGuidance(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg,
    const FVector3d& shipM, const FVector3d& shipVelocityMps, const FVector3d& targetM,
    const FVector3d& gravityAccelMps2, const double velocityGainPerS)
{
    FSOLGuidanceOutput out;
    const FSOLLanePoint point = LanePointOf(lane.Profile, lane.Frame, shipM);
    if (!leg.bFound || !TrafficLaneUsable(lane) || !point.bValid || !TrafficIsFinite(shipVelocityMps)
        || !TrafficIsFinite(gravityAccelMps2) || !TrafficIsFinite(targetM))
    {
        return out;
    }
    const FSOLLaneFrame& frame = lane.Frame;
    const FVector3d travel = frame.Axis * TrafficDirection(leg.DirectionSign);
    const double rho = SOL::NPC_LANE_AUTO_TRACK_WALL_RADII;
    const double trafficMps = TrafficSpeedMps(lane.Profile, frame, point.LegArcM, rho, leg.DirectionSign);
    const FVector3d frameVelocityMps = TrafficFrameVelocity(lane, point.FootFraction);

    // Lateral pull toward the track (perpendicular to the axis), capped
    const FVector3d lateralM = frame.Right * point.RightOffsetM + frame.Up * point.UpOffsetM;
    FVector3d pullMps = (LaneTrackOffsetM(lane.Profile, frame, leg.DirectionSign) - lateralM)
        * SOL::NPC_LANE_AUTO_LATERAL_GAIN_PER_S;
    const double pullSize = pullMps.Size();
    if (pullSize > SOL::NPC_LANE_AUTO_LATERAL_MAX_MPS)
    {
        pullMps *= SOL::NPC_LANE_AUTO_LATERAL_MAX_MPS / pullSize;
    }

    // Toward the desired velocity at the guidance gain, plus the traffic's own acceleration
    const FVector3d desiredMps = frameVelocityMps + travel * trafficMps + pullMps;
    const double gain = FMath::IsFinite(velocityGainPerS) ? FMath::Max(velocityGainPerS, 0.0) : 0.0;
    out.DesiredNetAccelMps2 = (desiredMps - shipVelocityMps) * gain
        + travel * TrafficAlongAccelMps2(lane.Profile, frame, point.LegArcM, rho, leg.DirectionSign);
    out.DesiredThrustAccelMps2 = out.DesiredNetAccelMps2 - gravityAccelMps2;

    // Nose along travel before the leg's midpoint, against it from there (D21); the phase from the mean leg
    const double l0 = lane.Profile.NominalLegM;
    const double u = TrafficNominalArc(lane.Profile, frame, point.LegArcM, leg.DirectionSign);
    out.DesiredForward = u < 0.5 * l0 ? travel : -travel;
    const FSOLLaneLeg mean = TrafficTrackLeg(lane);
    out.Phase = u < mean.AccelM ? ESOLGuidancePhase::Accelerate
        : (u > l0 - mean.AccelM ? ESOLGuidancePhase::Brake : ESOLGuidancePhase::Cruise);
    out.SpeedLimitMps = trafficMps;
    out.DistanceM = (targetM - shipM).Size();
    out.ClosingSpeedMps = (shipVelocityMps - frameVelocityMps).Dot(travel);
    return out;
}

//////////////////////////////////////////////////////////////////////////
// Returns the time between two leg arcs at the traffic speed of a radius (closed form on the nominal arcs)
double SOLLaneTraffic::LaneLegTimeS(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame, const double fromArcM,
    const double toArcM, const double rho, const int32 directionSign)
{
    if (!frame.bValid || !profile.bDrivable || FMath::IsNaN(fromArcM) || FMath::IsNaN(toArcM) || FMath::IsNaN(rho))
    {
        return 0.0;
    }
    const double l = frame.CurrentLegM;
    const double fromM = FMath::Clamp(fromArcM, 0.0, l);
    const double toM = FMath::Clamp(toArcM, 0.0, l);
    if (fromM == toM)
    {
        return 0.0;
    }

    // dt = dx / (vbar(u) X'(u)) and dx = X'(u) du, so the time is the mean leg's between the two nominal arcs
    const FSOLLaneLeg leg = TrafficMeanLeg(profile, rho);
    const double l0 = profile.NominalLegM;
    return FMath::Abs(TrafficLegTimeToArcS(leg, l0, TrafficNominalArc(profile, frame, toM, directionSign))
        - TrafficLegTimeToArcS(leg, l0, TrafficNominalArc(profile, frame, fromM, directionSign)));
}

//////////////////////////////////////////////////////////////////////////
// Returns the AUTO ETA with the lane: approach, the lane part on the track, leave
double SOLLaneTraffic::LaneAutoEtaS(const FSOLLaneMergeLane& lane, const FSOLLaneAutoLeg& leg, const FVector3d& shipM,
    const FVector3d& targetM, const double accelMps2, const double flipS)
{
    if (!leg.bFound || !TrafficLaneUsable(lane) || !TrafficIsFinite(targetM))
    {
        return 0.0;
    }
    const FSOLLanePoint point = LanePointOf(lane.Profile, lane.Frame, shipM);
    if (!point.bValid)
    {
        return 0.0;
    }
    const double d = TrafficDirection(leg.DirectionSign);
    const double rho = SOL::NPC_LANE_AUTO_TRACK_WALL_RADII;

    // The approach (none inside the halo), then the lane part from where the ship joins to the exit
    double approachS = 0.0;
    double startArcM = FMath::Clamp(point.LegArcM, 0.0, lane.Frame.CurrentLegM);
    if (!point.bInHalo)
    {
        const FSOLLaneMergeTarget merge = LaneMergeTarget(lane, leg);
        approachS = SOLNewtonian::EstimateTimeToArrivalS((merge.PositionM - shipM).Size(), 0.0, accelMps2, accelMps2,
            flipS, SOL::MAX_SPEED_CAP_MPS);
        startArcM = leg.EntryArcM;
    }

    // The leave arc: the first arc from where the ship joins at which the remaining distance falls within the leave
    // distance of the traffic speed there (as LaneAutoModeAt decides it in flight): a march, then a bisection
    const double leaveArcM = TrafficFirstLeaveArcM(lane, leg, startArcM, targetM, accelMps2, flipS);
    const double laneS = LaneLegTimeS(lane.Profile, lane.Frame, startArcM, leaveArcM, rho, leg.DirectionSign);

    // The leave: from the track at the leave arc to the target, closing at the traffic speed there (it brakes)
    const FVector3d exitM = TrafficTrackPointM(lane, leaveArcM, leg.DirectionSign);
    const FVector3d toTargetM = targetM - exitM;
    const double leaveM = toTargetM.Size();
    const double closingMps = leaveM > 0.0 ? FMath::Max(0.0, lane.Frame.Axis.Dot(toTargetM / leaveM) * d)
        * TrafficSpeedMps(lane.Profile, lane.Frame, leaveArcM, rho, leg.DirectionSign) : 0.0;
    const double leaveS = SOLNewtonian::EstimateTimeToArrivalS(leaveM, closingMps, accelMps2, accelMps2, flipS,
        SOL::MAX_SPEED_CAP_MPS);
    const double totalS = approachS + laneS + leaveS;
    return FMath::IsFinite(totalS) && totalS < TNumericLimits<double>::Max() ? totalS : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the approach's step target: the merge point when valid, else the order's own target
FSOLLaneStepTarget SOLLaneTraffic::LaneApproachStepTarget(const FSOLLaneMergeTarget& merge,
    const FVector3d& orderTargetM, const FVector3d& orderTargetVelocityMps, const FVector3d& orderTargetAccelMps2)
{
    FSOLLaneStepTarget target;
    target.PositionM = merge.bValid ? merge.PositionM : orderTargetM;
    target.VelocityMps = merge.bValid ? merge.VelocityMps : orderTargetVelocityMps;
    target.AccelMps2 = merge.bValid ? merge.AccelMps2 : orderTargetAccelMps2;
    return target;
}

//////////////////////////////////////////////////////////////////////////
// Returns the leave distance: the braking distance plus its margin, DBL_MAX for a bad brake or input
double SOLLaneTraffic::LaneLeaveDistanceM(const double relativeSpeedMps, const double brakeAccelMps2,
    const double flipS)
{
    if (!FMath::IsFinite(relativeSpeedMps) || !FMath::IsFinite(brakeAccelMps2) || !(brakeAccelMps2 > 0.0)
        || !FMath::IsFinite(flipS))
    {
        return TNumericLimits<double>::Max();
    }
    const double stopM = SOLNewtonian::StoppingDistanceM(FMath::Abs(relativeSpeedMps), brakeAccelMps2,
        FMath::Max(flipS, 0.0));
    return stopM * (1.0 + SOL::NPC_LANE_AUTO_LEAVE_MARGIN_FRACTION) + SOL::NPC_LANE_AUTO_LEAVE_MARGIN_M;
}

//////////////////////////////////////////////////////////////////////////
// Returns the re-check of a latched leg: its lane still the same (else Stale), its target and approach still on it
ESOLLaneLatchCheck SOLLaneTraffic::CheckLaneLatch(const TConstArrayView<FSOLLaneMergeLane> lanes,
    const FSOLLaneAutoLeg& leg, const ESOLLaneAutoMode mode, const FVector3d& shipM, const FVector3d& targetM)
{
    if (mode != ESOLLaneAutoMode::Approach && mode != ESOLLaneAutoMode::Hold)
    {
        return ESOLLaneLatchCheck::Keep;
    }
    if (!lanes.IsValidIndex(leg.Lane))
    {
        return ESOLLaneLatchCheck::Stale;
    }
    const FSOLLaneMergeLane& lane = lanes[leg.Lane];
    if (lane.BodyA != leg.BodyA || lane.BodyB != leg.BodyB || !TrafficLaneUsable(lane))
    {
        return ESOLLaneLatchCheck::Stale;
    }
    if (leg.bTargetInside && !LanePointOf(lane.Profile, lane.Frame, targetM).bInHalo)
    {
        return ESOLLaneLatchCheck::Release;
    }
    if (mode == ESOLLaneAutoMode::Approach && !FindLaneLeg(TConstArrayView<FSOLLaneMergeLane>(&lane, 1), shipM,
        targetM).bFound)
    {
        return ESOLLaneLatchCheck::Release;
    }
    return ESOLLaneLatchCheck::Keep;
}

//////////////////////////////////////////////////////////////////////////
// Returns one substep's lane decision: re-check, search, mode and step target
FSOLLaneAutoDecision SOLLaneTraffic::LaneAutoDecide(const TConstArrayView<FSOLLaneMergeLane> lanes,
    const FSOLLaneAutoLeg& latchedLeg, const ESOLLaneAutoMode latchedMode, const FSOLLaneAutoInput& input)
{
    FSOLLaneAutoDecision decision;
    decision.StepTarget.PositionM = input.TargetM;
    decision.StepTarget.VelocityMps = input.TargetVelocityMps;
    decision.StepTarget.AccelMps2 = input.TargetAccelMps2;
    if (input.bIgnoreLanes)
    {
        return decision;
    }

    // The latch re-check: a stale leg is dropped (searched again below), a released one leaves the lane
    FSOLLaneAutoLeg leg = latchedLeg;
    ESOLLaneAutoMode mode = latchedMode;
    const ESOLLaneLatchCheck check = CheckLaneLatch(lanes, leg, mode, input.ShipM, input.TargetM);
    if (check == ESOLLaneLatchCheck::Stale)
    {
        leg = FSOLLaneAutoLeg();
        mode = ESOLLaneAutoMode::None;
    }
    else if (check == ESOLLaneLatchCheck::Release)
    {
        mode = ESOLLaneAutoMode::Left;
    }

    // Nothing latched: a search when allowed (the first substep of a frame), else nothing
    if (mode == ESOLLaneAutoMode::None)
    {
        leg = input.bSearch && IsLaneSearchAllowed(mode, false) ? FindLaneLeg(lanes, input.ShipM, input.TargetM)
            : FSOLLaneAutoLeg();
    }

    // The exit follows the target on the current leg (the lane's length drifts with its bodies), then the mode
    const FSOLLaneMergeLane* const lane = leg.bFound && lanes.IsValidIndex(leg.Lane) ? &lanes[leg.Lane] : nullptr;
    if (lane != nullptr && lane->Frame.bValid && mode != ESOLLaneAutoMode::Left)
    {
        const double legM = lane->Frame.CurrentLegM;
        leg.ExitArcM = FMath::Clamp(TrafficTargetArcM(*lane, input.TargetM), 0.0, legM);
        leg.EntryArcM = FMath::Clamp(leg.EntryArcM, 0.0, legM);
    }
    decision.Mode = mode == ESOLLaneAutoMode::Left ? ESOLLaneAutoMode::Left : lane == nullptr
        ? ESOLLaneAutoMode::None : LaneAutoModeAt(*lane, leg, input.ShipM, input.ShipVelocityMps, input.TargetM,
        input.TargetVelocityMps, input.BrakeAccelMps2, input.FlipS, mode);
    decision.Leg = leg;
    if (decision.Mode == ESOLLaneAutoMode::Approach && lane != nullptr)
    {
        decision.StepTarget = LaneApproachStepTarget(LaneMergeTarget(*lane, leg), input.TargetM,
            input.TargetVelocityMps, input.TargetAccelMps2);
    }
    return decision;
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD ETA: the lane ETA in a lane step when usable, else the guidance's
double SOLLaneTraffic::LaneOrderEtaS(const ESOLLaneAutoMode mode, const double laneEtaS, const double guidanceEtaS)
{
    const bool bLaneStep = mode == ESOLLaneAutoMode::Approach || mode == ESOLLaneAutoMode::Hold;
    return bLaneStep && FMath::IsFinite(laneEtaS) && laneEtaS > 0.0 ? laneEtaS : guidanceEtaS;
}

//////////////////////////////////////////////////////////////////////////
// Returns the closing speed toward a target
double SOLLaneTraffic::LaneClosingSpeedMps(const FVector3d& shipM, const FVector3d& shipVelocityMps,
    const FVector3d& targetM, const FVector3d& targetVelocityMps)
{
    const FVector3d toTargetM = targetM - shipM;
    const double distanceM = toTargetM.Size();
    if (!TrafficIsFinite(toTargetM) || !TrafficIsFinite(shipVelocityMps) || !TrafficIsFinite(targetVelocityMps)
        || !(distanceM > 0.0) || !FMath::IsFinite(distanceM))
    {
        return 0.0;
    }
    return (shipVelocityMps - targetVelocityMps).Dot(toTargetM / distanceM);
}

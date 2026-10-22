/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcAiMath.h"

namespace
{
    // The ship classes: a human-piloted fighter (forward guns), a human-piloted gunship (turrets) and an AI drone
    // interceptor (forward guns). Thrust and angular limits per SDD 18 section 3.7; the gun cones are measured
    // around the nose (SDD 18 Amendment 5); the rest are placeholder numbers until the Part 8 art pass
    const SOLNpcAi::FClassParams NPC_CLASSES[] =
    {
        // Speed, pilot, thrust fwd / rev / lat / vert (G), G cap, angular accel, angular rate, leash, hit radius,
        // health, rate, muzzle, damage, lifetime, engage, fire range, cone, standoff, separation, spread, respawn,
        // gun gimbal
        { 250.0, ESOLPilotKind::Human, 8.0, 4.0, 4.0, 4.0, 6.0, FVector3d(6.0, 4.0, 4.0), FVector3d(3.0, 1.5, 1.5),
            3000.0, 15.0, 60.0, 3.0, 1500.0, 10.0, 2.5, 4000.0, 3000.0, 0.15, 600.0, 60.0, 0.004, 8.0, 0.015 },
        { 150.0, ESOLPilotKind::Human, 4.0, 2.0, 2.0, 2.0, 4.0, FVector3d(2.0, 1.5, 1.5), FVector3d(1.2, 0.8, 0.8),
            3000.0, 25.0, 150.0, 2.0, 1200.0, 20.0, 3.0, 4000.0, 3000.0, 3.1, 1200.0, 100.0, 0.006, 12.0,
            UE_DOUBLE_PI },
        { 350.0, ESOLPilotKind::AI, 15.0, 8.0, 8.0, 8.0, 20.0, FVector3d(8.0, 6.0, 6.0), FVector3d(4.0, 2.5, 2.5),
            3000.0, 10.0, 40.0, 4.0, 1800.0, 6.0, 2.0, 4000.0, 2500.0, 0.1, 400.0, 50.0, 0.003, 6.0, 0.015 },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns value when finite and non-negative, else 0
    double NonNegativeOrZero(const double value)
    {
        return FMath::IsFinite(value) && value > 0.0 ? value : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether every component of a vector is finite
    bool IsFiniteVector(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the vector, or zero when it has a non-finite component
    FVector3d FiniteOrZero(const FVector3d& v)
    {
        return IsFiniteVector(v) ? v : FVector3d::ZeroVector;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns v shortened to maxLength when longer (direction kept)
    FVector3d ClampLength(const FVector3d& v, const double maxLength)
    {
        const double length = v.Size();
        return length > maxLength && length > 0.0 ? v * (maxLength / length) : v;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a unit vector perpendicular to the unit vector u (deterministic)
    FVector3d AnyPerpendicular(const FVector3d& u)
    {
        const FVector3d helper = FMath::Abs(u.X) < 0.9 ? FVector3d::XAxisVector : FVector3d::YAxisVector;
        return FVector3d::CrossProduct(u, helper).GetSafeNormal();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of ship classes (at least 2)
int32 SOLNpcAi::ClassCount()
{
    return UE_ARRAY_COUNT(NPC_CLASSES);
}

//////////////////////////////////////////////////////////////////////////
// Returns the class at classIndex clamped to the valid range
const SOLNpcAi::FClassParams& SOLNpcAi::GetClass(const int32 classIndex)
{
    return NPC_CLASSES[FMath::Clamp(classIndex, 0, ClassCount() - 1)];
}

//////////////////////////////////////////////////////////////////////////
// Returns the class picked by a unit value in [0, 1)
int32 SOLNpcAi::PickClass(const double unit)
{
    const double u = FMath::IsNaN(unit) ? 0.0 : FMath::Clamp(unit, 0.0, 1.0);
    return FMath::Clamp(static_cast<int32>(FMath::FloorToDouble(u * ClassCount())), 0, ClassCount() - 1);
}

//////////////////////////////////////////////////////////////////////////
// Returns a class's thrust envelope: the G box, its angular limits and the total cap of its pilot kind
FSOLThrustEnvelope SOLNpcAi::ClassEnvelope(const FClassParams& shipClass)
{
    const double g0 = SOLNewtonian::STANDARD_GRAVITY_MPS2;
    FSOLThrustEnvelope env;
    env.ForwardAccelMps2 = shipClass.ForwardG * g0;
    env.ReverseAccelMps2 = shipClass.ReverseG * g0;
    env.LateralAccelMps2 = shipClass.LateralG * g0;
    env.VerticalAccelMps2 = shipClass.VerticalG * g0;
    env.BoostMultiplier = 1.0;
    env.MaxTotalAccelMps2 = SOLNewtonian::EffectiveGCapG(shipClass.PilotKind, shipClass.ClassGCapG) * g0;
    env.MaxAngularAccelRadPerS2 = shipClass.MaxAngularAccelRadPerS2;
    env.MaxAngularRateRadPerS = shipClass.MaxAngularRateRadPerS;
    env.MaxSpinRateRadPerS = CLASS_SPIN_RATE_SCALE * shipClass.MaxAngularRateRadPerS.GetMax();
    env.MaxSpeedMps = SOL::MAX_SPEED_CAP_MPS;
    return env;
}

//////////////////////////////////////////////////////////////////////////
// Returns the Mid tier's point-mass acceleration budget: the lateral thrust, at most the class cap
double SOLNpcAi::MidReturnAccelMps2(const FClassParams& shipClass)
{
    if (!FMath::IsFinite(shipClass.LateralG) || shipClass.LateralG <= 0.0)
    {
        return 0.0;
    }
    return FMath::Min(shipClass.LateralG, SOLNewtonian::EffectiveGCapG(shipClass.PilotKind, shipClass.ClassGCapG))
        * SOLNewtonian::STANDARD_GRAVITY_MPS2;
}

//////////////////////////////////////////////////////////////////////////
// Returns the guidance parameters of a Mid ship's return to its rail
FSOLGuidanceParams SOLNpcAi::MidReturnGuidanceParams(const FClassParams& shipClass)
{
    FSOLGuidanceParams params;
    params.PlanAccelMps2 = SOLNewtonian::GUIDANCE_ACCEL_MARGIN * MidReturnAccelMps2(shipClass);
    params.BrakeAccelMps2 = params.PlanAccelMps2;
    params.FlipTimeS = 0.0;
    params.MaxCruiseSpeedMps = shipClass.MaxSpeedMps;
    params.ArrivalRadiusM = MID_ARRIVAL_RADIUS_M;
    params.ArrivalSpeedMps = MID_ARRIVAL_SPEED_MPS;
    return params;
}

//////////////////////////////////////////////////////////////////////////
// Returns a Mid ship's deviation after one point-mass step back toward its rail (at rest once arrived)
SOLNpcAi::FMidReturnStep SOLNpcAi::StepMidReturn(const FVector3d& deviationM, const FVector3d& deviationVelocityMps,
    const FSOLGuidanceParams& params, const double dt)
{
    FMidReturnStep step;
    if (!IsFiniteVector(deviationM) || !IsFiniteVector(deviationVelocityMps)
        || (deviationM.IsZero() && deviationVelocityMps.IsZero()))
    {
        step.bAtRest = true;
        return step;
    }
    step.DeviationM = deviationM;
    step.DeviationVelocityMps = deviationVelocityMps;
    if (!FMath::IsFinite(dt) || dt <= 0.0)
    {
        return step;
    }

    // The rail is the rendezvous target: no target acceleration, no gravity (the rail absorbs it)
    const FSOLGuidanceOutput guidance = SOLNewtonian::ComputeRendezvousGuidance(-deviationM, deviationVelocityMps,
        FVector3d::ZeroVector, FVector3d::ZeroVector, params, dt, false);
    if (guidance.Phase == ESOLGuidancePhase::Arrived)
    {
        step.DeviationM = FVector3d::ZeroVector;
        step.DeviationVelocityMps = FVector3d::ZeroVector;
        step.bAtRest = true;
        return step;
    }
    step.DeviationVelocityMps = deviationVelocityMps + guidance.DesiredNetAccelMps2 * dt;
    step.DeviationM = deviationM + step.DeviationVelocityMps * dt;
    return step;
}

//////////////////////////////////////////////////////////////////////////
// Returns the unit hull heading for a velocity (a direction perpendicular to the up hint when nearly at rest)
FVector3d SOLNpcAi::HullForward(const FVector3d& velocityMps, const FVector3d& upHint)
{
    if (IsFiniteVector(velocityMps) && velocityMps.Size() > HULL_HEADING_MIN_SPEED_MPS)
    {
        return velocityMps.GetSafeNormal();
    }
    const double upLength = IsFiniteVector(upHint) ? upHint.Size() : 0.0;
    if (upLength >= STEER_EPSILON)
    {
        // Divided by hand: GetSafeNormal treats lengths below 1e-4 as zero
        return AnyPerpendicular(upHint / upLength);
    }
    return FVector3d::XAxisVector;
}

//////////////////////////////////////////////////////////////////////////
// Returns the orientation a ship is promoted to the Full tier with (its forward is HullForward)
FQuat4d SOLNpcAi::HandoffOrientation(const FVector3d& velocityMps, const FVector3d& upHint)
{
    return SOLNewtonian::OrientationFromForward(HullForward(velocityMps, upHint), upHint, FQuat4d::Identity);
}

//////////////////////////////////////////////////////////////////////////
// Returns where a Full ship points its nose: the aim near its target, else its desired or current velocity
FVector3d SOLNpcAi::FullShipDesiredForward(const FVector3d& aimDirection, const double targetDistanceM,
    const double fireRangeM, const FVector3d& desiredVelocityMps, const FVector3d& velocityMps,
    const FVector3d& currentForward)
{
    if (IsFiniteVector(aimDirection) && aimDirection.Size() >= STEER_EPSILON && FMath::IsFinite(targetDistanceM)
        && targetDistanceM > 0.0 && FMath::IsFinite(fireRangeM) && fireRangeM > 0.0
        && targetDistanceM <= NOSE_ON_TARGET_RANGE_SCALE * fireRangeM)
    {
        return aimDirection.GetSafeNormal();
    }
    for (const FVector3d& velocity : { desiredVelocityMps, velocityMps })
    {
        if (IsFiniteVector(velocity) && velocity.Size() > HULL_HEADING_MIN_SPEED_MPS)
        {
            return velocity.GetSafeNormal();
        }
    }
    return IsFiniteVector(currentForward) && !currentForward.IsZero() ? currentForward.GetSafeNormal()
        : FVector3d::XAxisVector;
}

//////////////////////////////////////////////////////////////////////////
// Returns a Full ship's state after one 6-DOF step in its rail frame: velocity hold, thrust allocation, attitude
FSOLShipState SOLNpcAi::StepFullShip(const FSOLShipState& state, const FVector3d& desiredVelocityMps,
    const FVector3d& desiredForward, const FVector3d& desiredUp, const FSOLThrustEnvelope& env, const double dt)
{
    if (!FMath::IsFinite(dt) || dt <= 0.0)
    {
        return state;
    }
    const FVector3d holdAccelMps2 = SOLNewtonian::VelocityHoldAccelMps2(state.VelocityMps, desiredVelocityMps,
        FVector3d::ZeroVector, FULL_HOLD_TIME_CONSTANT_S, dt);
    FSOLNewtonianCommand command;
    command.ThrustFraction = SOLNewtonian::AllocateThrust(holdAccelMps2, state.Orientation, env, false).ThrustFraction;
    command.AngularAccelFraction = SOLNewtonian::RateCommandAngularFraction(state.AngularVelocityRadPerS,
        SOLNewtonian::AttitudeRateCommand(state.Orientation, desiredForward, desiredUp, env), env, dt);
    command.bBoost = false;
    return SOLNewtonian::StepNewtonian(state, command, env, FVector3d::ZeroVector, dt);
}

//////////////////////////////////////////////////////////////////////////
// Returns the direction a shot leaves in: the aim, swivelled at most gimbalRad off the nose
FVector3d SOLNpcAi::GunDirection(const FVector3d& noseDirection, const FVector3d& aimDirection, const double gimbalRad)
{
    if (!IsFiniteVector(aimDirection) || aimDirection.Size() < STEER_EPSILON)
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d aim = aimDirection.GetSafeNormal();
    if (!IsFiniteVector(noseDirection) || noseDirection.Size() < STEER_EPSILON)
    {
        return aim;
    }
    const FVector3d nose = noseDirection.GetSafeNormal();
    const double gimbal = FMath::IsNaN(gimbalRad) || gimbalRad < 0.0 ? 0.0 : gimbalRad;   // +inf: any direction
    const double dot = FVector3d::DotProduct(nose, aim);
    if (FMath::Atan2(FVector3d::CrossProduct(nose, aim).Size(), dot) <= gimbal)
    {
        return aim;
    }

    // The gimbal's edge in the nose-aim plane, toward the aim
    const FVector3d towardAim = aim - nose * dot;
    const FVector3d perpendicular = towardAim.Size() >= STEER_EPSILON ? towardAim.GetSafeNormal()
        : AnyPerpendicular(nose);
    return nose * FMath::Cos(gimbal) + perpendicular * FMath::Sin(gimbal);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a ship may drop to the Far tier (its deviation snaps to the rail): sub-pixel or budget-forced
bool SOLNpcAi::AllowFarDemotion(const double deviationLengthM, const double distanceM, const double pixelAngleRad,
    const bool bBudgetForced)
{
    return bBudgetForced || SOLNewtonian::IsHandoffSnapInvisible(deviationLengthM, distanceM, pixelAngleRad);
}

//////////////////////////////////////////////////////////////////////////
// Returns the desired velocity to close to the standoff distance of a led target, strafing at the standoff
FVector3d SOLNpcAi::PursuitVelocity(const FVector3d& selfPositionM, const FVector3d& targetPositionM,
    const FVector3d& targetVelocityMps, const double maxSpeedMps, const double standoffM, const double leadTimeS,
    const FVector3d& strafeAxis)
{
    const double lead = NonNegativeOrZero(leadTimeS);
    const FVector3d toTarget = FiniteOrZero(targetPositionM) + FiniteOrZero(targetVelocityMps) * lead
        - FiniteOrZero(selfPositionM);
    const double distanceM = toTarget.Size();
    if (distanceM < STEER_EPSILON || !FMath::IsFinite(maxSpeedMps) || maxSpeedMps <= 0.0)
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d direction = toTarget / distanceM;
    const double closing = FMath::IsFinite(standoffM) && standoffM > 0.0
        ? FMath::Clamp((distanceM - standoffM) / standoffM, -1.0, 1.0) : 1.0;
    const FVector3d cross = FVector3d::CrossProduct(direction, FiniteOrZero(strafeAxis));
    const FVector3d strafe = cross.Size() >= STEER_EPSILON ? cross.GetSafeNormal() : FVector3d::ZeroVector;
    return (direction * closing + strafe * (1.0 - FMath::Abs(closing))) * maxSpeedMps;
}

//////////////////////////////////////////////////////////////////////////
// Returns the velocity pushing a ship away from the neighbours inside the avoidance radius
FVector3d SOLNpcAi::SeparationVelocity(const FVector3d& selfPositionM,
    const TConstArrayView<FVector3d> neighbourPositionsM,
    const double radiusM, const double maxSpeedMps)
{
    if (!FMath::IsFinite(radiusM) || radiusM <= 0.0 || !FMath::IsFinite(maxSpeedMps) || maxSpeedMps <= 0.0)
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d self = FiniteOrZero(selfPositionM);
    FVector3d push = FVector3d::ZeroVector;
    for (const FVector3d& neighbour : neighbourPositionsM)
    {
        if (!IsFiniteVector(neighbour))
        {
            continue;
        }
        const FVector3d away = self - neighbour;
        const double distanceM = away.Size();
        if (distanceM >= STEER_EPSILON && distanceM < radiusM)
        {
            push += away / distanceM * (1.0 - distanceM / radiusM);
        }
    }
    return ClampLength(push * maxSpeedMps, maxSpeedMps);
}

//////////////////////////////////////////////////////////////////////////
// Returns the desired velocity blended toward a pull back to the rail as the deviation nears the leash
FVector3d SOLNpcAi::LeashVelocity(const FVector3d& desiredVelocityMps, const FVector3d& deviationM,
    const double leashM, const double maxSpeedMps)
{
    if (!IsFiniteVector(deviationM))
    {
        return desiredVelocityMps;
    }
    const double length = deviationM.Size();
    if (length < STEER_EPSILON)
    {
        return desiredVelocityMps;
    }
    const double weight = FMath::IsFinite(leashM) && leashM > 0.0
        ? FMath::Clamp((length - LEASH_SOFT_FRACTION * leashM) / ((1.0 - LEASH_SOFT_FRACTION) * leashM), 0.0, 1.0)
        : 1.0;
    return desiredVelocityMps * (1.0 - weight) - deviationM / length * NonNegativeOrZero(maxSpeedMps) * weight;
}

//////////////////////////////////////////////////////////////////////////
// Returns the target index to engage (the sticky current target, else the nearest enemy in range, else none)
int32 SOLNpcAi::SelectTarget(const int32 selfIndex, const FVector3d& selfPositionM, const int32 selfFaction,
    const int32 currentTarget, const TConstArrayView<FTargetCandidate> candidates, const double engageRangeM,
    const double keepRangeScale)
{
    if (!FMath::IsFinite(engageRangeM) || engageRangeM <= 0.0)
    {
        return INDEX_NONE;
    }
    const double keep = FMath::IsFinite(keepRangeScale) && keepRangeScale >= 1.0 ? keepRangeScale : 1.0;
    const double keepRangeSq = FMath::Square(engageRangeM * keep);
    const double engageRangeSq = FMath::Square(engageRangeM);
    int32 nearest = INDEX_NONE;
    double nearestSq = engageRangeSq;
    for (const FTargetCandidate& candidate : candidates)
    {
        if (candidate.Index == selfIndex || candidate.Index == INDEX_NONE || candidate.Faction == selfFaction
            || !candidate.bAlive || !IsFiniteVector(candidate.PositionM))
        {
            continue;
        }
        const double distanceSq = (candidate.PositionM - selfPositionM).SizeSquared();
        if (currentTarget != INDEX_NONE && candidate.Index == currentTarget && distanceSq < keepRangeSq)
        {
            return currentTarget;
        }
        if (distanceSq < nearestSq || (distanceSq == nearestSq && nearest != INDEX_NONE && candidate.Index < nearest))
        {
            nearest = candidate.Index;
            nearestSq = distanceSq;
        }
    }
    return nearest;
}

//////////////////////////////////////////////////////////////////////////
// Returns the unit aim direction that makes a bolt meet a target moving at constant velocity
FVector3d SOLNpcAi::LeadAimDirection(const FVector3d& shooterPositionM, const FVector3d& shooterVelocityMps,
    const FVector3d& targetPositionM, const FVector3d& targetVelocityMps, const double muzzleSpeedMps)
{
    const FVector3d relativeM = FiniteOrZero(targetPositionM) - FiniteOrZero(shooterPositionM);
    if (relativeM.Size() < STEER_EPSILON)
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d direct = relativeM.GetSafeNormal();
    if (!FMath::IsFinite(muzzleSpeedMps) || muzzleSpeedMps <= 0.0)
    {
        return direct;
    }

    // |r + w t| = s t  ->  (w.w - s^2) t^2 + 2 (r.w) t + r.r = 0; the smallest positive root
    const FVector3d relativeVelocity = FiniteOrZero(targetVelocityMps) - FiniteOrZero(shooterVelocityMps);
    const double a = relativeVelocity.SizeSquared() - FMath::Square(muzzleSpeedMps);
    const double b = 2.0 * FVector3d::DotProduct(relativeM, relativeVelocity);
    const double c = relativeM.SizeSquared();
    double timeS = -1.0;
    if (FMath::Abs(a) < 1e-12)
    {
        timeS = b < 0.0 ? -c / b : -1.0;
    }
    else
    {
        const double discriminant = b * b - 4.0 * a * c;
        if (discriminant >= 0.0)
        {
            const double root = FMath::Sqrt(discriminant);
            const double t1 = (-b - root) / (2.0 * a);
            const double t2 = (-b + root) / (2.0 * a);
            const double smaller = FMath::Min(t1, t2);
            const double larger = FMath::Max(t1, t2);
            timeS = smaller > 0.0 ? smaller : larger;
        }
    }
    if (!(timeS > 0.0) || !FMath::IsFinite(timeS))
    {
        return direct;
    }
    const FVector3d aim = relativeM + relativeVelocity * timeS;
    return aim.Size() >= STEER_EPSILON ? aim.GetSafeNormal() : direct;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the gun may fire: target within range and the aim inside the cone around the heading
bool SOLNpcAi::ShouldFire(const FVector3d& headingDirection, const FVector3d& aimDirection, const double distanceM,
    const double fireRangeM, const double fireConeRad)
{
    if (!FMath::IsFinite(distanceM) || distanceM <= 0.0 || !FMath::IsFinite(fireRangeM) || distanceM > fireRangeM
        || !IsFiniteVector(aimDirection) || aimDirection.Size() < STEER_EPSILON)
    {
        return false;
    }
    const FVector3d heading = FiniteOrZero(headingDirection);
    if (heading.Size() < STEER_EPSILON)
    {
        return true;
    }
    const FVector3d headingDir = heading.GetSafeNormal();
    const FVector3d aimDir = aimDirection.GetSafeNormal();
    const double angle = FMath::Atan2(FVector3d::CrossProduct(headingDir, aimDir).Size(),
        FVector3d::DotProduct(headingDir, aimDir));
    return angle <= NonNegativeOrZero(fireConeRad) + 1e-9;
}

//////////////////////////////////////////////////////////////////////////
// Returns a fresh, alive state with full health
SOLNpcAi::FCombatState SOLNpcAi::MakeAlive(const double maxHealth)
{
    FCombatState state;
    state.Health = FMath::IsFinite(maxHealth) && maxHealth > 0.0 ? maxHealth : 1.0;
    state.RespawnS = 0.0;
    state.bAlive = true;
    return state;
}

//////////////////////////////////////////////////////////////////////////
// Returns the state after damage; a lethal hit starts the respawn timer
SOLNpcAi::FDamageResult SOLNpcAi::ApplyDamage(const FCombatState& state, const double damage,
    const double respawnDelayS)
{
    FDamageResult result;
    result.State = state;
    if (!state.bAlive || !FMath::IsFinite(damage) || damage <= 0.0)
    {
        return result;
    }
    result.State.Health -= damage;
    if (result.State.Health <= 0.0)
    {
        result.State.Health = 0.0;
        result.State.bAlive = false;
        result.State.RespawnS = NonNegativeOrZero(respawnDelayS);
        result.bKilled = true;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the state after dt seconds of the respawn timer; it respawns with full health once the timer runs out
SOLNpcAi::FRespawnStep SOLNpcAi::StepRespawn(const FCombatState& state, const double dt, const double maxHealth)
{
    FRespawnStep step;
    step.State = state;
    if (state.bAlive || !FMath::IsFinite(dt) || dt < 0.0)
    {
        return step;
    }
    step.State.RespawnS -= dt;
    if (!(step.State.RespawnS > 0.0))
    {
        step.State = MakeAlive(maxHealth);
        step.bRespawned = true;
    }
    return step;
}

//////////////////////////////////////////////////////////////////////////
// Returns the speed component perpendicular to the line of sight
double SOLNpcAi::TransverseSpeedMps(const FVector3d& relativePositionM, const FVector3d& relativeVelocityMps)
{
    if (!IsFiniteVector(relativePositionM) || !IsFiniteVector(relativeVelocityMps))
    {
        return 0.0;
    }
    const double distanceM = relativePositionM.Size();
    if (distanceM < STEER_EPSILON)
    {
        return relativeVelocityMps.Size();
    }
    const FVector3d lineOfSight = relativePositionM / distanceM;
    return (relativeVelocityMps - lineOfSight * FVector3d::DotProduct(relativeVelocityMps, lineOfSight)).Size();
}

//////////////////////////////////////////////////////////////////////////
// Returns the probability that one shot hits under the statistical model
double SOLNpcAi::StatisticalHitProbability(const double distanceM, const double transverseSpeedMps,
    const double hitRadiusM, const double aimSpreadRad, const double muzzleSpeedMps, const double fireRangeM)
{
    if (!FMath::IsFinite(distanceM) || !FMath::IsFinite(transverseSpeedMps) || !FMath::IsFinite(hitRadiusM)
        || !FMath::IsFinite(aimSpreadRad) || !FMath::IsFinite(muzzleSpeedMps) || !FMath::IsFinite(fireRangeM)
        || distanceM < 0.0 || distanceM > fireRangeM || hitRadiusM <= 0.0 || muzzleSpeedMps <= 0.0)
    {
        return 0.0;
    }
    const double flightS = distanceM / muzzleSpeedMps;
    const double missSq = FMath::Square(FMath::Max(aimSpreadRad, 0.0) * distanceM)
        + FMath::Square(TRANSVERSE_ERROR_FRACTION * FMath::Abs(transverseSpeedMps) * flightS);
    const double radiusSq = FMath::Square(hitRadiusM);
    return radiusSq / (radiusSq + missSq);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a shot with this probability hits, given a unit random value
bool SOLNpcAi::RollHit(const double unit, const double probability)
{
    const double p = FMath::IsNaN(probability) ? 0.0 : FMath::Clamp(probability, 0.0, 1.0);
    return !FMath::IsNaN(unit) && unit < p;
}

//////////////////////////////////////////////////////////////////////////
// Returns the combat model for an integer (console variable) value
SOLNpcAi::ECombatModel SOLNpcAi::CombatModelFromInt(const int32 value)
{
    switch (value)
    {
    case 1:
        return ECombatModel::Statistical;
    case 2:
        return ECombatModel::Hybrid;
    default:
        return ECombatModel::Bolts;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a shooter at this distance from the camera fires real (swept) bolts under the model
bool SOLNpcAi::UsesRealBolts(const ECombatModel model, const double shooterDistanceToCameraM,
    const double visualRadiusM)
{
    switch (model)
    {
    case ECombatModel::Bolts:
        return true;
    case ECombatModel::Hybrid:
        return FMath::IsFinite(shooterDistanceToCameraM) && FMath::IsFinite(visualRadiusM) && visualRadiusM > 0.0
            && FMath::Max(shooterDistanceToCameraM, 0.0) < visualRadiusM;
    case ECombatModel::Statistical:
    default:
        return false;
    }
}

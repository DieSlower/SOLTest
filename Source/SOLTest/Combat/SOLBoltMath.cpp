/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLBoltMath.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns whether every component of the vector is finite
    bool IsFiniteVector(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unit vector, or zero when the vector is non-finite or too short to normalize
    FVector3d SafeNormal(const FVector3d& v)
    {
        if (!IsFiniteVector(v) || v.SizeSquared() < 1.0e-24)
        {
            return FVector3d::ZeroVector;
        }
        return v.GetSafeNormal();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship velocity plus the aimed muzzle velocity
FVector3d SOLBoltMath::MuzzleVelocity(const FVector3d& shipVelocity, const FVector3d& aimDirection, double muzzleSpeed)
{
    return shipVelocity + SafeNormal(aimDirection) * muzzleSpeed;
}

//////////////////////////////////////////////////////////////////////////
// Returns the direction from the muzzle to the point where the camera ray is convergenceDistance ahead
FVector3d SOLBoltMath::ConvergedAimDirection(const FVector3d& muzzlePosition, const FVector3d& cameraPosition,
    const FVector3d& cameraForward, double convergenceDistance)
{
    const FVector3d forward = SafeNormal(cameraForward);
    const FVector3d fallback = forward.IsZero() ? FVector3d(1.0, 0.0, 0.0) : forward;
    if (forward.IsZero() || !(convergenceDistance > 0.0) || !FMath::IsFinite(convergenceDistance))
    {
        return fallback;
    }
    const FVector3d toPoint = SafeNormal(cameraPosition + forward * convergenceDistance - muzzlePosition);
    return toPoint.IsZero() ? fallback : toPoint;
}

//////////////////////////////////////////////////////////////////////////
// Returns the position advanced by velocity * dt
FVector3d SOLBoltMath::AdvancePosition(const FVector3d& position, const FVector3d& velocity, double dt)
{
    if (!(dt >= 0.0))
    {
        return position;
    }
    return position + velocity * dt;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the lifetime has run out
bool SOLBoltMath::IsExpired(double remainingLifetime)
{
    return !(remainingLifetime > 0.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the remaining lifetime after dt, never NaN
double SOLBoltMath::AdvanceLifetime(double remaining, double dt)
{
    if (FMath::IsNaN(remaining))
    {
        return 0.0;
    }
    return FMath::IsNaN(dt) ? remaining : remaining - dt;
}

//////////////////////////////////////////////////////////////////////////
// Returns the earliest hit of the segment against a stationary sphere
SOLBoltMath::FSweepHit SOLBoltMath::SweptSphereHit(const FVector3d& segmentStart, const FVector3d& segmentEnd,
    const FVector3d& sphereCenter, double sphereRadius)
{
    return SweptSphereHitMoving(segmentStart, segmentEnd, sphereCenter, sphereCenter, sphereRadius);
}

//////////////////////////////////////////////////////////////////////////
// Returns the earliest hit in relative motion: a point moving by (segment - sphere) delta against a sphere at the origin
SOLBoltMath::FSweepHit SOLBoltMath::SweptSphereHitMoving(const FVector3d& segmentStart, const FVector3d& segmentEnd,
    const FVector3d& sphereCenterStart, const FVector3d& sphereCenterEnd, double sphereRadius)
{
    FSweepHit result;
    if (!(sphereRadius > 0.0) || !FMath::IsFinite(sphereRadius) || !IsFiniteVector(segmentStart)
        || !IsFiniteVector(segmentEnd) || !IsFiniteVector(sphereCenterStart) || !IsFiniteVector(sphereCenterEnd))
    {
        return result;
    }

    // Relative position p(t) = p0 + d t of the bolt in the sphere's frame; solve |p(t)| = radius for the earliest t in [0,1]
    const FVector3d p0 = segmentStart - sphereCenterStart;
    const FVector3d d = (segmentEnd - segmentStart) - (sphereCenterEnd - sphereCenterStart);
    const double c = p0.SizeSquared() - sphereRadius * sphereRadius;
    if (c <= 0.0)
    {
        result.bHit = true;
        return result;
    }
    const double a = d.SizeSquared();
    if (a < 1.0e-24)
    {
        return result;
    }
    const double b = FVector3d::DotProduct(p0, d);
    if (b >= 0.0)
    {
        return result;    // Moving away from the sphere
    }
    const double discriminant = b * b - a * c;
    if (discriminant < 0.0)
    {
        return result;
    }
    const double t = (-b - FMath::Sqrt(discriminant)) / a;
    if (t >= 0.0 && t <= 1.0)
    {
        result.bHit = true;
        result.Time = t;
    }
    return result;
}

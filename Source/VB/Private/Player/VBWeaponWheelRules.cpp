#include "Player/VBWeaponWheelRules.h"

int32 VBWeaponWheel::PickSlot(FVector2D Direction, int32 SlotCount, float DeadRadius)
{
	if (SlotCount <= 0 || Direction.ContainsNaN() || !FMath::IsFinite(DeadRadius)
		|| Direction.SizeSquared() <= FMath::Square(FMath::Max(0.f, DeadRadius)))
	{
		return INDEX_NONE;
	}

	const double Sector = 2.0 * PI / SlotCount;
	const double Angle = FMath::Atan2(Direction.X, -Direction.Y);
	return FMath::FloorToInt(FMath::Fmod(Angle + 2.0 * PI + Sector * 0.5, 2.0 * PI) / Sector) % SlotCount;
}

FVector2D VBWeaponWheel::AccumulateCursor(FVector2D Cursor, FVector2D Delta, float Radius)
{
	if (Cursor.ContainsNaN() || Delta.ContainsNaN() || !FMath::IsFinite(Radius) || Radius <= 0.f)
	{
		return FVector2D::ZeroVector;
	}

	return (Cursor + Delta).GetClampedToMaxSize(Radius);
}

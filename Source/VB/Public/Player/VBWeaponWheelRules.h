#pragma once

#include "CoreMinimal.h"

namespace VBWeaponWheel
{
	VB_API int32 PickSlot(FVector2D Direction, int32 SlotCount, float DeadRadius);
	VB_API FVector2D AccumulateCursor(FVector2D Cursor, FVector2D Delta, float Radius);
}

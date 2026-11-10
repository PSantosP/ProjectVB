#include "Enemy/VBBossAttackRules.h"
#include "Data/VBBossConfig.h"

namespace VBBossAttackRules
{
	bool IsTargetBehind(const FVector& Forward, const FVector& ToTarget, float AngleDegrees)
	{
		const FVector Facing = Forward.GetSafeNormal2D();
		const FVector Direction = ToTarget.GetSafeNormal2D();
		if (Facing.IsZero() || Direction.IsZero())
		{
			return false;
		}

		return FVector::DotProduct(Facing, Direction) < FMath::Cos(FMath::DegreesToRadians(AngleDegrees));
	}

	int32 ChooseRow(TConstArrayView<FVBBossAttackRow> Rows, const FVBBossSituationFacts& Facts,
		TConstArrayView<double> CooldownReadyAt, double Now, TFunctionRef<bool(const FVBBossAttackRow&)> CanActivate, float Random01)
	{
		auto Holds = [&Facts](EVBBossAttackSituation Situation)
		{
			switch (Situation)
			{
			case EVBBossAttackSituation::TargetBehind: return Facts.bTargetBehind;
			case EVBBossAttackSituation::TargetFar: return Facts.bTargetFar;
			case EVBBossAttackSituation::AfterHitBurst: return Facts.bAfterHitBurst;
			}

			return false;
		};
		TArray<int32, TInlineAllocator<4>> Eligible;
		double TotalWeight = 0.0;
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			const FVBBossAttackRow& Row = Rows[Index];
			const bool bCooling = CooldownReadyAt.IsValidIndex(Index) && Now < CooldownReadyAt[Index];
			if (Row.Weight <= 0.0f || bCooling || !Holds(Row.Situation) || !CanActivate(Row))
			{
				continue;
			}

			Eligible.Add(Index);
			TotalWeight += Row.Weight;
		}

		if (Eligible.IsEmpty())
		{
			return INDEX_NONE;
		}

		double Pick = FMath::Clamp(double(Random01), 0.0, 0.999999) * TotalWeight;
		for (const int32 Index : Eligible)
		{
			Pick -= Rows[Index].Weight;
			if (Pick < 0.0)
			{
				return Index;
			}
		}

		return Eligible.Last();
	}
}

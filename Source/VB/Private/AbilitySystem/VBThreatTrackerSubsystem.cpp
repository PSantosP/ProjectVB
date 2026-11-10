// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBThreatTrackerSubsystem.h"

#include "Engine/World.h"

void UVBThreatTrackerSubsystem::Register(AActor* Enemy, const FGameplayTag& Answer, const FLinearColor& Color)
{
	if (!Enemy)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (FVBThreatEntry& Entry : Threats)
	{
		if (Entry.Enemy.Get() == Enemy)
		{
			// 갱신. 같은 적이 예고를 다시 시작한 것이다(직전 Remove 를 놓쳤어도 행이 둘이 되지 않는다).
			Entry.Answer = Answer;
			Entry.Color = Color;
			Entry.StartTime = Now;
			return;
		}
	}

	FVBThreatEntry& NewEntry = Threats.AddDefaulted_GetRef();
	NewEntry.Enemy = Enemy;
	NewEntry.Answer = Answer;
	NewEntry.Color = Color;
	NewEntry.StartTime = Now;
}

void UVBThreatTrackerSubsystem::Unregister(const AActor* Enemy)
{
	Threats.RemoveAll([Enemy](const FVBThreatEntry& Entry)
	{
		return !Entry.Enemy.IsValid() || Entry.Enemy.Get() == Enemy;
	});
}

const TArray<FVBThreatEntry>& UVBThreatTrackerSubsystem::GetThreats()
{
	// 죽은 적(약참조 무효)은 여기서 걷어낸다 - 큐 Remove 는 ASC 가 사는 동안만 온다.
	Threats.RemoveAll([](const FVBThreatEntry& Entry) { return !Entry.Enemy.IsValid(); });
	return Threats;
}

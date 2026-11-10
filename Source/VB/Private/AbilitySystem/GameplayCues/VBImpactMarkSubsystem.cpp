// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBImpactMarkSubsystem.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"

UVBImpactMarkSubsystem* UVBImpactMarkSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVBImpactMarkSubsystem>() : nullptr;
}

void UVBImpactMarkSubsystem::Track(USceneComponent* Mark, FName Kind, int32 MaxCount)
{
	if (!Mark || MaxCount <= 0)
	{
		return;
	}

	TArray<TWeakObjectPtr<USceneComponent>>& List = Marks.FindOrAdd(Kind);
	// 제 수명이 다해 이미 사라진 것은 센 자리가 없다.
	List.RemoveAll([](const TWeakObjectPtr<USceneComponent>& Entry) { return !Entry.IsValid(); });
	List.Add(Mark);
	while (List.Num() > MaxCount)
	{
		// 컴포넌트만 지운다. 소유 액터를 지우면 안 된다 - SpawnDecalAtLocation 의 데칼 소유 액터는 월드의 WorldSettings 다
		//   (엔진 GameplayStatics.cpp CreateDecalComponent), 상처는 맞은 캐릭터의 메시에 붙어 있다.
		if (USceneComponent* Oldest = List[0].Get())
		{
			Oldest->DestroyComponent();
		}

		List.RemoveAt(0, EAllowShrinking::No);
	}
}

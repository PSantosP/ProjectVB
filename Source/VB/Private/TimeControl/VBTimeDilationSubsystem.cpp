// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "TimeControl/VBTimeDilationSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h" // MinGlobalTimeDilation (clamp 진단)
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "VB/VB.h"

UVBTimeDilationSubsystem* UVBTimeDilationSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}
	// dilation 은 월드 단위 상태다.
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return World->GetSubsystem<UVBTimeDilationSubsystem>();
	}

	return nullptr;
}

void UVBTimeDilationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AppliedScale = 1.0f; // 월드 시작은 정상 속도
}

void UVBTimeDilationSubsystem::Deinitialize()
{
	// 남은 요청, 타이머를 정리하고 배율을 되돌린다.
	if (UWorld* World = GetWorld())
	{
		for (TPair<FName, FVBTimeDilationRequest>& Pair : ActiveRequests)
		{
			World->GetTimerManager().ClearTimer(Pair.Value.ExpiryTimer);
		}

		for (TPair<TWeakObjectPtr<AActor>, TMap<FName, FVBTimeDilationRequest>>& ActorPair : ActorRequests)
		{
			for (TPair<FName, FVBTimeDilationRequest>& Pair : ActorPair.Value)
			{
				World->GetTimerManager().ClearTimer(Pair.Value.ExpiryTimer);
			}
		}

		if (AppliedScale != 1.0f)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
		}
	}

	ActiveRequests.Empty();
	PendingSteps.Empty();
	ActorRequests.Empty();
	AppliedScale = 1.0f;
	Super::Deinitialize();
}

void UVBTimeDilationSubsystem::PushTimed(FName Handle, float Scale, float RealDuration)
{
	// 같은 이름의 순서를 끊는다 - 안 끊으면 만료 때 옛 순서가 되살아난다.
	PendingSteps.Remove(Handle);
	PushTimedStep(Handle, Scale, RealDuration);
}

void UVBTimeDilationSubsystem::PushSequence(FName Handle, const TArray<FVBTimeDilationStep>& Steps)
{
	TArray<FVBTimeDilationStep> Remaining;
	for (const FVBTimeDilationStep& Step : Steps)
	{
		if (Step.RealDuration > 0.0f)
		{
			Remaining.Add(Step);
		}
	}

	PendingSteps.Remove(Handle);
	if (Remaining.IsEmpty())
	{
		// 저작값 문제라 경고한다.
		VB_LOG(Warning, "시간 배율 순서 요청 %s: 길이가 있는 단계가 없다 - 아무것도 안 건다", *Handle.ToString());
		return;
	}

	const FVBTimeDilationStep First = Remaining[0];
	Remaining.RemoveAt(0);
	if (!Remaining.IsEmpty())
	{
		PendingSteps.Add(Handle, MoveTemp(Remaining));
	}

	PushTimedStep(Handle, First.Scale, First.RealDuration);
}

void UVBTimeDilationSubsystem::PushTimedStep(FName Handle, float Scale, float RealDuration)
{
	// 길이가 없으면 무기한 Push 로 위임한다(Pop 필요).
	if (RealDuration <= 0.0f)
	{
		Push(Handle, Scale);
		return;
	}

	// Scale 0 이면 GameDelay 가 0 이라 즉시 만료된다. 0 보다 크게 바닥을 둔다.
	Scale = FMath::Max(Scale, KINDA_SMALL_NUMBER);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ClearExistingTimer(Handle); // 같은 handle 재요청이면 기존 타이머 먼저 정리

	FVBTimeDilationRequest Request;
	Request.Scale = Scale;

	// 타이머는 게임시간으로 흐르므로 Scale 을 곱해야 RealDuration 실초 뒤에 만료된다.
	const float GameDelay = RealDuration * Scale;
	World->GetTimerManager().SetTimer(
		Request.ExpiryTimer,
		FTimerDelegate::CreateUObject(this, &UVBTimeDilationSubsystem::OnRequestExpired, Handle),
		GameDelay,
		false);

	ActiveRequests.Add(Handle, Request); // 같은 handle 이면 교체(중첩 아님)
	Resolve();
}

void UVBTimeDilationSubsystem::Push(FName Handle, float Scale)
{
	PendingSteps.Remove(Handle);
	ClearExistingTimer(Handle);

	FVBTimeDilationRequest Request;
	Request.Scale = Scale; // ExpiryTimer 무효 = 무기한(Pop 까지 유지)
	ActiveRequests.Add(Handle, Request);
	Resolve();
}

void UVBTimeDilationSubsystem::Pop(FName Handle)
{
	PendingSteps.Remove(Handle);
	if (ActiveRequests.Contains(Handle))
	{
		ClearExistingTimer(Handle);
		ActiveRequests.Remove(Handle);
		Resolve();
	}
}

void UVBTimeDilationSubsystem::OnRequestExpired(FName Handle)
{
	// 순서 요청이면 다음 단계로 교체한다(사이에 1.0 으로 튀는 프레임이 없다).
	if (TArray<FVBTimeDilationStep>* Steps = PendingSteps.Find(Handle))
	{
		const FVBTimeDilationStep Next = (*Steps)[0];
		Steps->RemoveAt(0);
		if (Steps->IsEmpty())
		{
			PendingSteps.Remove(Handle);
		}

		PushTimedStep(Handle, Next.Scale, Next.RealDuration);
		return;
	}

	ActiveRequests.Remove(Handle);
	Resolve();
}

void UVBTimeDilationSubsystem::ClearExistingTimer(FName Handle)
{
	if (FVBTimeDilationRequest* Request = ActiveRequests.Find(Handle))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(Request->ExpiryTimer);
		}
	}
}

void UVBTimeDilationSubsystem::Resolve()
{
	// 가장 느린 활성 요청이 이긴다. 비었으면 1.0.
	float Target = 1.0f;
	for (const TPair<FName, FVBTimeDilationRequest>& Pair : ActiveRequests)
	{
		Target = FMath::Min(Target, Pair.Value.Scale);
	}

	if (Target != AppliedScale && CanChangeTime())
	{
		if (UWorld* World = GetWorld())
		{
			// WorldSettings 하한 아래면 엔진이 clamp 해 AppliedScale 과 어긋난다.
			if (const AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				if (Target < WorldSettings->MinGlobalTimeDilation)
				{
					VB_LOG(Warning, "TimeDilation %.4f 가 WorldSettings 하한(%.4f) 아래 — 엔진 clamp, AppliedScale desync 가능",
						Target, WorldSettings->MinGlobalTimeDilation);
				}
			}

			UGameplayStatics::SetGlobalTimeDilation(World, Target);
			// 실제로 적용했을 때만 갱신한다.
			AppliedScale = Target;
		}
	}
}

void UVBTimeDilationSubsystem::PushActorTimed(AActor* Actor, FName Handle, float Scale, float RealDuration)
{
	UWorld* World = GetWorld();
	if (!Actor || !World || RealDuration <= 0.0f)
	{
		return;
	}

	Scale = FMath::Max(Scale, KINDA_SMALL_NUMBER);
	const TWeakObjectPtr<AActor> Key(Actor);
	TMap<FName, FVBTimeDilationRequest>& Requests = ActorRequests.FindOrAdd(Key);
	if (FVBTimeDilationRequest* Existing = Requests.Find(Handle))
	{
		World->GetTimerManager().ClearTimer(Existing->ExpiryTimer); // 같은 handle 재요청 = 교체(중첩 아님)
	}

	FVBTimeDilationRequest Request;
	Request.Scale = Scale;
	// 월드 타이머는 전역 배율만 받는다. 액터 배율은 월드 시간에 영향이 없으므로 곱하지 않는다.
	const float GameDelay = RealDuration * AppliedScale;
	World->GetTimerManager().SetTimer(
		Request.ExpiryTimer,
		FTimerDelegate::CreateUObject(this, &UVBTimeDilationSubsystem::OnActorRequestExpired, Key, Handle),
		GameDelay,
		false);
	Requests.Add(Handle, Request);
	ResolveActor(Key);
}

void UVBTimeDilationSubsystem::OnActorRequestExpired(TWeakObjectPtr<AActor> Actor, FName Handle)
{
	// 표를 정리한 뒤에 해소한다. ResolveActor 가 표를 지울 수 있어 그 뒤에 포인터를 읽으면 안 된다.
	TMap<FName, FVBTimeDilationRequest>* Requests = ActorRequests.Find(Actor);
	if (!Requests)
	{
		return;
	}

	Requests->Remove(Handle);
	if (Requests->IsEmpty())
	{
		ActorRequests.Remove(Actor);
	}

	ResolveActor(Actor);
}

void UVBTimeDilationSubsystem::ResolveActor(const TWeakObjectPtr<AActor>& Actor)
{
	AActor* Live = Actor.Get();
	if (!Live)
	{
		ActorRequests.Remove(Actor); // 사라진 액터의 요청은 버린다. 남은 타이머는 표가 없어 그냥 끝난다.
		return;
	}

	float Target = 1.0f;
	if (const TMap<FName, FVBTimeDilationRequest>* Requests = ActorRequests.Find(Actor))
	{
		for (const TPair<FName, FVBTimeDilationRequest>& Pair : *Requests)
		{
			Target = FMath::Min(Target, Pair.Value.Scale);
		}
	}

	if (CanChangeTime())
	{
		Live->CustomTimeDilation = Target;
	}
}

bool UVBTimeDilationSubsystem::CanChangeTime()
{
	// 네트워크 월드에서는 시간(전역, 액터 모두)을 바꾸지 않는다.
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Standalone)
	{
		return World != nullptr;
	}

	if (!bReportedNetworkSkip)
	{
		VB_LOG(Log, "TimeDilation: 네트워크 월드(%d)라 시간 조정 요청을 적용하지 않는다", int32(World->GetNetMode()));
		bReportedNetworkSkip = true;
	}

	return false;
}

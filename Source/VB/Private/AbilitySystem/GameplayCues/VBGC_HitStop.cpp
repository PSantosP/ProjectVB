// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_HitStop.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "Data/VBAttackConfig.h"   // 범위(bHitStopCombatantsOnly), 피격자 추가시간의 홈
#include "VB/VB.h"

namespace
{
	// 히트스톱 요청의 고정 handle. 같은 handle 재요청은 교체라 겹친 히트스톱이 하나로 합쳐져 연장된다.
	const FName GHitStopHandle(TEXT("HitStop"));
}

// 생성자에서 GameplayCueTag 를 세우지 않는다. 태그는 BP 자식의 Class Defaults 가 소유한다.
// 네이티브 부모가 태그를 들면 BP 자식의 GameplayCueName 이 None 이 되어 큐 세트 등록에서 조용히 빠진다.
UVBGC_HitStop::UVBGC_HitStop()
{
}

bool UVBGC_HitStop::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	// 데디 서버 스킵 가드를 넣지 않는다 - 큐는 데디 서버에서 실행되지 않는다(GameplayCueRunOnDedicatedServer=0).
	UWorld* World = MyTarget ? MyTarget->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	
	// 큐 파라미터가 히트스톱 값의 유일한 출처다(RawMagnitude=Duration, NormalizedMagnitude=TimeDilation).
	// 값이 없으면 기본값으로 때우지 않고 거부한다 - 배선 버그이고, Duration=0 은 복구 안 되는 무기한 슬로모가 된다.
	if (Parameters.RawMagnitude <= 0.0f || Parameters.NormalizedMagnitude <= 0.0f)
	{
		VB_LOG(Warning, "VBGC_HitStop: 큐에 히트스톱 크기가 없다 (Duration=%.3f Dilation=%.3f) - 배선 오류라 스킵",
			Parameters.RawMagnitude, Parameters.NormalizedMagnitude);
		return false;
	}

	const float Duration = Parameters.RawMagnitude;
	const float TimeDilation = Parameters.NormalizedMagnitude;
	
	// UVBTimeDilationSubsystem 에 위임한다(handle 요청 -> min-wins). 직접 SetGlobalTimeDilation 하면 겹친 히트스톱이 서로 덮어쓴다.
	UVBTimeDilationSubsystem* TimeSubsystem = UVBTimeDilationSubsystem::Get(World);
	if (!TimeSubsystem)
	{
		VB_LOG(Warning, "VBGC_HitStop: TimeDilationSubsystem 없음 — 히트스톱 스킵");
		return false;
	}
	// 공격 설정이 전투원만이면 공격자와 맞은 액터만 CustomTimeDilation 으로 멈춘다. 설정이 없거나 전역이면 전역 경로.
	const UVBAttackConfig* Config = Cast<UVBAttackConfig>(Parameters.SourceObject.Get());
	if (Config && Config->bHitStopCombatantsOnly)
	{
		AActor* Attacker = Parameters.GetEffectCauser() ? Parameters.GetEffectCauser() : Parameters.GetInstigator();
		if (Attacker)
		{
			TimeSubsystem->PushActorTimed(Attacker, GHitStopHandle, TimeDilation, Duration);
		}

		FGameplayEffectContextHandle Context = Parameters.EffectContext; // GetActors() 가 non-const 라 복사해서 읽는다
		for (const TWeakObjectPtr<AActor>& Victim : Context.GetActors())
		{
			if (AActor* V = Victim.Get())
			{
				TimeSubsystem->PushActorTimed(V, GHitStopHandle, TimeDilation, Duration + Config->HitStopVictimExtraSeconds);
			}
		}

		return true;
	}

	TimeSubsystem->PushTimed(GHitStopHandle, TimeDilation, Duration);

	return true;
}

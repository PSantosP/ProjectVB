// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "VBGA_CinematicSkill.generated.h"

class ACharacter;
class UVBCinematicSkillDefinition;

// 지금 스킬을 쓸 수 있는가를 판단하는 유일한 클래스다(게이지 HUD 가 묻는다). 서버가 고른 roster 하나를 가진다.
UCLASS()
class VB_API UVBGA_CinematicSkill : public UVBGA_MeleeAttackBase
{
	GENERATED_BODY()

public:
	UVBGA_CinematicSkill();
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	const UVBCinematicSkillDefinition* GetDefinition() const { return Definition; }

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual bool AllowsImmediateTraceFallback() const override { return false; }
	virtual void FilterWindowHits(int32 WindowKey, TArray<FHitResult>& InOutHits) override;
	// 여기서 연출을 펼칠 수 있는가(땅 위, 마지막 타격 창 저작됨). 시험은 연출만 떼어 보려고 덮어쓴다.
	virtual bool CanStageShow(const ACharacter* Avatar) const;

	// roster 규칙, 자세, 준비 조건. 데이터만 담는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|CinematicSkill")
	TObjectPtr<const UVBCinematicSkillDefinition> Definition;

private:
	// 필요한 값이 없으면 false(로그 남김). 그러면 활성화는 취소로 끝난다.
	bool BuildRoster();
	// 이번 활성화의 Montage 가 마지막 창의 열림 시각에 닿았는가(VBCinematicSkillRules::IsFinalPhase).
	bool IsInFinalWindow() const;
	// 데이터 문제는 하나당 한 번만 알린다 - 준비 검사는 태그가 바뀔 때마다 묻는다.
	mutable bool bReportedCostProblem = false;
	mutable bool bReportedStagingProblem = false;

	// 활성화 한 번의 연출: 붙잡은 대상, 이미 맞은 대상, 마지막 창이 열리는 곳. 실행과 끝에서 통째로 초기화한다.
	struct FSession
	{
		TSet<TWeakObjectPtr<AActor>> Roster;
		TSet<TWeakObjectPtr<AActor>> Consumed;
		float FinalWindowStart = 0.0f;
		float FinalWindowEnd = 0.0f; // 로그에만 쓴다. 판정에는 상한이 없다
	};
	FSession Session;
};

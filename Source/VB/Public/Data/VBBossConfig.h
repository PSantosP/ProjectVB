// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/VBEnemyConfig.h"
#include "GameplayTagContainer.h"
#include "VBBossConfig.generated.h"

class UAnimMontage;

// 페이즈의 기본 공격 대신 다른 공격이 나서는 상황.
UENUM(BlueprintType)
enum class EVBBossAttackSituation : uint8
{
	TargetBehind, // 대상이 등 뒤(UVBBossConfig::Situations 의 각도 밖)
	TargetFar, // 대상이 멀리 있고 한동안 공격이 없었다(카이팅)
	AfterHitBurst, // 짧은 시간에 여러 대를 맞았다
};

// 상황이 맞으면 기본 공격 대신 쓸 공격 하나. 토큰을 받을 때 AVBBossBase::ChooseNextAttack 이 고른다.
USTRUCT(BlueprintType)
struct FVBBossAttackRow
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(Categories="Input"))
	FGameplayTag InputTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss")
	EVBBossAttackSituation Situation = EVBBossAttackSituation::TargetBehind;

	// 이 행을 고른 뒤 다시 고를 수 있기까지(초). 같은 대응이 연달아 나오지 않게.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(ClampMin="0", Units="s"))
	float CooldownSeconds = 8.0f;

	// 같은 순간 여러 행이 맞으면 이 비율로 고른다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(ClampMin="0.01"))
	float Weight = 1.0f;
};

// 상황 판정의 문턱. 보스마다 다를 수 있어 설정에 둔다.
USTRUCT(BlueprintType)
struct FVBBossSituationTuning
{
	GENERATED_BODY()

	// 정면에서 이 각도보다 더 돌아가 있으면 등 뒤로 본다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(ClampMin="0", ClampMax="180", Units="deg"))
	float BehindAngleDegrees = 120.0f;

	// 이보다 멀고(수평) 마지막 공격 시작에서 FarSeconds 가 지났으면 멀다고 본다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(ClampMin="0", Units="cm"))
	float FarDistance = 600.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(ClampMin="0", Units="s"))
	float FarSeconds = 6.0f;

	// BurstSeconds 안에 체력이 BurstHits 번 깎였으면 몰아 맞은 것으로 본다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(ClampMin="1"))
	int32 BurstHits = 3;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(ClampMin="0.1", Units="s"))
	float BurstSeconds = 2.0f;
};

/**
 * 보스 페이즈 하나: 체력이 문턱 아래로 내려오면 이 공격을 쓰고, 들어갈 때 이 몽타주를 튼다.
 * 첫 페이즈는 UVBEnemyConfig 자체(AttackAbilityInputTag)다.
 */
USTRUCT(BlueprintType)
struct FVBBossPhase
{
	GENERATED_BODY()

	// 체력 비율(현재/최대)이 이 값 이하면 이 페이즈에 들어간다. 앞 페이즈보다 작아야 한다(IsDataValid).
	// 체력이 다시 올라가도 되돌아가지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(ClampMin="0.01", ClampMax="0.99"))
	float HealthRatioThreshold = 0.5f;

	// 이 페이즈의 기본 공격 GA InputTag. AVBBossBase::GetCurrentAttackInputTag 가 돌려준다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss", meta=(Categories="Input"))
	FGameplayTag AttackInputTag;

	// 이 페이즈에서 상황이 맞으면 기본 공격 대신 나서는 공격들. 비면 늘 기본 공격이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss")
	TArray<FVBBossAttackRow> SituationRows;

	// 페이즈 진입 몽타주(선택). 트는 동안 State.Combat.PhaseTransition 이 붙어 BT 가 이동, 공격을 닫는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss")
	TObjectPtr<UAnimMontage> TransitionMontage = nullptr;

	// 전환 몽타주 동안 슈퍼아머를 줄지(무적 아님, 체력은 깎인다).
	// false 면 문턱을 넘은 그 타격의 히트리액트가 같은 프레임에 전환 몽타주를 끊는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss")
	bool bSuperArmorDuringTransition = true;
};

/**
 * 보스 튜닝. UVBEnemyConfig 에 페이즈와 상황 공격만 더한다.
 * AVBBossBase 가 EnemyConfig 를 이 클래스로 캐스트해 읽는다(아니면 페이즈 없이 돌고 부팅 시 경고).
 */
UCLASS(BlueprintType)
class VB_API UVBBossConfig : public UVBEnemyConfig
{
	GENERATED_BODY()

public:
	// 첫 페이즈 다음의 페이즈들, 문턱 내림차순. CurrentPhaseIndex N(>0) = AdditionalPhases[N-1].
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss")
	TArray<FVBBossPhase> AdditionalPhases;

	// 첫 페이즈의 상황 행. 첫 페이즈의 기본 공격은 상속한 AttackAbilityInputTag 다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss")
	TArray<FVBBossAttackRow> FirstPhaseRows;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Boss")
	FVBBossSituationTuning Situations;

	// 페이즈 번호(0 = 첫 페이즈)의 상황 행. 없는 번호면 빈 목록.
	TConstArrayView<FVBBossAttackRow> GetSituationRows(int32 PhaseIndex) const;

#if WITH_EDITOR
	// 문턱 내림차순, (0,1) 범위, 태그 유효를 본다. 위반해도 런타임 로그가 없는 부류다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

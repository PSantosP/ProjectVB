// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h" // FGameplayTag (ResolveImpactCue)
#include "AbilitySystem/VBHitImpact.h" // EVBHitImpact (스윙 단위 등급 덮어쓰기)
#include "VBSwingCombatProfile.generated.h"

class FDataValidationContext; // IsDataValid 인자 - 헤더에서 Misc/DataValidation.h 를 끌지 않는다(VBSwingImpactProfile.h 동형)

/**
 * FVBSwingCombatScales - 스윙 한 번의 전투 수치 배율. 미저작(1.0)이 곧 현행 그대로다
 */
USTRUCT(BlueprintType)
struct VB_API FVBSwingCombatScales
{
	GENERATED_BODY()

	// 최종 데미지 배율. 실효 데미지 = AttackPower x leaf.DamageMultiplier x 변형.DamageScale x 이 값
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float DamageScale = 1.0f;

	// 넉백 속도 배율. 음수는 넉백 방향을 뒤집어 "한 스윙 = 한 방향" 을 깨므로 막는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float KnockbackStrengthScale = 1.0f;

	// 기존 자산의 상방 밀치기 배율 검출용. 반드시 1 이어야 한다
	UPROPERTY(EditDefaultsOnly, Category="VB|Deprecated", AdvancedDisplay)
	float KnockbackZScale = 1.0f;

	// 히트스톱 지속시간 배율. 곱이 0 이면 히트스톱 큐가 거부하므로 하한을 둔다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.001"))
	float HitStopDurationScale = 1.0f;

	// Poise(경직 게이지) 피해 배율. leaf 의 PoiseDamage 에 곱한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float PoiseDamageScale = 1.0f;

	// 판정 폭 배율. trace capsule 반지름 = 모양 표 반지름 x 이 값 (모양 표는 부위별이라 한 스윙만 넓힐 때 쓴다)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.1"))
	float TraceRadiusScale = 1.0f;

	// 배율이 하나라도 1.0 이 아닌가. IsDataValid 와 계측 로그 게이트가 쓴다
	bool HasAnyEffect() const;
};

/**
 * UVBSwingCombatProfile - 스윙 단위 전투 수치 저작 자산. 공격 몽타주의 Attack Trace 노티가 들고, GA(서버)만 읽는다
 * 노티가 GameplayEvent 의 OptionalObject 로 실어 보내야 해서 USTRUCT 가 아니라 자산이다
 */
UCLASS(BlueprintType)
class VB_API UVBSwingCombatProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBSwingCombatScales Scales;

	// 이 스윙만 반응 등급을 덮어쓴다. 꺼져 있으면 UVBAttackConfig::HitImpact 를 그대로 쓴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(InlineEditConditionToggle))
	bool bOverrideHitImpact = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(EditCondition="bOverrideHitImpact"))
	EVBHitImpact HitImpact = EVBHitImpact::Light;

	// 이 스윙만 띄우는 높이(cm). 음수 = UVBAttackConfig::LaunchHeight 그대로. 띄우려면 등급 Launch 와 함께 쓴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="-1.0"))
	float LaunchHeightOverride = -1.0f;

	// 프로필(null 가능)과 공격 자산 높이에서 이 스윙의 띄우는 높이를 정한다
	static float ResolveLaunchHeight(const UVBSwingCombatProfile* Profile, float AttackLaunchHeight);

	// 이 스윙의 화면 충격(강타). 있으면 스윙 쉐이크 대신 이 프로필의 화면 층이 난다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TObjectPtr<const class UVBCombatFeedbackProfile> ScreenFeedback;

	// 타격 이펙트를 맞은 부위 큐의 .Heavy 자식으로 바꾼다. 자식 태그가 없으면 원래 큐 그대로다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	bool bHeavyImpact = false;

	// 고른 타격 큐를 이 스윙의 등급에 맞춘다. 순수 함수. 프로필이 null 이거나 등급이 꺼져 있으면 BaseCue 그대로
	static FGameplayTag ResolveImpactCue(const UVBSwingCombatProfile* Profile, const FGameplayTag& BaseCue);

#if WITH_EDITOR
	// 전 배율이 1.0 인 자산을 저장 시점에 잡는다 (런타임에선 배선 실패와 구분이 안 된다)
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

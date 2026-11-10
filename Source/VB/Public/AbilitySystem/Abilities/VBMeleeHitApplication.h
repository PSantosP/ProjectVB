// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h" // FGameplayEffectSpecHandle 를 값으로 받는다

class AActor;
class ACharacter;
class UAbilitySystemComponent;
class UObject;
class UVBAttackConfig;
struct FGameplayEffectSpec;
struct FHitResult;
struct FVBGameplayEffectContext;
struct FVBSwingHitContext;

// 스윙이 맞힌 표적 하나에 피해 스펙을 채우고 적용하는 서버 단계들. 순서와 능력의 가상 함수 호출은 능력의 ApplySwingHits 가 맡는다.
namespace VBMeleeHitApplication
{
	// 맞은 표적이 피해 대상이면 그 ASC 를, 아니면 nullptr 을 돌려준다(이미 처리, 같은 편, 사망, 비관통).
	VB_API UAbilitySystemComponent* FindDamageableTarget(const FHitResult& Hit, const ACharacter& Avatar,
		const TSet<AActor*>& ProcessedActors);

	// 스펙 문맥에 맞은 결과와 미는 방향을 넣고 타격 사실을 돌려준다. 우리 문맥 타입이 아니면 nullptr 이고 이 표적을 건너뛴다.
	VB_API FVBGameplayEffectContext* AddHitToSpec(const FGameplayEffectSpecHandle& SpecHandle, const FHitResult& Hit,
		const FVBSwingHitContext& Context, const UObject& Ability);

	// 스윙의 반응 등급, 타격 큐, 피해, 넉백, 경직 피해, 답 태그를 스펙에 채운다.
	VB_API void FillHitSpec(FGameplayEffectSpec& Spec, FVBGameplayEffectContext& HitFacts, const FHitResult& Hit,
		const FVBSwingHitContext& Context, const UVBAttackConfig& Config);

	// 스펙을 적용하고 체력 또는 실드가 실제로 줄었으면 true 를 돌려준다.
	VB_API bool ApplyHitSpec(UAbilitySystemComponent& TargetASC, const FGameplayEffectSpecHandle& SpecHandle);

	// 적중한 스윙의 피해 곱셈 사슬을 로그로 남긴다.
	VB_API void LogSwingDamage(const UObject& Ability, const FVBSwingHitContext& Context, const UVBAttackConfig& Config,
		float VariantDamage, int32 TargetCount);
}

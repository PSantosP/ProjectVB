// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/VBHitImpact.h"
#include "CharacterCommon/VBCombatChargeCredit.h"

struct FGameplayEffectSpec;
struct FHitResult;

// 한 타격이 스펙에 싣는 값. 배율은 부르는 쪽이 이미 곱한 최종값이다.
struct FVBHitSpecValues
{
	// Effect.Damage.Physical 로 가는 최종 피해.
	float Damage = 0.0f;
	// 받는 쪽 넉백의 수평 세기(cm/s).
	float KnockbackStrength = 0.0f;
	// 경직 게이지 피해.
	float PoiseDamage = 0.0f;
	// 공격이 선언한 반응 등급. 저항은 받는 쪽이 뺀다.
	EVBHitImpact HitImpact = EVBHitImpact::Light;
	// 실효 등급이 띄움일 때의 높이(cm).
	float LaunchHeight = 0.0f;
	// 미는 방향(월드 yaw, 도). 받는 쪽은 공격 방향을 스스로 알 수 없어서 여기서 정한다.
	float PushYaw = 0.0f;
	// 막지도 피하지도 않은 타격에 받는 쪽이 낼 큐. 비면 없음.
	FGameplayTag ImpactCue;
	// 연출 게이지 적립의 출처.
	EVBCombatChargeSource ChargeSource = EVBCombatChargeSource::OrdinaryAttack;
	// 막기, 패링, 회피 중 무엇이 통하는가(Attack.Answer.*). 비면 동적 태그를 달지 않는다.
	FGameplayTag AttackAnswer;
};

// 타격 스펙을 채우는 하나뿐인 자리. 근접 스윙과 투사체가 같은 사실을 싣게 해 가드, 회피, 반응이 갈라지지 않게 한다.
namespace VBHitSpec
{
	// 스펙의 컨텍스트가 FVBGameplayEffectContext 가 아니거나 이미 HitResult 가 있으면 아무것도 쓰지 않고 false.
	VB_API bool Fill(FGameplayEffectSpec& Spec, const FVBHitSpecValues& Values, const FHitResult& Hit);
}

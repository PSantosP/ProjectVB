// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAbilitySystemComponent;
struct FGameplayEffectSpec;

/**
 * 받아들인 피해 한 번의 사실. 체력 세트가 가드, 실드, 체력 적용 뒤 채우고, 반응은 이것만 본다.
 * 스펙은 참조만 하므로 PostGameplayEffectExecute 안 동기 호출에서만 쓴다.
 */
struct FVBAcceptedDamage
{
	UAbilitySystemComponent& Victim;
	const FGameplayEffectSpec& Spec;
	// 가드 뒤, 실드 앞. 반응 크기, 섬광, 감지 보고가 쓴다.
	float Damage = 0.0f;
	// 실드 뒤 체력에 닿은 양(하한 보정 전). 체력 문턱 창 요청의 게이트.
	float HealthDamage = 0.0f;
	// 적용 뒤 체력과 상한. 창 이벤트 크기(비율)를 만든다.
	float HealthAfter = 0.0f;
	float MaxHealth = 0.0f;
	bool bGuarded = false;
};

namespace VBDamageResponse
{
	// 받아들인 피해에 대한 반응(권한 쪽). 순서가 계약: 창 요청 -> AI 감지 -> 반응, 넉백 -> 피 이펙트, 섬광.
	// SetHealth 뒤에 불러야 한다 - 치명타 반응이 이미 붙은 State.Dead 로 막히는 데 기댄다.
	VB_API void Respond(const FVBAcceptedDamage& Hit);
}

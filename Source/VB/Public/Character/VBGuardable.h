// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VBGuardable.generated.h"

class UVBParryConfig;
enum class EVBHitDirection : uint8;
class UAbilitySystemComponent;
struct FGameplayEffectSpec;

// 피해량과 확정된 가드 결과.
struct FVBGuardResult
{
	bool bPerfectParry = false;
	float Damage = 0.0f; // 가드 뒤 남은 피해(패링 0, 블록 x BlockDamageMultiplier, 안 막음 = 그대로)
	bool bGuarded = false; // 막았다(블록, 패링) - 뒤의 반응, 넉백, 섬광을 내지 않는다(가드는 받아넘김이지 피격이 아니다, user 06-26)
};

namespace VBGuardRules
{
	// 패링이 열 반격 창의 길이. 반격 능력이 있을 때만 Config.RiposteWindow, 아니면 0.
	//   반격이 없는 캐릭터에 창을 열면 그 동안 경공격만 막힌다.
	VB_API float RiposteWindowSeconds(const UAbilitySystemComponent& ASC, const UVBParryConfig& Config);
}

/**
 * 가드/패링을 할 수 있는 액터 계약. 데미지 hub 가 구체 클래스로 Cast 하지 않고 가드를 판정하게 한다.
 * 구현체는 GetParryConfig() 가 null 을 돌려주지 않고(미할당이면 CDO), 서버 권위 상태를 읽으며,
 * 비가드 시 GetGuardElapsed() 가 아주 큰 값을 돌려줘야 한다.
 */
UINTERFACE(MinimalAPI)
class UVBGuardable : public UInterface
{
	GENERATED_BODY()
};

class IVBGuardable
{
	GENERATED_BODY()

public:
	// 현재 가드 중인가 (서버 진실).
	virtual bool IsGuarding() const = 0;
	// 가드 개시 후 경과(초). 비가드면 매우 큰 값 -> 항상 패링 윈도우 밖.
	virtual float GetGuardElapsed() const = 0;
	// 받아넘김(Accept) 연출. 데미지 hub 가 블록/패링 확정 시 공격이 온 쪽과 함께 호출.
	virtual void PlayGuardAccept(EVBHitDirection Side) = 0;
	// 가드/패링 튜닝. null 이 아니다(미할당 시 CDO - 위 계약 1).
	virtual const UVBParryConfig* GetParryConfig() const = 0;

	// 패링 성공 시 반격 창을 열고 Attacker 쪽으로 락온한다. 타이머 소유자는 캐릭터다.
	//   Duration 0 = 창을 열지 않는다(락온, 바라보기는 한다).
	virtual void OpenRiposteWindow(float Duration, AActor* Attacker) = 0;

	// 가드 결과에 따른 스태미나 증감. 능력 코스트가 아니라 반응이라 GE 가 아니다.
	virtual void ApplyGuardStaminaDelta(float Delta) = 0;

	// 이 타격을 막는가와 그 부수효과를 담당한다(서버만). 체력 세트는 한 번 부르고 답만 쓴다.
	//   기본 구현은 막을 수 없는 공격 -> 패링 창 -> 블록 순이고, 다른 가드는 덮어쓴다.
	VB_API virtual FVBGuardResult ResolveIncomingHit(UAbilitySystemComponent& VictimASC, const FGameplayEffectSpec& Spec,
		float Damage);
};

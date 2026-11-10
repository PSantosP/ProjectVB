// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h" // FGameplayTag 를 값으로 받는다 - 전방선언 불가
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/VBReachRules.h"            // 기본 인자라 완전한 타입이 필요하다(설정 자산 헤더는 끌어오지 않는다).
#include "VBAbilitySystemStatics.generated.h"

class UAbilitySystemComponent;
struct FGameplayAbilitySpec;

/**
 * GAS 상태 질의(사망, 공격, 처형 창 등)와 ASC 획득의 유일한 홈. 상태 없는 정적 헬퍼만 둔다.
 * 판정을 호출처마다 따로 쓰면 의미가 바뀔 때 조용히 갈라지므로 여기로 모은다.
 */
UCLASS()
class VB_API UVBAbilitySystemStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 사망 판정. ASC 가 null 이면 false(사망 개념이 없는 대상은 살아 있다).
	static bool IsDead(const UAbilitySystemComponent* ASC);

	// 액터판 사망 판정. ASC 는 엔진 정본(GetAbilitySystemComponentFromActor)으로 얻는다.
	UFUNCTION(BlueprintPure, Category="VB|GAS")
	static bool IsActorDead(const AActor* Actor);

	// 공격 중(State.Combat.Attacking)인가. null 이면 false. 이 태그의 하강 에지가 어택 토큰 해제의 유일한 경로다
	static bool IsAttacking(const UAbilitySystemComponent* ASC);

	// 예고(텔레그래프) 중인가. ASC 가 null 이면 false. 코디네이터가 예고 겹침을 막는 데 쓴다.
	static bool IsTelegraphing(const UAbilitySystemComponent* ASC);

	// 처형 창이 열려 있는가. ASC 가 null 이면 false. 태그 생산자는 UVBGA_FinisherWindow 하나다.
	static bool IsInFinisherWindow(const UAbilitySystemComponent* ASC);

	UFUNCTION(BlueprintPure, Category="VB|GAS")
	static bool IsActorInFinisherWindow(const AActor* Actor);

	// 처형 창 GA 가 켜져 있는가(누운 대기 + 열림). ASC 가 null 이면 false.
	static bool IsFinisherWindowArmed(const UAbilitySystemComponent* ASC);

	// 처형, 정화 피해(Effect.Execute / Effect.Purify)인가.
	static bool IsFinisherDamage(const struct FGameplayEffectSpec& Spec);

	// 이 타격을 무시하는가(회피 중, 처형 중, 처형 창인데 처형 피해 아님). 가드/패링은 여기서 보지 않는다.
	static bool ShouldIgnoreHit(const UAbilitySystemComponent* Victim, const struct FGameplayEffectSpec& Spec,
		const TCHAR** OutReason = nullptr);

	// 기절 게이지가 이 타격을 무시하는가. 위 판정 + 처형 창이 켜져 있으면 게이지를 다 찬 채로 둔다.
	static bool ShouldIgnorePoiseHit(const UAbilitySystemComponent* Victim, const struct FGameplayEffectSpec& Spec,
		const TCHAR** OutReason = nullptr);

	// 처형 피해로 보는 행위 태그(Effect.Execute, Effect.Purify)인가. 의료 설정 검증도 이것을 쓴다.
	static bool IsFinisherActionTag(const FGameplayTag& Tag);

	// 경직(State.Combat.Staggered) 중인가. null 이면 false. 그로기(다운)도 포함된다
	static bool IsStaggered(const UAbilitySystemComponent* ASC);

	// 그로기로 엎드려 있는가(State.Combat.Downed). ASC 가 null 이면 false. 언제나 Staggered 와 함께 선다.
	static bool IsDowned(const UAbilitySystemComponent* ASC);

	// 처형을 걸 수 있는 대상(처형 창 열림, MaxRange 안, 처형 허용). 락온 대상 우선, 없으면 전방 원뿔. 없으면 nullptr
	static AActor* FindFinisherTarget(const AActor* Attacker, float MaxRange,
		const FVBReachRules& Rules = FVBReachRules());

	// 처형 키(Input.Execute)에 묶인 의료 능력의 사거리, 규칙으로 찾은 대상. 없으면 nullptr.
	static AActor* FindExecuteTarget(const UAbilitySystemComponent* ASC);

	// 거리를 제외한 높이, 방향, 시야를 검사한다. 같은 대상의 액터와 물리 조각만 시야에서 뺀다.
	static bool PassesReachRules(const AActor* From, float FacingYaw, const FVector& TargetLocation,
		const AActor* IgnoredActor, const FVBReachRules& Rules, TConstArrayView<TObjectPtr<AActor>> TargetParts = {});

	// 이 액터가 처형을 거는 중인가. GA 가 아닌 진입점(무기 소환, 트래버설)이 스스로 물러날 때 쓴다.
	UFUNCTION(BlueprintPure, Category="VB|GAS")
	static bool IsActorFinishing(const AActor* Actor);

	// 아군 오사인가(true 면 호출부가 대상을 건너뛴다). FGenericTeamId::GetAttitude 기준이고 Neutral 은 통과한다
	UFUNCTION(BlueprintPure, Category="VB|GAS")
	static bool IsFriendlyFire(const AActor* Attacker, const AActor* Target);

	// InputTag 로 활성화 가능한 어빌리티 스펙. 없으면 nullptr. 부여/제거 때 무효가 되므로 보관하지 않는다
	static const FGameplayAbilitySpec* FindActivatableAbilitySpecByInputTag(
			UAbilitySystemComponent* ASC, FGameplayTag InputTag);
};

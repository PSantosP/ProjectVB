#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBGuardComponent.generated.h"

class AActor;
class UAbilitySystemComponent;
class UAnimInstance;
class UAnimMontage;
class UVBParryConfig;
enum class EVBHitDirection : uint8;

// 가드 상태를 관리하는 컴포넌트: 가드 시작 시각, 진입 때 고정한 무기 세트, 받아넘김 잠금, 반격 창. 서버 권위(판정이 서버에서만 일어난다).
//   켜고 끄는 것은 가드 능력(UVBGA_Guard), 막을지 정하는 것은 IVBGuardable::ResolveIncomingHit - 캐릭터는 그 질문에 이 컴포넌트의 값으로 답한다.
//   반격 창은 능력보다 오래 살 수 있어(패링 뒤 가드를 놓아도 남는다) 능력이 아니라 여기에 있다.
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBGuardComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBGuardComponent();
	// 가드하는 캐릭터에만 붙는다. 없으면 가드가 없는 액터다.
	static UVBGuardComponent* FindFor(const AActor* Actor);

	// 서버. 손에 든 무기 정의의 가드 세트를 고정하고 가드를 시작한다. 이미 가드 중이면 아무것도 바꾸지 않는다. 가드 중이면 참.
	bool BeginGuard();
	// 서버. 가드를 끝내고 진입 때 재생한 몽타주의 End 로 돌아간다. 반격 창과 받아넘김 잠금은 남긴다(수명이 다르다).
	void EndGuard();
	// 서버. 캐릭터가 ASC 연결을 끊거나 컴포넌트가 사라질 때: 가드, 잠금, 반격 창(연 ASC 에서)을 모두 정리한다. 죽은 캐릭터에서도 정리한다.
	void ReleaseSession();

	bool IsGuarding() const { return GuardStartServerTime >= 0.0f; }
	// 비가드면 아주 큰 값 - 패링 창 밖(IVBGuardable 계약 3).
	float GetGuardElapsed() const;
	// 미할당이면 CDO - null 이 아니다(IVBGuardable 계약 1).
	const UVBParryConfig* GetParryConfig() const;

	void PlayGuardAccept(EVBHitDirection Side);
	// 길이 0 = 반격 능력이 없다 - 남은 창을 닫고 새로 열지 않는다(락온, 바라보기는 한다).
	void OpenRiposteWindow(float Duration, AActor* Attacker);
	void ApplyGuardStaminaDelta(float Delta);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool HasOwnerAuthority() const;
	// 타이머 정리 + 이 캐릭터가 더한 반격 창 몫을 더한 ASC 에서 뺀다. 만료, 길이 0, ReleaseSession 이 부른다.
	void CloseRiposteWindow();
	UAbilitySystemComponent* FindOwnerAbilitySystem() const;
	UAnimInstance* FindOwnerAnimInstance() const;

	// 가드, 패링 튜닝.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TObjectPtr<UVBParryConfig> ParryConfig;

	// 가드 시작 서버 시각(초). <0 = 비가드. 복제하지 않는다 - 패링 판정이 서버에서만 일어난다.
	float GuardStartServerTime = -1.0f;
	// 받아넘김 재생이 끝나는 시각. 그 전에는 다시 누른 가드가 가드 몽타주를 재생하지 않는다(받아넘김을 덮지 않게).
	float GuardMontageLockUntil = 0.0f;

	// 진입 때의 무기 세트. EndGuard, PlayGuardAccept 가 현재 무기가 아니라 이것을 읽어 가드 포즈와 짝이 어긋나지 않는다.
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveGuardMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveParryAcceptMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveParryAcceptMontageRight;
	FName ActiveGuardEndSection;

	FTimerHandle RiposteWindowHandle;
	// 반격 창 태그 중 이 캐릭터가 더한 몫과 그 몫을 더한 ASC - 닫을 때 다시 찾지 않고 더한 곳에서 뺀다.
	bool bRiposteWindowTagHeld = false;
	TWeakObjectPtr<UAbilitySystemComponent> RiposteWindowASC;
};

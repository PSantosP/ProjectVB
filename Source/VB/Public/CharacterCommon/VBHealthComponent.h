// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CharacterCommon/VBDeathFacts.h"
#include "VBHealthComponent.generated.h"

class UAbilitySystemComponent;
class UVBHealthAttributeSet;

/**
 * 액터 하나의 죽음을 담당한다(플레이어, 적 공용). 체력 세트가 0 이 됐다고 알릴 때 서버에서 한 번만 죽음 단계를 돈다.
 * 단계: 래치 -> BeforeDeathTag -> State.Dead + 능력 취소 -> AfterDeathTag -> PresentDeath -> OnDeathCompleted -> Message.Death.
 * 소유 액터는 IVBDeathSteps 를 구현해야 한다. 사망 표시, 클라 죽음 판정, 되살리기는 맡지 않는다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBHealthComponent();

	static UVBHealthComponent* FindFor(const AActor* Actor);

	// 이 ASC 의 체력 세트를 구독한다(이전 세트는 구독 해제한다). 소유자가 IVBDeathSteps 를 구현하고 ASC, 체력 세트가 있어야 하며, 어긋나면 ensure 후 구독하지 않는다.
	void InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystem);

	// 구독하던 세트에서 해제한다(멱등). 되살리지 않는다 - 죽음 래치는 액터 수명 동안 유지된다.
	void UninitializeFromAbilitySystem();

	// 이 액터의 죽음이 시작됐나(서버, 래치부터 참). State.Dead 는 능력 취소 단계에서야 붙으므로 이것을 묻는다.
	bool HasStartedDeath() const { return bDeathStarted; }

	// 죽음 단계가 다 끝난 뒤 한 번(서버). 소유자를 아는 쪽이 구독한다. 모르는 쪽은 버스 Message.Death 를 구독한다(같은 Facts, 이 방송 바로 뒤).
	DECLARE_MULTICAST_DELEGATE_OneParam(FVBDeathCompleted, const FVBDeathFacts&);
	FVBDeathCompleted OnDeathCompleted;

protected:
	virtual void OnUnregister() override;

private:
	void HandleOutOfHealth(const FVBDeathFacts& Facts);

	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<const UVBHealthAttributeSet> HealthSet;
	FDelegateHandle OutOfHealthHandle;
	bool bDeathStarted = false;
};

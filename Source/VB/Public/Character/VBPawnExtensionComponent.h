// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBPawnExtensionComponent.generated.h"

class UVBAbilitySystemComponent;

// Pawn 이 ASC 의 아바타가 된 뒤 한 번. 시작 부여는 이미 끝나 있다.
DECLARE_MULTICAST_DELEGATE_OneParam(FVBAbilitySystemInitializedDelegate, UVBAbilitySystemComponent& /*ASC*/);
// Pawn 이 ASC 연결을 해제하기 직전 한 번(포인터는 아직 유효하다). bAvatarSession = 그때 ASC 의 아바타가 아직 이 Pawn 이었다.
//   false 면 이미 다른 Pawn 이 아바타다 - 자기 구독만 거두고 ASC 쪽 상태(태그 개수 덮어쓰기, 능력 취소)는 건드리지 않는다.
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBAbilitySystemUninitializedDelegate, UVBAbilitySystemComponent& /*ASC*/, bool /*bAvatarSession*/);

/**
 * Pawn 하나와 PlayerState ASC 의 연결 한 회차(연결, 해제)를 맡는다(Lyra ULyraPawnExtensionComponent 의 ASC 부분).
 * 해제를 한 곳에 두어 Pawn 을 바꿀 때 이전 Pawn 의 구독이 새 Pawn 에 남지 않게 한다. 엔진 진입점은 Pawn 의 override 에 남는다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBPawnExtensionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBPawnExtensionComponent();

	static UVBPawnExtensionComponent* FindFor(const AActor* Actor);

	// Pawn 을 InASC 의 아바타로 연결한다. 서버면 PlayerState 의 시작 부여를 부른 뒤 통지한다.
	//   이미 같은 ASC 의 아바타면 아무것도 안 한다. 늦은 클라에서 이전 Pawn 이 아직 아바타면 그 Pawn 의 연결을 먼저 끊는다.
	void InitializeAbilitySystem(UVBAbilitySystemComponent* InASC, AActor* InOwnerActor);
	// 연결 한 회차를 해제한다. Pawn 의 UnPossessed 가 Super 전에 부른다(PlayerState, 컨트롤러가 아직 있을 때).
	void UninitializeAbilitySystem();

	UVBAbilitySystemComponent* GetAbilitySystemComponent() const { return AbilitySystemComponent.Get(); }
	// 지금 이 Pawn 이 ASC 의 아바타로 연결되어 있는가.
	bool IsAbilitySystemInitialized() const;

	// 묶고 이미 이어져 있으면 곧바로 한 번 부른다 - 늦게 묶은 쪽도 지금 상태를 놓치지 않는다.
	FDelegateHandle RegisterAndCallOnAbilitySystemInitialized(FVBAbilitySystemInitializedDelegate::FDelegate&& Delegate);
	FDelegateHandle RegisterOnAbilitySystemUninitialized(FVBAbilitySystemUninitializedDelegate::FDelegate&& Delegate);
	void UnregisterAbilitySystemDelegates(FDelegateHandle& InitializedHandle, FDelegateHandle& UninitializedHandle);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 아바타 회차에서만: 이 Pawn 에 묶인 활성 능력 취소. 부여 즉시 켜지는 상시 능력(OnSpawn)은 부여와 같은 PlayerState 수명이라 남긴다.
	static void CancelAvatarAbilities(UVBAbilitySystemComponent& ASC);

	TWeakObjectPtr<UVBAbilitySystemComponent> AbilitySystemComponent;
	FVBAbilitySystemInitializedDelegate OnAbilitySystemInitialized;
	FVBAbilitySystemUninitializedDelegate OnAbilitySystemUninitialized;
};

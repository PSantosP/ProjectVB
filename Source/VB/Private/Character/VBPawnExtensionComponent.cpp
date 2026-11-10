// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBPawnExtensionComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "GameFramework/Pawn.h"
#include "Player/VBPlayerState.h"

UVBPawnExtensionComponent::UVBPawnExtensionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

UVBPawnExtensionComponent* UVBPawnExtensionComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBPawnExtensionComponent>() : nullptr;
}

bool UVBPawnExtensionComponent::IsAbilitySystemInitialized() const
{
	const UVBAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	return ASC && ASC->GetAvatarActor_Direct() == GetOwner();
}

void UVBPawnExtensionComponent::InitializeAbilitySystem(UVBAbilitySystemComponent* InASC, AActor* InOwnerActor)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !InASC || !InOwnerActor)
	{
		return;
	}

	if (AbilitySystemComponent.Get() == InASC && InASC->GetAvatarActor_Direct() == Pawn)
	{
		return;
	}

	if (AbilitySystemComponent.IsValid())
	{
		UninitializeAbilitySystem();
	}

	// 늦은 클라: 새 Pawn 의 PlayerState 가 먼저 복제되면 이전 Pawn 이 아직 아바타다(Lyra 와 같은 경우). 이전 Pawn 의 ASC 초기화를 먼저 해제한다.
	if (AActor* ExistingAvatar = InASC->GetAvatarActor_Direct(); ExistingAvatar && ExistingAvatar != Pawn)
	{
		if (UVBPawnExtensionComponent* Previous = FindFor(ExistingAvatar))
		{
			Previous->UninitializeAbilitySystem();
		}
	}

	AbilitySystemComponent = InASC;
	InASC->InitAbilityActorInfo(InOwnerActor, Pawn);

	// 시작 부여 -> 통지 순서가 계약이다. Pawn 의 사망 판정이 통지에서 체력 변경을 구독하는데,
	//   시작 효과(GE_InitStats)가 Health 를 0 -> MaxHealth 로 올리며 변경 통지를 쏜다. 구독이 먼저면 그 0 이 스폰 즉시 사망으로 읽힌다.
	if (Pawn->HasAuthority())
	{
		if (AVBPlayerState* PS = Cast<AVBPlayerState>(InOwnerActor))
		{
			PS->GrantStartupAbilities();
		}
	}

	OnAbilitySystemInitialized.Broadcast(*InASC);
}

void UVBPawnExtensionComponent::UninitializeAbilitySystem()
{
	UVBAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	if (!ASC)
	{
		AbilitySystemComponent.Reset();
		return;
	}
	// 아바타가 이미 다른 Pawn 이면 늦은 정리다 - 그 Pawn 의 능력, 연결을 걷지 않는다.
	const bool bAvatarSession = ASC->GetAvatarActor_Direct() == GetOwner();

	// 포인터를 유지한 채 통지한다 - 기능의 종료 경로(SmartObject, 래그돌)는 Pawn 에서 ASC 를 다시 찾고, 락온은 PlayerState 를 다시 찾는다.
	OnAbilitySystemUninitialized.Broadcast(*ASC, bAvatarSession);
	if (bAvatarSession)
	{
		CancelAvatarAbilities(*ASC);
		// 소유자(PlayerState)가 있으면 아바타만 끊는다 - 부여된 능력, 효과는 소유자 수명을 따른다. 지울 때 남은 누름도 비워진다(UVBAbilitySystemComponent::InitAbilityActorInfo).
		if (ASC->GetOwnerActor())
		{
			ASC->SetAvatarActor(nullptr);
		}
		else
		{
			ASC->ClearActorInfo();
		}
	}

	AbilitySystemComponent.Reset();
}

void UVBPawnExtensionComponent::CancelAvatarAbilities(UVBAbilitySystemComponent& ASC)
{
	// 핸들을 먼저 모은다 - 취소가 끝 콜백에서 목록을 바꿀 수 있다.
	TArray<FGameplayAbilitySpecHandle> Active;
	for (const FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
	{
		if (!Spec.IsActive())
		{
			continue;
		}

		const UVBGameplayAbility* Ability = Cast<UVBGameplayAbility>(Spec.Ability);
		if (Ability && Ability->GetActivationPolicy() == EVBAbilityActivationPolicy::OnSpawn)
		{
			continue;
		}

		Active.Add(Spec.Handle);
	}

	for (const FGameplayAbilitySpecHandle& Handle : Active)
	{
		ASC.CancelAbilityHandle(Handle);
	}
}

FDelegateHandle UVBPawnExtensionComponent::RegisterAndCallOnAbilitySystemInitialized(
	FVBAbilitySystemInitializedDelegate::FDelegate&& Delegate)
{
	const FDelegateHandle Handle = OnAbilitySystemInitialized.Add(Delegate);
	if (IsAbilitySystemInitialized())
	{
		Delegate.ExecuteIfBound(*AbilitySystemComponent.Get());
	}

	return Handle;
}

FDelegateHandle UVBPawnExtensionComponent::RegisterOnAbilitySystemUninitialized(FVBAbilitySystemUninitializedDelegate::FDelegate&& Delegate)
{
	return OnAbilitySystemUninitialized.Add(MoveTemp(Delegate));
}

void UVBPawnExtensionComponent::UnregisterAbilitySystemDelegates(FDelegateHandle& InitializedHandle, FDelegateHandle& UninitializedHandle)
{
	OnAbilitySystemInitialized.Remove(InitializedHandle);
	OnAbilitySystemUninitialized.Remove(UninitializedHandle);
	InitializedHandle.Reset();
	UninitializedHandle.Reset();
}

void UVBPawnExtensionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 서버의 빙의된 Pawn 은 APawn::EndPlay/Destroyed 가 먼저 UnPossessed 를 거쳐 여기서는 비어 있다.
	//   남는 것은 클라(UnPossessed 가 없다)와 컨트롤러 없이 이어진 Pawn 이다.
	UninitializeAbilitySystem();
	Super::EndPlay(EndPlayReason);
}

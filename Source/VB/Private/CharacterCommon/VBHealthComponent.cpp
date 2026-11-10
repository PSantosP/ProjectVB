// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CharacterCommon/VBHealthComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "CharacterCommon/VBDeathSteps.h"
#include "GameFramework/Actor.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "VB/VB.h"

UVBHealthComponent::UVBHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UVBHealthComponent* UVBHealthComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBHealthComponent>() : nullptr;
}

void UVBHealthComponent::InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystem)
{
	if (InAbilitySystem && AbilitySystem.Get() == InAbilitySystem && OutOfHealthHandle.IsValid())
	{
		return;
	}

	UninitializeFromAbilitySystem();

	const AActor* Owner = GetOwner();
	if (!ensureMsgf(Owner && Owner->Implements<UVBDeathSteps>(),
			TEXT("%s: 체력 컴포넌트의 주인이 IVBDeathSteps 를 구현하지 않는다 - 듣지 않는다(이 몸은 죽지 않는다)"), *GetNameSafe(Owner))
		|| !ensureMsgf(InAbilitySystem, TEXT("%s: ASC 없이 체력 컴포넌트를 초기화했다 - 이 몸은 죽지 않는다"), *GetNameSafe(Owner)))
	{
		return;
	}

	const UVBHealthAttributeSet* Set = InAbilitySystem->GetSet<UVBHealthAttributeSet>();
	if (!ensureMsgf(Set, TEXT("%s: ASC 에 체력 세트가 없다 - 이 몸은 죽지 않는다"), *GetNameSafe(Owner)))
	{
		return;
	}

	AbilitySystem = InAbilitySystem;
	HealthSet = Set;
	OutOfHealthHandle = Set->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);
}

void UVBHealthComponent::UninitializeFromAbilitySystem()
{
	if (const UVBHealthAttributeSet* Set = HealthSet.Get())
	{
		Set->OnOutOfHealth.Remove(OutOfHealthHandle);
	}

	OutOfHealthHandle.Reset();
	HealthSet.Reset();
	AbilitySystem.Reset();
}

void UVBHealthComponent::OnUnregister()
{
	UninitializeFromAbilitySystem();
	Super::OnUnregister();
}

void UVBHealthComponent::HandleOutOfHealth(const FVBDeathFacts& WriteFacts)
{
	AActor* Owner = GetOwner();
	IVBDeathSteps* Steps = Cast<IVBDeathSteps>(Owner); // 듣기 시작할 때 확인했다
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	// 세트는 권위 쪽에서만 방송한다. 그 규율에만 기대지 않는다 - 클라에서 단계를 돌면 다음 복제가 덮을 표시만 남는다.
	if (bDeathStarted || !Steps || !ASC || !Owner->HasAuthority())
	{
		return;
	}
	// 캐릭터가 맡은 단계 안에서 같은 캐릭터에 다시 효과가 걸리면(명성 GE, 처치 효과) 그 쓰기도 여기 온다 - 첫 단계보다 먼저 올려야 return 한다.
	bDeathStarted = true;
	FVBDeathFacts Facts = WriteFacts;
	Facts.bDownedAtDeath = UVBAbilitySystemStatics::IsDowned(ASC); // 능력 취소가 Downed 를 걷기 전에
	if (const ACharacter* Body = Cast<ACharacter>(Owner))
	{
		const UCharacterMovementComponent* Move = Body->GetCharacterMovement();
		Facts.bAirborneAtDeath = Move && (Move->IsFalling() || Move->IsFlying());
	}

	VB_LOG(Log, "%s 사망 (죽인 자 %s, 의료 %d, 엎드림 %d)", *Owner->GetName(), *GetNameSafe(Facts.Instigator),
		Facts.bMedical ? 1 : 0, Facts.bDownedAtDeath ? 1 : 0);

	Steps->BeforeDeathTag(Facts);
	// CountToOwner: 태그는 모든 연결에 복제되고 개수만 소유자에게 간다. 새 활성화를 막는 것은 각 능력의 ActivationBlockedTags(State.Dead) 몫이다.
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Dead, 1, EGameplayTagReplicationState::CountToOwner);
	ASC->CancelAllAbilities();
	Steps->AfterDeathTag(Facts);
	Steps->PresentDeath(Facts);

	OnDeathCompleted.Broadcast(Facts);
	if (UVBGameplayMessageSubsystem* Bus = UVBGameplayMessageSubsystem::Get(Owner))
	{
		Bus->BroadcastMessage(VBGameplayTags::Message_Death, Facts);
	}
	else
	{
		VB_LOG(Warning, "%s: 메시지 버스가 없어 죽음 알림을 못 보낸다 - 이 몸을 노리던 AI 가 대상을 놓지 않는다", *Owner->GetName());
	}
}

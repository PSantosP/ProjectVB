// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Passive/VBPassiveComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Net/UnrealNetwork.h"
#include "Passive/VBPassiveSetDefinition.h"
#include "Player/VBPlayerStateLookup.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

UVBPassiveComponent::UVBPassiveComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBPassiveComponent* UVBPassiveComponent::FindFor(const AActor* Actor)
{
	return VBPlayerStateLookup::FindComponent<UVBPassiveComponent>(Actor);
}

EVBPassiveGrantResult UVBPassiveComponent::CanGrant(const FGameplayTag& PassiveTag) const
{
	if (!HasWriteAuthority(TEXT("Grant")))
	{
		return EVBPassiveGrantResult::Unavailable;
	}

	if (!PassiveSet || !GetAbilitySystem())
	{
		VB_LOG(Error, "패시브 %s 를 주지 못했다 - %s", *PassiveTag.ToString(), !PassiveSet ? TEXT("PassiveSet 이 비었다") : TEXT("ASC 가 없다"));
		return EVBPassiveGrantResult::Unavailable;
	}

	if (!PassiveSet->FindPassive(PassiveTag))
	{
		VB_LOG(Error, "패시브 %s 는 %s 에 없다 - 아이템의 얻기 효과가 틀렸다", *PassiveTag.ToString(), *GetNameSafe(PassiveSet.Get()));
		return EVBPassiveGrantResult::UnknownPassive;
	}

	if (State.Acquired.Contains(PassiveTag))
	{
		return EVBPassiveGrantResult::AlreadyAcquired;
	}

	return EVBPassiveGrantResult::Acquired;
}

EVBPassiveGrantResult UVBPassiveComponent::Grant(const FGameplayTag& PassiveTag)
{
	const EVBPassiveGrantResult Result = CanGrant(PassiveTag);
	if (Result != EVBPassiveGrantResult::Acquired)
	{
		return Result;
	}

	State.Acquired.Add(PassiveTag);
	SyncTags();
	VB_LOG(Log, "패시브 얻음: %s (%d개)", *PassiveTag.ToString(), State.Acquired.Num());
	NotifyPassivesChanged(false);
	return EVBPassiveGrantResult::Acquired;
}

void UVBPassiveComponent::SyncTags()
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!ASC)
	{
		VB_LOG(Error, "패시브: %s 에 ASC 가 없어 패시브 태그 %d개를 달지 못했다 - 그 능력들이 열리지 않는다", *GetNameSafe(GetOwner()), State.Acquired.Num());
		return;
	}
	// 바뀐 태그만 건드린다 - 그대로 가진 패시브의 태그가 0 을 거쳐 깜빡이면 그 순간 요구 태그 검사가 실패한다.
	//   소유자에게 복제한다 - 클라의 예측 활성화도 전투 규칙 표의 요구 태그를 본다.
	for (const FGameplayTag& Tag : AppliedTags)
	{
		if (!State.Acquired.Contains(Tag))
		{
			ASC->SetLooseGameplayTagCount(Tag, 0, EGameplayTagReplicationState::CountToOwner);
		}
	}

	for (const FGameplayTag& Tag : State.Acquired)
	{
		if (!AppliedTags.Contains(Tag))
		{
			ASC->SetLooseGameplayTagCount(Tag, 1, EGameplayTagReplicationState::CountToOwner);
		}
	}

	AppliedTags = State.Acquired;
}

void UVBPassiveComponent::OnRep_State()
{
	const bool bRestored = !bReceivedFirstState || State.SaveReadSerial != SeenSaveReadSerial;
	bReceivedFirstState = true;
	SeenSaveReadSerial = State.SaveReadSerial;
	NotifyPassivesChanged(bRestored);
}

void UVBPassiveComponent::NotifyPassivesChanged(bool bRestored)
{
	OnPassivesChanged.Broadcast(bRestored);
	OnPassivesChangedBP.Broadcast(bRestored);
}

void UVBPassiveComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner())
	{
		return;
	}

	Save.Passives.Acquired = State.Acquired;
}

void UVBPassiveComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner() || !HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}

	if (!PassiveSet)
	{
		// 목록이 없으면 무엇이 지워졌는지 알 수 없다 - 저장 목록을 그대로 유지하고(다음 저장이 지우지 않게) 태그도 그대로 단다.
		//   저장에 패시브가 없으면 잃을 것도 확인할 것도 없다 - 그때는 조용하다.
		VB_CLOG(!Save.Passives.Acquired.IsEmpty(), Error, "패시브 로드: PassiveSet 이 비었다 - 저장된 %d개를 목록 확인 없이 그대로 둔다",
			Save.Passives.Acquired.Num());
		State.Acquired = Save.Passives.Acquired;
		SyncTags();
	}
	else
	{
		State.Acquired.Reset();
		for (const FGameplayTag& PassiveTag : Save.Passives.Acquired)
		{
			// 목록에서 지운 패시브만 버린다.
			if (!PassiveSet->FindPassive(PassiveTag))
			{
				VB_LOG(Warning, "패시브 로드: %s 는 패시브 목록에 없다 - 버린다", *PassiveTag.ToString());
				continue;
			}

			State.Acquired.AddUnique(PassiveTag);
		}
		// 로드로 빠진 패시브의 능력이 이미 돌고 있어도 끊지 않는다 - 로드는 폰을 새로 세운다.
		SyncTags();
	}

	++State.SaveReadSerial;
	VB_LOG(Log, "패시브 로드: %d개", State.Acquired.Num());
	NotifyPassivesChanged(true);
}

void UVBPassiveComponent::InitializeComponent()
{
	Super::InitializeComponent();
	// 쓰는 쪽(서버)에서만 소리낸다 - 클라는 목록을 읽지 않는다. 주기, 불러오기가 실제로 막히면 그 자리에서 Error 를 낸다.
	VB_CLOG(!PassiveSet && GetOwner() && GetOwner()->HasAuthority() && !GetOwner()->IsTemplate(), Warning,
		"패시브: %s 에 PassiveSet 이 비었다 - 귀속 아이템이 패시브를 주지 못한다", *GetNameSafe(GetOwner()));
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBPassiveComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBPassiveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVBPassiveComponent, State, COND_OwnerOnly);
}

bool UVBPassiveComponent::HasWriteAuthority(const TCHAR* What) const
{
	// 패시브는 소유자의 ASC 에 태그를 쓰므로 다른 저장소와 달리 소유자 없이는 쓰지 못한다.
	if (!GetOwner())
	{
		VB_LOG(Warning, "패시브 %s 을 주인 없이 불렀다 - 무시한다", What);
		return false;
	}

	return VBServerWrite::IsAllowed(*this, TEXT("패시브"), What);
}

UAbilitySystemComponent* UVBPassiveComponent::GetAbilitySystem() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

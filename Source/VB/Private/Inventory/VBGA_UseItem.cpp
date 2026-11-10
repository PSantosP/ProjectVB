// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBGA_UseItem.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitAbilityActivate.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "VB/VB.h"

UVBGA_UseItem::UVBGA_UseItem()
{
	// 입력 GA 가 아니라 InputTag 없음. 정책은 베이스 기본값(OnSpawn 이면 부여 즉시 켜진다).
	// 비용, 쿨다운 없음 - 쓰는 값은 아이템 수다.

	// 이름표. 막고 막히는 관계는 전투 규칙 표(DA_CombatRules)에 있다. 회피, 가드는 HandleDefenseActivated 가 끊는다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Item_Use));

	// 부여하면 ASC 가 AbilityTriggers 를 이벤트 표에 등록한다.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Item_Use;
	AbilityTriggers.Add(Trigger);
}

bool UVBGA_UseItem::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character)
	{
		return false;
	}
	// 공중이면 몽타주가 떨어지는 캐릭터에 얹히므로 막는다.
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	return Movement && Movement->IsMovingOnGround();
}

bool UVBGA_UseItem::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}

	const UVBItemDefinition* Item = Payload ? Cast<UVBItemDefinition>(Payload->OptionalObject.Get()) : nullptr;
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	if (!Usable || !Usable->UseMontage)
	{
		// BeginUse 는 몽타주 있는 아이템만 보낸다. 여기 오면 배선 사고다.
		VB_LOG(Warning, "%s: 사용 요청의 아이템(%s)에 사용 몽타주가 없다 - 켜지 않는다", *GetName(), *GetNameSafe(Item));
		return false;
	}
	// 캐릭터를 움직이기 전에 거절한다 - 움직인 뒤 거절하면 했는데 안 된 것으로 보인다.
	const UVBInventoryComponent* Inventory = ActorInfo ? UVBInventoryComponent::FindFor(ActorInfo->AvatarActor.Get()) : nullptr;
	return Inventory && Inventory->CanUseItem(Item);
}

void UVBGA_UseItem::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bMarkerReached = false;
	ActiveItem = TriggerEventData ? Cast<UVBItemDefinition>(TriggerEventData->OptionalObject.Get()) : nullptr;
	const FVBItemFragment_Usable* Usable = ActiveItem ? ActiveItem->FindFragment<FVBItemFragment_Usable>() : nullptr;
	ActiveMontage = Usable ? Usable->UseMontage.Get() : nullptr;
	if (!ActiveMontage)
	{
		// 아이템을 못 찾은 소유 클라이언트뿐이다. 몽타주 없이 서버의 끝을 기다린다.
		VB_LOG(Warning, "%s: 사용 요청의 아이템(%s)을 풀지 못했거나 몽타주가 없다 - 이 쪽에서는 틀지 않는다", *GetName(), *GetNameSafe(ActiveItem));
		EndOnServer(true, TEXT("몽타주 없음"));
		return;
	}

	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		// 첫 프레임 표식도 받게 몽타주보다 먼저 건다. 버린 표식이 듣기를 끝내지 않게 OnlyTriggerOnce=false.
		UAbilityTask_WaitGameplayEvent* MarkerTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, VBGameplayTags::Event_Item_UseMarker, nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
		MarkerTask->EventReceived.AddDynamic(this, &UVBGA_UseItem::HandleMarker);
		MarkerTask->ReadyForActivation();

		// 실제 피해를 직접 듣는다 - 피격 반응 GA 가 안 켜지는 피격(슈퍼아머 등)에도 끊겨야 한다.
		UAbilityTask_WaitGameplayEvent* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, VBGameplayTags::Event_Combat_Hit, nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
		HitTask->EventReceived.AddDynamic(this, &UVBGA_UseItem::HandleHit);
		HitTask->ReadyForActivation();

		// 회피, 가드(Ability.Defense 자식 포함)가 켜지면 끊는다 - 끊김 조건을 이 파일 한 곳에 모은다.
		//   알림이 그 GA 본문보다 먼저 와서 가드가 곧바로 끝나도 사용은 끊긴다(방어 버튼 = 사용 취소).
		UAbilityTask_WaitAbilityActivate* DefenseTask = UAbilityTask_WaitAbilityActivate::WaitForAbilityActivate(
			this, VBGameplayTags::Ability_Defense, FGameplayTag(), /*IncludeTriggeredAbilities=*/false, /*TriggerOnce=*/true);
		DefenseTask->OnActivate.AddDynamic(this, &UVBGA_UseItem::HandleDefenseActivated);
		DefenseTask->ReadyForActivation();
	}

	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		SheatheIfWanted(*Usable);
	}

	// 몽타주는 서버, 소유 클라이언트 둘 다 튼다.
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, ActiveMontage, 1.0f, NAME_None, /*bStopWhenAbilityEnds=*/true);
	MontageTask->OnCompleted.AddDynamic(this, &UVBGA_UseItem::HandleMontageEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &UVBGA_UseItem::HandleMontageEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &UVBGA_UseItem::HandleMontageEnded);
	MontageTask->OnCancelled.AddDynamic(this, &UVBGA_UseItem::HandleMontageEnded);
	MontageTask->ReadyForActivation();
}

void UVBGA_UseItem::HandleMarker(FGameplayEventData Payload)
{
	if (!IsActive())
	{
		return;
	}

	if (bMarkerReached)
	{
		// 검증을 안 거친 자산의 두 번째 표식. 두 번 확정하지 않고 경고한다.
		VB_LOG(Warning, "%s: %s 에 사용 표식이 두 번 왔다 - 두 번째는 버린다", *GetName(), *GetNameSafe(ActiveMontage));
		return;
	}
	// 표식은 몽타주를 싣지 않아 내 몽타주의 인스턴스가 있는지로 가른다.
	//   블렌드아웃이 시작되는 틱의 표식도 받으려고 Montage_IsPlaying 이 아니라 GetInstanceForMontage 로 본다.
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const UAnimInstance* Anim = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (!Anim || !Anim->GetInstanceForMontage(ActiveMontage))
	{
		return;
	}

	bMarkerReached = true;
	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetAvatarActorFromActorInfo());
	if (!Inventory || !Inventory->ConfirmUse(ActiveItem))
	{
		// 확정 거절. 끊으면 손이 튀므로 몽타주는 끝까지 두고 소모, 효과만 안 낸다.
		VB_LOG(Log, "%s: %s 확정 거절(그 사이 수·조건이 바뀜) - 동작만 끝까지", *GetName(), *GetNameSafe(ActiveItem));
	}
}

void UVBGA_UseItem::HandleHit(FGameplayEventData Payload)
{
	// 확정 뒤의 피격으로는 끊지 않는다 - 효과는 이미 났다.
	if (!bMarkerReached)
	{
		EndOnServer(true, TEXT("표식 전 피격"));
	}
}

void UVBGA_UseItem::HandleDefenseActivated(UGameplayAbility* ActivatedAbility)
{
	EndOnServer(true, bMarkerReached ? TEXT("확정 뒤 회피·가드") : TEXT("표식 전 회피·가드"));
}

void UVBGA_UseItem::HandleMontageEnded()
{
	if (!bMarkerReached && IsActive() && GetCurrentActorInfo() && GetCurrentActorInfo()->IsNetAuthority())
	{
		// 끊김이면 흔한 경로, 끝까지 돌았다면 표식 없는 자산이다. 둘을 가르지 못해 Log 로 둔다.
		VB_LOG(Log, "%s: %s 가 표식 없이 끝났다 - 끊겼거나 표식이 없다(IsDataValid 확인)", *GetName(), *GetNameSafe(ActiveMontage));
	}

	EndOnServer(!bMarkerReached, TEXT("몽타주 끝"));
}

void UVBGA_UseItem::SheatheIfWanted(const FVBItemFragment_Usable& Usable)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UVBWeaponStateComponent* Weapon = Avatar ? Avatar->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
	// 이미 수납돼 있으면 손대지 않는다 - 돌려놓는 것도 우리 몫이 아니다.
	if (!Usable.bSheatheWeaponDuringUse || !Weapon || !Weapon->IsWeaponSummoned() || Weapon->IsTempSheathed())
	{
		return;
	}

	Weapon->TempSheatheForParkour();
	bSheathedWeapon = true;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(VBGameplayTags::State_Weapon_TempSheathed);
	}
}

void UVBGA_UseItem::EndOnServer(bool bWasCancelled, const TCHAR* Why)
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!IsActive() || !ActorInfo || !ActorInfo->IsNetAuthority())
	{
		// 클라가 먼저 끝내면 서버가 표식 전에 취소될 수 있다. 끝은 서버가 정한다.
		return;
	}

	VB_LOG(Log, "%s: %s 사용 끝 - %s (확정 %s)", *GetName(), *GetNameSafe(ActiveItem), Why, bMarkerReached ? TEXT("함") : TEXT("안 함"));
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, bWasCancelled);
}

void UVBGA_UseItem::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 끝 경로가 여럿이라 정리는 여기 한 곳에서 한다.
	if (bSheathedWeapon)
	{
		bSheathedWeapon = false;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Weapon_TempSheathed);
		}
		// 죽어서 끝났으면 돌려놓지 않는다 - 시체 손에 칼이 돌아온다.
		AActor* Avatar = GetAvatarActorFromActorInfo();
		if (!UVBAbilitySystemStatics::IsActorDead(Avatar))
		{
			if (UVBWeaponStateComponent* Weapon = Avatar ? Avatar->FindComponentByClass<UVBWeaponStateComponent>() : nullptr)
			{
				Weapon->RestoreFromTempSheathe();
			}
		}
	}

	ActiveItem = nullptr;
	ActiveMontage = nullptr;
	bMarkerReached = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

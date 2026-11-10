// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Character/VBWeaponStateComponent.h"

#include "AbilitySystemGlobals.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Character/VBStartingLoadout.h"
#include "EngineUtils.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponChangeActing.h"
#include "Character/VBWeaponDefinitionList.h"
#include "CharacterCommon/VBHitReceivedMessage.h"
#include "AbilitySystem/VBStatBonus.h"
#include "Inventory/Enhancement/VBEnhancementComponent.h"
#include "Character/VBTargetable.h"
#include "Character/VBTargetLockComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "Data/VBWeaponStateConfig.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "VB/VB.h"
#include "TimerManager.h"
#include "CoreLoop/VBServerWrite.h"


UVBWeaponStateComponent::UVBWeaponStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBWeaponStateComponent* UVBWeaponStateComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
}

UAbilitySystemComponent* UVBWeaponStateComponent::FindOwnerAbilitySystem() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

void UVBWeaponStateComponent::InitializeComponent()
{
	Super::InitializeComponent();
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	// 시작값. 이어하기라면 곧 저장 복원(SetOwnedWeapons)이 덮는다.
	UWorld* World = GetWorld();
	const AVBStartingLoadout* Loadout = nullptr;
	int32 LoadoutCount = 0;
	if (World)
	{
		for (TActorIterator<AVBStartingLoadout> It(World); It; ++It)
		{
			Loadout = Loadout ? Loadout : *It;
			++LoadoutCount;
		}
	}

	if (LoadoutCount > 1)
	{
		VB_LOG(Warning, "%s: 시작 소지(AVBStartingLoadout)가 맵에 %d 개 - 첫째(%s)를 쓴다", *GetNameSafe(GetOwner()), LoadoutCount,
			*GetNameSafe(Loadout));
	}

	TArray<EVBWeaponType> Start;
	if (Loadout)
	{
		Start = Loadout->StartingWeapons;
	}
	else
	{
		Start = VBWeaponDefinitionList::GetKnownWeapons(WeaponDefinitions);
	}

	VBWeaponDefinitionList::WarnAboutList(WeaponDefinitions, GetOwner());
	SetOwnedWeapons(Start);
}

const UVBWeaponDefinition* UVBWeaponStateComponent::FindDefinitionForSlot(int32 Slot) const
{
	return VBWeaponDefinitionList::FindForSlot(WeaponDefinitions, OwnedWeapons, Slot, GetOwner());
}

const UVBWeaponDefinition* UVBWeaponStateComponent::FindDefinition(EVBWeaponType InWeaponType) const
{
	return VBWeaponDefinitionList::Find(WeaponDefinitions, InWeaponType);
}

bool UVBWeaponStateComponent::IsWeaponOwned(EVBWeaponType InWeaponType) const
{
	return InWeaponType != EVBWeaponType::None && OwnedWeapons.Contains(InWeaponType);
}

EVBWeaponType UVBWeaponStateComponent::GetResummonWeaponType() const
{
	return VBWeaponOwnership::PickResummon(CurrentWeaponType, LastUsedWeaponType, DefaultWeaponType, OwnedWeapons);
}

EVBWeaponGrantResult UVBWeaponStateComponent::GrantWeapon(EVBWeaponType InWeaponType)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		VB_LOG(Warning, "%s: 무기 주기는 서버에서만 - 클라 호출은 버린다", *GetNameSafe(GetOwner()));
		return EVBWeaponGrantResult::Failed;
	}

	const EVBWeaponGrantResult Result = VBWeaponOwnership::Grant(OwnedWeapons,
		VBWeaponDefinitionList::GetKnownWeapons(WeaponDefinitions), InWeaponType);
	switch (Result)
	{
	case EVBWeaponGrantResult::Failed:
		VB_LOG(Warning, "%s: 무기 주기 실패 - 정의(WeaponDefinitions)가 없는 무기(%d)", *GetNameSafe(GetOwner()), (int32)InWeaponType);
		break;
	case EVBWeaponGrantResult::NewlyOwned:
		VB_LOG(Log, "%s: 무기를 새로 가졌다 - %d (가진 수 %d)", *GetNameSafe(GetOwner()), (int32)InWeaponType, OwnedWeapons.Num());
		VBWeaponDefinitionList::WarnAboutSlotClash(WeaponDefinitions, OwnedWeapons, GetOwner());
		OnOwnedWeaponsChanged.Broadcast();
		break;
	case EVBWeaponGrantResult::AlreadyOwned:
		break;
	}

	return Result;
}

void UVBWeaponStateComponent::SetOwnedWeapons(const TArray<EVBWeaponType>& InWeapons)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	OwnedWeapons.Reset();
	for (const EVBWeaponType Type : InWeapons)
	{
		if (!FindDefinition(Type))
		{
			VB_LOG(Warning, "%s: 가진 무기 목록의 %d 는 정의(WeaponDefinitions)가 없어 버린다", *GetNameSafe(GetOwner()), (int32)Type);
			continue;
		}

		OwnedWeapons.AddUnique(Type);
	}

	VBWeaponDefinitionList::WarnAboutSlotClash(WeaponDefinitions, OwnedWeapons, GetOwner());
	OnOwnedWeaponsChanged.Broadcast();
}

bool UVBWeaponStateComponent::RevokeWeapon(EVBWeaponType InWeaponType)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		VB_LOG(Warning, "%s: 무기 거두기는 서버에서만 - 클라 호출은 버린다", *GetNameSafe(GetOwner()));
		return false;
	}
	// 손에 든 채 거두면 그 무기의 능력 세트가 남는다 - 먼저 넣는다.
	if (CurrentWeaponType == InWeaponType && IsWeaponSummoned())
	{
		ServerSheatheWeapon_Implementation();
	}

	if (!VBWeaponOwnership::Revoke(OwnedWeapons, InWeaponType))
	{
		return false;
	}

	VB_LOG(Log, "%s: 무기를 거뒀다 - %d (가진 수 %d)", *GetNameSafe(GetOwner()), (int32)InWeaponType, OwnedWeapons.Num());
	const EVBWeaponType Selection = VBWeaponOwnership::SelectionAfterRevoke(CurrentWeaponType, InWeaponType);
	if (Selection != CurrentWeaponType)
	{
		CurrentWeaponType = Selection;
		OnLocomotionStateChanged.Broadcast(LocomotionState, CurrentWeaponType);
	}

	OnOwnedWeaponsChanged.Broadcast();
	return true;
}

void UVBWeaponStateComponent::OnRep_OwnedWeapons()
{
	OnOwnedWeaponsChanged.Broadcast();
}

const UVBWeaponStateConfig* UVBWeaponStateComponent::GetWeaponStateConfig() const
{
	// 할당된 config 우선, 없으면 클래스 CDO 디폴트(절대 null 아님).
	return WeaponStateConfig ? WeaponStateConfig : GetDefault<UVBWeaponStateConfig>();
}

void UVBWeaponStateComponent::CancelGuardAbility()
{
	// 서버 권위에서만. 가드 상태(GuardStartServerTime)가 서버 진실이라 클라 호출은 무의미하다.
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	// ASC 는 캐시된 소유 캐릭터에서 찾는다(약참조라 IsValid()/Get()).
	UAbilitySystemComponent* ASC =
		OwnerCharacter.IsValid() ? OwnerCharacter->GetAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return;
	}

	// CancelAbilities 는 대상 GA 의 AssetTags 로 찾는다 - 가드 GA 생성자의 SetAssetTags 가 전제다.
	FGameplayTagContainer GuardTags;
	GuardTags.AddTag(VBGameplayTags::Ability_Defense_Guard);
	ASC->CancelAbilities(&GuardTags);
}

void UVBWeaponStateComponent::SummonWeapon(EVBWeaponType InWeaponType)
{
	ServerSummonWeapon(InWeaponType);
}

void UVBWeaponStateComponent::SheatheWeapon()
{
	ServerSheatheWeapon();
}

void UVBWeaponStateComponent::EnterCombat()
{
	// 권한 확인은 아래 두 작성 함수가 한다(클라에서 부르면 둘 다 아무것도 안 한다). 소유 캐릭터는 BeginPlay 에서 받는다 - 그 전의 상태 전환은 CMC 적용이 안전하지 않다.
	if (!OwnerCharacter.IsValid())
	{
		return;
	}
	// 자동 발도는 없다 - 평상이면 전투, 무기 넣음(상태2)으로 간다. 발도는 명시 행위(Tab, 슬롯 키)나 공격, 가드 누름의 DrawForPress 만 한다.
	if (LocomotionState == EVBLocomotionState::Unarmed)
	{
		SetLocomotionState(EVBLocomotionState::CombatSheathed);
	}
	else
	{
		ResetCombatTimer(); // 이미 전투(상태2, 3) - 이탈 타이머만 리셋
	}
}

void UVBWeaponStateComponent::ResetCombatTimer()
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	if (IsInCombat())
	{
		GetWorld()->GetTimerManager().ClearTimer(CombatExitTimerHandle);
		ArmCombatExitTimer();
	}
}

void UVBWeaponStateComponent::ArmCombatExitTimer()
{
	GetWorld()->GetTimerManager().SetTimer(CombatExitTimerHandle,
		this, &ThisClass::TryCombatExit, GetWeaponStateConfig()->CombatExitDelay, false);
}

void UVBWeaponStateComponent::StopCombatTimer()
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		// ClearTimer 여야 한다 - 파쿠르 중 사망 뒤 임시수납 복귀가 부르는 UnPauseTimer 가 무효 핸들이라 타이머가 되살아나지 않는다.
		World->GetTimerManager().ClearTimer(CombatExitTimerHandle);
	}
}

void UVBWeaponStateComponent::TempSheatheForParkour()
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	if (bIsTempSheathed || !IsWeaponSummoned())
	{
		return;
	}

	CancelGuardAbility();
	bIsTempSheathed = true;
	StateBeforeTempSheathe = LocomotionState;
	WeaponTypeBeforeTempSheathe = CurrentWeaponType;
	GetWorld()->GetTimerManager().PauseTimer(CombatExitTimerHandle);
	VB_LOG_CTX(this, Verbose, "[WeaponState] source=TempSheathe");
	ApplyLocalLocomotionState();
}

void UVBWeaponStateComponent::RestoreFromTempSheathe()
{
	if (!VBServerWrite::HasOwnerAuthority(*this) || !bIsTempSheathed)
	{
		return;
	}

	bIsTempSheathed = false;
	GetWorld()->GetTimerManager().UnPauseTimer(CombatExitTimerHandle);
	VB_LOG_CTX(this, Verbose, "[WeaponState] source=RestoreTempSheathe");
	ApplyLocalLocomotionState();
}

void UVBWeaponStateComponent::EnsureWeaponSummoned(EVBWeaponType InWeaponType)
{
	if (IsWeaponSummoned())
	{
		return;
	}

	SummonWeapon(InWeaponType);
}

EVBPressDraw UVBWeaponStateComponent::DrawForPress(const FGameplayTag& InputTag)
{
	if (IsWeaponSummoned())
	{
		return EVBPressDraw::InHand;
	}

	const EVBWeaponType Weapon = VBWeaponOwnership::PickPressDraw(CurrentWeaponType, LastUsedWeaponType, DefaultWeaponType, OwnedWeapons);
	if (Weapon == EVBWeaponType::None)
	{
		return EVBPressDraw::NoOwnedWeapon;
	}

	const UAbilitySystemComponent* ASC = FindOwnerAbilitySystem();
	const UVBWeaponDefinition* Definition = FindDefinition(Weapon);
	const UVBAbilitySet* Set = Definition ? Definition->GetAbilitySet() : nullptr;
	if (!ASC || !Set || !GetOwner()->HasAuthority())
	{
		return EVBPressDraw::NotNow;
	}

	bool bCanStart = false;
	for (const FVBAbilitySetAbility& Entry : Set->GetGrantedAbilities())
	{
		const UVBGameplayAbility* Ability = Entry.Ability ? Cast<UVBGameplayAbility>(Entry.Ability->GetDefaultObject()) : nullptr;
		bCanStart |= Ability && Ability->InputTag.MatchesTagExact(InputTag) && Ability->DoesAbilitySatisfyTagRequirements(*ASC);
	}

	if (!bCanStart)
	{
		return EVBPressDraw::NotNow;
	}
	// 서버의 발도는 동기라 세트가 이 자리에서 부여된다 - 호출자가 같은 누름을 ASC 에 전달하면 새 스펙이 받는다.
	SummonWeapon(Weapon);
	return IsWeaponSummoned() ? EVBPressDraw::Drawn : EVBPressDraw::NotNow;
}

void UVBWeaponStateComponent::BeginAvatarSession()
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}
	// 강화 단계는 PlayerState 에 있다 - 빙의해 있는 동안 그 강화 컴포넌트의 단계 변경을 듣는다(다시 빙의하면 한 번만 묶는다).
	UVBEnhancementComponent* Enhancement = UVBEnhancementComponent::FindFor(GetOwner());
	if (Enhancement && Enhancement != BoundEnhancement.Get())
	{
		if (UVBEnhancementComponent* Previous = BoundEnhancement.Get())
		{
			Previous->OnLevelChanged.Remove(EnhancementChangedHandle);
		}

		EnhancementChangedHandle = Enhancement->OnLevelChanged.AddUObject(this, &ThisClass::HandleEnhancementChanged);
		BoundEnhancement = Enhancement;
	}

	if (IsWeaponSummoned() && WeaponSetGrant.IsEmpty())
	{
		GiveWeaponSet(CurrentWeaponType);
	}
}

void UVBWeaponStateComponent::EndAvatarSession()
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	TakeWeaponSet();
	if (UVBEnhancementComponent* Enhancement = BoundEnhancement.Get())
	{
		Enhancement->OnLevelChanged.Remove(EnhancementChangedHandle);
	}

	BoundEnhancement.Reset();
	EnhancementChangedHandle.Reset();
}


void UVBWeaponStateComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<AVBCharacter>(GetOwner());
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	if (UVBGameplayMessageSubsystem* Bus = UVBGameplayMessageSubsystem::Get(this))
	{
		HitListener = Bus->RegisterListener(VBGameplayTags::Message_Combat_HitReceived, this, &ThisClass::HandleHitReceived);
	}
	else
	{
		VB_LOG(Warning, "%s: 메시지 버스가 없어 맞은 알림을 못 듣는다 - 맞아도 전투 자세로 바뀌지 않는다", *GetNameSafe(GetOwner()));
	}
}

void UVBWeaponStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HitListener.Unregister();
	EndAvatarSession();
	Super::EndPlay(EndPlayReason);
}

void UVBWeaponStateComponent::HandleHitReceived(FGameplayTag Channel, const FVBHitReceivedMessage& Hit)
{
	if (Hit.Victim == GetOwner())
	{
		EnterCombat();
	}
}

void UVBWeaponStateComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBWeaponStateComponent, LocomotionState, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBWeaponStateComponent, CurrentWeaponType, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBWeaponStateComponent, bIsTempSheathed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION(UVBWeaponStateComponent, OwnedWeapons, COND_OwnerOnly);
}

void UVBWeaponStateComponent::ServerSheatheWeapon_Implementation()
{
	// 무기On(ArmedCombat)이 아니면 납도할 것이 없다
	if (!IsWeaponSummoned())
	{
		return;
	}

	// 납도도 가드를 끊는다 - 세트 회수보다 먼저(끊지 않으면 무기를 치운 채 패링, 블록 판정이 남는다).
	CancelGuardAbility();

	// 납도 연기용 - 상태 변경 전에 손에 들려 있던 무기(From)를 잡아 둔다.
	const EVBWeaponType FromType = CurrentWeaponType;

	// 납도 = 무기만 끄고 전투는 유지, CurrentWeaponType 은 재발도용으로 남긴다. 세트 회수는 SetLocomotionState, 이탈은 타이머.
	SetLocomotionState(EVBLocomotionState::CombatSheathed);

	// 납도 연기 - SheatheMontage 가 없는 무기는 소환 VFX 만 낸다.
	MulticastPlayWeaponChangeActing(FromType, EVBWeaponType::None, /*bCombatStance=*/true);

	// 납도 뒤 경계 자세를 유지하고 전투 이탈 타이머를 새로 감는다(납도 시점부터 온전한 경계 시간).
	ResetCombatTimer();
}

bool UVBWeaponStateComponent::ServerSheatheWeapon_Validate()
{
	// 무기On(ArmedCombat) 상태에서만 납도 RPC 허용
	return IsWeaponSummoned();
}

void UVBWeaponStateComponent::ServerSummonWeapon_Implementation(EVBWeaponType InWeaponType)
{
	if (CurrentWeaponType == InWeaponType && IsWeaponSummoned())
	{
		VB_LOG_CTX(this, Verbose, "[WeaponState] source=SummonNoOp type=%d", (int32)InWeaponType);
		return;
	}

	// 가진 무기만 꺼낸다 - 꺼내는 길 전부가 지나는 관문. 막힌 요청은 상태를 하나도 바꾸지 않는다.
	if (!IsWeaponOwned(InWeaponType))
	{
		VB_LOG_CTX(this, Log, "[WeaponState] source=SummonRejected type=%d", (int32)InWeaponType);
		return;
	}

	VB_LOG_CTX(this, Log, "[WeaponState] source=SummonAccepted requested=%d current=%d", (int32)InWeaponType, (int32)CurrentWeaponType);

	// 무기 교체는 가드를 끊는다 - 슬롯 키는 GA 를 거치지 않으므로 발도, 교체의 단일 관문인 여기서.
	//   교체 몽타주가 같은 슬롯 그룹이라 가드 End 섹션의 위치 복귀는 대부분 끊긴다(알려진 잔여).
	CancelGuardAbility();

	// 전환 연기용 변경 전 무기 - From 은 손에 들려 있던 무기(칼집 상태 발도면 None).
	const bool bWasWeaponOn = IsWeaponSummoned();
	const EVBWeaponType FromType = bWasWeaponOn ? CurrentWeaponType : EVBWeaponType::None;

	if (bWasWeaponOn)
	{
		// 무기On 유지 중 타입 스왑 - 상태 무변경이라 SetLocomotionState 세트 규칙이 안 돌므로 여기서 새 무기 세트로 바꾼다(앞 세트는 GiveWeaponSet 이 거둔다)
		CurrentWeaponType = InWeaponType;
		GiveWeaponSet(InWeaponType);
		// 무기 교체도 전투 행위 - 이탈 타이머 리셋
		ResetCombatTimer();
		ApplyLocalLocomotionState(); // L1 armed swap
	}
	else
	{
		// 발도 = 무기를 들고 전투 자세. 세트 부여는 SetLocomotionState - CurrentWeaponType 을 먼저 세운다.
		CurrentWeaponType = InWeaponType;
		SetLocomotionState(EVBLocomotionState::ArmedCombat);
	}
	// 마지막 사용 무기 기록 - 평상 복귀 후 Tab 재발도가 이 무기를 다시 꺼낸다.
	LastUsedWeaponType = InWeaponType;

	// 전환 연기(짝 몽타주 + 소환 VFX). bCombatStance = 변경 전에 무기를 들었나.
	MulticastPlayWeaponChangeActing(FromType, InWeaponType, bWasWeaponOn);
}

bool UVBWeaponStateComponent::ServerSummonWeapon_Validate(EVBWeaponType InWeaponType)
{
	// 선언된 enum 값만 통과(None 포함). false 는 연결 종료라 상한 리터럴을 두지 않는다.
	//   IsValidEnumValue 는 _MAX 를 거르지 않는다 - EVBWeaponType 에 _MAX 가 없어 안전하다.
	const UEnum* WeaponEnum = StaticEnum<EVBWeaponType>();
	return WeaponEnum && WeaponEnum->IsValidEnumValue(static_cast<int64>(InWeaponType));
}

void UVBWeaponStateComponent::MulticastPlayWeaponChangeActing_Implementation(EVBWeaponType FromType, EVBWeaponType ToType,
	bool bCombatStance)
{
	// 코스메틱 전용 - 데디 서버는 스킵, 게임 상태는 일절 건드리지 않는다.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (!OwnerCharacter.IsValid() || !OwnerCharacter->GetMesh())
	{
		return;
	}

	const FVBWeaponChangeRequest Request{FromType, ToType, bCombatStance};
	const UVBWeaponDefinition* Definition = FindDefinition(VBWeaponChangeActing::GetDataWeapon(Request));
	VBWeaponChangeActing::Play(*OwnerCharacter, Definition, Request, GetWeaponStateConfig()->ChangeActingMaxSpeed);
}


void UVBWeaponStateComponent::OnRep_LocomotionState()
{
	VB_LOG_CTX(this, Verbose, "[WeaponState] source=OnRep");
	ApplyLocalLocomotionState();
}

void UVBWeaponStateComponent::ApplyLocalLocomotionState()
{
	const EVBLocomotionState EffectiveState = GetEffectiveLocomotionState();
	const EVBWeaponType EffectiveType = bIsTempSheathed ? EVBWeaponType::None : CurrentWeaponType;
	VB_LOG_CTX(this, Verbose, "[WeaponState] apply rawState=%d rawType=%d temp=%d effectiveState=%d effectiveType=%d",
		(int32)LocomotionState, (int32)CurrentWeaponType, bIsTempSheathed, (int32)EffectiveState, (int32)EffectiveType);
	OnLocomotionStateChanged.Broadcast(EffectiveState, EffectiveType);
}

void UVBWeaponStateComponent::SetLocomotionState(EVBLocomotionState NewState)
{
	// 서버 전용 - 상태의 단일 작성자라 권한 확인도 여기서 한다(EnterCombat 은 클라에서도 불린다).
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	if (LocomotionState == NewState)
	{
		return;
	}

	VB_LOG_CTX(this, Log, "[WeaponState] source=SetState old=%d new=%d", (int32)LocomotionState, (int32)NewState);
	EVBLocomotionState OldState = LocomotionState;
	LocomotionState = NewState;

	// 무기 세트(능력, 장착 태그)를 주고 거두는 곳은 여기 하나 - 무기를 든 채 타입만 바꾸는 것은 ServerSummonWeapon.
	if (OldState == EVBLocomotionState::ArmedCombat && NewState != EVBLocomotionState::ArmedCombat)
	{
		TakeWeaponSet();
	}
	else if (NewState == EVBLocomotionState::ArmedCombat && OldState != EVBLocomotionState::ArmedCombat)
	{
		// 호출자(ServerSummonWeapon)가 CurrentWeaponType 을 먼저 세팅해 두는 계약
		GiveWeaponSet(CurrentWeaponType);
	}

	// "선택된 무기" 기억은 전투를 떠날 때만 비운다 - 상태2(전투, 무기Off)는 재발도를 위해 유지한다.
	if (NewState == EVBLocomotionState::Unarmed)
	{
		CurrentWeaponType = EVBWeaponType::None;
	}

	// 전투 타이머 - 전투(상태2, 3) 진입/전이 시 이탈 타이머 (재)가동, 평상 복귀 시 해제.
	if (NewState != EVBLocomotionState::Unarmed)
	{
		ArmCombatExitTimer();
	}
	else
	{
		GetWorld()->GetTimerManager().ClearTimer(CombatExitTimerHandle);
	}

	ApplyLocalLocomotionState();
}

void UVBWeaponStateComponent::TryCombatExit()
{
	// 아직 떠나지 않으면 타이머를 다시 건다 - 멈춘 뒤 적, 락온이 없으면 평상 복귀.
	if (!VBWeaponStateRules::ShouldExitCombat(GatherCombatExitFacts()))
	{
		ArmCombatExitTimer();
		return;
	}

	// 전투 이탈 = 즉시 평상 복귀 - 상태3에서도 Unarmed 로 바로 가고 납도와 이탈이 함께 일어난다.
	const bool bWasWeaponOn = IsWeaponSummoned();
	const EVBWeaponType FromType = CurrentWeaponType; // SetLocomotionState(Unarmed)가 클리어하므로 선캡처
	SetLocomotionState(EVBLocomotionState::Unarmed);

	// 무기를 든 채 이탈했다면 납도 연기를 낸다(SheatheMontage 가 없는 무기는 VFX 만).
	if (bWasWeaponOn)
	{
		MulticastPlayWeaponChangeActing(FromType, EVBWeaponType::None, /*bCombatStance=*/true);
	}
}

FVBCombatExitFacts UVBWeaponStateComponent::GatherCombatExitFacts() const
{
	FVBCombatExitFacts Facts;
	const UVBTargetLockComponent* LockComponent = UVBTargetLockComponent::FindFor(GetOwner());
	Facts.bLockedOn = LockComponent && LockComponent->IsLockedOn();
	// 락온 중이면 겹침 질의를 하지 않는다 - 답이 이미 정해졌다.
	Facts.bEnemiesNearby = !Facts.bLockedOn && AreEnemiesNearby();
	Facts.bMoving = OwnerCharacter.IsValid()
		&& VBWeaponStateRules::IsActiveSpeed(OwnerCharacter->GetVelocity().Size2D(), GetWeaponStateConfig()->CombatExitIdleSpeedThreshold);
	return Facts;
}

bool UVBWeaponStateComponent::AreEnemiesNearby() const
{
	if (!OwnerCharacter.IsValid())
	{
		return false;
	}
	
	FCollisionShape Shape = FCollisionShape::MakeSphere(GetWeaponStateConfig()->EnemyProximityRadius);
	TArray<FOverlapResult> Overlaps;

	GetWorld()->OverlapMultiByChannel(Overlaps,
		OwnerCharacter->GetActorLocation(),
		FQuat::Identity,
		EnemyTraceChannel,
		Shape);

	// IVBTargetable 인터페이스로 필터를 거른다
	for (auto& Overlap : Overlaps)
	{
		if (IVBTargetable* Targetable = Cast<IVBTargetable>(Overlap.GetActor()))
		{
			if (Targetable->IsTargetable())
			{
				return true;
			}
		}
	}

	return false;
}

const TArray<TObjectPtr<UVBFinisherConfig>>* UVBWeaponStateComponent::GetFinisherConfigs() const
{
	// 조회 키는 CurrentWeaponType 이다 - 처형은 무기를 든 상태에서만 걸리므로 「선택된 무기」가 곧 「든 무기」다.
	const UVBWeaponDefinition* Definition = GetSelectedDefinition();
	return Definition ? &Definition->GetSummonData().FinisherConfigs : nullptr;
}

void UVBWeaponStateComponent::GetAllFinisherConfigs(TArray<const UVBFinisherConfig*>& OutConfigs) const
{
	VBWeaponDefinitionList::CollectFinisherConfigs(WeaponDefinitions, OutConfigs);
}

EVBWeaponType UVBWeaponStateComponent::GetMMPoolForState(EVBLocomotionState InState) const
{
	// 미등록 무기는 기본 풀 - 맨손이 정상으로 오고, 무기별 MM 풀은 선택 사항이라 경고하지 않는다.
	const UVBWeaponDefinition* Definition = GetSelectedDefinition();
	return Definition ? VBWeaponStateRules::MMPoolForState(Definition->GetSummonData(), InState) : EVBWeaponType::None;
}

void UVBWeaponStateComponent::GiveWeaponSet(EVBWeaponType InWeaponType)
{
	// 캐시(OwnerCharacter)가 아니라 액터에서 바로 찾는다 - 저장 복원의 발도는 BeginPlay 전에 온다.
	UAbilitySystemComponent* ASC = FindOwnerAbilitySystem();
	// 맨손은 줄 세트가 없다. ASC 가 아직 없으면(빙의 전) 회차가 시작될 때 BeginAvatarSession 이 준다.
	if (!ASC || InWeaponType == EVBWeaponType::None)
	{
		return;
	}
	// 영수증 하나 = 손에 든 무기 하나. 앞 무기의 것을 먼저 거둔다 - 바꾸기가 이 한 줄로 거두고 주기가 된다.
	TakeWeaponSet();

	const UVBWeaponDefinition* Definition = FindDefinition(InWeaponType);
	const UVBAbilitySet* Set = Definition ? Definition->GetAbilitySet() : nullptr;
	if (!Set)
	{
		// 경고 없이 무시하지 않는다 - 증상이 "무기를 들었는데 공격, 가드가 안 된다" 로만 나타나 화면에 단서가 없다.
		VB_LOG(Warning, "%s: 무기 %s 의 정의(%s)에 능력 세트가 없다 - 공격 · 가드 · 장착 태그를 주지 않는다",
			*GetNameSafe(GetOwner()), *UEnum::GetValueAsString(InWeaponType), *GetNameSafe(Definition));
		return;
	}
	// 스펙은 SourceObject 를 읽기만 한다 - 능력이 어느 무기가 준 능력인지를 이것으로 안다(Lyra GetAssociatedEquipment).
	Set->GiveToAbilitySystem(ASC, WeaponSetGrant, const_cast<UVBWeaponDefinition*>(Definition));
	ApplyWeaponEnhancement();
}

void UVBWeaponStateComponent::TakeWeaponSet()
{
	WeaponSetGrant.TakeFromAbilitySystem();
	RemoveWeaponEnhancement();
}

void UVBWeaponStateComponent::ApplyWeaponEnhancement()
{
	RemoveWeaponEnhancement();
	UAbilitySystemComponent* ASC = FindOwnerAbilitySystem();
	if (!ASC || !IsWeaponSummoned())
	{
		return;
	}

	const TSubclassOf<UGameplayEffect> Effect = GetWeaponStateConfig()->WeaponEnhancementEffect;
	const UVBEnhancementComponent* Enhancement = UVBEnhancementComponent::FindFor(GetOwner());
	if (!Effect || !Enhancement)
	{
		// 경고 없이 1 로 처리하지 않는다 - 강화한 무기가 강화 전 피해로 동작한다.
		VB_LOG(Warning, "%s: 무기 강화가 피해에 붙지 않는다 - %s", *GetNameSafe(GetOwner()),
			!Effect ? TEXT("무기 상태 설정에 WeaponEnhancementEffect 가 없다") : TEXT("PlayerState 에 강화 컴포넌트가 없다"));
		return;
	}

	const TMap<FGameplayTag, float> Scale =
		{{VBGameplayTags::Data_WeaponDamageScale, Enhancement->GetWeaponDamageScale(CurrentWeaponType)}};
	WeaponEnhancementHandle = VBStatBonus::Apply(ASC, Effect, Scale, UEnum::GetValueAsString(CurrentWeaponType));
}

void UVBWeaponStateComponent::RemoveWeaponEnhancement()
{
	if (!WeaponEnhancementHandle.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = WeaponEnhancementHandle.GetOwningAbilitySystemComponent())
	{
		ASC->RemoveActiveGameplayEffect(WeaponEnhancementHandle);
	}

	WeaponEnhancementHandle.Invalidate();
}

void UVBWeaponStateComponent::HandleEnhancementChanged(const FVBEnhanceTarget& Target)
{
	// 빈 대상 = 전부(저장 불러오기, 복제). 손에 든 무기의 단계면 강화 효과만 다시 건다 - 능력은 거두지 않아 도는 공격이 끊기지 않는다.
	if (IsWeaponSummoned() && (!Target.IsValid() || (Target.IsWeapon() && Target.Weapon == CurrentWeaponType)))
	{
		ApplyWeaponEnhancement();
	}
}


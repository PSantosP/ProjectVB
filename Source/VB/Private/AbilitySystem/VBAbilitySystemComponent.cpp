// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystemGlobals.h"
#include "Data/VBCombatRuleTable.h"
#include "Data/VBCombatSettings.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Settings/VBOptionRules.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "VB/VB.h"


UVBAbilitySystemComponent::UVBAbilitySystemComponent()
{
	// Mixed 모드 : GE는 서버->오너만, Cue/Tag는 모든 클라이언트
	// 생성자(CDO 생성 시점)에선 SetIsReplicated 가 ensure 발화 - ByDefault 가 공식 경로 (ActorComponent.cpp::UActorComponent::SetIsReplicated)
	SetIsReplicatedByDefault(true);
	UAbilitySystemComponent::SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

TOptional<int32> UVBAbilitySystemComponent::GetReplicatedLooseTagCountForObservation(const FGameplayTag& Tag) const
{
	const int32* Count = GetReplicatedLooseTags().TagMap.Find(Tag);
	return Count ? TOptional<int32>(*Count) : TOptional<int32>();
}

void UVBAbilitySystemComponent::OnRep_ActivateAbilities()
{
	Super::OnRep_ActivateAbilities();
	OnAbilitiesReplicated.Broadcast();
}

void UVBAbilitySystemComponent::OnRemoveAbility(FGameplayAbilitySpec& Spec)
{
	Super::OnRemoveAbility(Spec);
	OnAbilityRemoved.Broadcast(Spec.Handle);
}

void UVBAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	const AActor* PreviousAvatar = GetAvatarActor_Direct();
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
	if (PreviousAvatar != InAvatarActor)
	{
		ClearInputPresses();
	}

	// 몽타주를 틀 메시는 캐릭터 본체 메시로 고정한다. 엔진은 아바타의 첫 스켈레탈 메시를 고른다(FGameplayAbilityActorInfo::InitFromActor).
	//   나중에 메시를 캐릭터에 직접 더해도 몽타주가 본체를 떠나지 않게 하는 방어다.
	if (const ACharacter* AvatarCharacter = Cast<ACharacter>(InAvatarActor); AvatarCharacter && AbilityActorInfo.IsValid()
		&& AvatarCharacter->GetMesh() && AbilityActorInfo->SkeletalMeshComponent.Get() != AvatarCharacter->GetMesh())
	{
		AbilityActorInfo->SkeletalMeshComponent = AvatarCharacter->GetMesh();
	}

	// 표는 한 번 읽어 강한 참조로 보관한다. 능력을 시작할 때마다 쓰므로 GC 뒤 다시 읽히면 안 된다.
	if (!CombatRules)
	{
		CombatRules = GetDefault<UVBCombatSettings>()->CombatRules.LoadSynchronous();
	}
}

bool UVBAbilitySystemComponent::ResolveStartTags(const FGameplayTagContainer& Identity, FGameplayTagContainer& OutBlockedBy,
	FGameplayTagContainer& OutRequired) const
{
	if (!CombatRules)
	{
		static bool bReported = false;
		if (!bReported)
		{
			bReported = true;
			VB_LOG(Error, "전투 규칙 표가 없어 능력을 켜지 않는다(ASC %s). Project Settings > VB Combat > CombatRules 를 채워라.",
				*GetNameSafe(this));
		}

		return false;
	}

	CombatRules->AppendActivationTags(Identity, OutBlockedBy, OutRequired);
	return true;
}

bool UVBAbilitySystemComponent::HoldsAnyOf(const FGameplayTagContainer& BlockedBy, FGameplayTagContainer* OutHeld) const
{
	if (!HasAnyMatchingGameplayTags(BlockedBy))
	{
		return false;
	}

	if (OutHeld)
	{
		FGameplayTagContainer Owned;
		GetOwnedGameplayTags(Owned);
		*OutHeld = Owned.Filter(BlockedBy);
	}

	return true;
}

bool UVBAbilitySystemComponent::IsBlockedByHeldState(const FGameplayTag& Identity, FGameplayTagContainer* OutHeld) const
{
	FGameplayTagContainer BlockedBy;
	FGameplayTagContainer Required;
	return !ResolveStartTags(FGameplayTagContainer(Identity), BlockedBy, Required) || HoldsAnyOf(BlockedBy, OutHeld);
}

bool UVBAbilitySystemComponent::CanStartUnderCombatRules(const FGameplayTagContainer& Identity, FGameplayTagContainer* OutFailTags) const
{
	FGameplayTagContainer BlockedBy;
	FGameplayTagContainer Required;
	if (!ResolveStartTags(Identity, BlockedBy, Required))
	{
		if (OutFailTags)
		{
			OutFailTags->AddTag(VBGameplayTags::Ability_Fail_Unknown);
		}

		return false;
	}

	const bool bBlocked = HoldsAnyOf(BlockedBy, nullptr);
	const bool bMissing = !HasAllMatchingGameplayTags(Required);
	if (OutFailTags)
	{
		// 엔진과 같은 실패 태그 - 실패 알림(NotifyAbilityFailed)이 같은 말로 읽는다.
		const UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
		if (bBlocked)
		{
			OutFailTags->AddTag(Globals.ActivateFailTagsBlockedTag);
		}

		if (bMissing)
		{
			OutFailTags->AddTag(Globals.ActivateFailTagsMissingTag);
		}
	}

	return !bBlocked && !bMissing;
}

void UVBAbilitySystemComponent::ApplyAbilityBlockAndCancelTags(const FGameplayTagContainer& AbilityTags,
	UGameplayAbility* RequestingAbility,
	bool bEnableBlockTags, const FGameplayTagContainer& BlockTags, bool bExecuteCancelTags, const FGameplayTagContainer& CancelTags)
{
	// 시작과 끝이 같은 이름표로 불리므로 같은 줄이 더해져 막기 횟수가 맞는다.
	FGameplayTagContainer AllBlock = BlockTags;
	FGameplayTagContainer AllCancel = CancelTags;
	if (CombatRules)
	{
		CombatRules->AppendBlockAndCancelTags(AbilityTags, AllBlock, AllCancel);
	}

	Super::ApplyAbilityBlockAndCancelTags(AbilityTags, RequestingAbility, bEnableBlockTags, AllBlock, bExecuteCancelTags, AllCancel);
}

void UVBAbilitySystemComponent::OnTagUpdated(const FGameplayTag& Tag, bool bTagExists)
{
	Super::OnTagUpdated(Tag, bTagExists);
	// 취소는 서버가 한다. 클라이언트는 서버의 능력 종료를 복제로 받는다.
	if (!bTagExists || !CombatRules || !IsOwnerActorAuthoritative())
	{
		return;
	}

	FGameplayTagContainer Cancel;
	CombatRules->AppendCancelsOnEntry(Tag, Cancel);
	if (!Cancel.IsEmpty())
	{
		CancelAbilities(&Cancel);
	}
}

void UVBAbilitySystemComponent::NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability,
	const FGameplayTagContainer& FailureReason)
{
	FVBAbilityFailureReport Report;
	Report.Handle = Handle;
	Report.AbilityClass = Ability ? Ability->GetClass() : nullptr;
	Report.Avatar = GetAvatarActor_Direct();
	Report.FailureTags = FailureReason;
	if (Report.FailureTags.IsEmpty())
	{
		Report.FailureTags.AddTag(VBGameplayTags::Ability_Fail_Unknown);
	}

	Super::NotifyAbilityFailed(Handle, Ability, FailureReason);
	ReportAbilityFailure(Report);
}

void UVBAbilitySystemComponent::ReportAbilityFailure(const FVBAbilityFailureReport& Report) const
{
	VB_LOG(Log, "[AbilityFailure] ability=%s handle=%s tags=%s", *GetNameSafe(Report.AbilityClass.Get()),
		*Report.Handle.ToString(), *Report.FailureTags.ToStringSimple());
}

void UVBAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag, int32 PressSerial, bool bForwardClaim)
{
	// 활성화가 목록을 바꿀 수 있다(활성 중 무기 교체가 세트를 거두고 줌, CancelAbilities). 순회가 끝날 때까지 재할당을 미룬다.
	ABILITYLIST_SCOPE_LOCK();
	const TWeakObjectPtr<AActor> InputAvatar = GetAvatarActor_Direct();
	const TWeakObjectPtr<APlayerController> InputPC = AbilityActorInfo.IsValid() ? AbilityActorInfo->PlayerController.Get() : nullptr;
	if (TryPressClaimingAbility(InputTag, bForwardClaim) || TryPressActiveInputOwner(InputTag, PressSerial))
	{
		return;
	}

	TOptional<FVBAbilityFailureReport> FirstDisplayFailure;
	if (ActivateInputCandidates(InputTag, PressSerial, InputAvatar, FirstDisplayFailure))
	{
		return;
	}
	// 진단은 Notify 마다 하고, 연출은 전부 실패한 opt-in 입력 묶음만 낸다
	if (FirstDisplayFailure.IsSet() && IsLocalPresentationTarget(InputAvatar.Get(), InputPC.Get()))
	{
		OnPlayerAbilityFailure.Broadcast(FirstDisplayFailure.GetValue());
	}
}

bool UVBAbilitySystemComponent::TryPressClaimingAbility(const FGameplayTag& InputTag, bool bForwardClaim)
{
	FGameplayAbilitySpec* Claiming = FindClaimingSpec(InputTag);
	if (!Claiming)
	{
		return false;
	}

	VB_LOG(Verbose, "[Input] %s 를 켜진 %s 가 가져감", *InputTag.ToString(), *GetNameSafe(Claiming->Ability));
	AbilitySpecInputPressed(*Claiming);
	if (ShouldForwardClaimedInput(IsOwnerActorAuthoritative(), bForwardClaim))
	{
		ServerSetInputPressed(Claiming->Handle);
	}

	return true;
}

bool UVBAbilitySystemComponent::TryPressActiveInputOwner(const FGameplayTag& InputTag, int32 PressSerial)
{
	// 부여 순서와 무관하게 활성 입력 소유자를 먼저 확인한다.
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		const UVBGameplayAbility* Ability = Cast<UVBGameplayAbility>(Spec.Ability);
		if (!Spec.IsActive() || !Ability || !Ability->InputTag.MatchesTagExact(InputTag))
		{
			continue;
		}

		if (IsToggleInput(InputTag, Ability->bIsToggleAbility))
		{
			CancelAbilityHandle(Spec.Handle);
		}
		else if (!TryActivateInputReplacement(InputTag, PressSerial, Spec))
		{
			AbilitySpecInputPressed(Spec);
		}

		return true;
	}

	return false;
}

bool UVBAbilitySystemComponent::ActivateInputCandidates(const FGameplayTag& InputTag, int32 PressSerial,
	const TWeakObjectPtr<AActor>& InputAvatar, TOptional<FVBAbilityFailureReport>& OutFirstDisplayFailure)
{
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		const UVBGameplayAbility* Ability = Cast<UVBGameplayAbility>(Spec.Ability);
		if (!Ability || !Ability->InputTag.MatchesTagExact(InputTag))
		{
			continue;
		}

		FGameplayTagContainer FailureTags;
		FDelegateHandle FailureHandle;
		if (Ability->bShowFailureToPlayer && !OutFirstDisplayFailure.IsSet())
		{
			// 시도한 이 spec 만 잡는다 - 다른 능력의 중첩 이벤트 실패는 아니다
			FailureHandle = AbilityFailedCallbacks.AddLambda([&Spec, &FailureTags](const UGameplayAbility* FailedAbility,
				const FGameplayTagContainer& Reasons)
			{
				if (FailedAbility == Spec.Ability.Get() || FailedAbility == Spec.GetPrimaryInstance())
				{
					FailureTags.AppendTags(Reasons);
				}
			});
		}

		const bool bActivated = TryActivateFromInput(Spec, {InputTag, PressSerial});
		if (FailureHandle.IsValid())
		{
			AbilityFailedCallbacks.Remove(FailureHandle);
		}

		if (bActivated)
		{
			Spec.InputPressed = true; // Lyra ProcessAbilityInput 처럼 켠 누름도 누르는 중으로 둔다. 놓을 때 내린다
			return true;
		}

		if (Ability->bShowFailureToPlayer && !OutFirstDisplayFailure.IsSet())
		{
			FVBAbilityFailureReport Report;
			Report.Handle = Spec.Handle;
			Report.AbilityClass = Ability->GetClass();
			Report.Avatar = InputAvatar;
			Report.FailureTags = MoveTemp(FailureTags);
			if (Report.FailureTags.IsEmpty())
			{
				Report.FailureTags.AddTag(VBGameplayTags::Ability_Fail_Unknown);
			}

			OutFirstDisplayFailure = MoveTemp(Report);
		}
		// 실패 = 태그 게이트에 막혔다. 같은 입력의 다음 후보를 시도한다 - 경공격(Guarding, RiposteWindow 차단)과 반격이 LMB 를 나눈다.
	}

	return false;
}
bool UVBAbilitySystemComponent::IsLocalPresentationTarget(const AActor* Avatar, const APlayerController* PC) const
{
	return Avatar && GetAvatarActor_Direct() == Avatar && AbilityActorInfo.IsValid() && AbilityActorInfo->PlayerController.Get() == PC
		&& PC && PC->IsLocalController() && PC->GetLocalPlayer();
}

void UVBAbilitySystemComponent::PresentPressFailure(const FGameplayTag& FailureTag)
{
	FVBAbilityFailureReport Report;
	Report.Avatar = GetAvatarActor_Direct();
	Report.FailureTags.AddTag(FailureTag);
	ReportAbilityFailure(Report);
	if (IsLocalPresentationTarget(Report.Avatar.Get(), AbilityActorInfo.IsValid() ? AbilityActorInfo->PlayerController.Get() : nullptr))
	{
		OnPlayerAbilityFailure.Broadcast(Report);
	}
}

bool UVBAbilitySystemComponent::TryActivateInputReplacement(const FGameplayTag& InputTag, int32 PressSerial,
	const FGameplayAbilitySpec& InputOwner)
{
	if (!CombatRules || !InputOwner.Ability)
	{
		return false;
	}

	for (FGameplayAbilitySpec& Candidate : ActivatableAbilities.Items)
	{
		const UVBGameplayAbility* Ability = Cast<UVBGameplayAbility>(Candidate.Ability);
		if (!Ability || Candidate.IsActive() || !Ability->InputTag.MatchesTagExact(InputTag))
		{
			continue;
		}

		FGameplayTagContainer Block;
		FGameplayTagContainer Cancel;
		CombatRules->AppendBlockAndCancelTags(Ability->GetAssetTags(), Block, Cancel);
		if (InputOwner.Ability->GetAssetTags().HasAny(Cancel) && TryActivateFromInput(Candidate, {InputTag, PressSerial}))
		{
			Candidate.InputPressed = true;
			return true;
		}
	}

	return false;
}

bool UVBAbilitySystemComponent::TryActivateFromInput(FGameplayAbilitySpec& Spec, const FVBActivationRequest& Request)
{
	// 서버에서 능력이 준비한 이벤트 데이터가 있으면 그것으로 켠다 - 엔진이 같은 데이터를 소유 클라 활성화에 싣는다.
	//   유효한 이벤트 태그가 필수다(없으면 클라 쪽 데이터가 nullptr 로 떨어진다).
	FGameplayEventData Payload;
	const UVBGameplayAbility* Instance = Cast<UVBGameplayAbility>(Spec.GetPrimaryInstance());
	if (IsOwnerActorAuthoritative() && Instance && Instance->PrepareActivation(Request, Payload))
	{
		Payload.EventTag = VBGameplayTags::Event_Ability_InputActivate;
		return TriggerAbilityFromGameplayEvent(Spec.Handle, AbilityActorInfo.Get(), Payload.EventTag, &Payload, *this);
	}

	return TryActivateAbility(Spec.Handle);
}

void UVBAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag, bool bForwardClaim)
{
	ABILITYLIST_SCOPE_LOCK();
	// 가져가는 능력의 '누르는 중' 은 켜져 있든 그사이 끝났든 내린다 - 끝난 뒤 놓으면 그 표시가 남는다.
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		const UVBGameplayAbility* Ability = Cast<UVBGameplayAbility>(Spec.Ability);
		if (Ability && !Spec.IsActive() && Ability->ClaimedInputTags.HasTagExact(InputTag))
		{
			Spec.InputPressed = false;
		}
	}
	// 누름을 가져간 켜진 능력이 놓음도 받는다. 같은 입력을 나눈 다른 스펙의 '누르는 중' 은 그래도 내린다.
	if (FGameplayAbilitySpec* Claiming = FindClaimingSpec(InputTag))
	{
		for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
		{
			const UVBGameplayAbility* Ability = Cast<UVBGameplayAbility>(Spec.Ability);
			if (&Spec != Claiming && Ability && Ability->InputTag.MatchesTagExact(InputTag))
			{
				Spec.InputPressed = false;
			}
		}

		AbilitySpecInputReleased(*Claiming);
		if (ShouldForwardClaimedInput(IsOwnerActorAuthoritative(), bForwardClaim))
		{
			ServerSetInputReleased(Claiming->Handle);
		}

		return;
	}
	// 같은 입력의 모든 능력이 '누르는 중' 을 내린다(Lyra ProcessAbilityInput) - 버튼보다 먼저 끝난 공격에 남지 않게.
	//   놓음 통지는 그 입력을 받은 능력 = 켜진 첫 능력 하나에게만. 꺼진 앞 후보에서 멈추면 같은 입력을 나눈 반격이 놓음을 못 받는다.
	bool bNotified = false;
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		const UVBGameplayAbility* Ability = Cast<UVBGameplayAbility>(Spec.Ability);
		if (!Ability || !Ability->InputTag.MatchesTagExact(InputTag))
		{
			continue;
		}

		if (bNotified || !Spec.IsActive())
		{
			Spec.InputPressed = false;
			continue;
		}

		bNotified = true;
		if (IsToggleInput(InputTag, Ability->bIsToggleAbility))
		{
			continue; // 토글은 다시 누를 때 끈다 - 누르는 중 표시도 그대로 둔다
		}

		AbilitySpecInputReleased(Spec);
		const UVBGameplayAbility* Instance = Cast<UVBGameplayAbility>(Spec.GetPrimaryInstance());
		if (!Instance || Instance->ShouldCancelOnInputRelease())
		{
			// ServerInitiated 라 소유 클라에도 인스턴스가 있다 - 로컬 취소를 엔진이 서버로 올린다(ReplicateEndOrCancelAbility).
			CancelAbilityHandle(Spec.Handle);
		}
	}
}

int32 UVBAbilitySystemComponent::BeginLocalPress(const FGameplayTag& InputTag)
{
	const int32 Serial = LastPressSerial.FindRef(InputTag) + 1;
	RecordPress(InputTag, Serial);
	return Serial;
}

int32 UVBAbilitySystemComponent::EndLocalPress(const FGameplayTag& InputTag)
{
	const TArray<FVBInputPress>* Presses = InputPresses.Find(InputTag);
	if (!Presses || Presses->IsEmpty() || !Presses->Last().IsHeld())
	{
		return INDEX_NONE;
	}

	const int32 Serial = Presses->Last().Serial;
	RecordRelease(InputTag, Serial);
	return Serial;
}

void UVBAbilitySystemComponent::RecordPress(const FGameplayTag& InputTag, int32 Serial)
{
	int32& Last = LastPressSerial.FindOrAdd(InputTag);
	if (Serial <= Last)
	{
		return; // 이미 아는 누름(리슨 호스트의 로컬 기록, 중복 RPC) 또는 지운 뒤 늦게 온 옛 누름
	}

	Last = Serial;
	TArray<FVBInputPress>& Presses = InputPresses.FindOrAdd(InputTag);
	FVBInputPress& Press = Presses.AddDefaulted_GetRef();
	Press.Serial = Serial;
	Press.PressTime = NowSeconds();
	if (Presses.Num() > MaxPressesPerTag)
	{
		Presses.RemoveAt(0, Presses.Num() - MaxPressesPerTag, EAllowShrinking::No);
	}
}

void UVBAbilitySystemComponent::RecordRelease(const FGameplayTag& InputTag, int32 Serial)
{
	if (FVBInputPress* Press = FindPressMutable(InputTag, Serial); Press && Press->IsHeld())
	{
		Press->ReleaseTime = NowSeconds();
	}
}

FGameplayAbilitySpec* UVBAbilitySystemComponent::FindClaimingSpec(const FGameplayTag& InputTag)
{
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		const UVBGameplayAbility* Ability = Cast<UVBGameplayAbility>(Spec.Ability);
		if (Spec.IsActive() && Ability && Ability->ClaimedInputTags.HasTagExact(InputTag))
		{
			return &Spec;
		}
	}

	return nullptr;
}

bool UVBAbilitySystemComponent::AcceptPressRequest(const FGameplayTag& InputTag, int32 Serial)
{
	int32& LastAccepted = LastAcceptedPressSerial.FindOrAdd(InputTag);
	if (Serial == INDEX_NONE || Serial <= LastAccepted || Serial > LastAccepted + MaxPressSerialJump)
	{
		return false; // 무효, 이미 받은 요청, 지운 뒤 늦게 온 옛 요청, 한 번에 너무 멀리 뛴 번호(한 번에 기준을 영구히 올려 그 뒤 입력을 다 막는다)
	}

	LastAccepted = Serial;
	RecordPress(InputTag, Serial); // 리슨 호스트는 로컬 기록이 이미 있어 그대로 둔다
	return FindPress(InputTag, Serial) != nullptr;
}

bool UVBAbilitySystemComponent::AcceptReleaseRequest(const FGameplayTag& InputTag, int32 Serial)
{
	FVBInputPress* Press = FindPressMutable(InputTag, Serial);
	if (!Press || Press->bReleaseRequestAccepted)
	{
		return false; // 모르는, 지운, 이미 받은 놓음
	}

	Press->bReleaseRequestAccepted = true;
	RecordRelease(InputTag, Serial); // 리슨 호스트는 로컬이 이미 놓아 그대로 둔다
	return true;
}

const FVBInputPress* UVBAbilitySystemComponent::FindPress(const FGameplayTag& InputTag, int32 Serial) const
{
	const TArray<FVBInputPress>* Presses = InputPresses.Find(InputTag);
	return Presses ? Presses->FindByPredicate([Serial](const FVBInputPress& Press) { return Press.Serial == Serial; }) : nullptr;
}

FVBInputPress* UVBAbilitySystemComponent::FindPressMutable(const FGameplayTag& InputTag, int32 Serial)
{
	return const_cast<FVBInputPress*>(FindPress(InputTag, Serial));
}

void UVBAbilitySystemComponent::ClearInputPresses()
{
	InputPresses.Reset(); // 번호(LastPressSerial)는 남긴다 - 지운 뒤 늦게 온 옛 번호가 새 누름으로 기록되지 않게
}

double UVBAbilitySystemComponent::NowSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool UVBAbilitySystemComponent::IsToggleInput(const FGameplayTag& InputTag, bool bAbilityIsToggle) const
{
	return VBOptionRules::IsToggleInput(InputTag, bAbilityIsToggle, UVBSaveGameSubsystem::GetSettingsData(GetAvatarActor()));
}

bool UVBAbilitySystemComponent::AcquireExternalMontageOwnership(UObject* InOwner, uint8 PreviousPlayInstanceId)
{
	if (!IsValid(InOwner) || (ExternalMontageOwner.IsValid() && ExternalMontageOwner.Get() != InOwner))
	{
		return false;
	}

	ExternalMontageOwner = InOwner;
	SupersededPlayInstanceId = PreviousPlayInstanceId;
	bHasSupersededMontage = true;
	// 외부 생산자가 짝을 틀기 전에 GAS 가 추적하는 montage 만 멈춘다.
	// authority 에서는 IsStopped 도 알리므로 callback 이 경쟁하는 GAS montage 를 시작하지 못한다.
	CurrentMontageStop();
	return true;
}

void UVBAbilitySystemComponent::ReleaseExternalMontageOwnership(UObject* InOwner)
{
	if (ExternalMontageOwner.Get() != InOwner)
	{
		return;
	}

	ExternalMontageOwner.Reset();
	if (bDeferredExternalMontageRep && !IsOwnerActorAuthoritative())
	{
		bDeferredExternalMontageRep = false;
		OnRep_ReplicatedAnimMontage();
	}
}

float UVBAbilitySystemComponent::PlayMontage(UGameplayAbility* AnimatingAbility,
	FGameplayAbilityActivationInfo ActivationInfo, UAnimMontage* Montage, float InPlayRate,
	FName StartSectionName, float StartTimeSeconds)
{
	if (ExternalMontageOwner.IsValid())
	{
		return 0.0f;
	}

	return Super::PlayMontage(AnimatingAbility, ActivationInfo, Montage, InPlayRate, StartSectionName, StartTimeSeconds);
}

void UVBAbilitySystemComponent::OnRep_ReplicatedAnimMontage()
{
	if (ExternalMontageOwner.IsValid())
	{
		bDeferredExternalMontageRep = true;
		return;
	}

	const FGameplayAbilityRepAnimMontage& Rep = GetRepAnimMontageInfo();
	// 처형 채널이 닫힌 뒤에도 옛 actor-channel 갱신이 올 수 있다.
	// 자산이 아니라 그 GAS 재생 instance 를 은퇴시킨다 - 뒤 능력이 같은 자산을 다시 틀 수 있다.
	if (HasMatchingGameplayTag(VBGameplayTags::State_Dead) ||
		(bHasSupersededMontage && SupersededPlayInstanceId == Rep.PlayInstanceId))
	{
		bPendingMontageRep = false;
		return;
	}

	if (Rep.IsStopped)
	{
		// 기본 OnRep 은 들어온 상태가 stopped 여도 다른 montage 를 먼저 시작할 수 있다.
		// 그 낡은 instance 를 만들지 않고, 맞는 기존 GAS instance 만 멈춘다.
		if (LocalAnimMontageInfo.AnimMontage == Rep.Animation &&
			LocalAnimMontageInfo.PlayInstanceId == Rep.PlayInstanceId)
		{
			CurrentMontageStop(Rep.BlendTime);
		}

		bPendingMontageRep = false;
		return;
	}

	bHasSupersededMontage = false;
	Super::OnRep_ReplicatedAnimMontage();
}


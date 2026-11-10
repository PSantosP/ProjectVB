#include "CharacterCommon/VBCinematicSkillChargeComponent.h"

#include "AbilitySystemInterface.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBGA_CinematicSkill.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBPawnExtensionComponent.h"
#include "Data/VBCinematicSkillChargeRules.h"
#include "Data/VBCinematicSkillDefinition.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Interaction/VBSetupValidation.h"
#include "VB/VB.h"

UVBCinematicSkillChargeComponent::UVBCinematicSkillChargeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBCinematicSkillChargeComponent::BeginPlay()
{
	Super::BeginPlay();
	APlayerState* Player = Cast<APlayerState>(GetOwner());
	TInlineComponentArray<UVBCinematicSkillChargeComponent*> Owners(GetOwner());
	if (Owners.Num() != 1)
	{
		VB_LOG(Error, "%s: PlayerState must own exactly one charge component", *GetPathName());
		return;
	}

	const IAbilitySystemInterface* Provider = Cast<IAbilitySystemInterface>(Player);
	UAbilitySystemComponent* ASC = Provider ? Provider->GetAbilitySystemComponent() : nullptr;
	const FString Problem = Rules ? Rules->ValidateSetup() : TEXT("Rules are missing");
	if (!Player || !ASC || !ASC->HasAttributeSetForAttribute(UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute())
		|| !ASC->HasAttributeSetForAttribute(UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute()) || !Problem.IsEmpty())
	{
		VB_LOG(Error, "%s: charge owner unavailable: %s", *GetPathName(), *Problem);
		return;
	}

	SourceASC = ASC;
	CurrentSubscription.Bind(*ASC, UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute(), this, &ThisClass::HandleAttributeChanged);
	MaximumSubscription.Bind(*ASC, UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute(), this, &ThisClass::HandleAttributeChanged);
	TagHandle = ASC->RegisterGenericGameplayTagEvent().AddUObject(this, &ThisClass::HandleTagChanged);
	AbilityChangedHandle = ASC->AbilitySpecDirtiedCallbacks.AddUObject(this, &ThisClass::HandleAbilityChanged);
	if (UVBAbilitySystemComponent* VBASC = Cast<UVBAbilitySystemComponent>(ASC))
	{
		AbilitiesReplicatedHandle = VBASC->OnAbilitiesReplicated.AddUObject(this, &ThisClass::RefreshReady);
		AbilityRemovedHandle = VBASC->OnAbilityRemoved.AddUObject(this, &ThisClass::HandleAbilityRemoved);
	}

	Player->OnPawnSet.AddDynamic(this, &ThisClass::HandlePawnSet);
	SaveRegistry = GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (UVBSaveParticipantSubsystem* Save = SaveRegistry.Get())
	{
		SaveHandle = Save->OnSaveFinalized.AddUObject(this, &ThisClass::RefreshReady);
	}

	if (GetOwner()->HasAuthority())
	{
		UVBGameplayMessageSubsystem* Bus = UVBGameplayMessageSubsystem::Get(this);
		if (Bus)
		{
			CreditListener = Bus->RegisterListener(VBGameplayTags::Message_Combat_ChargeCredit, this, &ThisClass::HandleCredit);
		}
		else
		{
			VB_LOG(Error, "%s: combat credit bus is missing", *GetPathName());
		}
	}

	HandlePawnSet(Player, Player->GetPawn(), nullptr);
}

EVBWeaponType UVBCinematicSkillChargeComponent::GetRequiredWeapon() const
{
	return Rules && Rules->SkillDefinition ? Rules->SkillDefinition->RequiredWeapon : EVBWeaponType::None;
}

bool UVBCinematicSkillChargeComponent::IsUnlocked() const
{
	const UVBWeaponStateComponent* State = Weapons.Get();
	return SourceASC.IsValid() && State && GetRequiredWeapon() != EVBWeaponType::None && State->IsWeaponOwned(GetRequiredWeapon());
}

void UVBCinematicSkillChargeComponent::HandleCredit(FGameplayTag Channel, const FVBCombatChargeCredit& Credit)
{
	UAbilitySystemComponent* ASC = SourceASC.Get();
	// Suspended = Pawn 을 교체하는 사이 - ASC 가 아직 이전 Pawn 을 가리킬 수 있고, 그 늦은 타격이 새 Pawn 을 채우면 안 된다.
	if (bEnding || bAvatarSuspended || !ASC || !ASC->IsOwnerActorAuthoritative() || Credit.RecipientASC != ASC
		|| Credit.RecipientAvatar != ASC->GetAvatarActor() || !Credit.Id.IsValid() || RecentCredits.Contains(Credit.Id))
	{
		return;
	}

	RecentCredits.Add(Credit.Id);
	if (RecentCredits.Num() > Rules->RecentCreditLimit)
	{
		RecentCredits.RemoveAt(0);
	}

	const UVBSaveParticipantSubsystem* Save = SaveRegistry.Get();
	if (!IsUnlocked() || Credit.Reason == EVBCombatChargeReason::None
		|| (Credit.Reason != EVBCombatChargeReason::Parry && (!FMath::IsFinite(Credit.AppliedDamage) || Credit.AppliedDamage <= 0))
		|| UVBAbilitySystemStatics::IsDead(ASC)
		|| ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_CinematicSkill) || (Save && Save->IsApplyingSave()))
	{
		return;
	}

	if (ASC->GetNumericAttribute(UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute()) <= 0.0f)
	{
		if (!bReportedNoMaximum)
		{
			VB_LOG(Error, "%s: MaxCinematicSkillGauge is 0 - no init effect set it, the gauge cannot fill", *GetPathName());
		}

		bReportedNoMaximum = true;
		return;
	}

	const float Gain = Rules->GetGain(Credit.Reason);
	if (!FMath::IsFinite(Gain) || Gain < 0)
	{
		VB_LOG(Error, "%s: invalid charge gain", *GetPathName());
		return;
	}

	if (Gain == 0)
	{
		return;
	}

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Rules->GainEffect, 1.0f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		VB_LOG(Error, "%s: gain effect spec could not be created", *GetPathName());
		return;
	}

	Spec.Data->SetSetByCallerMagnitude(VBGameplayTags::Data_CinematicSkillGaugeGain, Gain);
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void UVBCinematicSkillChargeComponent::DisconnectPawn()
{
	if (UVBWeaponStateComponent* Previous = Weapons.Get())
	{
		Previous->OnOwnedWeaponsChanged.Remove(OwnedWeaponsHandle);
		Previous->OnLocomotionStateChanged.RemoveDynamic(this, &ThisClass::HandleWeaponChanged);
	}

	if (ACharacter* Previous = MovementSource.Get())
	{
		Previous->MovementModeChangedDelegate.RemoveDynamic(this, &ThisClass::HandleMovementModeChanged);
	}

	if (UVBPawnExtensionComponent* Previous = PawnExtension.Get())
	{
		Previous->UnregisterAbilitySystemDelegates(AvatarInitializedHandle, AvatarUninitializedHandle);
	}

	Weapons.Reset();
	MovementSource.Reset();
	PawnExtension.Reset();
	OwnedWeaponsHandle.Reset();
	AvatarInitializedHandle.Reset();
	AvatarUninitializedHandle.Reset();
}

void UVBCinematicSkillChargeComponent::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	ReleaseReady();
	DisconnectPawn();
	Weapons = UVBWeaponStateComponent::FindFor(NewPawn);
	if (UVBWeaponStateComponent* State = Weapons.Get())
	{
		OwnedWeaponsHandle = State->OnOwnedWeaponsChanged.AddUObject(this, &ThisClass::RefreshReady);
		State->OnLocomotionStateChanged.AddDynamic(this, &ThisClass::HandleWeaponChanged);
	}

	MovementSource = Cast<ACharacter>(NewPawn);
	if (ACharacter* Character = MovementSource.Get())
	{
		Character->MovementModeChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandleMovementModeChanged);
	}

	PawnExtension = UVBPawnExtensionComponent::FindFor(NewPawn);
	// Pawn 이 없으면 ASC 가 이전 Pawn 을 가리켜도 현재 avatar 는 없다. PawnExtension 이 있는 Pawn 은 초기화를 기다린다.
	bAvatarSuspended = !NewPawn || (PawnExtension.IsValid() && !PawnExtension->IsAbilitySystemInitialized());
	if (UVBPawnExtensionComponent* Extension = PawnExtension.Get())
	{
		AvatarUninitializedHandle = Extension->RegisterOnAbilitySystemUninitialized(
			FVBAbilitySystemUninitializedDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAvatarUninitialized));
		AvatarInitializedHandle = Extension->RegisterAndCallOnAbilitySystemInitialized(
			FVBAbilitySystemInitializedDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAvatarInitialized));
	}

	RefreshReady();
}

void UVBCinematicSkillChargeComponent::HandleAvatarInitialized(UVBAbilitySystemComponent& ASC)
{
	if (&ASC != SourceASC.Get())
	{
		return;
	}

	bAvatarSuspended = false;
	RefreshReady();
}

void UVBCinematicSkillChargeComponent::HandleAvatarUninitialized(UVBAbilitySystemComponent& ASC, bool bAvatarSession)
{
	if (&ASC != SourceASC.Get())
	{
		return;
	}

	bAvatarSuspended = true;
	ReleaseReady();
	OnGaugeStateChanged.Broadcast();
}

void UVBCinematicSkillChargeComponent::HandleAttributeChanged(const FOnAttributeChangeData& Data)
{
	RefreshReady();
	OnGaugeStateChanged.Broadcast();
}
void UVBCinematicSkillChargeComponent::HandleTagChanged(FGameplayTag Tag, int32 Count) { RefreshReady(); }
void UVBCinematicSkillChargeComponent::HandleWeaponChanged(EVBLocomotionState State, EVBWeaponType Weapon) { RefreshReady(); }
void UVBCinematicSkillChargeComponent::HandleAbilityChanged(const FGameplayAbilitySpec& Spec) { RefreshReady(); }
void UVBCinematicSkillChargeComponent::HandleMovementModeChanged(ACharacter* Character, EMovementMode PreviousMode,
	uint8 PreviousCustomMode)
{
	RefreshReady();
}

FGameplayAbilitySpecHandle UVBCinematicSkillChargeComponent::FindReadySkill(UAbilitySystemComponent& ASC) const
{
	FScopedAbilityListLock Lock(ASC);
	for (const FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
	{
		const UVBGA_CinematicSkill* Skill = Cast<UVBGA_CinematicSkill>(Spec.Ability);
		if (Skill && Skill->GetDefinition() == Rules->SkillDefinition && !Spec.IsActive()
			&& !Spec.PendingRemove && Skill->CanActivateAbility(Spec.Handle, ASC.AbilityActorInfo.Get()))
		{
			return Spec.Handle;
		}
	}

	return FGameplayAbilitySpecHandle();
}

void UVBCinematicSkillChargeComponent::HandleAbilityRemoved(FGameplayAbilitySpecHandle Handle)
{
	if (Handle != ReadySkill)
	{
		return;
	}

	ReleaseReady();
	OnGaugeStateChanged.Broadcast();
}

void UVBCinematicSkillChargeComponent::ReleaseReady()
{
	// Cue 제거가 같은 ASC 구독을 동기로 다시 부를 수 있다.
	TGuardValue<bool> Releasing(bRefreshingReady, true);
	AActor* Previous = ReadyAvatar.Get();
	ReadyAvatar.Reset();
	ReadySkill = FGameplayAbilitySpecHandle();
	const FActiveGameplayEffectHandle PreviousEffect = ReadyEffect;
	ReadyEffect.Invalidate();
	if (UAbilitySystemComponent* ASC = SourceASC.Get(); ASC && PreviousEffect.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(PreviousEffect);
	}

	if (Previous)
	{
		OnReadyChanged.Broadcast(false, Previous);
	}
}

void UVBCinematicSkillChargeComponent::RefreshReady()
{
	if (bEnding || bRefreshingReady)
	{
		return;
	}
	// 피드백 효과가 ASC 태그를 동기로 바꿀 수 있다.
	TGuardValue<bool> Updating(bRefreshingReady, true);
	UAbilitySystemComponent* ASC = SourceASC.Get();
	APawn* Pawn = ASC ? Cast<APawn>(ASC->GetAvatarActor()) : nullptr;
	// Ready = 스킬 자신이 발동할 수 있다(게이지, 자세, 죽음, 처형 장면, 저장은 스킬의 조건이지 여기 것이 아니다).
	const FGameplayAbilitySpecHandle AvailableSkill = Pawn && !bAvatarSuspended ? FindReadySkill(*ASC) : FGameplayAbilitySpecHandle();
	const bool bReady = AvailableSkill.IsValid();
	const AActor* PreviousAvatar = ReadyAvatar.Get();
	const FGameplayAbilitySpecHandle PreviousSkill = ReadySkill;
	if (!bReady || ReadyAvatar.Get() != Pawn || ReadySkill != AvailableSkill)
	{
		ReleaseReady();
	}

	if (bReady && !ReadyAvatar.IsValid())
	{
		ReadyAvatar = Pawn;
		ReadySkill = AvailableSkill;
		if (ASC->IsOwnerActorAuthoritative() && Rules->ReadyFeedbackEffect)
		{
			ReadyEffect = ASC->ApplyGameplayEffectToSelf(Rules->ReadyFeedbackEffect->GetDefaultObject<UGameplayEffect>(), 1.0f,
				ASC->MakeEffectContext());
		}

		OnReadyChanged.Broadcast(true, Pawn);
	}
	// 전투 중 태그 변화는 잦다 - 듣는 쪽에는 실제로 바뀐 것만 알린다(값 자체는 HandleAttributeChanged 가 맡는다).
	const bool bUnlocked = IsUnlocked();
	if (PreviousAvatar != ReadyAvatar.Get() || PreviousSkill != ReadySkill || bUnlocked != bWasUnlocked)
	{
		bWasUnlocked = bUnlocked;
		OnGaugeStateChanged.Broadcast();
	}
}

void UVBCinematicSkillChargeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	CreditListener.Unregister();
	ReleaseReady();
	DisconnectPawn();
	if (APlayerState* Player = Cast<APlayerState>(GetOwner()))
	{
		Player->OnPawnSet.RemoveDynamic(this, &ThisClass::HandlePawnSet);
	}

	if (UAbilitySystemComponent* ASC = SourceASC.Get())
	{
		ASC->RegisterGenericGameplayTagEvent().Remove(TagHandle);
		ASC->AbilitySpecDirtiedCallbacks.Remove(AbilityChangedHandle);
		if (UVBAbilitySystemComponent* VBASC = Cast<UVBAbilitySystemComponent>(ASC))
		{
			VBASC->OnAbilitiesReplicated.Remove(AbilitiesReplicatedHandle);
			VBASC->OnAbilityRemoved.Remove(AbilityRemovedHandle);
		}
	}
	// 새로 찾지 않고 BeginPlay 에 묶은 registry 를 쓴다 - GC 가 월드 subsystem 이 사라진 뒤 이것을 부를 수 있다.
	if (UVBSaveParticipantSubsystem* Save = SaveRegistry.Get())
	{
		Save->OnSaveFinalized.Remove(SaveHandle);
	}

	SaveRegistry.Reset();
	CurrentSubscription.Reset();
	MaximumSubscription.Reset();
	SourceASC.Reset();
	RecentCredits.Reset();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult UVBCinematicSkillChargeComponent::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Base = Super::IsDataValid(Context);
	TInlineComponentArray<UVBCinematicSkillChargeComponent*> Owners(GetOwner());
	const FString Problem = Owners.Num() > 1 ? TEXT("PlayerState must own exactly one charge component")
		: Rules ? Rules->ValidateSetup() : TEXT("Rules are missing");
	return VBReportSetupProblem(Context, *this, Problem,
		Base == EDataValidationResult::Invalid ? Base : EDataValidationResult::Valid);
}
#endif

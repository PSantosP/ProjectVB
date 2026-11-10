// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBEncounterGateComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "CharacterCommon/VBHealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Narrative/Story/VBAreaTrigger.h"
#include "EngineUtils.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "VB/VB.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBEncounterGate"

// 쓰러진 조우: 새 보류는 무시하고, 풀 때는 태그만 걷고 알리지 않는다 - 알리면 죽은 적의 행동 트리가 이어진다.
EVBEncounterHoldAction VBEncounterHold::Decide(bool bHeldNow, bool bWantHeld, bool bDefeated)
{
	if (bHeldNow == bWantHeld)
	{
		return EVBEncounterHoldAction::Ignore;
	}

	if (bDefeated)
	{
		return bWantHeld ? EVBEncounterHoldAction::Ignore : EVBEncounterHoldAction::ApplySilently;
	}

	return EVBEncounterHoldAction::ApplyAndNotify;
}

int32 VBEncounterDormant::Choose(int32 NumPoses, TFunctionRef<bool(int32 PoseIndex)> Holds)
{
	for (int32 Index = 0; Index < NumPoses; ++Index)
	{
		if (Holds(Index))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

namespace
{
	// 제자리 클립 반복 횟수. 몽타주 길이 = 클립 × 횟수라 int 최대는 쓰지 않는다.
	constexpr int32 DormantLoopCount = 10000;
}

UVBEncounterGateComponent::UVBEncounterGateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 잠든 자세의 줄 번호만 복제한다(나머지 판정은 서버 전용 그대로).
	SetIsReplicatedByDefault(true);
}

void UVBEncounterGateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBEncounterGateComponent, DormantPoseIndex);
}

APlayerState* UVBEncounterGateComponent::GetFirstPlayerState() const
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	return Controller ? Controller->PlayerState : nullptr;
}

void UVBEncounterGateComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	// 양쪽이 사실 변화를 듣는다: 서버는 자세를 다시 고르고, 클라는 멈춘 자세를 다시 건다(줄이 그대로면 OnRep 이 없다).
	const FSimpleDelegate OnStory = FSimpleDelegate::CreateUObject(this, &UVBEncounterGateComponent::HandleStoryChanged);
	if (!Owner || !Owner->HasAuthority())
	{
		// 클라: 묶는 즉시 한 번 불려 시작 전에 복제된 줄을 지금 건다.
		StoryBinding.Refresh(this, GetWorld(), OnStory);
		return;
	}

	const FVBRuleContext Context = FVBRuleContext::Make(this, GetFirstPlayerState());
	if (!VBRules::EvaluateAll(ExistConditions, Context))
	{
		VB_LOG(Log, "조우 문: %s 는 나오지 않는다(존재 조건 거짓)", *Owner->GetName());
		Owner->Destroy();
		return;
	}

	bExisted = true;

	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner))
	{
		HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(UVBHealthAttributeSet::GetHealthAttribute())
			.AddUObject(this, &UVBEncounterGateComponent::HandleHealthChanged);
	}
	else
	{
		VB_LOG(Warning, "조우 문: %s 에 ASC 가 없다 - 맞아도 깨지 않는다", *Owner->GetName());
	}
	// 쓰러짐은 체력 수치가 아니라 소유 액터의 사망 완료로 받는다 - 중첩 쓰기에도 한 번만.
	if (UVBHealthComponent* Health = UVBHealthComponent::FindFor(Owner))
	{
		BoundHealth = Health;
		DeathCompletedHandle = Health->OnDeathCompleted.AddUObject(this, &UVBEncounterGateComponent::HandleOwnerDeathCompleted);
	}
	else
	{
		VB_LOG(Warning, "조우 문: %s 에 체력 컴포넌트가 없다 - 쓰러져도 효과가 없다", *Owner->GetName());
	}

	RefreshPlayerBindings();
	// 존재, 체력 준비가 끝난 뒤에 묶는다 - 묶는 즉시 자세 고르기와 깨우기 판정이 한 번 돈다.
	StoryBinding.Refresh(this, GetWorld(), OnStory);
}

void UVBEncounterGateComponent::HandleStoryChanged()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		ApplyDormantPose();
		return;
	}

	RefreshDormantPose();
	EvaluateWake();
}

void UVBEncounterGateComponent::RefreshDormantPose()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	int32 Chosen = INDEX_NONE;
	if (bExisted && !bAwake && !bDefeated && !bEnding)
	{
		const FVBRuleContext Context = FVBRuleContext::Make(this, GetFirstPlayerState());
		Chosen = VBEncounterDormant::Choose(DormantPoses.Num(),
			[this, &Context](int32 PoseIndex) { return VBRules::EvaluateAll(DormantPoses[PoseIndex].Conditions, Context); });
	}

	DormantPoseIndex = Chosen;
	ApplyDormantPose();
}

void UVBEncounterGateComponent::OnRep_DormantPoseIndex()
{
	ApplyDormantPose();
}

void UVBEncounterGateComponent::ApplyDormantPose()
{
	UAnimSequenceBase* Wanted = DormantPoses.IsValidIndex(DormantPoseIndex) ? DormantPoses[DormantPoseIndex].Animation.Get() : nullptr;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UAnimInstance* AnimInstance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		// 시작 전 OnRep 은 경고 없이 무시한다 - BeginPlay 가 다시 적용한다.
		if (Wanted && HasBegunPlay())
		{
			VB_LOG(Warning, "조우 문: %s 에 애님 인스턴스가 없다 - 잠든 자세 %s 를 걸 수 없다", *GetNameSafe(GetOwner()), *GetNameSafe(Wanted));
		}

		return;
	}

	UAnimMontage* Playing = DormantMontage.Get();
	const bool bPlaying = Playing && AnimInstance->Montage_IsPlaying(Playing);
	if (Wanted && bPlaying && DormantAnimation.Get() == Wanted)
	{
		return;
	}

	if (bPlaying)
	{
		AnimInstance->Montage_Stop(DormantBlendSeconds, Playing);
	}

	DormantMontage.Reset();
	DormantAnimation.Reset();
	if (!Wanted)
	{
		return;
	}

	DormantMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(Wanted, DormantSlotName, DormantBlendSeconds, DormantBlendSeconds,
		1.f, DormantLoopCount);
	DormantAnimation = Wanted;
}

void UVBEncounterGateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	// KillZ 낙하로 파괴된 적도 해소로 친다 - 안 그러면 진행이 잠긴다. 레벨 전환, 종료, 스스로 나감, 시체 정리는 해소가 아니다.
	// FellOutOfWorld 를 컴포넌트가 덮을 수 없어 "KillZ 아래에서 파괴됨" 으로 가른다.
	if (EndPlayReason == EEndPlayReason::Destroyed && bExisted && !bDefeated && GetOwner() && GetOwner()->HasAuthority())
	{
		const UWorld* World = GetWorld();
		const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
		if (Settings && Settings->bEnableWorldBoundsChecks && GetOwner()->GetActorLocation().Z < Settings->KillZ)
		{
			ApplyDefeated(nullptr, TEXT("KillZ 낙하"));
		}
	}

	StoryBinding.Disconnect();
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UVBHealthAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
	}

	if (UVBHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnDeathCompleted.Remove(DeathCompletedHandle);
	}

	BoundHealth.Reset();
	DeathCompletedHandle.Reset();
	DisconnectPlayerBindings();
	if (bRequirePlayerPreparation)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearAllTimersForObject(this);
		}
	}

	HealthChangedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void UVBEncounterGateComponent::RefreshPlayerBindings()
{
	if (!bRequirePlayerPreparation || bEnding || !GetWorld())
	{
		return;
	}

	PlayerBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &ThisClass::RequestEvaluateWake));

	AVBAreaTrigger* CurrentArea = HasValidWakeArea() ? WakeArea.Get() : nullptr;
	if (BoundWakeArea.Get() != CurrentArea)
	{
		if (AVBAreaTrigger* Old = BoundWakeArea.Get())
		{
			Old->OnOccupancyChanged.Remove(AreaOccupancyHandle);
		}

	AreaOccupancyHandle.Reset(); BoundWakeArea = CurrentArea;
	if (CurrentArea)
	{
		AreaOccupancyHandle = CurrentArea->OnOccupancyChanged.AddUObject(this, &ThisClass::RequestEvaluateWake);
	}
	}
}

void UVBEncounterGateComponent::DisconnectPlayerBindings()
{
	PlayerBinding.Disconnect();

	if (AVBAreaTrigger* Old = BoundWakeArea.Get())
	{
		Old->OnOccupancyChanged.Remove(AreaOccupancyHandle);
	}

	AreaOccupancyHandle.Reset(); BoundWakeArea.Reset();
}

void UVBEncounterGateComponent::RequestEvaluateWake()
{
	AActor* Owner = GetOwner();
	if (bEnding || bEvaluatePending || bAwake || bDefeated || !bExisted || !Owner || !Owner->HasAuthority() || !GetWorld())
	{
		return;
	}

	bEvaluatePending = true;
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::EvaluateWake);
}

void UVBEncounterGateComponent::EvaluateWake()
{
	bEvaluatePending = false;
	if (bEnding || bAwake || bDefeated)
	{
		return;
	}

	Wake(WakeConditions.IsEmpty() ? TEXT("조건 없음") : TEXT("조건 참"));
}

void UVBEncounterGateComponent::Wake(const TCHAR* Why, bool bFromDamage)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (bEnding || bAwake || bDefeated || !bExisted || !IsValid(Owner) || !Owner->HasAuthority() || !World)
	{
		return;
	}

	RefreshPlayerBindings();
	APlayerController* Controller = World->GetFirstPlayerController();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	FVBRuleContext Context = FVBRuleContext::Make(this, Controller ? Controller->PlayerState.Get() : nullptr);
	if (!bFromDamage && !VBRules::EvaluateAll(WakeConditions, Context))
	{
		return;
	}

	if (bRequirePlayerPreparation)
	{
		if (!HasValidWakeArea() && !bWarnedInvalidWakeArea)
		{
			bWarnedInvalidWakeArea = true;
			VB_LOG(Warning, "조우 준비: %s WakeArea가 없거나 다른 월드입니다", *GetNameSafe(GetOwner()));
		}
		// 조건/피격 깨우기의 마지막 관문. 미준비 피격은 예약하지 않는다.
		if (!IsValid(Controller) || !IsValid(Pawn) || Pawn->GetWorld() != World
			|| Controller != World->GetFirstPlayerController() || Controller->GetPawn() != Pawn
			|| !IsValid(Controller->PlayerState) || Pawn->GetPlayerState() != Controller->PlayerState
			|| UVBPlayerReadinessComponent::QueryForPawn(Pawn).State != EVBPlayerPreparationState::Ready)
		{
			return;
		}

		if (!bFromDamage && (!HasValidWakeArea() || !WakeArea->IsPawnInside(Pawn)))
		{
			return;
		}
	}

	bAwake = true;
	VB_LOG(Log, "조우 문: %s 깨어남(%s)", *GetNameSafe(GetOwner()), Why);
	RefreshDormantPose();
	if (!WokenEffects.IsEmpty())
	{
		Context.Counterpart = Owner;
		VBRules::ApplyAll(WokenEffects, Context);
	}

	OnWoken.Broadcast();
}

void UVBEncounterGateComponent::SetHeld(bool bInHeld)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const EVBEncounterHoldAction Action = VBEncounterHold::Decide(bHeld, bInHeld, bDefeated);
	if (Action == EVBEncounterHoldAction::Ignore)
	{
		VB_CLOG(bDefeated && bInHeld && !bHeld, Log, "조우 문: %s 는 이미 쓰러졌다 - 보류를 걸지 않는다", *Owner->GetName());
		return;
	}

	bHeld = bInHeld;
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner))
	{
		if (bHeld)
		{
			// 새 공격 막기와 실행 중인 공격 취소는 전투 규칙 표의 State.Encounter.Held 줄이 정한다.
			ASC->AddLooseGameplayTag(VBGameplayTags::State_Encounter_Held);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Encounter_Held);
		}
	}
	else
	{
		// 태그를 못 붙이면 공격 GA 막기가 빠진다 - 행동 트리 멈춤만 남는다.
		VB_LOG(Warning, "조우 문: %s 에 ASC 가 없다 - 보류 태그를 못 붙인다(행동 트리 멈춤만 걸린다)", *Owner->GetName());
	}

	const bool bNotify = Action == EVBEncounterHoldAction::ApplyAndNotify;
	VB_LOG(Log, "조우 문: %s 보류 %s%s", *Owner->GetName(), bHeld ? TEXT("켬") : TEXT("끔"), bNotify ? TEXT("") : TEXT("(쓰러짐 - 행동 트리는 안 잇는다)"));
	if (bNotify)
	{
		OnHeldChanged.Broadcast(bHeld);
	}
}

void UVBEncounterGateComponent::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue >= Data.OldValue || bDefeated)
	{
		return;
	}

	if (!bAwake && bWakeWhenDamaged)
	{
		Wake(TEXT("맞음"), true);
	}
}

void UVBEncounterGateComponent::HandleOwnerDeathCompleted(const FVBDeathFacts& Facts)
{
	if (bDefeated)
	{
		return;
	}
	// 쓰러뜨린 사람 = 죽인 효과의 Instigator(플레이어면 PlayerState). 효과 밖의 죽음이면 null - ApplyDefeated 가 첫 플레이어로 본다.
	ApplyDefeated(Cast<APlayerState>(Facts.Instigator), TEXT("체력 0"));
}

void UVBEncounterGateComponent::ApplyDefeated(APlayerState* Killer, const TCHAR* Why)
{
	if (bDefeated)
	{
		return;
	}

	bDefeated = true;
	RefreshDormantPose();
	if (!DefeatedEffects.IsEmpty())
	{
		FVBRuleContext Context = FVBRuleContext::Make(this, Killer ? Killer : GetFirstPlayerState());
		Context.Counterpart = GetOwner();
		VBRules::ApplyAll(DefeatedEffects, Context);
	}

	VB_LOG(Log, "조우 문: %s 해소(%s) - 효과 %d", *GetNameSafe(GetOwner()), Why, DefeatedEffects.Num());
}

#if WITH_EDITOR
EDataValidationResult UVBEncounterGateComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (bRequirePlayerPreparation && !IsValid(WakeArea))
	{
		Context.AddError(LOCTEXT("PreparationNeedsArea", "플레이어 준비를 확인하는 조우에는 현재 점유를 읽을 WakeArea가 필요합니다."));
		Result = EDataValidationResult::Invalid;
	}

	if (!VBRules::ValidateStructArray(ExistConditions, FVBCondition::StaticStruct(), LOCTEXT("ExistWhere", "존재 조건"), Context)
		|| !VBRules::ValidateStructArray(WakeConditions, FVBCondition::StaticStruct(), LOCTEXT("WakeWhere", "깨어날 조건"), Context)
		|| !VBRules::ValidateStructArray(WokenEffects, FVBEffect::StaticStruct(), LOCTEXT("WokenWhere", "깨어날 때 효과"), Context)
		|| !VBRules::ValidateStructArray(DefeatedEffects, FVBEffect::StaticStruct(), LOCTEXT("DefeatedWhere", "쓰러질 때 효과"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	// 장면 완료 영수증은 저장되지 않아, 불러온 뒤에도 참이 되도록 영역의 OnceFact 를 읽는다.
	if (const UWorld* World = GetWorld())
	{
		for (const TArray<FInstancedStruct>* Conditions : {&ExistConditions, &WakeConditions})
		{
			for (const FInstancedStruct& Item : *Conditions)
			{
				const FVBCondition* Condition = Item.GetPtr<FVBCondition>();
				const UObject* Scene = Condition ? Condition->GetTransientSceneRead() : nullptr;
				if (!Scene)
				{
					continue;
				}

				for (TActorIterator<AVBAreaTrigger> It(World); It; ++It)
				{
					if (!It->PlaysSceneOnce(Scene))
					{
						continue;
					}

					Context.AddError(FText::Format(LOCTEXT("TransientSceneWake",
						"{0}: 장면 {1} 의 완료는 저장되지 않는데 영역 {2} 는 그 장면을 한 번만 튼다 - 불러온 뒤 영영 참이 안 된다. 그 영역의 OnceFact 를 조건으로 쓴다."),
						FText::FromString(GetNameSafe(GetOwner())), FText::FromString(GetNameSafe(Scene)),
						FText::FromString(It->GetName())));
					Result = EDataValidationResult::Invalid;
				}
			}
		}
	}

	for (int32 Index = 0; Index < DormantPoses.Num(); ++Index)
	{
		const FVBEncounterDormantPose& Pose = DormantPoses[Index];
		if (!IsValid(Pose.Animation))
		{
			Context.AddError(FText::Format(LOCTEXT("DormantNoAnimation", "잠든 자세 {0}번 줄에 클립이 없습니다."), FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (!VBRules::ValidateStructArray(Pose.Conditions, FVBCondition::StaticStruct(), LOCTEXT("DormantWhere", "잠든 자세 조건"), Context))
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE


bool UVBEncounterGateComponent::HasValidWakeArea() const
{
	return IsValid(WakeArea) && WakeArea->GetWorld() == GetWorld() && !WakeArea->IsActorBeingDestroyed();
}

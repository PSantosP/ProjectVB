// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBInteractorComponent.h"

#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Interaction/VBGA_Interact.h"
#include "Interaction/VBPushableComponent.h"
#include "GameFramework/Character.h"
#include "Character/VBCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractableSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

UVBInteractorComponent* UVBInteractorComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBInteractorComponent>() : nullptr;
}

UVBInteractorComponent::UVBInteractorComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	// 시작은 꺼 둔다. BeginPlay 가 로컬 조종 폰일 때만 켠다.
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UVBInteractorComponent::BeginPlay()
{
	Super::BeginPlay();
	// 폰 소유가 BeginPlay 뒤에 정해질 수 있어(빙의 순서) 틱을 켠 뒤 틱에서 거른다.
	SetComponentTickEnabled(true);
}

bool UVBInteractorComponent::IsLocallyControlledOwner() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled();
}

void UVBInteractorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (IsLocallyControlledOwner() && GetPushable() && GetWorld()->GetTimeSeconds() >= NextPushInputTime)
	{
		ServerPushInput(LocalPushInput);
		NextPushInputTime = GetWorld()->GetTimeSeconds() + 0.1;
	}

	const bool bDead = UVBAbilitySystemStatics::IsActorDead(GetOwner());

	// 자동 대상은 서버가 판정한다(완료 판정을 서버가 맡는 것과 같은 규칙).
	if (GetOwner() && GetOwner()->HasAuthority() && !bDead)
	{
		UpdateAutoInteractables();
	}

	if (!IsLocallyControlledOwner())
	{
		SetFocus(nullptr);
		return;
	}

	// 사망하면 프롬프트를 내린다. 입력이 막혀도 초점이 남아 있으면 시체 위에 「열기」가 뜬다.
	if (bDead)
	{
		SetFocus(nullptr);
		return;
	}

	SetFocus(FindBestInteractable());
}

void UVBInteractorComponent::UpdateAutoInteractables()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	UVBInteractableSubsystem* Subsystem = World ? World->GetSubsystem<UVBInteractableSubsystem>() : nullptr;
	if (!Pawn || !Subsystem)
	{
		return;
	}

	for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : Subsystem->GetInteractables())
	{
		UVBInteractableComponent* Candidate = Entry.Get();
		if (!Candidate)
		{
			continue;
		}

		const UVBInteractableConfig* Config = Candidate->GetConfig();
		if (Config->Mode != EVBInteractMode::Auto)
		{
			continue;
		}
		// 조건 칸이 거짓인 동안은 들어옴으로 안 쳐서, 안에 있는 동안 참이 되면 그때 한 번 발화한다.
		// 자동은 시도가 아니라 거절 글, 효과를 안 낸다. 거리를 먼저 봐 범위 밖 대상의 조건은 평가하지 않는다.
		const bool bInside = Candidate->IsInteractionEnabled() &&
			FVector::DistSquared(Pawn->GetActorLocation(), Candidate->GetComponentLocation()) <= FMath::Square(Config->InteractRange) &&
			Candidate->PassesUseConditions(GetOwner());
		const bool bWasInside = AutoInside.Contains(Candidate);
		if (bInside && !bWasInside)
		{
			// 들어오는 순간 한 번. 머무는 동안 매 틱 다시 걸면 저장 포인트가 계속 저장한다.
			AutoInside.Add(Candidate);
			Candidate->NotifyInteracted(GetOwner());
		}
		else if (!bInside && bWasInside)
		{
			AutoInside.Remove(Candidate);
		}
	}
	// 파괴된 대상의 약참조를 걷는다. 끊긴 약참조는 null 과 같다고 비교되지 않아 Remove(nullptr) 로는 안 빠진다.
	for (auto It = AutoInside.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UVBInteractorComponent::ClientInteractionRefused_Implementation(const FText& RefusedText)
{
	VB_LOG(Log, "상호작용 거절 받음: %s", *RefusedText.ToString());
	// 듣는 쪽(HUD 알림 줄)이 없으면 거절이 화면에 안 와 E 가 안 먹는 것처럼 보이므로 경고한다.
	VB_CLOG(!RefusedText.IsEmpty() && !OnInteractionRefused.IsBound(), Warning,
		"상호작용 거절 글을 띄울 곳이 없다(HUD 미구독): %s", *RefusedText.ToString());
	OnInteractionRefused.Broadcast(RefusedText);
}

void UVBInteractorComponent::SetFocus(UVBInteractableComponent* NewFocus)
{
	if (FocusedInteractable.Get() == NewFocus)
	{
		return;
	}

	FocusedInteractable = NewFocus;
	OnFocusChanged.Broadcast(NewFocus);
}

bool UVBInteractorComponent::CanReach(const UVBInteractableComponent* Interactable) const
{
	return Reaches(Interactable, /*bCheckFacing=*/true);
}

bool UVBInteractorComponent::CanKeep(const UVBInteractableComponent* Interactable) const
{
	return Reaches(Interactable, /*bCheckFacing=*/false);
}

bool UVBInteractorComponent::Reaches(const UVBInteractableComponent* Interactable, bool bCheckFacing) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Interactable || !Interactable->IsInteractionEnabled())
	{
		return false;
	}

	const UVBInteractableConfig* Config = Interactable->GetConfig();
	const FVector TargetLocation = Interactable->GetComponentLocation();

	if (FVector::DistSquared(Pawn->GetActorLocation(), TargetLocation) > FMath::Square(Config->InteractRange))
	{
		return false;
	}
	// 기준 방향 = 컨트롤 회전(카메라). 서버에도 있어 서버 판정과 클라 초점이 같은 답을 낸다.
	// 이어 가기는 방향만 끈다(높이, 시야는 그대로).
	FVBReachRules Rules = Config->ReachRules;
	if (!bCheckFacing)
	{
		Rules.MaxYawAngle = 180.0f;
	}

	return UVBAbilitySystemStatics::PassesReachRules(Pawn, Pawn->GetControlRotation().Yaw, TargetLocation,
		Interactable->GetOwner(), Rules, Interactable->GetTargetParts());
}

UVBInteractableComponent* UVBInteractorComponent::FindBestInteractable() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	UVBInteractableSubsystem* Subsystem = World ? World->GetSubsystem<UVBInteractableSubsystem>() : nullptr;
	if (!Pawn || !Subsystem)
	{
		return nullptr;
	}

	const FVector Origin = Pawn->GetActorLocation();
	const float ViewYaw = Pawn->GetControlRotation().Yaw;

	// 카메라 방향과의 평면 각이 가장 작은 것. 거리는 섞지 않는다 - 고르는 수단이 보는 방향 하나여야 예측된다.
	UVBInteractableComponent* Best = nullptr;
	float BestDeltaYaw = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : Subsystem->GetInteractables())
	{
		UVBInteractableComponent* Candidate = Entry.Get();
		// 자동 대상은 누를 것이 아니라 프롬프트를 띄우지 않는다.
		if (!CanReach(Candidate) || Candidate->GetConfig()->Mode == EVBInteractMode::Auto)
		{
			continue;
		}

		FVector ToCandidate = Candidate->GetComponentLocation() - Origin;
		ToCandidate.Z = 0.0f;
		const float DeltaYaw = ToCandidate.IsNearlyZero()
			? 0.0f
			: FMath::Abs(FMath::FindDeltaAngleDegrees(ViewYaw, ToCandidate.Rotation().Yaw));
		if (DeltaYaw < BestDeltaYaw)
		{
			BestDeltaYaw = DeltaYaw;
			Best = Candidate;
		}
	}

	return Best;
}

void UVBInteractorComponent::BeginMotion(UVBInteractableComponent* Target, uint8 MotionIndex)
{
	if (!VBServerWrite::HasOwnerAuthority(*this) || !IsValid(Target)
		|| !Target->GetConfig()->GetMotionByIndex(MotionIndex))
	{
		return;
	}

	MotionState.Target = Target;
	MotionState.MotionIndex = MotionIndex;
	OnRep_MotionState();
	GetOwner()->ForceNetUpdate();
}

bool UVBInteractorComponent::OwnsCapsuleFacing() const
{
	return IsValid(MotionState.Target);
}

void UVBInteractorComponent::EndMotion()
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	MotionState = FVBEnvironmentMotionState();
	LocalPushInput = 0.0f;
	OnRep_MotionState();
	GetOwner()->ForceNetUpdate();
}

const FVBInteractionMotion* UVBInteractorComponent::GetActiveMotion() const
{
	return IsValid(MotionState.Target) ? MotionState.Target->GetConfig()->GetMotionByIndex(MotionState.MotionIndex) : nullptr;
}

void UVBInteractorComponent::OnRep_MotionState()
{
	if (!GetPushable())
	{
		LocalPushInput = 0.0f;
	}
}

UVBPushableComponent* UVBInteractorComponent::GetPushable() const
{
	return Cast<UVBPushableComponent>(MotionState.Target);
}

void UVBInteractorComponent::SetPushInput(float Value)
{
	LocalPushInput = FMath::IsFinite(Value) ? FMath::Clamp(Value, -1.0f, 1.0f) : 0.0f;
}

void UVBInteractorComponent::ServerPushInput_Implementation(float Value)
{
	if (UVBPushableComponent* Pushable = GetPushable())
	{
		Pushable->SetPushInput(Cast<ACharacter>(GetOwner()), Value);
	}
}

void UVBInteractorComponent::RequestRepeatInteraction()
{
	ServerRepeatInteraction();
}

void UVBInteractorComponent::ServerRepeatInteraction_Implementation()
{
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!ASC)
	{
		return;
	}

	FScopedAbilityListLock ActiveLock(*ASC);
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.IsActive())
		{
			continue;
		}

		if (UVBGA_Interact* Ability = Cast<UVBGA_Interact>(Spec.GetPrimaryInstance()))
		{
			Ability->HandleRepeatPress();
			break;
		}
	}
}

void UVBInteractorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBInteractorComponent, MotionState);
}

void UVBInteractorComponent::BeginHoldDisplay(float Duration)
{
	const UWorld* World = GetWorld();
	HoldStartTime = World ? World->GetTimeSeconds() : -1.0;
	HoldDuration = Duration;
}

void UVBInteractorComponent::EndHoldDisplay()
{
	HoldStartTime = -1.0;
	HoldDuration = 0.0f;
}

float UVBInteractorComponent::GetHoldProgress() const
{
	const UWorld* World = GetWorld();
	if (HoldStartTime < 0.0 || HoldDuration <= 0.0f || !World)
	{
		return -1.0f;
	}

	return FMath::Clamp(static_cast<float>(World->GetTimeSeconds() - HoldStartTime) / HoldDuration, 0.0f, 1.0f);
}

#include "Interaction/VBPushableComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/VBPushableConfig.h"

#define LOCTEXT_NAMESPACE "VBPushable"

// 밀기 몽타주가 가져야 하는 섹션 이름.
namespace VBPushSection
{
	const FName Grab(TEXT("Grab"));
	const FName Idle(TEXT("Idle"));
	const FName Push(TEXT("Push"));
	const FName Pull(TEXT("Pull"));
	const FName Release(TEXT("Release"));
}

UVBPushableComponent::UVBPushableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bInteractionEnabled = false;
	bSaveState = false;
}

const UVBPushableConfig* UVBPushableComponent::GetPushConfig() const
{
	return Cast<UVBPushableConfig>(Config);
}

FCollisionQueryParams UVBPushableComponent::MakePushSweepParams(const AActor* Owner, const AActor* Character, TConstArrayView<TObjectPtr<AActor>> Parts)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PushPair), false, Owner);
	Params.AddIgnoredActor(Character);
	for (const TObjectPtr<AActor>& Part : Parts)
	{
		// 비거나 다른 월드의 조각은 IsDataValid 가 저장 전에 막는다. 여기서는 유효한 것만 뺀다.
		if (IsValid(Part.Get()))
		{
			Params.AddIgnoredActor(Part.Get());
		}
	}
	return Params;
}

bool UVBPushableComponent::CaptureMovedTransform(FTransform& Out) const
{
	const AActor* Owner = GetOwner();
	// 1cm 안쪽은 '안 밀었음' - 밀기 끝의 미세한 위치 오차로 저장 맵이 쌓이지 않게.
	if (!Owner || FVector::DistSquared(Owner->GetActorLocation(), InitialLocation) <= FMath::Square(1.0))
	{
		return false;
	}

	Out = Owner->GetActorTransform();
	return true;
}

bool UVBPushableComponent::ApplySavedTransform(const FTransform& Saved)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Pusher.IsValid())
	{
		return false;
	}

	Owner->SetActorTransform(Saved, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

void UVBPushableComponent::BeginPlay()
{
	bInteractionEnabled = GetPushConfig() != nullptr;
	// 기준점은 등록보다 먼저 잡는다 - Super::BeginPlay 의 등록이 저장된 자리를 바로 적용하므로, 뒤에서 잡으면 민 자리가 기준이 되어
	//   이동 한계가 불러올 때마다 밀려난다(a6 검토 F2).
	InitialLocation = GetOwner()->GetActorLocation();
	Super::BeginPlay();
	if (GetPushConfig() && GetOwner()->HasAuthority())
	{
		GetOwner()->SetReplicates(true);
		GetOwner()->SetReplicateMovement(true);
	}
}

FTransform UVBPushableComponent::GetStandTransform() const
{
	const UVBPushableConfig* Settings = GetPushConfig();
	if (!Settings)
	{
		return GetOwner()->GetActorTransform();
	}

	const FQuat Rotation = GetOwner()->GetActorQuat();
	const FVector Axis = Rotation.RotateVector(Settings->MoveAxis).GetSafeNormal2D();
	return FTransform(Axis.Rotation(), GetOwner()->GetActorLocation() + Rotation.RotateVector(Settings->StandOffset));
}

bool UVBPushableComponent::Reserve(ACharacter* Character)
{
	const UVBPushableConfig* Settings = GetPushConfig();
	const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!GetOwner()->HasAuthority() || !IsValid(Character) || Pusher.IsValid() || !Settings
		|| !Settings->ActivateMotion.CharacterMontage || Settings->MoveAxis.GetSafeNormal2D().IsNearlyZero()
		|| !Root || Root->Mobility != EComponentMobility::Movable || Root->IsSimulatingPhysics()
		|| !Character->GetCharacterMovement()->IsMovingOnGround())
	{
		return false;
	}

	for (const FName Section :
		{ VBPushSection::Grab, VBPushSection::Idle, VBPushSection::Push, VBPushSection::Pull, VBPushSection::Release })
	{
		if (!Settings->ActivateMotion.CharacterMontage->IsValidSectionName(Section))
		{
			return false;
		}
	}

	Pusher = Character;
	bReleasing = false;
	Input = 0.0f;
	return true;
}

bool UVBPushableComponent::StartPushing(ACharacter* Character)
{
	if (!GetOwner()->HasAuthority() || Pusher.Get() != Character || !IsValid(Character))
	{
		return false;
	}

	PreviousMovementMode = Character->GetCharacterMovement()->MovementMode;
	Character->GetCharacterMovement()->DisableMovement();
	bMoving = true;
	LastInputTime = GetWorld()->GetTimeSeconds();
	SetComponentTickEnabled(true);
	SetActionTextOverride(LOCTEXT("Release", "\ub193\uae30"));
	return true;
}

void UVBPushableComponent::StopPushing(ACharacter* Character)
{
	if (!GetOwner()->HasAuthority() || Pusher.Get() != Character)
	{
		return;
	}

	if (bMoving && IsValid(Character) && Character->GetCharacterMovement()->MovementMode == MOVE_None
		&& !UVBAbilitySystemStatics::IsActorDead(Character))
	{
		Character->GetCharacterMovement()->SetMovementMode(static_cast<EMovementMode>(PreviousMovementMode));
	}

	bMoving = false;
	bReleasing = false;
	Input = 0.0f;
	Pusher.Reset();
	SetComponentTickEnabled(false);
	SetActionTextOverride(FText::GetEmpty());
}

void UVBPushableComponent::SetPushInput(ACharacter* Character, float Value)
{
	if (!GetOwner()->HasAuthority() || Pusher.Get() != Character || !FMath::IsFinite(Value))
	{
		return;
	}

	Input = FMath::Clamp(Value, -1.0f, 1.0f);
	LastInputTime = GetWorld()->GetTimeSeconds();
}

void UVBPushableComponent::RequestRelease(ACharacter* Character)
{
	if (!GetOwner()->HasAuthority() || Pusher.Get() != Character || bReleasing)
	{
		return;
	}

	bReleasing = true;
	Input = 0.0f;
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character))
	{
		ASC->CurrentMontageJumpToSection(VBPushSection::Release);
		ASC->CurrentMontageSetPlayRate(1.0f);
	}
}

void UVBPushableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	ACharacter* Character = Pusher.Get();
	const UVBPushableConfig* Settings = GetPushConfig();
	if (GetOwner()->HasAuthority() && bMoving && !Character)
	{
		StopPushing(nullptr);
		return;
	}

	if (!GetOwner()->HasAuthority() || !bMoving || !Character || !Settings || bReleasing)
	{
		return;
	}

	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	UAnimMontage* Montage = Settings->ActivateMotion.CharacterMontage;
	if (!Anim || !Anim->Montage_IsPlaying(Montage))
	{
		return;
	}

	const FName Section = Anim->Montage_GetCurrentSection(Montage);
	if (Section == VBPushSection::Grab || Section == VBPushSection::Release)
	{
		return;
	}

	const float Intent = GetWorld()->GetTimeSeconds() - LastInputTime <= 0.35 ? Input : 0.0f;
	const FVector Axis = GetOwner()->GetActorQuat().RotateVector(Settings->MoveAxis).GetSafeNormal2D();
	const float CurrentTravel = FVector::DotProduct(GetOwner()->GetActorLocation() - InitialLocation, Axis);
	const float Distance = Settings->ClampTravel(CurrentTravel + Intent * Settings->MoveSpeed * DeltaTime) - CurrentTravel;
	const FVector Delta = Axis * Distance;
	float AppliedDistance = 0.0f;
	if (!Delta.IsNearlyZero())
	{
		// 둘을 각각 검사하고 더 짧은 이동을 함께 적용한다. 캐릭터만 끼이거나 물체만 벽을 뚫지 않는다.
		const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
		const FCollisionQueryParams Params = MakePushSweepParams(GetOwner(), Character, GetTargetParts());
		FHitResult PropHit, PawnHit;
		// 회전된 메시도 보수적인 월드 AABB로 검사한다. 바닥 접촉면에 1cm 여유를 둔다.
		FVector Extent = Root->Bounds.BoxExtent;
		Extent.Z = FMath::Max(1.0, Extent.Z - 0.5);
		const FVector Center = Root->Bounds.Origin + FVector(0, 0, 0.5);
		const bool bPropBlocked = GetWorld()->SweepSingleByChannel(PropHit, Center, Center + Delta, FQuat::Identity,
			ECC_Pawn, FCollisionShape::MakeBox(Extent), Params);
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const FVector PawnLocation = Character->GetActorLocation();
		const bool bPawnBlocked = GetWorld()->SweepSingleByChannel(PawnHit, PawnLocation, PawnLocation + Delta, FQuat::Identity,
			ECC_Pawn, Capsule->GetCollisionShape(), Params);
		const float Fraction = UVBPushableConfig::SharedMoveFraction(bPropBlocked ? PropHit.Time : 1.0f,
			bPawnBlocked ? PawnHit.Time : 1.0f);
		AppliedDistance = Distance * Fraction;
		const FVector AllowedDelta = Axis * AppliedDistance;
		GetOwner()->SetActorLocation(GetOwner()->GetActorLocation() + AllowedDelta);
		Character->SetActorLocation(PawnLocation + AllowedDelta);
	}

	const FName Wanted = FMath::Abs(AppliedDistance) < 0.01f ? VBPushSection::Idle
		: (AppliedDistance > 0.0f ? VBPushSection::Push : VBPushSection::Pull);
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character))
	{
		if (Section != Wanted)
		{
			ASC->CurrentMontageJumpToSection(Wanted);
		}

		const float ReferenceSpeed = Wanted == VBPushSection::Pull ? Settings->PullAnimationReferenceSpeed : Settings->AnimationReferenceSpeed;
		const float Rate = Wanted == VBPushSection::Idle ? 1.0f : FMath::Max(0.01f,
			FMath::Abs(AppliedDistance) / FMath::Max(DeltaTime, UE_SMALL_NUMBER)
			/ FMath::Max(ReferenceSpeed, 1.0f));
		if (!FMath::IsNearlyEqual(Anim->Montage_GetPlayRate(Montage), Rate, 0.01f))
		{
			ASC->CurrentMontageSetPlayRate(Rate);
		}
	}
}

bool UVBPushableComponent::GetHandTargets(UAnimInstance* Anim, FTransform& Left, FTransform& Right, float& Weight) const
{
	const UVBPushableConfig* Settings = GetPushConfig();
	if (!Settings || !Anim || !Anim->Montage_IsPlaying(Settings->ActivateMotion.CharacterMontage))
	{
		return false;
	}

	UAnimMontage* Montage = Settings->ActivateMotion.CharacterMontage;
	const float Time = Anim->Montage_GetPosition(Montage);
	Weight = FMath::Clamp((Time - Settings->GrabContactStartTime) / FMath::Max(0.01f, Settings->GrabBlendTime), 0.0f, 1.0f);
	if (Anim->Montage_GetCurrentSection(Montage) == VBPushSection::Release)
	{
		float Start, End;
		Montage->GetSectionStartAndEndTime(Montage->GetSectionIndex(VBPushSection::Release), Start, End);
		Weight = 1.0f - FMath::Clamp((Time - Start) / FMath::Max(0.01f, Settings->ReleaseBlendTime), 0.0f, 1.0f);
	}

	const FTransform Prop(GetOwner()->GetActorQuat(), GetOwner()->GetActorLocation());
	Left = Settings->LeftHand * Prop;
	Right = Settings->RightHand * Prop;
	return Weight > 0.0f;
}

void UVBPushableComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ACharacter* Character = Pusher.Get())
	{
		if (GetOwner()->HasAuthority())
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character))
			{
				FGameplayTagContainer Tags(VBGameplayTags::Ability_Interact);
				ASC->CancelAbilities(&Tags);
			}

			StopPushing(Character);
		}
	}

	Super::EndPlay(Reason);
}

#undef LOCTEXT_NAMESPACE

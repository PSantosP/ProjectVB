#include "Character/VBWeaponVisualComponent.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBSummonedWeapon.h"
#include "Character/VBSwingTrailComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "AbilitySystemComponent.h"
#include "VB/VB.h"

UVBWeaponVisualComponent::UVBWeaponVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UVBWeaponVisualComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<AVBCharacter>(GetOwner());
	WeaponState = Character ? Character->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
	Appearance = Character ? Character->FindComponentByClass<UVBCharacterAppearanceComponent>() : nullptr;
	if (UVBCharacterAppearanceComponent* AppearanceComp = Appearance.Get())
	{
		AppearanceChangedHandle = AppearanceComp->OnAppearanceChanged.AddUObject(this, &UVBWeaponVisualComponent::HandleAppearanceChanged);
	}

	if (GetNetMode() == NM_DedicatedServer || !WeaponState || !VisualConfig)
	{
		SetComponentTickEnabled(false);
		if (GetNetMode() != NM_DedicatedServer && !VisualConfig)
		{
			VB_LOG(Warning, "WeaponVisual: VisualConfig is not assigned on %s", *GetNameSafe(GetOwner()));
		}
	}
}

void UVBWeaponVisualComponent::CreateVisuals(EVBWeaponType Type)
{
	VisibleType = Type;
	const FVBWeaponVisualStyle* Style = VisualConfig->Styles.Find(Type);
	if (!Style)
	{
		return;
	}

	FormationDuration = FMath::Max(Style->FormationDuration, 0.05f);
	DissolveDuration = FMath::Max(Style->DissolveDuration, 0.05f);
	Visuals.SetNum(Style->Parts.Num());
	for (int32 PartIndex = 0; PartIndex < Style->Parts.Num(); ++PartIndex)
	{
		const FVBWeaponVisualPart& Part = Style->Parts[PartIndex];
		if (!Character->GetMesh()->DoesSocketExist(Part.Socket))
		{
			VB_LOG(Warning, "WeaponVisual: socket %s missing on %s", *Part.Socket.ToString(), *GetNameSafe(Character));
			continue;
		}

		FActorSpawnParameters Params;
		Params.Owner = Character;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVBSummonedWeapon* Visual = GetWorld()->SpawnActor<AVBSummonedWeapon>(AVBSummonedWeapon::StaticClass(),
			FTransform::Identity, Params);
		if (!Visual)
		{
			continue;
		}

		Visual->VisualPart = Part;
		Visual->SetShapeBody(ResolveShapeBody());
		Visual->RebuildVisual();
		AttachVisualToHand(Visual);
		Visual->SetActorTickEnabled(false); // 진행도는 이 컴포넌트 한 곳에서 관리한다.
		Visuals[PartIndex] = Visual;
	}

	RefreshGatedParts();
}

FGuid UVBWeaponVisualComponent::BeginPresentationHide()
{
	if (!IsRegistered() || IsBeingDestroyed() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed())
	{
		return FGuid();
	}

	const FGuid Token = FGuid::NewGuid();
	PresentationHolds.Add(Token);
	RefreshGatedParts();
	return Token;
}

bool UVBWeaponVisualComponent::EndPresentationHide(FGuid Token)
{
	if (PresentationHolds.Remove(Token) == 0)
	{
		return false;
	}

	RefreshGatedParts();
	return true;
}

bool UVBWeaponVisualComponent::IsPartShown(const FVBWeaponVisualPart& Part, const FGameplayTagContainer& OwnedTags)
{
	return !Part.RequiredPassive.IsValid() || OwnedTags.HasTagExact(Part.RequiredPassive);
}

UAbilitySystemComponent* UVBWeaponVisualComponent::GetBodyAbilitySystem() const
{
	return Character ? Character->GetAbilitySystemComponent() : nullptr;
}

void UVBWeaponVisualComponent::OnGatedTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	RefreshGatedParts();
}

void UVBWeaponVisualComponent::UnbindGatedTags()
{
	if (UAbilitySystemComponent* ASC = GatedTagASC.Get())
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Entry : GatedTagHandles)
		{
			ASC->RegisterGameplayTagEvent(Entry.Key, EGameplayTagEventType::NewOrRemoved).Remove(Entry.Value);
		}
	}

	GatedTagHandles.Reset();
	GatedTagASC.Reset();
}

void UVBWeaponVisualComponent::RefreshGatedParts()
{
	UAbilitySystemComponent* ASC = GetBodyAbilitySystem();
	FGameplayTagContainer Owned;
	if (ASC)
	{
		ASC->GetOwnedGameplayTags(Owned);
	}
	// 외형이 바뀔 때마다 그 외형이 거는 태그만 다시 듣는다(ASC 가 바뀌어도 새 ASC 에).
	if (GatedTagASC.Get() != ASC)
	{
		UnbindGatedTags();
	}

	for (AVBSummonedWeapon* Visual : Visuals)
	{
		if (!IsValid(Visual))
		{
			continue;
		}

		const FGameplayTag Gate = Visual->VisualPart.RequiredPassive;
		VB_CLOG(Gate.IsValid() && !ASC, Warning, "WeaponVisual: %s 의 %s 외형 - 몸에 ASC 가 없어 숨긴다", *GetNameSafe(Character), *Gate.ToString());
		if (ASC && Gate.IsValid()
			&& !GatedTagHandles.ContainsByPredicate([&Gate](const TPair<FGameplayTag, FDelegateHandle>& E) { return E.Key == Gate; }))
		{
			GatedTagHandles.Emplace(Gate,
				ASC->RegisterGameplayTagEvent(Gate, EGameplayTagEventType::NewOrRemoved).AddUObject(this,
				&UVBWeaponVisualComponent::OnGatedTagChanged));
			GatedTagASC = ASC;
		}

		Visual->SetActorHiddenInGame(!PresentationHolds.IsEmpty() || !IsPartShown(Visual->VisualPart, Owned));
	}
}

void UVBWeaponVisualComponent::AttachVisualToHand(AVBSummonedWeapon* Visual) const
{
	USceneComponent* Parent = Character->GetMesh();
	FName Socket = Visual->VisualPart.Socket;
	if (const UVBCharacterAppearanceComponent* AppearanceComp = Appearance.Get())
	{
		AppearanceComp->ResolveAttachTarget(Visual->VisualPart.Socket, Parent, Socket);
	}

	Visual->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
	Visual->SetActorRelativeTransform(Visual->VisualPart.AttachmentTransform);
}

void UVBWeaponVisualComponent::HandleAppearanceChanged(UVBCharacterAppearanceComponent* InAppearance)
{
	RetireVisualTrailSources();
	// 진행도, 전환 추적은 그대로 두고 부착 위치와 모양을 따올 메시만 바꾼다 - 소환 중이던 칼이 처음부터 다시 생기지 않는다.
	USkeletalMeshComponent* Body = ResolveShapeBody();
	for (AVBSummonedWeapon* Visual : Visuals)
	{
		if (IsValid(Visual))
		{
			AttachVisualToHand(Visual);
			Visual->SetShapeBody(Body);
		}
	}
}

USkeletalMeshComponent* UVBWeaponVisualComponent::ResolveShapeBody() const
{
	if (Appearance.IsValid() && Appearance->GetPrimaryVisualMesh())
	{
		return Appearance->GetPrimaryVisualMesh();
	}

	return Character ? Character->GetMesh() : nullptr;
}

bool UVBWeaponVisualComponent::ResolveTrailSource(int32 VisualPartIndex, FName FirstSocket, FName SecondSocket,
	FVBTrailMeshSource& OutSource) const
{
	OutSource = {};
	UMeshComponent* Source = nullptr;
	if (VisualPartIndex != INDEX_NONE)
	{
		const AVBSummonedWeapon* Visual = Visuals.IsValidIndex(VisualPartIndex) ? Visuals[VisualPartIndex].Get() : nullptr;
		if (!IsValid(Visual) || Visual->IsHidden() || !Visual->WeaponMesh->GetStaticMesh())
		{
			return false;
		}

		Source = Visual->WeaponMesh;
	}
	else
	{
		Source = ResolveShapeBody();
		if (const UVBCharacterAppearanceComponent* AppearanceOwner = Appearance.Get())
		{
			USceneComponent* FirstParent = nullptr;
			USceneComponent* SecondParent = nullptr;
			AppearanceOwner->ResolveAttachTarget(FirstSocket, FirstParent, FirstSocket);
			AppearanceOwner->ResolveAttachTarget(SecondSocket, SecondParent, SecondSocket);
			if (FirstParent != Source || SecondParent != Source)
			{
				return false;
			}
		}
	}

	if (!Source || FirstSocket.IsNone() || SecondSocket.IsNone() || FirstSocket == SecondSocket
		|| !Source->DoesSocketExist(FirstSocket) || !Source->DoesSocketExist(SecondSocket))
	{
		return false;
	}

	OutSource.Mesh = Source;
	OutSource.FirstSocket = FirstSocket;
	OutSource.SecondSocket = SecondSocket;
	return true;
}

void UVBWeaponVisualComponent::RetireVisualTrailSources()
{
	AActor* Owner = GetOwner();
	if (UVBSwingTrailComponent* Trail = Owner ? Owner->FindComponentByClass<UVBSwingTrailComponent>() : nullptr)
	{
		Trail->RetireVisualSources();
	}
}

void UVBWeaponVisualComponent::BeginWeaponTransition(EVBWeaponType ToType, UAnimInstance* AnimInstance, UAnimMontage* Montage)
{
	ResetTransition();
	if (GetNetMode() == NM_DedicatedServer || !VisualConfig)
	{
		return;
	}

	TransitionTo = ToType;
	ReplicationGraceRemaining = FMath::Max(VisualConfig->ReplicationGracePeriod, 0.0f);
	if (!AnimInstance || !Montage)
	{
		return;
	}

	const FVBWeaponVisualTiming* Timing = VisualConfig->MontageTimings.Find(Montage);
	FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!Timing || !Timing->IsValid(Montage->GetPlayLength()) || !Instance)
	{
		VB_LOG(Warning, "WeaponVisual: missing/invalid timing or instance for %s; using state fallback", *GetNameSafe(Montage));
		return;
	}
	// 빠른 재교체 때 서버의 From 타입으로 새 외형을 만들면 아직 나타나지도 않은 검이 번쩍인다.
	// 실제 화면에 남아 있는 외형과 그 진행도를 다음 전환의 출발점으로 사용한다.
	TransitionFrom = VisibleType;
	TransitionStartProgress = Progress;
	TransitionTiming = *Timing;
	TransitionAnimInstance = AnimInstance;
	TransitionInstanceId = Instance->GetInstanceID();
}

void UVBWeaponVisualComponent::ApplyVisual(EVBWeaponType Type, float NewProgress)
{
	NewProgress = FMath::Clamp(NewProgress, 0.0f, 1.0f);
	if (Type == EVBWeaponType::None || NewProgress <= 0.0f)
	{
		DestroyVisuals();
		return;
	}

	if (VisibleType != Type)
	{
		DestroyVisuals();
		CreateVisuals(Type);
	}

	if (Progress == NewProgress)
	{
		return;
	}

	Progress = NewProgress;
	for (AVBSummonedWeapon* Visual : Visuals)
	{
		if (IsValid(Visual))
		{
			Visual->SetManifestProgress(Progress);
		}
	}
}

void UVBWeaponVisualComponent::ResetTransition()
{
	TransitionAnimInstance.Reset();
	TransitionInstanceId = INDEX_NONE;
	ReplicationGraceRemaining = 0.0f;
}

bool UVBWeaponVisualComponent::UpdateTransition()
{
	if (TransitionInstanceId == INDEX_NONE)
	{
		return false;
	}

	UAnimInstance* AnimInstance = TransitionAnimInstance.Get();
	FAnimMontageInstance* Instance = AnimInstance ? AnimInstance->GetMontageInstanceForID(TransitionInstanceId) : nullptr;
	// 동일 에셋을 다시 재생해도 이전 인스턴스를 추적한다. 중단된 납도/발도는 목표 외형으로
	// 확정하여 공격 몽타주가 시작했는데 미완성 검 또는 빈손으로 남는 상황을 막는다.
	if (!Instance || !Instance->IsActive() || !Character->GetMesh() || Character->GetMesh()->GetAnimInstance() != AnimInstance)
	{
		ApplyVisual(TransitionTo, 1.0f);
		TransitionAnimInstance.Reset();
		TransitionInstanceId = INDEX_NONE;
		return true;
	}

	const float Position = Instance->GetPosition();
	if (Position < TransitionTiming.DissolveEnd)
	{
		ApplyVisual(TransitionFrom, TransitionStartProgress * TransitionTiming.GetOutgoingProgress(Position));
	}
	else
	{
		ApplyVisual(TransitionTo, TransitionTiming.GetIncomingProgress(Position));
	}

	return true;
}

void UVBWeaponVisualComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsValid(Character) || !IsValid(WeaponState) || !VisualConfig)
	{
		return;
	}

	if (Character->IsDead() || WeaponState->IsTempSheathed() || Character->IsHidden())
	{
		ResetTransition();
		DestroyVisuals();
		return;
	}

	EVBWeaponType Desired = WeaponState->IsWeaponSummoned() ? WeaponState->GetCurrentWeaponType() : EVBWeaponType::None;
	// RPC/프로퍼티의 수신 순서에 의존하지 않는다. 늦은 합류는 RPC 없이 아래 상태 폴백으로 복구한다.
	if (Desired == TransitionTo)
	{
		ReplicationGraceRemaining = 0.0f;
	}

	if (ReplicationGraceRemaining > 0.0f)
	{
		ReplicationGraceRemaining = FMath::Max(0.0f, ReplicationGraceRemaining - DeltaTime);
		Desired = TransitionTo;
	}

	if (UpdateTransition())
	{
		return;
	}

	// 몽타주 부재/재생 실패/이동 게이트로 생략된 경우에만 기존 시간 기반 실체화를 사용한다.
	if (Desired != EVBWeaponType::None && Desired != VisibleType)
	{
		DestroyVisuals();
		CreateVisuals(Desired);
	}

	if (VisibleType == EVBWeaponType::None)
	{
		return;
	}

	ApplyVisual(VisibleType, Progress + DeltaTime * (Desired == EVBWeaponType::None ? -1.0f / DissolveDuration : 1.0f / FormationDuration));
}

void UVBWeaponVisualComponent::DestroyVisuals()
{
	RetireVisualTrailSources();
	for (AVBSummonedWeapon* Visual : Visuals)
	{
		if (IsValid(Visual))
		{
			Visual->Destroy();
		}
	}

	Visuals.Reset();
	UnbindGatedTags();
	VisibleType = EVBWeaponType::None;
	Progress = 0.0f;
}

void UVBWeaponVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PresentationHolds.Reset();
	ResetTransition();
	DestroyVisuals();
	if (UVBCharacterAppearanceComponent* AppearanceComp = Appearance.Get())
	{
		AppearanceComp->OnAppearanceChanged.Remove(AppearanceChangedHandle);
	}

	AppearanceChangedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBFinisherComponent.h"
#include "Character/VBFinisherActorQueries.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

#include "ContextualAnimSceneActorComponent.h"
#include "ContextualAnimSceneAsset.h" // GetNameSafe 가 UObject* 로 변환하려면 완전한 타입이 필요하다
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBInteractionAnimInterface.h"
#include "Animation/VBInteractionIK.h"
#include "Animation/VBAnimNotify_InteractionRagdoll.h"
#include "Animation/VBAnimNotifyState_InteractionWarp.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Types/VBWeaponStateTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBFinisherConfig.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h"
#include "Net/UnrealNetwork.h"        // DOREPLIFETIME
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchDatabase.h"           // 불완전 타입이면 GetNameSafe 가 UObject* 로 변환을 못 한다
#include "PoseSearch/PoseSearchLibrary.h"             // UPoseSearchLibrary::GetActor
#include "VB/VB.h"        // VB_LOG

// 클래스 하나에 붙는 델리게이트다. 구독자는 자기 월드인지 스스로 가른다(헤더 선언 주석).
FVBFinisherConfigArrived UVBFinisherComponent::OnConfigArrived;
FVBInteractionParticipantChanged UVBFinisherComponent::OnInteractionParticipantChanged;
TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UVBFinisherComponent>> UVBFinisherComponent::ParticipantOwners;

UVBFinisherComponent::UVBFinisherComponent()
{
	// 자리 보정 동안에만 돈다. bCanEverTick 이 false 면 나중에 켜도 조용히 안 켜지므로 꺼진 채로 둔다.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 컴포넌트가 복제 대상이어야 ActiveConfig 가 원격 클라로 간다.
	SetIsReplicatedByDefault(true);
}

void UVBFinisherComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 조건 없음 - 구경하는 클라도 연출을 보고, 처형당 두 번 바뀌는 포인터 하나다.
	DOREPLIFETIME(UVBFinisherComponent, ActiveConfig);
	DOREPLIFETIME(UVBFinisherComponent, InteractionReplay);
}

void UVBFinisherComponent::BeginPlay()
{
	Super::BeginPlay();
	Selection.Prewarm(GetOwner()); // 첫 처형이 색인 빌드를 기다리지 않게
}

void UVBFinisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(InteractionReplayRetryTimer);
	}

	FinishInteraction(EVBInteractionEnd::Abandoned);
	Super::EndPlay(EndPlayReason);
}

void UVBFinisherComponent::OnRep_ActiveConfig()
{
	if (LocalInteractionSession != 0 && LocalInteractionSession == InteractionReplay.SessionId && InteractionReplay.bActive)
	{
		ActiveConfig = InteractionReplay.Config;
	}

	VB_LOG(Log, "처형 설정 복제 도착: %s (owner=%s)",
		ActiveConfig ? *GetNameSafe(ActiveConfig) : TEXT("없음(장면 종료)"), *GetNameSafe(GetOwner()));

	// 도착만 알린다. 연출은 듣는 쪽이 장면 시작까지 기다렸다가 스스로 시작한다(도착 순서가 정해지지 않았다).
	OnConfigArrived.Broadcast(GetOwner(), ActiveConfig);
}

UVBFinisherComponent* UVBFinisherComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBFinisherComponent>() : nullptr;
}

UContextualAnimSceneActorComponent* UVBFinisherComponent::FindSceneActorComponent(const AActor* Actor)
{
	return FVBFinisherLegacySession::FindSceneComponent(Actor);
}

void UVBFinisherComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	LegacySession.TickAlign(DeltaTime, *this);
}

bool UVBFinisherComponent::StartFinisher(const UVBFinisherConfig* Config, AActor* Target)
{
	bLastFinisherCancelled = false;
	AActor* Owner = GetOwner();
	if (!Owner || !Target || !Config || !Config->Scene)
	{
		return false;
	}

	// 씬 시작은 권위 행위다. 클라가 시작하면 두 액터의 상태가 갈린다.
	if (!Owner->HasAuthority())
	{
		return false;
	}

	if (IsPlaying())
	{
		// 이미 장면 안이다. 겹쳐 시작하면 플러그인 쪽 바인딩이 어긋난다.
		return false;
	}

	UContextualAnimSceneActorComponent* OwnerScene = LegacySession.Start(*Config, *Owner, *Target, *this);
	if (!OwnerScene)
	{
		return false;
	}

	// 끝을 받아야 피해와 카메라 복귀가 걸린다. 처형마다 구독하므로 AddUniqueDynamic 으로 중복을 막는다.
	OwnerScene->OnLeftSceneDelegate.AddUniqueDynamic(this, &UVBFinisherComponent::HandleLeftScene);

	ActiveConfig = Config;

	// 서버에서는 엔진이 OnRep_ 을 부르지 않으므로 손으로 부른다 - 없으면 호스트, 싱글에서 연출이 안 걸린다.
	OnRep_ActiveConfig();

	VB_LOG(Log, "StartFinisher: 시작 (scene=%s target=%s)", *GetNameSafe(Config->Scene), *GetNameSafe(Target));
	return true;
}

// ============================================================================
// GASP 5.8 경로 - chooser -> MotionMatchMulti -> 역할별 몽타주 + 모션워핑
// ============================================================================

bool UVBFinisherComponent::StartInteractionFinisher(AActor* Target)
{
	// 고르기, 성립 재검사, 검색, 역할 검증은 선택 담당 클래스가 한 번에 한다. 여기는 그 후보를 재생하고, 성공한 뒤에만 게시한다.
	TArray<FPoseSearchBlueprintResult> Results;
	FVBInteractionReplay Replay;
	if (!Selection.TrySelectInteraction({GetOwner(), Target, IsPlaying(), IsActorInScene(Target)}, Results, Replay))
	{
		return false;
	}

	AActor* Owner = GetOwner();
	Replay.SessionId = InteractionReplay.SessionId + 1;
	Replay.bActive = true;

	if (!PlayInteractionLocally(Results, 0.0f))
	{
		return false;
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	Replay.ServerStartTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	InteractionReplay = MoveTemp(Replay);
	LocalInteractionSession = InteractionReplay.SessionId;
	ActiveConfig = InteractionReplay.Config;
	OnRep_ActiveConfig();
	Owner->ForceNetUpdate();
	VB_LOG(Log, "상호작용 처형: 두 역할 시작, 세션 %d", LocalInteractionSession);
	return true;
}

// PlayInteractionLocally 가 재생 전에 모은 참가자별 준비물이다.
struct UVBFinisherComponent::FInteractionPlayPlan
{
	struct FEntry
	{
		const FPoseSearchBlueprintResult* Result = nullptr; // PlayInteractionLocally 의 호출 동안 산다
		AActor* Actor = nullptr;
		UAnimMontage* Montage = nullptr;
		float Position = 0.0f; // 음수면 이미 놓인 원격 피해자라 재생하지 않는다
		UVBAbilitySystemComponent* AbilitySystem = nullptr;
	};

	TArray<FEntry> Entries;
};

namespace
{
	// 피해자 몽타주의 시작 시각 뒤에 물리 전환 notify 가 남아 있는가.
	bool HasRagdollNotifyAfter(const UAnimMontage& Montage, float StartTime)
	{
		return Montage.Notifies.ContainsByPredicate(
			[StartTime](const FAnimNotifyEvent& Event)
			{
				return Cast<UVBAnimNotify_InteractionRagdoll>(Event.Notify) && Event.GetTriggerTime() >= StartTime;
			});
	}

	// 재생할 참가자의 이동을 세운다.
	void StopParticipantMovement(AActor* Actor)
	{
		// 이동 컴포넌트가 없는 캐릭터 파생(시험 대역 등)도 참가자가 될 수 있다 - 멈출 이동이 없으면 건너뛴다.
		const ACharacter* Character = Cast<ACharacter>(Actor);
		if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
		{
			Movement->StopMovementImmediately();
			Movement->ConsumeInputVector();
		}
	}

	// 이 처형이 튼 몽타주 인스턴스의 콜백을 떼고, 아직 그 인스턴스가 돌고 있으면 멈춘다.
	void StopParticipantMontage(AActor* Actor, UAnimMontage* Montage, int32 MontageInstanceId, float BlendOutTime)
	{
		if (UAnimInstance* Anim = VBFinisherActorQueries::FindAnimInstance(Actor); Anim && Montage)
		{
			FAnimMontageInstance* Instance = Anim->GetMontageInstanceForID(MontageInstanceId);
			if (Instance)
			{
				Instance->OnMontageEnded.Unbind();
				Instance->OnMontageBlendingOutStarted.Unbind();
				if (Instance == Anim->GetActiveInstanceForMontage(Montage))
				{
					Anim->Montage_Stop(BlendOutTime, Montage);
				}
			}
		}
	}

	// 몽타주의 워프 구간이 걸어 둔 목표를 지운다.
	void RemoveParticipantWarpTargets(AActor* Actor, UAnimMontage* Montage)
	{
		if (UMotionWarpingComponent* Warping = Actor ? Actor->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
			Warping && Montage)
		{
			for (const FAnimNotifyEvent& Event : Montage->Notifies)
			{
				const UVBAnimNotifyState_InteractionWarp* Notify = Cast<UVBAnimNotifyState_InteractionWarp>(Event.NotifyStateClass);
				const URootMotionModifier_Warp* Modifier = Notify ? Cast<URootMotionModifier_Warp>(Notify->RootMotionModifier) : nullptr;
				if (Modifier)
				{
					Warping->RemoveWarpTarget(Modifier->WarpTargetName);
				}
			}
		}
	}
}

bool UVBFinisherComponent::PlayInteractionLocally(const TArray<FPoseSearchBlueprintResult>& Results,
	float ElapsedSeconds)
{
	if (Results.Num() != 2 || !InteractionParticipants.IsEmpty())
	{
		return false;
	}

	FInteractionPlayPlan Plan;
	if (!BuildInteractionPlayPlan(Results, ElapsedSeconds, Plan))
	{
		return false;
	}

	++LocalGeneration;
	bInteractionVictimLanded = !GetOwner()->HasAuthority() && InteractionReplay.bVictimReleased;
	bLastFinisherCancelled = false;
	// 모든 전제를 확인한 뒤에만 결과/소유권을 설치한다. 재생 실패는 아래에서 전체 롤백한다.
	InstallInteractionParticipants(Plan);

	const int32 StartGeneration = LocalGeneration;
	for (const FInteractionPlayPlan::FEntry& Entry : Plan.Entries)
	{
		if (Entry.Position >= 0.0f)
		{
			StopParticipantMovement(Entry.Actor);
		}
	}

	if (!AcquireParticipantMontages(Plan, StartGeneration) || !PlayParticipantMontages(Plan))
	{
		return false;
	}

	BindInteractionMontageDelegates(Plan);
	for (const FInteractionPlayPlan::FEntry& Entry : Plan.Entries)
	{
		OnInteractionParticipantChanged.Broadcast(Entry.Actor);
	}
	return true;
}

bool UVBFinisherComponent::BuildInteractionPlayPlan(const TArray<FPoseSearchBlueprintResult>& Results, float ElapsedSeconds,
	FInteractionPlayPlan& Plan) const
{
	bool bHasOwner = false;
	for (const FPoseSearchBlueprintResult& Result : Results)
	{
		AActor* Actor = const_cast<AActor*>(UPoseSearchLibrary::GetActor(Result));
		UAbilitySystemComponent* BaseASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(BaseASC);
		if (ASC && !GetOwner()->HasAuthority() && !InteractionReplay.SupersededMontagePlayIds.IsValidIndex(Result.RoleIndex))
		{
			return false;
		}

		UAnimInstance* Anim = VBFinisherActorQueries::FindAnimInstance(Actor);
		const UMultiAnimAsset* Asset = Cast<UMultiAnimAsset>(Result.SelectedAnim);
		UAnimMontage* Montage = Asset ? Cast<UAnimMontage>(Asset->GetAnimationAsset(Result.Role)) : nullptr;
		const bool bActorAlreadyClaimed = Plan.Entries.ContainsByPredicate(
			[Actor](const FInteractionPlayPlan::FEntry& Entry) { return Entry.Actor == Actor; });
		if (!CanPlayInteractionResult(Result, Actor, BaseASC && !ASC, Anim, Montage, bActorAlreadyClaimed))
		{
			return false;
		}

		const float Position = Result.SelectedTime + ElapsedSeconds * Result.WantedPlayRate * Montage->RateScale;
		const bool bReleasedRemoteVictim = !GetOwner()->HasAuthority() && Actor != GetOwner() && InteractionReplay.bVictimReleased;
		if (!FMath::IsFinite(Position) || Position < 0.0f ||
			(Position >= Montage->GetPlayLength() && !bReleasedRemoteVictim))
		{
			return false;
		}

		if (Actor != GetOwner() && !HasRagdollNotifyAfter(*Montage, Result.SelectedTime))
		{
			VB_LOG(Warning, "상호작용 처형: %s 에 native InteractionRagdoll notify가 없다", *GetNameSafe(Montage));
			return false;
		}

		bHasOwner |= Actor == GetOwner();
		const bool bSkipReleasedPose = bReleasedRemoteVictim &&
			(Position >= Montage->GetPlayLength() || VBFinisherActorQueries::FindBodyMesh(Actor)->IsAnySimulatingPhysics());
		Plan.Entries.Add({&Result, Actor, Montage, bSkipReleasedPose ? -1.0f : Position, ASC});
	}

	return bHasOwner;
}

bool UVBFinisherComponent::CanPlayInteractionResult(const FPoseSearchBlueprintResult& Result, AActor* Actor, bool bForeignAbilitySystem,
	const UAnimInstance* Anim, const UAnimMontage* Montage, bool bActorAlreadyClaimed) const
{
	const TWeakObjectPtr<UVBFinisherComponent>* ExistingOwner = ParticipantOwners.Find(Actor);
	// Montage_Play 자체는 미러링을 지원하지 않는다. 검색 결과만 반사하면 포즈와 워프가 달라진다.
	if (!Actor || bForeignAbilitySystem || !Anim || !VBFinisherActorQueries::FindInteractionAnim(Actor) || !Montage
		|| bActorAlreadyClaimed ||
		(ExistingOwner && ExistingOwner->IsValid()) || Result.bIsMirrored || Result.bLoop ||
		!FMath::IsFinite(Result.WantedPlayRate) || Result.WantedPlayRate <= 0.0f ||
		!FMath::IsFinite(Montage->RateScale) || Montage->RateScale <= 0.0f ||
		!UVBInteractionIKLibrary::DoesSkeletonMatchSchema(Result.SelectedDatabase, Result.Role, Anim) ||
		Result.ActorRootTransforms.Num() != 2 || Result.ActorRootBoneTransforms.Num() != 2 ||
		Result.AnimContexts.Num() != 2 || !Result.AnimContexts.IsValidIndex(Result.RoleIndex))
	{
		VB_LOG(Warning, "상호작용 처형: 참가자/골격/재생 설정 검증 실패 (%s)", *GetNameSafe(Actor));
		return false;
	}

	return true;
}

void UVBFinisherComponent::InstallInteractionParticipants(const FInteractionPlayPlan& Plan)
{
	for (const FInteractionPlayPlan::FEntry& Entry : Plan.Entries)
	{
		// 기록을 먼저 완성한다(인스턴스 번호, 관계 표시는 기본값). 등록, 결과 설치는 그 뒤, 지금 순서 그대로.
		InteractionParticipants.Add(FVBFinisherParticipant{Entry.Actor, Entry.Montage, Entry.AbilitySystem});
		ParticipantOwners.Add(Entry.Actor, this);
		Entry.Actor->OnDestroyed.AddUniqueDynamic(this, &UVBFinisherComponent::HandleInteractionParticipantDestroyed);
		VBFinisherActorQueries::FindInteractionAnim(Entry.Actor)->SetInteractionResult(*Entry.Result);
	}
}

bool UVBFinisherComponent::AcquireParticipantMontages(const FInteractionPlayPlan& Plan, int32 StartGeneration)
{
	for (const FInteractionPlayPlan::FEntry& Entry : Plan.Entries)
	{
		UVBAbilitySystemComponent* ASC = Entry.AbilitySystem;
		const uint8 PreviousPlayId = ASC ? (GetOwner()->HasAuthority() ? ASC->GetMontagePlayInstanceId() :
			InteractionReplay.SupersededMontagePlayIds[Entry.Result->RoleIndex]) : 0;
		if (ASC && !ASC->AcquireExternalMontageOwnership(this, PreviousPlayId))
		{
			FinishInteraction(EVBInteractionEnd::Abandoned);
			return false;
		}

		if (LocalGeneration != StartGeneration)
		{
			return false; // 이전 GAS task 를 멈추면 그 자리에서 참가자가 파괴되거나 취소될 수 있다.
		}
	}

	return true;
}

bool UVBFinisherComponent::PlayParticipantMontages(const FInteractionPlayPlan& Plan)
{
	for (int32 Index = 0; Index < Plan.Entries.Num(); ++Index)
	{
		const FInteractionPlayPlan::FEntry& Entry = Plan.Entries[Index];
		if (Entry.Position < 0.0f)
		{
			continue; // 이미 물리로 전환된 원격 피해자의 몽타주를 다시 시작하지 않는다.
		}

		if (VBFinisherActorQueries::FindAnimInstance(Entry.Actor)->Montage_Play(Entry.Montage, Entry.Result->WantedPlayRate,
			EMontagePlayReturnType::MontageLength, Entry.Position, false) <= 0.0f)
		{
			FinishInteraction(EVBInteractionEnd::Abandoned);
			return false;
		}

		if (FAnimMontageInstance* Instance =
			VBFinisherActorQueries::FindAnimInstance(Entry.Actor)->GetActiveInstanceForMontage(Entry.Montage))
		{
			InteractionParticipants[Index].MontageInstanceId = Instance->GetInstanceID();
		}

		const ACharacter* Character = Cast<ACharacter>(Entry.Actor);
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		VB_LOG(Log, "InteractionFinisher start: actor=%s role=%s movementMode=%d montage=%s position=%.3f",
			*GetNameSafe(Entry.Actor), *Entry.Result->Role.ToString(),
			Movement ? static_cast<int32>(Movement->MovementMode) : -1,
			*Entry.Montage->GetPathName(), Entry.Position);
	}

	return true;
}

void UVBFinisherComponent::BindInteractionMontageDelegates(const FInteractionPlayPlan& Plan)
{
	USkeletalMeshComponent* OwnerMesh = VBFinisherActorQueries::FindBodyMesh(GetOwner());
	UPrimitiveComponent* OwnerRoot = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	for (int32 Index = 0; Index < Plan.Entries.Num(); ++Index)
	{
		const FInteractionPlayPlan::FEntry& Entry = Plan.Entries[Index];
		AActor* Actor = Entry.Actor;
		FOnMontageEnded EndDelegate;
		if (Actor == GetOwner())
		{
			EndDelegate.BindUObject(this, &UVBFinisherComponent::HandleInteractionMontageEnded, LocalGeneration);
		}
		else
		{
			EndDelegate.BindUObject(this, &UVBFinisherComponent::HandleInteractionVictimMontageEnded, LocalGeneration);
			InteractionParticipants[Index].LinkVictim(OwnerMesh, OwnerRoot, *Actor);
		}

		UAnimInstance* Anim = VBFinisherActorQueries::FindAnimInstance(Actor);
		if (Entry.Position < 0.0f)
		{
			continue;
		}

		Anim->Montage_SetEndDelegate(EndDelegate, Entry.Montage);
		FOnMontageBlendingOutStarted BlendDelegate;
		BlendDelegate.BindUObject(this, &UVBFinisherComponent::HandleInteractionBlendOut, LocalGeneration);
		Anim->Montage_SetBlendingOutDelegate(BlendDelegate, Entry.Montage);
	}
}

void UVBFinisherComponent::OnRep_InteractionReplay()
{
	const bool bWasRetrying = GetWorld()->GetTimerManager().IsTimerActive(InteractionReplayRetryTimer);
	GetWorld()->GetTimerManager().ClearTimer(InteractionReplayRetryTimer);
	if (ReplayRetrySession != InteractionReplay.SessionId ||
		(!bWasRetrying && LocalInteractionSession == 0 && FinishedInteractionSession != InteractionReplay.SessionId))
	{
		ReplayRetrySession = InteractionReplay.SessionId;
		// 늦은 relevancy 진입에도 수신 후 준비 시간을 준다. 서버 재생 경과와 다른 시계다.
		ReplayRetryDeadline = GetWorld()->GetTimeSeconds() + 2.0;
	}

	if (LocalInteractionSession != InteractionReplay.SessionId || !InteractionReplay.bActive)
	{
		FinishInteraction(EVBInteractionEnd::Abandoned);
	}

	if (!InteractionReplay.bActive)
	{
		FinishedInteractionSession = InteractionReplay.SessionId;
		return;
	}

	if (InteractionReplay.bActive && FinishedInteractionSession != InteractionReplay.SessionId)
	{
		if (LocalInteractionSession == InteractionReplay.SessionId && InteractionReplay.bVictimReleased)
		{
			ApplyReplicatedVictimRelease();
		}

		TryReplayInteraction();
	}
}

void UVBFinisherComponent::TryReplayInteraction()
{
	if (!GetWorld() || !InteractionReplay.bActive || LocalInteractionSession == InteractionReplay.SessionId ||
		FinishedInteractionSession == InteractionReplay.SessionId)
	{
		return;
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Elapsed = FMath::Max(0.0, ServerNow - InteractionReplay.ServerStartTime);
	const UMultiAnimAsset* Asset = InteractionReplay.Asset;
	TArray<FPoseSearchBlueprintResult> Results;
	if (Asset && InteractionReplay.Database && Asset->GetNumRoles() == 2 &&
		InteractionReplay.Actors.Num() == 2 && InteractionReplay.StartTimes.Num() == 2 &&
		InteractionReplay.PlayRates.Num() == 2)
	{
		TArray<TObjectPtr<const UObject>> Contexts;
		for (AActor* Actor : InteractionReplay.Actors)
		{
			Contexts.Add(VBFinisherActorQueries::FindAnimInstance(Actor));
		}

		if (Contexts[0] && Contexts[1])
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FPoseSearchBlueprintResult& Result = Results.AddDefaulted_GetRef();
				Result.SelectedAnim = InteractionReplay.Asset;
				Result.SelectedDatabase = InteractionReplay.Database;
				Result.SelectedTime = InteractionReplay.StartTimes[Index];
				Result.WantedPlayRate = InteractionReplay.PlayRates[Index];
				Result.Role = Asset->GetRole(Index);
				Result.RoleIndex = Index;
				Result.bIsInteraction = true;
				Result.BlendParameters = InteractionReplay.BlendParameters;
				Result.ActorRootTransforms = InteractionReplay.RootTransforms;
				Result.ActorRootBoneTransforms = InteractionReplay.RootBoneTransforms;
				Result.AnimContexts = Contexts;
			}
		}
	}

	if (Results.Num() == 2 && PlayInteractionLocally(Results, Elapsed))
	{
		LocalInteractionSession = InteractionReplay.SessionId;
		ActiveConfig = InteractionReplay.Config;
		OnRep_ActiveConfig();
		if (InteractionReplay.bVictimReleased)
		{
			ApplyReplicatedVictimRelease();
		}

		return;
	}
	// Actor/AnimInstance가 아직 준비되지 않은 채널 순서를 허용하되 영구 재시도하지 않는다.
	if (GetWorld()->GetTimeSeconds() < ReplayRetryDeadline)
	{
		GetWorld()->GetTimerManager().SetTimer(InteractionReplayRetryTimer, this,
			&UVBFinisherComponent::TryReplayInteraction, 0.05f, false);
	}
	else
	{
		VB_LOG(Warning, "상호작용 재생: 세션 %d 참가자가 준비되지 않았거나 이미 종료됨", InteractionReplay.SessionId);
	}
}

void UVBFinisherComponent::ApplyReplicatedVictimRelease()
{
	// notify 시각을 지나 받은 클라도 release blend-out 을 적용한다. 피해는 재방송하지 않는다.
	bInteractionVictimLanded = true;
	for (int32 Index = 0; Index < InteractionParticipants.Num(); ++Index)
	{
		AActor* Actor = InteractionParticipants[Index].Actor.Get();
		UAnimMontage* Montage = InteractionParticipants[Index].Montage.Get();
		if (!Actor || Actor == GetOwner() || !Montage)
		{
			continue;
		}

		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (const UVBAnimNotify_InteractionRagdoll* Notify = Cast<UVBAnimNotify_InteractionRagdoll>(Event.Notify))
			{
				Notify->StopReleasedMontage(VBFinisherActorQueries::FindAnimInstance(Actor), Montage,
					InteractionParticipants[Index].MontageInstanceId);
				break;
			}
		}

		if (InteractionParticipants.IsValidIndex(Index))
		{
			InteractionParticipants[Index].ReleaseMontageOwnership(this);
		}
	}
}

bool UVBFinisherComponent::NotifyInteractionRagdoll(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, int32 MontageInstanceId)
{
	if (!Mesh)
	{
		return false;
	}

	if (const TWeakObjectPtr<UVBFinisherComponent>* Owner = ParticipantOwners.Find(Mesh->GetOwner()))
	{
		if (Owner->IsValid())
		{
			return Owner->Get()->HandleRagdollNotify(Mesh->GetOwner(), Animation, MontageInstanceId);
		}
	}

	return false;
}

bool UVBFinisherComponent::HandleRagdollNotify(AActor* Victim, UAnimSequenceBase* Animation, int32 MontageInstanceId)
{
	const int32 Index = InteractionParticipants.IndexOfByPredicate(
		[Victim](const FVBFinisherParticipant& Participant) { return Participant.Actor.Get() == Victim; });
	if (bFinishingInteraction || Victim == GetOwner() || Index == INDEX_NONE ||
		!InteractionParticipants.IsValidIndex(Index) || Animation != InteractionParticipants[Index].Montage.Get() ||
		MontageInstanceId == INDEX_NONE || InteractionParticipants[Index].MontageInstanceId != MontageInstanceId)
	{
		return false;
	}

	UAnimInstance* Anim = VBFinisherActorQueries::FindAnimInstance(Victim);
	UAnimMontage* Montage = InteractionParticipants[Index].Montage.Get();
	if (!Anim || !Anim->Montage_IsPlaying(Montage))
	{
		return false;
	}
	// 서버 복제가 로컬 notify 보다 먼저 와도 blend-out 은 허용한다(이전 세션은 위 ID 검증이 거른다).
	if (bInteractionVictimLanded)
	{
		return true;
	}

	ReleaseVictim(Index, Anim, Montage);
	return true;
}

void UVBFinisherComponent::ReleaseVictim(int32 Index, UAnimInstance* Anim, UAnimMontage* Montage)
{
	// 피해에 따른 사망/몽타주 중단 콜백이 동기적으로 들어와도 이미 도달한 전환으로 취급한다.
	bInteractionVictimLanded = true;
	// 피해자가 짝 애니메이션을 떠났다. 공격자가 끝나기 전에 일어서기 몽타주를 틀 수 있게 ASC 를 풀어 준다.
	if (InteractionParticipants.IsValidIndex(Index))
	{
		InteractionParticipants[Index].ReleaseMontageOwnership(this);
	}

	if (GetOwner()->HasAuthority())
	{
		InteractionReplay.bVictimReleased = true;
		GetOwner()->ForceNetUpdate();
		OnInteractionVictimLanded.Broadcast(ComputeVictimLaunch(Anim, Montage));
	}
}

FVector UVBFinisherComponent::ComputeVictimLaunch(UAnimInstance* Anim, UAnimMontage* Montage) const
{
	USkeletalMeshComponent* Mesh = Anim ? Anim->GetSkelMeshComponent() : nullptr;
	if (!Mesh || !Montage)
	{
		return FVector::ZeroVector;
	}
	// 전환 직후의 저작 루트 운동만 읽는다. 미래 전체의 최고 속도를 임의로 고르지 않는다.
	const float Start = Anim->Montage_GetPosition(Montage);
	const float End = FMath::Min(Start + 0.05f, Montage->GetPlayLength());
	const float Rate = Anim->Montage_GetPlayRate(Montage) * Montage->RateScale;
	if (End <= Start || Rate <= 0.0f)
	{
		return FVector::ZeroVector;
	}

	FAnimExtractContext Context;
	Context.bExtractRootMotion = true;
	const FTransform Delta = Montage->ExtractRootMotionFromTrackRange(Start, End, Context);
	return Mesh->ConvertLocalRootMotionToWorld(Delta).GetTranslation() *
		(Rate / (End - Start)) * InteractionRagdollLaunchScale;
}

void UVBFinisherComponent::HandleInteractionBlendOut(UAnimMontage* Montage, bool bInterrupted, int32 Generation)
{
	if (Generation != LocalGeneration || InteractionParticipants.IsEmpty())
	{
		return;
	}

	if (!bInterrupted || bFinishingInteraction || !GetOwner()->HasAuthority())
	{
		return;
	}

	for (int32 Index = 0; Index < InteractionParticipants.Num(); ++Index)
	{
		if (InteractionParticipants[Index].Montage.Get() == Montage &&
			(InteractionParticipants[Index].Actor.Get() == GetOwner() || !bInteractionVictimLanded))
		{
			FinishInteraction(EVBInteractionEnd::Cancelled);
			return;
		}
	}
}

void UVBFinisherComponent::HandleInteractionVictimMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Generation)
{
	if (Generation != LocalGeneration || InteractionParticipants.IsEmpty())
	{
		return;
	}

	if (!bFinishingInteraction && !bInteractionVictimLanded && GetOwner()->HasAuthority())
	{
		VB_LOG(Warning, "상호작용 피해자 종료: 전환 notify 미도달 (%s, 중단=%d)", *GetNameSafe(Montage), bInterrupted);
		FinishInteraction(EVBInteractionEnd::Cancelled);
	}
}

void UVBFinisherComponent::HandleInteractionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Generation)
{
	if (Generation != LocalGeneration || InteractionParticipants.IsEmpty())
	{
		return;
	}
	// 공격자 클립이 스스로 끝났으면 피해자 notify 가 한 프레임 늦어도 완료다. 진짜 끊김만 취소한다.
	if (!bInterrupted && !bInteractionVictimLanded && GetOwner()->HasAuthority())
	{
		for (int32 Index = 0; Index < InteractionParticipants.Num(); ++Index)
		{
			AActor* Victim = InteractionParticipants[Index].Actor.Get();
			if (Victim && Victim != GetOwner() && !bInteractionVictimLanded)
			{
				VB_LOG(Log, "상호작용 공격자 클립이 피해자 전환 notify 보다 먼저 끝났다 - 지금 피해자를 놓는다 (%s)", *GetNameSafe(Victim));
				// 이미 멈춘 클립은 위치 0 을 읽으므로 애님 없이 전달해 틀린 속도 대신 0 으로 놓는다.
				UAnimInstance* VictimAnim = VBFinisherActorQueries::FindAnimInstance(Victim);
				UAnimMontage* VictimMontage = InteractionParticipants.IsValidIndex(Index)
					? InteractionParticipants[Index].Montage.Get()
					: nullptr;
				const bool bVictimClipPlaying = VictimAnim && VictimMontage && VictimAnim->Montage_IsPlaying(VictimMontage);
				ReleaseVictim(Index, bVictimClipPlaying ? VictimAnim : nullptr, VictimMontage);
			}
		}
	}

	FinishInteraction(bInterrupted || !bInteractionVictimLanded ? EVBInteractionEnd::Cancelled : EVBInteractionEnd::Completed);
}

void UVBFinisherComponent::HandleInteractionParticipantDestroyed(AActor* Actor)
{
	const bool bRetryLocalLoss = GetOwner() && !GetOwner()->HasAuthority() && Actor != GetOwner() &&
		InteractionReplay.bActive && InteractionReplay.SessionId == LocalInteractionSession;
	FinishInteraction(EVBInteractionEnd::Cancelled);
	if (bRetryLocalLoss)
	{
		// 클라이언트의 relevancy 이탈은 서버의 세션 취소가 아니다.
		FinishedInteractionSession = 0;
		ReplayRetryDeadline = GetWorld()->GetTimeSeconds() + 2.0;
		GetWorld()->GetTimerManager().SetTimer(InteractionReplayRetryTimer, this,
			&UVBFinisherComponent::TryReplayInteraction, 0.05f, false);
	}
}

void UVBFinisherComponent::FinishInteraction(EVBInteractionEnd End)
{
	if (bFinishingInteraction || InteractionParticipants.IsEmpty())
	{
		return;
	}

	TGuardValue<bool> FinishingGuard(bFinishingInteraction, true);
	++LocalGeneration; // 이전 몽타주의 늦은 종료 콜백은 다음 세션을 건드리지 못한다.
	bLastFinisherCancelled = End != EVBInteractionEnd::Completed;
	USkeletalMeshComponent* OwnerMesh = VBFinisherActorQueries::FindBodyMesh(GetOwner());
	UPrimitiveComponent* OwnerRoot = GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
	for (int32 Index = 0; Index < InteractionParticipants.Num(); ++Index)
	{
		TearDownParticipant(Index, OwnerMesh, OwnerRoot);
	}

	InteractionParticipants.Reset();
	FinishedInteractionSession = LocalInteractionSession;
	LocalInteractionSession = 0;
	ActiveConfig = nullptr;
	OnRep_ActiveConfig();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		InteractionReplay.bActive = false;
		GetOwner()->ForceNetUpdate();
		if (End != EVBInteractionEnd::Abandoned)
		{
			OnFinisherEnded.Broadcast();
		}
	}
}

void UVBFinisherComponent::TearDownParticipant(int32 Index, USkeletalMeshComponent* OwnerMesh, UPrimitiveComponent* OwnerRoot)
{
	AActor* Actor = InteractionParticipants[Index].Actor.Get();
	UAnimMontage* Montage = InteractionParticipants.IsValidIndex(Index) ? InteractionParticipants[Index].Montage.Get() : nullptr;
	StopParticipantMontage(Actor, Montage, InteractionParticipants[Index].MontageInstanceId, InteractionEarlyOutBlendTime);

	if (IVBInteractionAnimInterface* Anim = VBFinisherActorQueries::FindInteractionAnim(Actor))
	{
		Anim->SetInteractionResult(FPoseSearchBlueprintResult());
	}

	RemoveParticipantWarpTargets(Actor, Montage);

	ParticipantOwners.Remove(InteractionParticipants[Index].Actor);
	OnInteractionParticipantChanged.Broadcast(Actor);
	if (Actor)
	{
		Actor->OnDestroyed.RemoveDynamic(this, &UVBFinisherComponent::HandleInteractionParticipantDestroyed);
	}

	if (InteractionParticipants.IsValidIndex(Index))
	{
		InteractionParticipants[Index].ReleaseMontageOwnership(this);
	}

	if (!Actor || Actor == GetOwner())
	{
		return;
	}

	InteractionParticipants[Index].UnlinkVictim(OwnerMesh, OwnerRoot, *Actor);
}

bool UVBFinisherComponent::TransitionSection(FName SectionName)
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsPlaying())
	{
		return false;
	}

	return FVBFinisherLegacySession::TransitionSection(Owner, SectionName);
}

void UVBFinisherComponent::EarlyOut()
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsPlaying())
	{
		return;
	}

	// 새 경로는 취소 즉시 세션을 닫는다. 블렌드아웃 중의 notify도 더 이상 피해를 주지 못한다.
	if (!InteractionParticipants.IsEmpty())
	{
		FinishInteraction(EVBInteractionEnd::Cancelled);
		return;
	}

	// 정리와 발화는 HandleLeftScene 한 곳에서만 한다.
	FVBFinisherLegacySession::EarlyOut(Owner);
}

void UVBFinisherComponent::HandleLeftScene(UContextualAnimSceneActorComponent* SceneActorComponent)
{
	if (SceneActorComponent)
	{
		SceneActorComponent->OnLeftSceneDelegate.RemoveDynamic(this, &UVBFinisherComponent::HandleLeftScene);
	}

	// 상태를 먼저 비운다. 구독자가 IsPlaying() 을 물었을 때 「아직 도는 중」이면 안 된다.
	ActiveConfig = nullptr;

	// 시작 때와 같은 이유로 손으로 부른다 - 없으면 호스트에서 카메라가 남는다.
	OnRep_ActiveConfig();

	OnFinisherEnded.Broadcast();
}

bool UVBFinisherComponent::IsActorInScene(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (FVBFinisherLegacySession::IsActorInScene(Actor))
	{
		return true;
	}

	// 새 경로에는 장면이 없어 그 액터의 AnimInstance 가 든 상호작용 결과가 진실원이다.
	//   부르는 쪽은 경로를 모르므로 두 경로를 모두 센다.
	if (const IVBInteractionAnimInterface* InteractionAnim = VBFinisherActorQueries::FindInteractionAnim(Actor))
	{
		const FPoseSearchBlueprintResult& Result = InteractionAnim->GetInteractionResult();
		return Result.bIsInteraction && Result.SelectedAnim != nullptr;
	}

	return false;
}

bool UVBFinisherComponent::IsPlaying() const
{
	// 옛 경로는 플러그인 장면, 새 경로는 참가자 목록이 진실원이다. 부르는 쪽이 경로를 모르게 여기서 합친다.
	return IsActorInScene(GetOwner()) || !InteractionParticipants.IsEmpty();
}

AActor* UVBFinisherComponent::GetParticipant(FName Role) const
{
	if (!InteractionParticipants.IsEmpty())
	{
		for (const FVBFinisherParticipant& Participant : InteractionParticipants)
		{
			AActor* Actor = Participant.Actor.Get();
			if (Actor && Role == (Actor == GetOwner() ? VBFinisherActorQueries::RoleAttacker : VBFinisherActorQueries::RoleVictim))
			{
				return Actor;
			}
		}

		return nullptr;
	}

	return FVBFinisherLegacySession::GetParticipant(GetOwner(), Role);
}

void UVBFinisherComponent::GetParticipants(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	if (!InteractionParticipants.IsEmpty())
	{
		for (const FVBFinisherParticipant& Participant : InteractionParticipants)
		{
			if (AActor* Actor = Participant.Actor.Get())
			{
				OutActors.Add(Actor);
			}
		}

		return;
	}

	FVBFinisherLegacySession::AppendParticipants(GetOwner(), OutActors);
}

float UVBFinisherComponent::GetPlaybackTime() const
{
	const int32 Index = InteractionParticipants.IndexOfByPredicate(
		[this](const FVBFinisherParticipant& Participant) { return Participant.Actor.Get() == GetOwner(); });
	if (Index != INDEX_NONE && InteractionParticipants.IsValidIndex(Index))
	{
		const UAnimInstance* Anim = VBFinisherActorQueries::FindAnimInstance(GetOwner());
		return Anim ? Anim->Montage_GetPosition(InteractionParticipants[Index].Montage.Get()) : -1.0f;
	}

	return FVBFinisherLegacySession::GetPlaybackTime(GetOwner());
}

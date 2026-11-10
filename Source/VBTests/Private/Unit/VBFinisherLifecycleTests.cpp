#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "VBCinematicSkillGaugeTestTypes.h"
#include "Character/VBSlideMovementComponent.h"
#include "Character/VBFinisherComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimMontage.h"
#include "Character/VBCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "VBFinisherLandedProbe.h"

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherLifecycleTest, "VB.Unit.Finisher.SessionCleanup",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBFinisherLifecycleTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ACharacter* Attacker = World->SpawnActor<ACharacter>();
	ACharacter* Victim = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("attacker"), Attacker) || !TestNotNull(TEXT("victim"), Victim))
	{
		World->DestroyWorld(false);
		return false;
	}

	UVBFinisherComponent* Finisher = NewObject<UVBFinisherComponent>(Attacker);
	Attacker->AddInstanceComponent(Finisher);
	Finisher->RegisterComponent();
	UCapsuleComponent* OwnerRoot = Attacker->GetCapsuleComponent();
	UCapsuleComponent* VictimRoot = Victim->GetCapsuleComponent();
	USkeletalMeshComponent* OwnerMesh = Attacker->GetMesh();
	USkeletalMeshComponent* VictimMesh = Victim->GetMesh();
	TArray<AActor*> ChangedParticipants;
	const FDelegateHandle StateHandle = UVBFinisherComponent::OnInteractionParticipantChanged.AddLambda(
		[&ChangedParticipants](AActor* Actor) { ChangedParticipants.Add(Actor); });
	ON_SCOPE_EXIT { UVBFinisherComponent::OnInteractionParticipantChanged.Remove(StateHandle); };

	// 짝의 반쪽이 없으면 소유, 관계를 설치하기 전에 실패해야 한다
	TArray<FPoseSearchBlueprintResult> IncompletePair;
	IncompletePair.AddDefaulted();
	TestFalse(TEXT("incomplete pair rejected"), Finisher->PlayInteractionLocally(IncompletePair, 0.0f));
	TestTrue(TEXT("failed validation leaves no participants"), Finisher->InteractionParticipants.IsEmpty());
	TestFalse(TEXT("failed validation leaves no owner registration"), UVBFinisherComponent::ParticipantOwners.Contains(Attacker));

	// 실제 등록된 scene component 에 대해 진짜 세션 해체를 돌린다.
	// Montage/pose 렌더링은 일부러 뺐다 - 그 자산은 PIE 가 본다.
	auto InstallSession = [&](int32 Session, bool bOwnRelationships)
	{
		Finisher->LocalInteractionSession = Session;
		Finisher->InteractionReplay.SessionId = Session;
		Finisher->InteractionReplay.bActive = true;
		++Finisher->LocalGeneration;
		// Montage 와 AbilitySystem 은 비우고 instance id 는 INDEX_NONE - 정리가 instance 0 을 멈추거나 null ASC 를 건드리면 안 된다
		FVBFinisherParticipant AttackerRecord;
		AttackerRecord.Actor = Attacker;
		FVBFinisherParticipant VictimRecord;
		VictimRecord.Actor = Victim;
		VictimRecord.bAddedTickPrerequisite = bOwnRelationships;
		VictimRecord.bAddedOwnerCollisionIgnore = bOwnRelationships;
		VictimRecord.bAddedVictimCollisionIgnore = bOwnRelationships;
		Finisher->InteractionParticipants = {AttackerRecord, VictimRecord};
		UVBFinisherComponent::ParticipantOwners.Add(Attacker, Finisher);
		UVBFinisherComponent::ParticipantOwners.Add(Victim, Finisher);
		OwnerMesh->AddTickPrerequisiteComponent(VictimMesh);
		OwnerRoot->IgnoreComponentWhenMoving(VictimRoot, true);
		VictimRoot->IgnoreComponentWhenMoving(OwnerRoot, true);
	};
	// 끝나는 방식마다 알림이 정해져 있다 - 취소는 알리고, 버림(시작 실패, 되돌리기)은 알리지 않는다.
	UVBGaugeTestStateListener* Ended = NewObject<UVBGaugeTestStateListener>();
	Finisher->OnFinisherEnded.AddDynamic(Ended, &UVBGaugeTestStateListener::Heard);
	InstallSession(1, true);
	const int32 FirstGeneration = Finisher->LocalGeneration;
	Finisher->EarlyOut();
	TestTrue(TEXT("actual teardown publishes victim state removal"), ChangedParticipants.Contains(Victim));
	TestTrue(TEXT("actual teardown publishes attacker state removal"), ChangedParticipants.Contains(Attacker));
	TestTrue(TEXT("cancel outcome retained"), Finisher->WasLastFinisherCancelled());
	TestEqual(TEXT("cancel broadcasts the end once"), Ended->Count, 1);
	TestFalse(TEXT("cancel ends session immediately"), Finisher->IsPlaying());
	TestFalse(TEXT("cancel replicates inactive state"), Finisher->InteractionReplay.bActive);
	TestFalse(TEXT("cancel removes attacker collision ignore"), OwnerRoot->GetMoveIgnoreComponents().Contains(VictimRoot));
	TestFalse(TEXT("cancel removes victim collision ignore"), VictimRoot->GetMoveIgnoreComponents().Contains(OwnerRoot));
	TestFalse(TEXT("cancel removes tick prerequisite"), OwnerMesh->PrimaryComponentTick.GetPrerequisites().Contains(
		FTickPrerequisite(VictimMesh, VictimMesh->PrimaryComponentTick)));
	TestFalse(TEXT("cancel removes notify owner"), UVBFinisherComponent::ParticipantOwners.Contains(Victim));

	InstallSession(2, false);
	Finisher->HandleInteractionMontageEnded(nullptr, false, FirstGeneration);
	TestTrue(TEXT("old montage completion cannot finish next session"), Finisher->IsPlaying());
	Finisher->HandleInteractionVictimMontageEnded(nullptr, true, FirstGeneration);
	TestTrue(TEXT("old victim interruption cannot cancel next session"), Finisher->IsPlaying());
	Finisher->FinishInteraction(UVBFinisherComponent::EVBInteractionEnd::Abandoned); // Same rollback path used by partial start failure.
	TestTrue(TEXT("rollback preserves pre-existing attacker ignore"), OwnerRoot->GetMoveIgnoreComponents().Contains(VictimRoot));
	TestTrue(TEXT("rollback preserves pre-existing victim ignore"), VictimRoot->GetMoveIgnoreComponents().Contains(OwnerRoot));
	TestTrue(TEXT("rollback preserves pre-existing tick relationship"), OwnerMesh->PrimaryComponentTick.GetPrerequisites().Contains(
		FTickPrerequisite(VictimMesh, VictimMesh->PrimaryComponentTick)));
	TestFalse(TEXT("rollback releases registry"), UVBFinisherComponent::ParticipantOwners.Contains(Attacker));
	TestEqual(TEXT("abandoned session does not broadcast"), Ended->Count, 1);

	// 실제 ASC OnRep 입구를 돌린다 - 끊긴 GAS 대기가 slot 을 되찾으면 안 된다
	UVBAbilitySystemComponent* ASC = NewObject<UVBAbilitySystemComponent>(Attacker);
	Attacker->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Attacker, Attacker);
	Attacker->SetRole(ROLE_SimulatedProxy);
	// ASC 는 등록 때 authority 를 캐시하므로 actor role 만 바꿔서는 갱신되지 않는다
	ASC->CacheIsNetSimulated();
	TestFalse(TEXT("ASC fixture is a simulated proxy"), ASC->IsOwnerActorAuthoritative());
	FGameplayAbilityRepAnimMontage& Rep = ASC->GetRepAnimMontageInfo_Mutable();
	Rep.Animation = NewObject<UAnimMontage>(ASC);
	Rep.PlayInstanceId = 7;
	Rep.IsStopped = false;
	TestTrue(TEXT("external producer acquires GAS montage ownership"), ASC->AcquireExternalMontageOwnership(Finisher, 7));
	TestFalse(TEXT("another producer cannot steal ownership"), ASC->AcquireExternalMontageOwnership(Victim, 7));
	TestEqual(TEXT("GAS play is rejected while external producer owns slots"),
		ASC->PlayMontage(nullptr, FGameplayAbilityActivationInfo(), Cast<UAnimMontage>(Rep.Animation), 1.0f), 0.0f);
	ASC->OnRep_ReplicatedAnimMontage();
	TestTrue(TEXT("replication is deferred while external producer owns slots"), ASC->bDeferredExternalMontageRep);
	ASC->ReleaseExternalMontageOwnership(Victim);
	TestTrue(TEXT("unrelated release cannot remove ownership"), ASC->ExternalMontageOwner.IsValid());
	ASC->ReleaseExternalMontageOwnership(Finisher);
	TestFalse(TEXT("matching release relinquishes montage ownership"), ASC->ExternalMontageOwner.IsValid());
	TestFalse(TEXT("superseded wait does not reach engine replay after release"), ASC->bPendingMontageRep);
	ASC->OnRep_ReplicatedAnimMontage();
	TestFalse(TEXT("late old actor-channel update cannot resurrect superseded wait"), ASC->bPendingMontageRep);
	Rep.PlayInstanceId = 8;
	TestTrue(TEXT("replay uses authoritative previous play id"), ASC->AcquireExternalMontageOwnership(Finisher, 7));
	ASC->OnRep_ReplicatedAnimMontage();
	ASC->ReleaseExternalMontageOwnership(Finisher);
	TestTrue(TEXT("new play arriving during old session reaches engine preparation on release"), ASC->bPendingMontageRep);
	Rep.IsStopped = true;
	ASC->OnRep_ReplicatedAnimMontage();
	TestFalse(TEXT("stopped state never starts a missing montage"), ASC->bPendingMontageRep);
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Dead);
	Rep.IsStopped = false;
	Rep.PlayInstanceId = 9;
	ASC->OnRep_ReplicatedAnimMontage();
	TestFalse(TEXT("dead avatar rejects obsolete ability montage replay"), ASC->bPendingMontageRep);
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
	Attacker->SetRole(ROLE_Authority);
	ASC->CacheIsNetSimulated();

	// 2초가 지나 처음 본 세션도 actor channel 용 재시도 예산이 있어야 한다
	Finisher->InteractionReplay.SessionId = 3;
	Finisher->InteractionReplay.bActive = true;
	Finisher->InteractionReplay.ServerStartTime = World->GetTimeSeconds() - 3.0;
	Finisher->OnRep_InteractionReplay();
	TestTrue(TEXT("late relevancy retries unresolved participants"),
		World->GetTimerManager().IsTimerActive(Finisher->InteractionReplayRetryTimer));
	TestNotEqual(TEXT("late relevancy does not mark unresolved session finished"), Finisher->FinishedInteractionSession, 3);
	const double ReceiptDeadline = Finisher->ReplayRetryDeadline;
	Finisher->OnRep_InteractionReplay();
	TestEqual(TEXT("same-session updates preserve bounded receipt deadline"), Finisher->ReplayRetryDeadline, ReceiptDeadline);
	Finisher->InteractionReplay.bActive = false;
	Finisher->OnRep_InteractionReplay();
	TestFalse(TEXT("completion while waiting cancels replay timer"),
		World->GetTimerManager().IsTimerActive(Finisher->InteractionReplayRetryTimer));
	TestEqual(TEXT("completion records never-started session as finished"), Finisher->FinishedInteractionSession, 3);

	InstallSession(4, true);
	Attacker->SetRole(ROLE_SimulatedProxy);
	Victim->OnDestroyed.AddUniqueDynamic(Finisher, &UVBFinisherComponent::HandleInteractionParticipantDestroyed);
	Victim->Destroy();
	TestFalse(TEXT("local participant loss releases current playback"), Finisher->IsPlaying());
	TestTrue(TEXT("local participant loss preserves authoritative active state"), Finisher->InteractionReplay.bActive);
	TestNotEqual(TEXT("local participant loss does not declare server completion"), Finisher->FinishedInteractionSession, 4);
	TestTrue(TEXT("local participant loss retries active replay"),
		World->GetTimerManager().IsTimerActive(Finisher->InteractionReplayRetryTimer));
	// 앞선 제한 시도가 지난 뒤에도 뒤의 참조 재매핑 갱신은 다시 시도할 수 있다
	World->GetTimerManager().ClearTimer(Finisher->InteractionReplayRetryTimer);
	Finisher->ReplayRetryDeadline = World->GetTimeSeconds() - 1.0;
	Finisher->OnRep_InteractionReplay();
	TestTrue(TEXT("new remapping update reopens bounded preparation window"),
		Finisher->ReplayRetryDeadline > World->GetTimeSeconds());
	Finisher->InteractionReplay.bActive = false;
	Finisher->OnRep_InteractionReplay();
	TestEqual(TEXT("authoritative completion prevents reentry"), Finisher->FinishedInteractionSession, 4);
	Attacker->SetRole(ROLE_Authority);

	// 처치 경합 - 공격자 클립이 피해자 ragdoll notify 한 프레임 전에 끝날 수 있다. 자연 종료도 피해자를 한 번 눕히고, 진짜 끊김은 취소로 남아야 한다.
	// 세션 4 가 첫 피해자를 지웠고 죽은 weak pointer 는 눕히기 루프가 건너뛰므로 살아 있는 피해자를 쓴다.
	Victim = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("fresh victim"), Victim))
	{
		World->DestroyWorld(false);
		return false;
	}

	VictimRoot = Victim->GetCapsuleComponent();
	VictimMesh = Victim->GetMesh();
	UVBFinisherLandedProbe* Probe = NewObject<UVBFinisherLandedProbe>();
	Finisher->OnInteractionVictimLanded.AddDynamic(Probe, &UVBFinisherLandedProbe::OnLanded);
	InstallSession(5, true);
	Finisher->bInteractionVictimLanded = false;
	Finisher->InteractionReplay.bVictimReleased = false;
	Finisher->HandleInteractionMontageEnded(nullptr, /*bInterrupted=*/false, Finisher->LocalGeneration);
	TestEqual(TEXT("natural attacker end before the notify lands the victim once"), Probe->Count, 1);
	TestFalse(TEXT("natural attacker end before the notify is not a cancel"), Finisher->WasLastFinisherCancelled());
	TestTrue(TEXT("late landing replicates the victim release"), Finisher->InteractionReplay.bVictimReleased);
	TestFalse(TEXT("late landing still ends the session"), Finisher->IsPlaying());
	Finisher->HandleInteractionMontageEnded(nullptr, false, Finisher->LocalGeneration - 1);
	TestEqual(TEXT("a repeated end of the finished session cannot land again"), Probe->Count, 1);

	InstallSession(6, true);
	Finisher->bInteractionVictimLanded = true; // The notify already landed the victim (and broadcast) during the scene.
	Finisher->HandleInteractionMontageEnded(nullptr, false, Finisher->LocalGeneration);
	TestEqual(TEXT("a notify-landed victim is not landed a second time at attacker end"), Probe->Count, 1);
	TestFalse(TEXT("notify-landed scene completes"), Finisher->WasLastFinisherCancelled());

	InstallSession(7, true);
	Finisher->bInteractionVictimLanded = false;
	Finisher->HandleInteractionMontageEnded(nullptr, /*bInterrupted=*/true, Finisher->LocalGeneration);
	TestEqual(TEXT("an interrupted attacker does not land the victim"), Probe->Count, 1);
	TestTrue(TEXT("an interrupted attacker before the notify stays cancelled"), Finisher->WasLastFinisherCancelled());
	Finisher->OnInteractionVictimLanded.RemoveDynamic(Probe, &UVBFinisherLandedProbe::OnLanded);

	// 장면 중에 회전 플래그가 정해져 둘 다 꺼졌어도(피해자 사망 -> 락온 해제) 장면 끝에서 회전 정책이 다시 정한다.
	AVBCharacter* Player = World->SpawnActor<AVBCharacter>();
	// C++ 몸에는 finisher 가 없고 BP_VBCharacter 가 단다 - 시험은 필요한 것을 직접 단다
	UVBFinisherComponent* PlayerFinisher = Player ? NewObject<UVBFinisherComponent>(Player) : nullptr;
	if (PlayerFinisher)
	{
		Player->AddInstanceComponent(PlayerFinisher);
		PlayerFinisher->RegisterComponent();
	}

	if (TestNotNull(TEXT("player finisher"), PlayerFinisher))
	{
		UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
		FVBFinisherParticipant PlayerRecord; // owner-only session, every other field at its default
		PlayerRecord.Actor = Player;
		PlayerFinisher->InteractionParticipants = {PlayerRecord};
		PlayerFinisher->FinishInteraction(UVBFinisherComponent::EVBInteractionEnd::Abandoned);
		Cast<UVBSlideMovementComponent>(Movement)->RefreshRotationMode(); // the next movement tick decides the rotation mode
		TestTrue(TEXT("scene end returns facing to the rotation policy"),
			Movement->bOrientRotationToMovement || Movement->bUseControllerDesiredRotation);
	}

	OwnerMesh->RemoveTickPrerequisiteComponent(VictimMesh);
	OwnerRoot->IgnoreComponentWhenMoving(VictimRoot, false);
	VictimRoot->IgnoreComponentWhenMoving(OwnerRoot, false);
	World->DestroyWorld(false);
	return true;
}
#endif

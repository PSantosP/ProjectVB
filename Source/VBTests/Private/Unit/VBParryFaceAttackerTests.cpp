#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBTargetLockPolicy.h"
#include "Data/VBEnemyConfig.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCombatAuditTestAbility.h"

// 패링은 몸과 카메라를 공격자로 돌리고 lock 을 그쪽으로 옮긴다 - 보통 피격은 둘 다 안 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBParryFaceAttackerTest, "VB.Unit.LockOn.ParryFaceAttacker",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBParryFaceAttackerTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("Behind faces 180"),
		FMath::Abs(VBTargetLockPolicy::FacingYaw(FVector::ZeroVector, FVector(-200, 0, 0), 0.0f)), 180.0f, 0.01f);
	TestNearlyEqual(TEXT("Left (+Y) faces 90"), VBTargetLockPolicy::FacingYaw(FVector::ZeroVector, FVector(0, 200, 50), 0.0f),
		90.0f, 0.01f);
	TestEqual(TEXT("Coincident points keep the current yaw"),
		VBTargetLockPolicy::FacingYaw(FVector(5, 5, 0), FVector(5, 5, 90), 37.0f), 37.0f);

	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};

	AVBCharacter* Player = World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	AVBCombatAuditTestEnemy* Front = World->SpawnActor<AVBCombatAuditTestEnemy>(FVector(200, 0, 0), FRotator::ZeroRotator);
	AVBCombatAuditTestEnemy* Behind = World->SpawnActor<AVBCombatAuditTestEnemy>(FVector(-200, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("controller"), Controller)
		|| !TestNotNull(TEXT("front enemy"), Front) || !TestNotNull(TEXT("behind enemy"), Behind))
	{
		return false;
	}

	Front->Configure(NewObject<UVBEnemyConfig>(Front));
	Behind->Configure(NewObject<UVBEnemyConfig>(Behind));
	Controller->Possess(Player);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	UVBTargetLockComponent* Lock = Player->FindComponentByClass<UVBTargetLockComponent>();
	if (!TestNotNull(TEXT("lock component"), Lock))
	{
		return false;
	}

	Lock->FaceAndLockParryTarget(Front, /*bLock=*/true, /*bFace=*/false);
	TestTrue(TEXT("fixture: locked on the front enemy"), Lock->GetLockedTarget() == Front);

	// 반대 경우 먼저: 뒤에서 온 보통 피격은 lock 과 방향을 그대로 둔다.
	Lock->TryAutoLockOnTarget(Behind);
	TestTrue(TEXT("a normal hit does not move an existing lock"), Lock->GetLockedTarget() == Front);
	TestNearlyEqual(TEXT("a normal hit does not turn the body"), Player->GetActorRotation().Yaw, 0.0, 0.01);

	// 뒤의 적을 패링: 같은 호출에서 lock 이 옮겨 가고 몸과 카메라가 그쪽을 본다.
	Lock->FaceAndLockParryTarget(Behind, /*bLock=*/true, /*bFace=*/true);
	TestTrue(TEXT("parry moves the lock to the attacker"), Lock->GetLockedTarget() == Behind);
	TestTrue(TEXT("lock reason is Parry"), Lock->GetLockReason() == EVBLockReason::Parry);
	TestNearlyEqual(TEXT("parry turns the body to the attacker"), FMath::Abs(Player->GetActorRotation().Yaw), 180.0, 0.01);
	TestNearlyEqual(TEXT("parry turns the camera yaw to the attacker"),
		FMath::Abs(Controller->GetControlRotation().GetNormalized().Yaw), 180.0, 0.01);

	// 플래그를 끄면 데이터가 각 부분을 따로 끌 수 있다.
	Lock->FaceAndLockParryTarget(Front, /*bLock=*/true, /*bFace=*/false);
	Player->SetActorRotation(FRotator::ZeroRotator);
	Lock->FaceAndLockParryTarget(Behind, /*bLock=*/false, /*bFace=*/false);
	TestTrue(TEXT("lock flag off keeps the lock"), Lock->GetLockedTarget() == Front);
	TestNearlyEqual(TEXT("face flag off keeps the body"), Player->GetActorRotation().Yaw, 0.0, 0.01);
	return true;
}

// 방향 공격 전환의 확정: 같은 판일 때만, 이유는 Switch, 같은 대상, 옛 판은 거절.
//   고르기는 순수 정책 시험(DirectionalPick)이 맡는다 - 이 세계는 물리 장면이 없어 겹침 수집을 못 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDirectionalCommitTest, "VB.Unit.LockOn.DirectionalCommit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDirectionalCommitTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};

	AVBCharacter* Player = World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	AVBCombatAuditTestEnemy* Front = World->SpawnActor<AVBCombatAuditTestEnemy>(FVector(200, 0, 0), FRotator::ZeroRotator);
	AVBCombatAuditTestEnemy* Side = World->SpawnActor<AVBCombatAuditTestEnemy>(FVector(0, 200, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("controller"), Controller)
		|| !TestNotNull(TEXT("front enemy"), Front) || !TestNotNull(TEXT("side enemy"), Side))
	{
		return false;
	}

	Front->Configure(NewObject<UVBEnemyConfig>(Front));
	Side->Configure(NewObject<UVBEnemyConfig>(Side));
	Controller->Possess(Player);
	UVBTargetLockComponent* Lock = Player->FindComponentByClass<UVBTargetLockComponent>();
	if (!TestNotNull(TEXT("lock component"), Lock))
	{
		return false;
	}

	Lock->FaceAndLockParryTarget(Front, /*bLock=*/true, /*bFace=*/false);
	if (!TestTrue(TEXT("fixture: locked on the front enemy"), Lock->GetLockedTarget() == Front))
	{
		return false;
	}

	const uint32 Revision = Lock->GetLockRevision();
	TestFalse(TEXT("a stale revision is refused"), Lock->CommitDirectionalSwitch(Side, Revision + 1));
	TestTrue(TEXT("refused commit keeps the lock"), Lock->GetLockedTarget() == Front);
	TestFalse(TEXT("the current target is not a switch"), Lock->CommitDirectionalSwitch(Front, Revision));
	TestFalse(TEXT("no target is refused"), Lock->CommitDirectionalSwitch(nullptr, Revision));

	TestTrue(TEXT("matching revision commits"), Lock->CommitDirectionalSwitch(Side, Revision));
	TestTrue(TEXT("lock moved to the side enemy"), Lock->GetLockedTarget() == Side);
	TestTrue(TEXT("reason is Switch"), Lock->GetLockReason() == EVBLockReason::Switch);
	TestNotEqual(TEXT("revision advanced"), Lock->GetLockRevision(), Revision);
	TestFalse(TEXT("the old revision cannot commit again"), Lock->CommitDirectionalSwitch(Front, Revision));
	return true;
}
#endif

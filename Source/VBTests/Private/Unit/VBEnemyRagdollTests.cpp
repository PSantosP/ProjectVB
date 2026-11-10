#include "Unit/VBAirDeathFeedbackTestProbe.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "CharacterCommon/VBDeathFacts.h"
#include "Components/CapsuleComponent.h"
#include "Enemy/VBEnemyDeathPresentationComponent.h"
#include "Enemy/VBEnemyRagdollComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_TESTS
namespace VBEnemyRagdollTest
{
	UWorld* MakeWorld()
	{
		const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
		if (World)
		{
			World->GetOutermost()->SetFlags(RF_Transient);
		}

		return World;
	}

	AVBAirDeathFeedbackTestEnemy* SpawnEnemy(UWorld& World)
	{
		AVBAirDeathFeedbackTestEnemy* Enemy = World.SpawnActor<AVBAirDeathFeedbackTestEnemy>();
		if (Enemy)
		{
			Enemy->GetAbilitySystemComponent()->InitAbilityActorInfo(Enemy, Enemy);
		}

		return Enemy;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyRagdollReplicatesTest, "VB.Unit.Enemy.Ragdoll.ComponentsReplicate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyRagdollReplicatesTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBEnemyRagdollTest::MakeWorld();
	ON_SCOPE_EXIT { if (World) { World->DestroyWorld(false); } };
	AVBAirDeathFeedbackTestEnemy* Enemy = World ? VBEnemyRagdollTest::SpawnEnemy(*World) : nullptr;
	if (!TestNotNull(TEXT("enemy"), Enemy)) { return false; }

	// 복제가 꺼지면 원격 클라에서 쓰러짐과 죽음이 아무 말 없이 안 보인다 - 그 상태를 여기서 막는다.
	const UVBEnemyRagdollComponent* Ragdoll = UVBEnemyRagdollComponent::FindFor(Enemy);
	const UVBEnemyDeathPresentationComponent* Death = UVBEnemyDeathPresentationComponent::FindFor(Enemy);
	if (!TestNotNull(TEXT("enemy ragdoll assembled"), Ragdoll) || !TestNotNull(TEXT("death presentation assembled"), Death)) { return false; }
	TestTrue(TEXT("enemy ragdoll replicates"), Ragdoll->GetIsReplicated());
	TestTrue(TEXT("death presentation replicates"), Death->GetIsReplicated());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyRagdollCorpseOnceTest, "VB.Unit.Enemy.Ragdoll.CorpseOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyRagdollCorpseOnceTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBEnemyRagdollTest::MakeWorld();
	ON_SCOPE_EXIT { if (World) { World->DestroyWorld(false); } };
	AVBAirDeathFeedbackTestEnemy* Enemy = World ? VBEnemyRagdollTest::SpawnEnemy(*World) : nullptr;
	UVBEnemyRagdollComponent* Ragdoll = UVBEnemyRagdollComponent::FindFor(Enemy);
	if (!TestNotNull(TEXT("enemy ragdoll"), Ragdoll)) { return false; }

	TestFalse(TEXT("alive body starts outside corpse"), Ragdoll->HasEnteredCorpse());
	TestTrue(TEXT("first corpse entry switches the body"), Ragdoll->EnterCorpse(FVBCorpseLaunch(), 0.0));
	TestTrue(TEXT("corpse entry is remembered"), Ragdoll->HasEnteredCorpse());
	TestEqual(TEXT("corpse capsule no longer blocks"), Enemy->GetCapsuleComponent()->GetCollisionEnabled(),
		ECollisionEnabled::NoCollision);
	// 몽타주 종료와 안전망 타이머가 둘 다 와도 시체는 한 번만 만든다.
	TestFalse(TEXT("second corpse entry is refused"), Ragdoll->EnterCorpse(FVBCorpseLaunch(), 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyDeathWaitsForBodyTest, "VB.Unit.Enemy.DeathPresentation.WaitsForBody",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyDeathWaitsForBodyTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBEnemyRagdollTest::MakeWorld();
	ON_SCOPE_EXIT { if (World) { World->DestroyWorld(false); } };
	AVBAirDeathFeedbackTestEnemy* Enemy = World ? VBEnemyRagdollTest::SpawnEnemy(*World) : nullptr;
	UVBEnemyDeathPresentationComponent* Death = UVBEnemyDeathPresentationComponent::FindFor(Enemy);
	const UVBEnemyRagdollComponent* Ragdoll = UVBEnemyRagdollComponent::FindFor(Enemy);
	if (!TestNotNull(TEXT("death presentation"), Death) || !TestNotNull(TEXT("enemy ragdoll"), Ragdoll)) { return false; }

	FVBDeathFacts Facts;
	Facts.DeadActor = Enemy;
	// 늦게 받은 기계처럼 몸 BeginPlay 전에 죽음이 온다. 스냅샷은 받지만 재생은 몸이 준비될 때까지 미룬다.
	Death->Present(Facts);
	TestTrue(TEXT("snapshot makes the body dead at once"), Enemy->IsDead());
	TestFalse(TEXT("death does not play before the body is ready"), Ragdoll->HasEnteredCorpse());
	Death->MarkBodyReady();
	TestTrue(TEXT("pending death plays when the body is ready"), Ragdoll->HasEnteredCorpse());
	return true;
}
#endif

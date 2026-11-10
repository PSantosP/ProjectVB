// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"
#include "Animation/VBMotionWarpingNames.h"
#include "Character/VBCharacter.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "MotionWarpingComponent.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

// 공격 워프 목표의 수명은 공격 GA 하나가 정한다: 캐릭터 Tick 은 멈춤 거리에 닿아 붙잡은 점도, 안으로 파고든 대상의 따라가는 목표도 지우지 않는다.
//   대조: 캐릭터 Tick 이 컴포넌트 없는 목표를 지우면 "held point" 줄이, CanKeepAttackWarp 가 늘 참이면 "destroyed" 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttackWarpLifetimeTest, "VB.Unit.MeleeApproach.WarpLifetime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAttackWarpLifetimeTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVBCharacter* Character = World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	AActor* Enemy = World->SpawnActor<AActor>();
	UMotionWarpingComponent* Warping = Character ? Character->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
	if (!TestTrue(TEXT("actors"), Character && Enemy && Warping))
	{
		return false;
	}

	USceneComponent* EnemyRoot = NewObject<USceneComponent>(Enemy);
	Enemy->SetRootComponent(EnemyRoot);
	EnemyRoot->RegisterComponent();
	const float Frame = 1.0f / 60.0f;

	// 멈춤 거리에 닿아 GA 가 붙잡은 점(컴포넌트 없는 점) - 한 프레임 전진으로 공격자 뒤 3 cm 가 됐다.
	EnemyRoot->SetWorldLocation(FVector(100.0, 0.0, 0.0));
	Warping->AddOrUpdateWarpTargetFromLocationAndRotation(VBMotionWarpingNames::AttackTarget, FVector(-3.0, 0.0, 0.0),
		FRotator::ZeroRotator);
	AActor* CharacterActor = Character; // AVBCharacter::Tick 은 protected 라 엔진처럼 AActor 로 부른다
	CharacterActor->Tick(Frame);
	TestNotNull(TEXT("held point behind the attacker: the character frame keeps it"),
		Warping->FindWarpTarget(VBMotionWarpingNames::AttackTarget));

	// 따라가는 목표(멈춤 100 cm)인데 대상이 60 cm 안으로 파고들었다 - 목표 점이 공격자 뒤다.
	EnemyRoot->SetWorldLocation(FVector(60.0, 0.0, 0.0));
	Warping->AddOrUpdateWarpTargetFromComponent(VBMotionWarpingNames::AttackTarget, EnemyRoot, NAME_None, true,
		EWarpTargetLocationOffsetDirection::VectorFromTargetToOwner, FVector(100.0, 0.0, 0.0), FRotator::ZeroRotator);
	CharacterActor->Tick(Frame);
	TestNotNull(TEXT("enemy lunged inside the stop distance: the character frame keeps the follow target"),
		Warping->FindWarpTarget(VBMotionWarpingNames::AttackTarget));

	// 이어 가도 되는 대상 - GA 가 매 워프 갱신에 묻는다.
	TestFalse(TEXT("no target: drop"), VBMeleeAttackRules::CanKeepAttackWarp(nullptr));
	TestTrue(TEXT("live actor without a targetable rule: keep"), VBMeleeAttackRules::CanKeepAttackWarp(Enemy));
	Enemy->Destroy();
	TestFalse(TEXT("destroyed target: drop"), VBMeleeAttackRules::CanKeepAttackWarp(Enemy));
	Character->Destroy();
	return true;
}

#endif

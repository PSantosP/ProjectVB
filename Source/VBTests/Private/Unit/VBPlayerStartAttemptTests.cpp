// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VBPlayerStartAttempt.h"
#include "Misc/AutomationTest.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Unit/VBIsolatedGameWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

// 시작할 때 본 세이브, 폰, 컨트롤러가 그대로일 때만 시도가 유효하다.
//   세이브 시도 번호가 바뀌는 갈래는 여기서 보지 않는다. 번호를 바꾸는 시작 경로는 디스크 쓰기를 막는 시험 통로가 있어야 돈다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayerStartAttemptCurrentTest, "VB.Unit.PlayerStart.AttemptIsCurrent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayerStartAttemptCurrentTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld* World = Isolated.World;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Pawn = World->SpawnActor<ACharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	ACharacter* OtherPawn = World->SpawnActor<ACharacter>(FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator, Spawn);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	AActor* Mode = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("pawn"), Pawn) || !TestNotNull(TEXT("other pawn"), OtherPawn) || !TestNotNull(TEXT("controller"), Controller)
		|| !TestNotNull(TEXT("game mode stand-in"), Mode))
	{
		return false;
	}

	UVBPlayerReadinessComponent* Readiness = NewObject<UVBPlayerReadinessComponent>(Pawn);
	Pawn->AddInstanceComponent(Readiness);
	Readiness->RegisterComponent();
	Controller->Possess(Pawn);
	if (!TestTrue(TEXT("fixture controller possesses the pawn"), Pawn->GetController() == Controller && Controller->GetPawn() == Pawn))
	{
		return false;
	}

	const TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	const TStrongObjectPtr<UVBSaveGameSubsystem> Save(NewObject<UVBSaveGameSubsystem>(Instance.Get()));
	const FVBPlayerStartAttempt Attempt(*Mode, Controller, Save.Get());
	TestTrue(TEXT("nothing changed: current"), Attempt.IsCurrent());

	Controller->UnPossess();
	TestFalse(TEXT("controller left the pawn: not current"), Attempt.IsCurrent());
	Controller->Possess(OtherPawn);
	TestFalse(TEXT("controller moved to another pawn: not current"), Attempt.IsCurrent());
	Controller->Possess(Pawn);
	TestTrue(TEXT("back on the pawn: current"), Attempt.IsCurrent());

	const FVBPlayerStartAttempt NoSave(*Mode, Controller, nullptr);
	TestFalse(TEXT("no save subsystem: not current"), NoSave.IsCurrent());
	const FVBPlayerStartAttempt NoReadiness(*Mode, nullptr, Save.Get());
	TestFalse(TEXT("no player: not current"), NoReadiness.IsCurrent());
	return true;
}

#endif

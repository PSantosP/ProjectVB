// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetLockComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Player/VBPlayerState.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBPlayerBodyTestTypes.h"

namespace VBPlayerBodyTest
{
	// local player 가 있는 controller 하나, PlayerState(공유 ASC) 하나, 몸 둘, 잠금 표적 하나. 빙의는 각 시험이 직접 한다.
	//   A 는 원점에서 표적까지 300 cm(기본 획득 범위 800 cm 안), B 는 800 cm 옆이다.
	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		APlayerController* PC = nullptr;
		AVBPlayerState* State = nullptr;
		AVBCharacter* A = nullptr;
		AVBCharacter* B = nullptr;
		AVBTestLockTarget* Target = nullptr;
		UVBAbilitySystemComponent* ASC = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->InitializeActorsForPlay(FURL());
			PC = World->SpawnActor<APlayerController>();
			if (PC)
			{
				LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient));
				PC->Player = LocalPlayer.Get();
				LocalPlayer->PlayerController = PC;
			}

			State = World->SpawnActor<AVBPlayerState>();
			A = World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			B = World->SpawnActor<AVBCharacter>(FVector(0.0, 800.0, 0.0), FRotator::ZeroRotator);
			Target = World->SpawnActor<AVBTestLockTarget>(FVector(300.0, 0.0, 0.0), FRotator::ZeroRotator);
			if (!Test.TestNotNull(TEXT("controller"), PC) || !Test.TestNotNull(TEXT("player state"), State)
				|| !Test.TestNotNull(TEXT("body A"), A)
				|| !Test.TestNotNull(TEXT("body B"), B) || !Test.TestNotNull(TEXT("lock target"), Target))
			{
				return false;
			}

			State->SetOwner(PC);
			PC->PlayerState = State;
			ASC = State->GetVBAbilitySystemComponent();
			return Test.TestNotNull(TEXT("PlayerState ASC"), ASC) && Test.TestNotNull(TEXT("A lock-on component"),
				UVBTargetLockComponent::FindFor(A));
		}

		~FFixture()
		{
			if (World)
			{
				if (PC)
				{
					PC->Player = nullptr;
				}

				World->DestroyWorld(false);
			}

			if (Package)
			{
				Package->SetDirtyFlag(false);
			}
		}
	};
}
#endif

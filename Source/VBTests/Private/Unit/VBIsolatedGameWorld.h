// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

// 액터 초기화가 켜진 격리 게임 월드 - 스폰 때 InitializeComponent 가 불려 저장 참여자가 등록된다. 내비, AI 는 없다.
//   물리 장면은 bPhysicsScene 일 때만 만든다(트레이스, 겹침 질의가 필요한 시험). 시뮬레이션, 틱은 없다.
//   강화, 제작, 빠른 이동 시험이 같이 쓴다. 범위를 벗어나면 월드와 그 문맥을 치운다.
struct FVBIsolatedGameWorld
{
	UWorld* World = nullptr;
	UPackage* Package = nullptr;

	bool Init(FAutomationTestBase& Test, bool bPhysicsScene = false)
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).CreatePhysicsScene(bPhysicsScene).CreateNavigation(false)
			.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
		World = GEngine ? UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values) : nullptr;
		if (!Test.TestNotNull(TEXT("isolated world"), World))
		{
			return false;
		}

		Package = World->GetPackage();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return true;
	}

	~FVBIsolatedGameWorld()
	{
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			Package->SetDirtyFlag(false);
		}
	}
};

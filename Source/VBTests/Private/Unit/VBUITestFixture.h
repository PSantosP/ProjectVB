// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Player/VBPlayerController.h"
#include "Player/VBPlayerState.h"

// UI 위젯 시험의 공용 셋업(세 번째로 필요해져 뽑았다 - HUD 줄 묶기, UI 슬롯).
// 격리된 게임 월드 + 실제 로컬 플레이어 문맥의 PC. 구성, 해체는 실제 Slate 길(TakeWidget / ReleaseSlateResources)로 탄다.
namespace VBUITest
{
	struct FRowFixture
	{
		UWorld* World = nullptr;
		ULocalPlayer* LocalPlayer = nullptr;
		AVBPlayerController* Controller = nullptr;

		// ControllerClass: 엔진 쪽 호출을 기록하는 시험용 PC 가 필요할 때(진동 시험). 비우면 AVBPlayerController.
		bool Initialize(FAutomationTestBase& Test, TSubclassOf<AVBPlayerController> ControllerClass = nullptr)
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
			if (!Test.TestNotNull(TEXT("isolated world"), World))
			{
				return false;
			}

			World->GetOutermost()->SetFlags(RF_Transient);
			if (!Test.TestNotNull(TEXT("engine"), GEngine))
			{
				return false;
			}

			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			// 액터 초기화를 켠다. 안 켜면 스폰이 PostInitializeComponents 를 건너뛰어(AActor::PostActorConstruction 이 AreActorsInitialized 를 본다)
			//   PC 가 월드의 PC 목록에 안 오르고 - 위젯의 주인 플레이어는 그 목록에서 찾는다(FLocalPlayerContext::IsValid) - ASC 도 속성 세트를 등록하지 않는다.
			World->InitializeActorsForPlay(FURL());
			Controller = World->SpawnActor<AVBPlayerController>(ControllerClass ? *ControllerClass : AVBPlayerController::StaticClass());
			if (!Test.TestNotNull(TEXT("controller"), Controller))
			{
				return false;
			}
			// 위젯의 주인 플레이어가 이 PC 가 되도록 실제 로컬 플레이어 문맥을 세운다(알림 생산자 시험과 같은 fixture).
			LocalPlayer = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
			Controller->Player = LocalPlayer;
			LocalPlayer->PlayerController = Controller;
			LocalPlayer->PlayerAdded(nullptr, 0);
			return true;
		}

		AVBPlayerState* SpawnPlayerState(FAutomationTestBase& Test) const
		{
			AVBPlayerState* PlayerState = World->SpawnActor<AVBPlayerState>();
			Test.TestNotNull(TEXT("player state"), PlayerState);
			return PlayerState;
		}

		// PlayerState 가 정해졌다는 신호를 실제 길(OnRep -> BroadcastPlayerStateSet)로 보낸다.
		void SetPlayerState(APlayerState* PlayerState) const
		{
			Controller->PlayerState = PlayerState;
			Controller->OnRep_PlayerState();
		}

		template <class TWidget>
		TWidget* MakeRow(FAutomationTestBase& Test, const TCHAR* ClassPath) const
		{
			// 줄 위젯 클래스는 Abstract 이고 필수 BindWidget 이 있어 실제 WBP 로 짓는다.
			UClass* Class = ClassPath ? LoadClass<TWidget>(nullptr, ClassPath) : TWidget::StaticClass();
			if (!Test.TestNotNull(*FString::Printf(TEXT("widget class %s"), ClassPath ? ClassPath : TEXT("native")), Class))
			{
				return nullptr;
			}

			TWidget* Widget = CreateWidget<TWidget>(Controller, Class);
			Test.TestNotNull(*FString::Printf(TEXT("widget %s"), *GetNameSafe(Class)), Widget);
			return Widget;
		}

		void Shutdown()
		{
			if (LocalPlayer)
			{
				LocalPlayer->PlayerRemoved();
				if (Controller)
				{
					Controller->Player = nullptr;
				}

				LocalPlayer->PlayerController = nullptr;
			}

			if (World)
			{
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}

				World->DestroyWorld(false);
			}
		}
	};

	// 실제 Slate 수명. 구성 = TakeWidget, 해체 = 위젯의 Slate 를 놓고 마지막 참조를 버린다.
	struct FSlateLife
	{
		TSharedPtr<SWidget> Slate;

		void Construct(UUserWidget& Widget) { Slate = Widget.TakeWidget(); }
		void Destruct(UUserWidget& Widget)
		{
			Widget.ReleaseSlateResources( /*bReleaseChildren=*/true);
			Slate.Reset();
		}
	};
}
#endif

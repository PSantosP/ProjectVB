#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Data/VBActivationVisualProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Interaction/VBActivationVisualComponent.h"
#include "Interaction/VBInteractableComponent.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <type_traits>

namespace
{
	// 켜짐 표시만 보는 격리 월드. Editor World 와 제품 맵은 쓰지 않는다(VBAreaTriggerOccupancyTests 와 같은 정리 순서).
	struct FActivationVisualWorld
	{
		UWorld* World = nullptr;
		AGameStateBase* GameState = nullptr;

		~FActivationVisualWorld()
		{
			if (World)
			{
				const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
				if (World->HasBegunPlay())
				{
					World->EndPlay(EEndPlayReason::Quit);
				}

				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}

				OwnedPackage->SetDirtyFlag(false);
			}
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("engine for isolated world"), GEngine))
			{
				return false;
			}

			const UWorld::InitializationValues Initialization = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
			if (!Test.TestNotNull(TEXT("isolated world"), World))
			{
				return false;
			}

			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			GameState = World->SpawnActor<AGameStateBase>();
			if (!Test.TestNotNull(TEXT("isolated game state"), GameState))
			{
				return false;
			}

			World->SetGameState(GameState);
			return true;
		}

		void BeginPlay() const
		{
			World->InitializeActorsForPlay(FURL());
			GameState->HandleBeginPlay();
		}

		AActor* SpawnFixture() const
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Actor->SetRootComponent(Root);
			Actor->AddInstanceComponent(Root);
			Root->RegisterComponent();
			return Actor;
		}

		template <typename T>
		T* Add(AActor* Actor, const FName Name) const
		{
			T* Component = NewObject<T>(Actor, Name);
			// 관계없는 형에 Cast 를 쓰면 컴파일 경고(C4996)가 난다 - 형으로 가른다.
			if constexpr (std::is_base_of_v<USceneComponent, T>)
			{
				Component->SetupAttachment(Actor->GetRootComponent());
			}

			Actor->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}

		UPointLightComponent* AddLight(AActor* Actor, const FName Name, const EComponentMobility::Type Mobility) const
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(Actor, Name);
			// 엔진은 Static 을 움직이는 부모에 붙이지 않는다(경고 뒤 중단) - 그 경우는 붙이지 않고 둔다.
			if (Mobility != EComponentMobility::Static)
			{
				Light->SetupAttachment(Actor->GetRootComponent());
			}

			Light->SetMobility(Mobility);
			Actor->AddInstanceComponent(Light);
			Light->RegisterComponent();
			return Light;
		}
	};

	UVBActivationVisualProfile* MakeProfile(const bool bLightsOnWhenActivated)
	{
		UVBActivationVisualProfile* Profile = NewObject<UVBActivationVisualProfile>(GetTransientPackage());
		Profile->bLightsOnWhenActivated = bLightsOnWhenActivated;
		return Profile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBActivationVisualResolveTest, "VB.Unit.Interaction.ActivationVisual.Resolve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBActivationVisualResolveTest::RunTest(const FString& Parameters)
{
	UVBActivationVisualProfile* Profile = MakeProfile(true);
	FVBActivationScalarParam Strength;
	Strength.Parameter = TEXT("EmissiveStrength");
	Strength.Off = 0.0f;
	Strength.On = 20.0f;
	Profile->ScalarParams.Add(Strength);
	FVBActivationColorParam Color;
	Color.Parameter = TEXT("EmissiveColor");
	Color.Off = FLinearColor::Black;
	Color.On = FLinearColor::Green;
	Profile->ColorParams.Add(Color);

	const FVBActivationVisualValues Off = Profile->Resolve(false);
	const FVBActivationVisualValues On = Profile->Resolve(true);
	TestEqual(TEXT("off scalar"), Off.ScalarValues[0], 0.0f);
	TestEqual(TEXT("on scalar"), On.ScalarValues[0], 20.0f);
	TestEqual(TEXT("off color"), Off.ColorValues[0], FLinearColor::Black);
	TestEqual(TEXT("on color"), On.ColorValues[0], FLinearColor::Green);
	TestFalse(TEXT("off hides lights"), Off.bLightsVisible);
	TestTrue(TEXT("on shows lights"), On.bLightsVisible);

	// 반대 회로: 켜지면 꺼지는 등.
	Profile->bLightsOnWhenActivated = false;
	TestTrue(TEXT("inverse: off shows lights"), Profile->Resolve(false).bLightsVisible);
	TestFalse(TEXT("inverse: on hides lights"), Profile->Resolve(true).bLightsVisible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBActivationVisualApplyTest, "VB.Unit.Interaction.ActivationVisual.Apply",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBActivationVisualApplyTest::RunTest(const FString& Parameters)
{
	// 시험 대상에는 상호작용 설정 자산이 없다 - 그 경고는 이 시험이 보는 것이 아니다.
	AddExpectedMessagePlain(TEXT("Config 가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("Static 이라"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("SourceInteractableName"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	FActivationVisualWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBActivationVisualProfile* Normal = MakeProfile(true);

	// 1. 기본(꺼짐). 표시가 상호작용보다 먼저 붙은 순서.
	AActor* Plain = Fixture.SpawnFixture();
	UVBActivationVisualComponent* PlainVisual = Fixture.Add<UVBActivationVisualComponent>(Plain, TEXT("Visual"));
	PlainVisual->Profile = Normal;
	UVBInteractableComponent* PlainSource = Fixture.Add<UVBInteractableComponent>(Plain, TEXT("Interactable"));
	UPointLightComponent* PlainLight = Fixture.AddLight(Plain, TEXT("Lamp"), EComponentMobility::Movable);

	// 2. 시작 전에 이미 켜짐(세이브 복원이 먼저 끝난 경우). 상호작용이 먼저 붙은 순서.
	AActor* Restored = Fixture.SpawnFixture();
	UVBInteractableComponent* RestoredSource = Fixture.Add<UVBInteractableComponent>(Restored, TEXT("Interactable"));
	UVBActivationVisualComponent* RestoredVisual = Fixture.Add<UVBActivationVisualComponent>(Restored, TEXT("Visual"));
	RestoredVisual->Profile = Normal;
	UPointLightComponent* RestoredLight = Fixture.AddLight(Restored, TEXT("Lamp"), EComponentMobility::Movable);
	RestoredSource->SetActivated(true, /*bInstant=*/true);

	// 3. 반대 회로.
	AActor* Inverse = Fixture.SpawnFixture();
	UVBInteractableComponent* InverseSource = Fixture.Add<UVBInteractableComponent>(Inverse, TEXT("Interactable"));
	UVBActivationVisualComponent* InverseVisual = Fixture.Add<UVBActivationVisualComponent>(Inverse, TEXT("Visual"));
	InverseVisual->Profile = MakeProfile(false);
	UPointLightComponent* InverseLight = Fixture.AddLight(Inverse, TEXT("Lamp"), EComponentMobility::Movable);

	// 4. Static 조명은 건너뛴다(엔진이 런타임 변경을 거절한다).
	AActor* StaticOwner = Fixture.SpawnFixture();
	Fixture.Add<UVBInteractableComponent>(StaticOwner, TEXT("Interactable"));
	UVBActivationVisualComponent* StaticVisual = Fixture.Add<UVBActivationVisualComponent>(StaticOwner, TEXT("Visual"));
	StaticVisual->Profile = Normal;
	UPointLightComponent* StaticLight = Fixture.AddLight(StaticOwner, TEXT("Lamp"), EComponentMobility::Static);

	// 5. 상호작용이 둘인데 이름이 없다 - 묶지 않는다. 이름을 적은 쪽은 그것을 따른다.
	AActor* Ambiguous = Fixture.SpawnFixture();
	Fixture.Add<UVBInteractableComponent>(Ambiguous, TEXT("Open"));
	Fixture.Add<UVBInteractableComponent>(Ambiguous, TEXT("Lock"));
	UVBActivationVisualComponent* AmbiguousVisual = Fixture.Add<UVBActivationVisualComponent>(Ambiguous, TEXT("Visual"));
	AmbiguousVisual->Profile = Normal;
	AActor* Named = Fixture.SpawnFixture();
	Fixture.Add<UVBInteractableComponent>(Named, TEXT("Open"));
	UVBInteractableComponent* NamedLock = Fixture.Add<UVBInteractableComponent>(Named, TEXT("Lock"));
	UVBActivationVisualComponent* NamedVisual = Fixture.Add<UVBActivationVisualComponent>(Named, TEXT("Visual"));
	NamedVisual->Profile = Normal;
	NamedVisual->SourceInteractableName = TEXT("Lock");
	UPointLightComponent* NamedLight = Fixture.AddLight(Named, TEXT("Lamp"), EComponentMobility::Movable);

	Fixture.BeginPlay();

	TestTrue(TEXT("plain: bound"), PlainVisual->IsBoundToSource());
	TestFalse(TEXT("plain: default off hides the lamp"), PlainLight->GetVisibleFlag());
	TestTrue(TEXT("restored: on before BeginPlay shows the lamp"), RestoredLight->GetVisibleFlag());
	TestTrue(TEXT("inverse: off shows the lamp"), InverseLight->GetVisibleFlag());
	TestTrue(TEXT("static: lamp untouched (authored visible)"), StaticLight->GetVisibleFlag());
	TestFalse(TEXT("ambiguous: not bound"), AmbiguousVisual->IsBoundToSource());
	TestTrue(TEXT("named: bound"), NamedVisual->IsBoundToSource());
	TestFalse(TEXT("named: lock off hides the lamp"), NamedLight->GetVisibleFlag());

	PlainSource->SetActivated(true);
	TestTrue(TEXT("plain: on shows the lamp"), PlainLight->GetVisibleFlag());
	PlainSource->SetActivated(false);
	TestFalse(TEXT("plain: off again hides the lamp"), PlainLight->GetVisibleFlag());
	for (int32 Round = 0; Round < 5; ++Round)
	{
		PlainSource->SetActivated(true);
		PlainSource->SetActivated(false);
	}

	PlainSource->SetActivated(true);
	TestTrue(TEXT("plain: after repeated toggles the last state wins"), PlainLight->GetVisibleFlag());

	InverseSource->SetActivated(true);
	TestFalse(TEXT("inverse: on hides the lamp"), InverseLight->GetVisibleFlag());

	NamedLock->SetActivated(true);
	TestTrue(TEXT("named: lock on shows the lamp"), NamedLight->GetVisibleFlag());
	return true;
}

#endif

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBFinisherSelection.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/SceneComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "PoseSearch/PoseSearchResult.h"
#include "Types/VBWeaponStateTypes.h"

// 실제 플레이어 BP 에서 컴포넌트의 public GetSelection() 으로 선택 주인을 부른다 - friend 도, private 주입도 없다.
// 빈 월드에서 BP 데이터에 닿지 못하면(장면 처형을 가진 무기 없음) 기대값을 짐작하지 않고 warning 으로 밝힌다.

#if WITH_AUTOMATION_TESTS
namespace VBFinisherSelectionTest
{
	struct FFixture
	{
		UWorld* World = nullptr;
		ACharacter* Owner = nullptr;
		UVBFinisherComponent* Session = nullptr;

		bool Make(FAutomationTestBase& Test)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			UClass* CharacterClass = LoadClass<ACharacter>(nullptr,
				TEXT("/Game/VB/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C"));
			Owner = World && CharacterClass ? World->SpawnActor<ACharacter>(CharacterClass) : nullptr;
			Session = UVBFinisherComponent::FindFor(Owner);
			return Test.TestNotNull(TEXT("selection fixture world"), World) && Test.TestNotNull(TEXT("player BP class"), CharacterClass)
				&& Test.TestNotNull(TEXT("player BP spawned"), Owner) && Test.TestNotNull(TEXT("finisher component"), Session);
		}

		~FFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		// ability 와 컴포넌트가 묻는 그대로의 시작 질문 - 세션 사실은 세션 주인에게서 온다.
		FVBFinisherStartQuery Query(AActor* Target) const
		{
			return {Owner, Target, Session->IsPlaying(), UVBFinisherComponent::IsActorInScene(Target)};
		}

		// +X 를 보는 root 만 있는 액터 - 접근 정책이 위치와 forward vector 를 읽을 수 있게.
		AActor* SpawnTarget(const FVector& Location, const FRotator& Rotation) const
		{
			AActor* Target = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Target);
			Target->SetRootComponent(Root);
			Root->RegisterComponent();
			Target->SetActorLocationAndRotation(Location, Rotation);
			return Target;
		}

		// 장면 처형이 있는 무기를 손에 든다. 빈 월드는 무기가 없어 후보마다 저장 복원 입구로 주고 SummonWeapon 한다(EnsureWeaponSummoned 는 무기가 나와 있으면 안 한다).
		//   장면 처형이 아예 없으면 warning, 있는데 못 닿으면 fixture 가 깨진 것이라 error.
		bool SummonWeaponWithSceneFinisher(FAutomationTestBase& Test) const
		{
			UVBWeaponStateComponent* Weapons = Owner->FindComponentByClass<UVBWeaponStateComponent>();
			const UEnum* Types = StaticEnum<EVBWeaponType>();
			if (!Test.TestNotNull(TEXT("weapon state on the player BP"), Weapons) || !Types)
			{
				return false;
			}

			const auto HasScene = [](const UVBFinisherConfig* Config) { return Config && Config->Scene; };
			TArray<const UVBFinisherConfig*> AllConfigs;
			Weapons->GetAllFinisherConfigs(AllConfigs);
			if (!AllConfigs.ContainsByPredicate(HasScene))
			{
				Test.AddWarning(TEXT("fixture: the player BP has no scene finisher on any weapon - the approach filter is not observed here"));
				return false;
			}

			for (int32 Index = 0; Index < Types->NumEnums() - 1; ++Index)
			{
				const EVBWeaponType Type = static_cast<EVBWeaponType>(Types->GetValueByIndex(Index));
				if (Type == EVBWeaponType::None)
				{
					continue;
				}

				Weapons->SetOwnedWeapons({Type});
				Weapons->SummonWeapon(Type);
				const TArray<TObjectPtr<UVBFinisherConfig>>* Configs = Weapons->GetFinisherConfigs();
				if (Weapons->GetCurrentWeaponType() == Type && Configs && Configs->ContainsByPredicate(HasScene))
				{
					return true;
				}
			}

			Test.AddError(TEXT("fixture: the player data has a scene finisher but no weapon holding it could be summoned"));
			return false;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherSelectionApproachTest, "VB.Unit.Finisher.SelectionApproachFilter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBFinisherSelectionApproachTest::RunTest(const FString& Parameters)
{
	VBFinisherSelectionTest::FFixture Fixture;
	if (!Fixture.Make(*this))
	{
		return false;
	}

	const FVBFinisherSelection& Selection = Fixture.Session->GetSelection();
	if (!Fixture.SummonWeaponWithSceneFinisher(*this))
	{
		return true;
	}

	const FVector At = Fixture.Owner->GetActorLocation();
	// 선택 주인이 접근 정책의 실제 소비자다 - 공격자 자리에 겹친 대상은 앞도 뒤도 없다.
	AActor* Coincident = Fixture.SpawnTarget(At, FRotator::ZeroRotator);
	TestNull(TEXT("a coincident target gets no finisher"), Selection.SelectFinisher(Fixture.Owner, Coincident));
	// 양성 대조: 같은 데이터로 적어도 한쪽에서는 처형이 나온다.
	AActor* Ahead = Fixture.SpawnTarget(At + FVector(200.0, 0.0, 0.0), FRotator(0.0, 180.0, 0.0));
	AActor* Behind = Fixture.SpawnTarget(At + FVector(200.0, 0.0, 0.0), FRotator::ZeroRotator);
	TestTrue(TEXT("front or back placement gets a finisher"),
		Selection.SelectFinisher(Fixture.Owner, Ahead) != nullptr || Selection.SelectFinisher(Fixture.Owner, Behind) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherSelectionHasIsNotCanTest, "VB.Unit.Finisher.SelectionHasIsNotCan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBFinisherSelectionHasIsNotCanTest::RunTest(const FString& Parameters)
{
	VBFinisherSelectionTest::FFixture Fixture;
	if (!Fixture.Make(*this))
	{
		return false;
	}

	const FVBFinisherSelection& Selection = Fixture.Session->GetSelection();
	// 길을 정하는 질문은 데이터 유무뿐 - 거절된 시작이 그 답을 바꾸면 거절이 옛 길로 간다.
	const bool bHadData = Selection.HasInteractionFinisherData(Fixture.Owner);
	TestFalse(TEXT("the attacker cannot finish itself"), Selection.CanStartInteractionFinisher(Fixture.Query(Fixture.Owner)));
	TestFalse(TEXT("no target, no start"), Selection.CanStartInteractionFinisher(Fixture.Query(nullptr)));
	TestEqual(TEXT("a refused start leaves the data answer as it was"), Selection.HasInteractionFinisherData(Fixture.Owner), bHadData);
	TestFalse(TEXT("a refused start opens no session"), Fixture.Session->IsPlaying());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherSelectionAtomicTest, "VB.Unit.Finisher.TrySelectRejectsAtomically",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBFinisherSelectionAtomicTest::RunTest(const FString& Parameters)
{
	VBFinisherSelectionTest::FFixture Fixture;
	if (!Fixture.Make(*this))
	{
		return false;
	}

	const FVBFinisherSelection& Selection = Fixture.Session->GetSelection();
	// 앞선 호출의 출력이 거절 뒤에 남으면 안 된다 - 컴포넌트가 낡은 짝을 튼다.
	TArray<FPoseSearchBlueprintResult> Results;
	Results.AddDefaulted();
	FVBInteractionReplay Choice;
	Choice.SessionId = 7;
	Choice.Actors.Add(Fixture.Owner);
	TestFalse(TEXT("pairing the attacker with itself is refused"),
		Selection.TrySelectInteraction(Fixture.Query(Fixture.Owner), Results, Choice));
	TestTrue(TEXT("a refusal leaves no results"), Results.IsEmpty());
	TestTrue(TEXT("a refusal leaves an empty choice"), Choice.SessionId == 0 && Choice.Actors.IsEmpty() && !Choice.bActive);
	TestFalse(TEXT("a refusal opens no session"), Fixture.Session->IsPlaying());
	return true;
}
#endif

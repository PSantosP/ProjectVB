#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interaction/VBInteractableSubsystem.h"
#include "Interaction/VBPushableComponent.h"
#include "Interaction/VBPushableConfig.h"
#include "UObject/UnrealType.h"
#include "Narrative/Progress/VBChapterDefinition.h"
#include "Narrative/Progress/VBChapterProgress.h"
#include "Narrative/Progress/VBRegionDefinition.h"
#include "Narrative/Story/VBStoryState.h"

// 월드 상태 저장, 챕터, 지역 틀의 단위 시험.

#if WITH_AUTOMATION_TESTS
namespace
{
	// 이름은 파일마다 다르게 - 시험 모듈은 unity 하나로 묶여 익명 네임스페이스끼리도 부딪힌다.
	FGameplayTag ProgressTag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name), false); }

	UVBChapterDefinition* MakeChapter(const TCHAR* Id, const TCHAR* Fact, int32 SortOrder)
	{
		UVBChapterDefinition* Chapter = NewObject<UVBChapterDefinition>();
		Chapter->ChapterId = ProgressTag(Id);
		Chapter->ReachedFact = ProgressTag(Fact);
		Chapter->SortOrder = SortOrder;
		Chapter->Title = FText::FromString(Id);
		return Chapter;
	}

	struct FPushWorld
	{
		UWorld* World = nullptr;
		UVBInteractableSubsystem* Interactables = nullptr;

		FPushWorld()
		{
			const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			Interactables = World ? World->GetSubsystem<UVBInteractableSubsystem>() : nullptr;
		}

		~FPushWorld() { if (World) { World->DestroyWorld(false); } }

		// 놓인 자리 At 에 밀기 물체 하나. SaveId 로 이름표를 고정한다(액터 이름은 스폰마다 다르다). BeginPlay 까지 돌려 등록시킨다.
		AActor* SpawnBox(FName SaveId, const FVector& At) const
		{
			AActor* Box = World->SpawnActor<AActor>(At, FRotator::ZeroRotator);
			USceneComponent* Root = NewObject<USceneComponent>(Box);
			Root->SetMobility(EComponentMobility::Movable);
			Box->SetRootComponent(Root);
			Root->RegisterComponent();
			Box->SetActorLocation(At);
			UVBPushableComponent* Push = NewObject<UVBPushableComponent>(Box);
			// 저작 칸(Config, SaveId)은 보호돼 있다 - 시험만을 위한 공개 API 를 만들지 않고 저작 도구처럼 리플렉션으로 채운다.
			//   Config 가 없으면 밀기 물체는 등록하지 않는다(UVBInteractableComponent::BeginPlay) - 게임에서도 같다.
			if (const FObjectProperty* ConfigProp = FindFProperty<FObjectProperty>(UVBInteractableComponent::StaticClass(), TEXT("Config")))
			{
				ConfigProp->SetObjectPropertyValue_InContainer(Push, NewObject<UVBPushableConfig>(Push));
			}

			if (const FNameProperty* IdProp = FindFProperty<FNameProperty>(UVBInteractableComponent::StaticClass(), TEXT("SaveId")))
			{
				IdProp->SetPropertyValue_InContainer(Push, SaveId);
			}

			Push->SetupAttachment(Root);
			Box->AddInstanceComponent(Push);
			Push->RegisterComponent();
			// 이 시험 월드는 InitializeActorsForPlay 를 안 거쳐 스폰이 액터 초기화를 건너뛴다(AActor::PostActorConstruction 이 월드의
			//   AreActorsInitialized 를 본다). 그러면 bActorInitialized 가 거짓이라 RouteEndPlay 가 아무것도 안 한다 - 게임과 같은 순서를 손으로 밟는다.
			Box->PreInitializeComponents();
			Box->InitializeComponents();
			Box->PostInitializeComponents();
			Box->DispatchBeginPlay();
			return Box;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBChapterResolveTest, "VB.Unit.WorldProgress.ChapterResolve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBChapterResolveTest::RunTest(const FString& Parameters)
{
	TArray<TObjectPtr<UVBChapterDefinition>> Chapters;
	Chapters.Add(MakeChapter(TEXT("Progress.Chapter.Test.One"), TEXT("Story.Test.ChapterOne"), 1));
	Chapters.Add(MakeChapter(TEXT("Progress.Chapter.Test.Two"), TEXT("Story.Test.ChapterTwo"), 2));
	if (!TestTrue(TEXT("시험 태그가 등록돼 있다(DefaultGameplayTags.ini)"), Chapters[1]->ReachedFact.IsValid()))
	{
		return false;
	}

	FVBStoryState Story;
	TestNull(TEXT("켜진 사실 없음 = 챕터 없음"), VBChapterProgress::ResolveCurrent(Chapters, Story));
	Story.SetFact(ProgressTag(TEXT("Story.Test.ChapterOne")), true);
	TestTrue(TEXT("하나 켜짐 = 그 챕터"), VBChapterProgress::ResolveCurrent(Chapters, Story) == Chapters[0].Get());
	Story.SetFact(ProgressTag(TEXT("Story.Test.ChapterTwo")), true);
	TestTrue(TEXT("둘 켜짐 = SortOrder 큰 쪽"), VBChapterProgress::ResolveCurrent(Chapters, Story) == Chapters[1].Get());
	Chapters[0]->SortOrder = 2;
	TestTrue(TEXT("SortOrder 같으면 ID 이름이 큰 쪽(Two > One) - 순서와 무관하게 같다"),
		VBChapterProgress::ResolveCurrent(Chapters, Story) == Chapters[1].Get());

	UVBRegionDefinition* Region = NewObject<UVBRegionDefinition>();
	TestTrue(TEXT("열림 사실이 비면 열린 지역"), VBChapterProgress::IsRegionUnlocked(*Region, Story));
	Region->UnlockFact = ProgressTag(TEXT("Story.Test.Never"));
	TestFalse(TEXT("열림 사실이 꺼져 있으면 닫힌 지역"), VBChapterProgress::IsRegionUnlocked(*Region, Story));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBChapterBaselineTest, "VB.Unit.WorldProgress.ChapterBaseline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBChapterBaselineTest::RunTest(const FString& Parameters)
{
	UVBChapterDefinition* One = MakeChapter(TEXT("Progress.Chapter.Test.One"), TEXT("Story.Test.ChapterOne"), 1);
	UVBChapterDefinition* Two = MakeChapter(TEXT("Progress.Chapter.Test.Two"), TEXT("Story.Test.ChapterTwo"), 2);
	VBChapterProgress::FBaseline Baseline;
	TestNull(TEXT("처음 본 챕터는 기준만 - 이어하기로 들어온 챕터에 카드 없음"), VBChapterProgress::StepBaseline(Baseline, One, 0));
	TestNull(TEXT("같은 챕터 다시 = 카드 없음"), VBChapterProgress::StepBaseline(Baseline, One, 0));
	TestTrue(TEXT("새 챕터에 들어옴 = 카드"), VBChapterProgress::StepBaseline(Baseline, Two, 0) == Two);
	TestNull(TEXT("세이브를 읽어 바뀐 챕터(Serial 바뀜) = 기준만"), VBChapterProgress::StepBaseline(Baseline, One, 1));
	TestTrue(TEXT("그 뒤의 진짜 진입은 다시 카드"), VBChapterProgress::StepBaseline(Baseline, Two, 1) == Two);
	TestNull(TEXT("챕터가 없어지면 카드 없음"), VBChapterProgress::StepBaseline(Baseline, nullptr, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPushableTransformSaveTest, "VB.Unit.WorldProgress.PushableTransformSave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPushableTransformSaveTest::RunTest(const FString& Parameters)
{
	FPushWorld Fixture;
	if (!TestNotNull(TEXT("월드와 상호작용 서브시스템"), Fixture.Interactables))
	{
		return false;
	}

	UVBInteractableSubsystem& Interactables = *Fixture.Interactables;
	const FName KeyA(TEXT("TestBoxA"));
	const FName KeyC(TEXT("TestBoxC"));

	// 민 자리는 저장되고, 원자리로 돌아오면 맵에서 빠진다.
	AActor* BoxA = Fixture.SpawnBox(KeyA, FVector::ZeroVector);
	TMap<FName, FTransform> Saved;
	Interactables.CollectSavedTransforms(Saved);
	TestFalse(TEXT("안 민 물체는 저장 맵에 없다"), Saved.Contains(KeyA));
	BoxA->SetActorLocation(FVector(100.0, 0.0, 0.0));
	Interactables.CollectSavedTransforms(Saved);
	TestTrue(TEXT("민 물체는 저장 맵에 있다"), Saved.Contains(KeyA));
	TestEqual(TEXT("저장된 자리 = 민 자리"), Saved.FindRef(KeyA).GetLocation(), FVector(100.0, 0.0, 0.0));
	BoxA->SetActorLocation(FVector::ZeroVector);
	Interactables.CollectSavedTransforms(Saved);
	TestFalse(TEXT("원자리로 돌아온 물체의 옛 자리는 빠진다(F3)"), Saved.Contains(KeyA));
	// 레벨에서 빠지는 길(스트리밍 아웃)을 직접 태운다 - 엔진에 알리지 않은 시험 월드에서는 Destroy 가 World has no context 로 아무것도 안 한다.
	BoxA->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	const UVBInteractableComponent* PushA = BoxA->FindComponentByClass<UVBPushableComponent>();
	TestFalse(TEXT("레벨에서 빠진 물체는 등록부에서 빠진다"), Interactables.GetInteractables().ContainsByPredicate(
		[PushA](const TWeakObjectPtr<UVBInteractableComponent>& Entry) { return Entry.Get() == PushA; }));

	// 스트리밍 아웃 직전의 자리를 남긴다(F3). 모으기(Collect) 전에 서브시스템 맵을 바로 본다 - 모으기는 떠 있는 대상에서 다시 읽어 이 길을 가린다.
	AActor* BoxC = Fixture.SpawnBox(KeyC, FVector::ZeroVector);
	BoxC->SetActorLocation(FVector(50.0, 0.0, 0.0));
	BoxC->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	TestTrue(TEXT("내려간 물체의 마지막 자리가 서브시스템 맵에 남는다"), Interactables.GetKnownTransforms().Contains(KeyC));
	Interactables.CollectSavedTransforms(Saved);
	TestTrue(TEXT("그리고 저장 맵에도 간다"), Saved.Contains(KeyC));

	// 불러오기: 등록 전에 읽은 자리가 등록 때 적용되고, 기준점은 놓인 자리 그대로다(F2).
	TMap<FName, FTransform> Loaded;
	Loaded.Add(KeyA, FTransform(FVector(100.0, 0.0, 0.0)));
	Interactables.ApplySavedTransforms(Loaded);
	AActor* Restored = Fixture.SpawnBox(KeyA, FVector::ZeroVector);
	TestEqual(TEXT("등록 때 저장된 자리로 간다"), Restored->GetActorLocation(), FVector(100.0, 0.0, 0.0));
	FTransform Moved;
	const UVBPushableComponent* Push = Restored->FindComponentByClass<UVBPushableComponent>();
	TestTrue(TEXT("기준점은 놓인 자리(0) - 복원된 자리(100)가 기준이 되지 않는다"), Push && Push->CaptureMovedTransform(Moved));
	TestFalse(TEXT("불러오기는 맵을 교체한다 - 옛 BoxC 기록 없음"), Interactables.GetKnownTransforms().Contains(KeyC));
	return true;
}
#endif

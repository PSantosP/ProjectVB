#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interaction/VBInteractHighlightComponent.h"

// 상호작용 대상 외곽선 찍기, 되돌리기. 포커스 신호 없이 SetHighlightedActor 를 직접 부른다.

#if WITH_AUTOMATION_TESTS
namespace
{
	// 이름은 파일마다 다르게 - 시험 모듈은 unity 하나로 묶여 익명 네임스페이스끼리도 부딪힌다.
	struct FHighlightWorld
	{
		UWorld* World = nullptr;

		FHighlightWorld()
		{
			const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		}

		~FHighlightWorld() { if (World) { World->DestroyWorld(false); } }

		AActor* SpawnBare() const
		{
			AActor* Actor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			return Actor;
		}

		UStaticMeshComponent* AddPart(AActor* Actor) const
		{
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Actor);
			Part->SetupAttachment(Actor->GetRootComponent());
			Part->RegisterComponent();
			return Part;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractHighlightStampTest, "VB.Unit.Interaction.HighlightStamp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractHighlightStampTest::RunTest(const FString& Parameters)
{
	FHighlightWorld Fixture;
	if (!TestNotNull(TEXT("시험 월드"), Fixture.World))
	{
		return false;
	}

	AActor* Owner = Fixture.SpawnBare();
	UVBInteractHighlightComponent* Highlight = NewObject<UVBInteractHighlightComponent>(Owner);
	Highlight->RegisterComponent();

	// 대상 A: 보통 조각 하나, 다른 표시가 이미 번호 1 로 켜 둔 조각 하나, 숨은 조각 하나.
	AActor* A = Fixture.SpawnBare();
	UStaticMeshComponent* Plain = Fixture.AddPart(A);
	UStaticMeshComponent* Shared = Fixture.AddPart(A);
	Shared->SetCustomDepthStencilValue(1);
	Shared->SetRenderCustomDepth(true);
	UStaticMeshComponent* Hidden = Fixture.AddPart(A);
	Hidden->SetVisibility(false);
	AActor* B = Fixture.SpawnBare();
	UStaticMeshComponent* Other = Fixture.AddPart(B);

	Highlight->SetHighlightedActor(A);
	TestTrue(TEXT("A 보통 조각: 커스텀 뎁스 켜짐"), static_cast<bool>(Plain->bRenderCustomDepth));
	TestEqual(TEXT("A 보통 조각: 번호 2"), Plain->CustomDepthStencilValue, 2);
	TestEqual(TEXT("A 공유 조각: 번호 2 로 덮임"), Shared->CustomDepthStencilValue, 2);
	TestFalse(TEXT("A 숨은 조각: 안 찍힘"), static_cast<bool>(Hidden->bRenderCustomDepth));

	// 포커스가 B 로 넘어가면 A 는 찍기 전 값으로 돌아간다 - 남이 켜 둔 번호 1 은 살아 있어야 한다(끄기가 아니라 되돌리기).
	Highlight->SetHighlightedActor(B);
	TestFalse(TEXT("A 보통 조각: 꺼짐으로 복원"), static_cast<bool>(Plain->bRenderCustomDepth));
	TestEqual(TEXT("A 보통 조각: 번호 0 으로 복원"), Plain->CustomDepthStencilValue, 0);
	TestTrue(TEXT("A 공유 조각: 켜짐 유지"), static_cast<bool>(Shared->bRenderCustomDepth));
	TestEqual(TEXT("A 공유 조각: 번호 1 로 복원"), Shared->CustomDepthStencilValue, 1);
	TestTrue(TEXT("B: 찍힘"), static_cast<bool>(Other->bRenderCustomDepth));
	TestEqual(TEXT("B: 번호 2"), Other->CustomDepthStencilValue, 2);
	TestTrue(TEXT("지금 대상 = B"), Highlight->GetHighlightedActor() == B);

	// 포커스가 사라지면 B 도 되돌린다.
	Highlight->SetHighlightedActor(nullptr);
	TestFalse(TEXT("B: 꺼짐으로 복원"), static_cast<bool>(Other->bRenderCustomDepth));
	TestNull(TEXT("지금 대상 없음"), Highlight->GetHighlightedActor());
	return true;
}
#endif

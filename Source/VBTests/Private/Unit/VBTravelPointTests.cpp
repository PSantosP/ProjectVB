// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBSavePoint.h"
#include "Map/VBTravelPointSubsystem.h"
#include "SaveSystem/VBSaveGame.h"
#include "Unit/VBIsolatedGameWorld.h"

// 빠른 이동: 지점 등록부와 발견한 지점 저장. 지점 ID = Map.TravelPoint.Test.* (시험 전용 태그).

class FVBTravelPointTestAccess
{
public:
	static void SetPointId(AVBSavePoint& Point, const FGameplayTag& Id) { Point.PointId = Id; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTravelPointRegistryTest, "VB.Unit.FastTravel.Registry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTravelPointRegistryTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(Isolated.World);
	if (!TestNotNull(TEXT("travel subsystem in a game world"), Travel))
	{
		return false;
	}

	const FGameplayTag Ward = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.A"));
	const FGameplayTag Gate = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.B"));
	const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint"));

	// ID 있는 저장점은 BeginPlay 에 올라가고 EndPlay 에 내려간다.
	AVBSavePoint* A = Isolated.World->SpawnActor<AVBSavePoint>();
	AVBSavePoint* NoId = Isolated.World->SpawnActor<AVBSavePoint>();
	AVBSavePoint* Twin = Isolated.World->SpawnActor<AVBSavePoint>();
	if (!TestNotNull(TEXT("save points"), A) || !NoId || !Twin)
	{
		return false;
	}

	FVBTravelPointTestAccess::SetPointId(*A, Ward);
	FVBTravelPointTestAccess::SetPointId(*Twin, Ward);
	AddExpectedMessagePlain(TEXT("지점 ID(PointId) 가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("같은 지점 ID"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	A->DispatchBeginPlay();
	NoId->DispatchBeginPlay();
	Twin->DispatchBeginPlay();
	TestEqual(TEXT("only the save point with a unique id registers"), Travel->GetPoints().Num(), 1);
	TestTrue(TEXT("found by id"), Travel->FindPoint(Ward) == A);
	TestNull(TEXT("an empty id finds nothing"), Travel->FindPoint(FGameplayTag()));
	const FBoolProperty* SpatiallyLoaded = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bIsSpatiallyLoaded"));
	TestTrue(TEXT("save points stay loaded (not spatially loaded)"), SpatiallyLoaded && !SpatiallyLoaded->GetPropertyValue_InContainer(A));
	A->Destroy();
	TestEqual(TEXT("a destroyed point leaves the registry"), Travel->GetPoints().Num(), 0);

	// 발견: 처음만 참, 빈 ID 는 거절.
	int32 Notices = 0;
	Travel->OnDiscoveredChanged.AddLambda([&Notices]() { ++Notices; });
	TestTrue(TEXT("first discovery"), Travel->Discover(Ward));
	TestFalse(TEXT("second discovery is not new"), Travel->Discover(Ward));
	TestFalse(TEXT("empty id is refused"), Travel->Discover(FGameplayTag()));
	TestFalse(TEXT("the bare root tag is refused (not chosen yet)"), Travel->Discover(Root));
	TestTrue(TEXT("discovered"), Travel->IsDiscovered(Ward));
	TestFalse(TEXT("not discovered"), Travel->IsDiscovered(Gate));
	TestEqual(TEXT("one notice per new discovery"), Notices, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTravelPointSaveTest, "VB.Unit.FastTravel.SaveRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTravelPointSaveTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(Isolated.World);
	if (!TestNotNull(TEXT("travel subsystem"), Travel))
	{
		return false;
	}

	const FGameplayTag Ward = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.A"));
	// 이 맵에 배치되지 않은 지점(다른 맵에서 발견) - 등록부에 없어도 저장에서 사라지면 안 된다.
	const FGameplayTag Elsewhere = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.B"));
	const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint"));

	UVBSaveGame* Loaded = NewObject<UVBSaveGame>();
	Loaded->Progress.DiscoveredTravelPoints = {Ward, FGameplayTag(), Elsewhere, Ward, Root};
	AddExpectedMessagePlain(TEXT("쓸 수 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	const FVBSaveContext Context;
	int32 Notices = 0;
	Travel->OnDiscoveredChanged.AddLambda([&Notices]() { ++Notices; });
	Travel->ReadSave(*Loaded, Context);
	TestEqual(TEXT("a load notifies once (remote owners get the new list)"), Notices, 1);
	TestEqual(TEXT("read drops empty, root and duplicate ids"), Travel->GetDiscovered().Num(), 2);
	TestTrue(TEXT("unplaced id is kept"), Travel->IsDiscovered(Elsewhere));

	UVBSaveGame* Written = NewObject<UVBSaveGame>();
	Travel->WriteSave(*Written, Context);
	TestEqual(TEXT("write keeps both ids"), Written->Progress.DiscoveredTravelPoints.Num(), 2);
	TestTrue(TEXT("written ids include the unplaced one"), Written->Progress.DiscoveredTravelPoints.Contains(Elsewhere));
	return true;
}

// 빠른 이동 상호작용은 눌러서 고르는 초점 대상이다. 표지 앞 1.5 m 에 선 캐릭터가 실제 닿기 규칙으로 고를 수 있어야 하고,
//   바닥(원점) 자리는 같은 규칙에서 떨어져야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTravelPointReachTest, "VB.Unit.FastTravel.TravelPointReach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTravelPointReachTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	AVBSavePoint* Point = Isolated.World->SpawnActor<AVBSavePoint>();
	AStaticMeshActor* Standing = Isolated.World->SpawnActor<AStaticMeshActor>(FVector(150.0, 0.0, 90.0), FRotator::ZeroRotator);
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!TestNotNull(TEXT("save point"), Point) || !TestNotNull(TEXT("standing stand-in"), Standing)
		|| !TestNotNull(TEXT("marker mesh"), Cylinder))
	{
		return false;
	}

	// 배치된 저장점처럼 표지에 원기둥을 단다(원점 바로 위 5 cm).
	UStaticMeshComponent* Marker = nullptr;
	UVBInteractableComponent* Travel = nullptr;
	for (UActorComponent* Component : Point->GetComponents())
	{
		if (Component->GetFName() == TEXT("Marker"))
		{
			Marker = Cast<UStaticMeshComponent>(Component);
		}
		else if (Component->GetFName() == TEXT("TravelInteractable"))
		{
			Travel = Cast<UVBInteractableComponent>(Component);
		}
	}

	if (!TestNotNull(TEXT("marker component"), Marker) || !TestNotNull(TEXT("travel interactable"), Travel))
	{
		return false;
	}

	Marker->SetStaticMesh(Cylinder);
	Marker->SetRelativeLocation(FVector(0.0, 0.0, 5.0));
	FVBTravelPointTestAccess::SetPointId(*Point, FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.A")));
	Point->DispatchBeginPlay();

	const FVBReachRules& Rules = Travel->GetConfig()->ReachRules;
	TestTrue(TEXT("a character 1.5 m in front reaches the travel point"),
		UVBAbilitySystemStatics::PassesReachRules(Standing, 180.0f, Travel->GetComponentLocation(), Point, Rules));
	TestFalse(TEXT("the old floor-level spot fails the same rule"),
		UVBAbilitySystemStatics::PassesReachRules(Standing, 180.0f, Point->GetActorLocation(), Point, Rules));
	return true;
}
#endif

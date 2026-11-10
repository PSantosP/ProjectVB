#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "UObject/Package.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Map/VBWorldMarkerComponent.h"
#include "Map/VBWorldMarkerSubsystem.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestMarkers.h"
#include "UI/VBScreenEdge.h"

// 목표 마커의 단위 시험.
// 표지 태그는 판정과 무관하다 - 네이티브 태그를 빌려 쓴다(성장, 장비 시험과 같은 방식).

#if WITH_AUTOMATION_TESTS
namespace
{
	FInstancedStruct MakeObjective(const FGameplayTag& Marker, int32 Required, bool bOptional = false)
	{
		FVBQuestObjective_Event Objective;
		Objective.MarkerTag = Marker;
		Objective.RequiredCount = Required;
		Objective.bOptional = bOptional;
		return FInstancedStruct::Make(Objective);
	}

	UVBWorldMarkerComponent* MakeMarker(const FGameplayTag& Tag)
	{
		UVBWorldMarkerComponent* Marker = NewObject<UVBWorldMarkerComponent>(GetTransientPackage());
		// MarkerTag 는 레벨 저작 값이라 보호돼 있다 - 시험은 리플렉션으로 넣는다.
		if (FStructProperty* Property = FindFProperty<FStructProperty>(UVBWorldMarkerComponent::StaticClass(), TEXT("MarkerTag")))
		{
			*Property->ContainerPtrToValuePtr<FGameplayTag>(Marker) = Tag;
		}

		return Marker;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBObjectiveMarkerTagsTest, "VB.Unit.ObjectiveMarker.CollectTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBObjectiveMarkerTagsTest::RunTest(const FString& Parameters)
{
	const FGameplayTag A = VBGameplayTags::State_Combat_Guarding;
	const FGameplayTag B = VBGameplayTags::State_Combat_Dodging;
	const FGameplayTag C = VBGameplayTags::State_Combat_Attacking;

	UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
	FVBQuestStage& First = Quest->Stages.AddDefaulted_GetRef();
	First.Objectives.Add(MakeObjective(A, 3));
	First.Objectives.Add(MakeObjective(FGameplayTag(), 1)); // 표지 없는 목표
	First.Objectives.Add(MakeObjective(B, 1, /*bOptional=*/true));
	FVBQuestStage& Second = Quest->Stages.AddDefaulted_GetRef();
	Second.Objectives.Add(MakeObjective(C, 1));

	FGameplayTagContainer Tags;
	VBQuestMarkers::CollectActiveMarkerTags(Quest, 0, TArray<int32>{ 2, 0, 0 }, Tags);
	TestTrue(TEXT("unfinished objective points at its marker"), Tags.HasTagExact(A));
	TestTrue(TEXT("optional unfinished objective also shows"), Tags.HasTagExact(B));
	TestFalse(TEXT("next stage is not shown yet"), Tags.HasTagExact(C));
	TestEqual(TEXT("objective without a marker adds nothing"), Tags.Num(), 2);

	Tags.Reset();
	VBQuestMarkers::CollectActiveMarkerTags(Quest, 0, TArray<int32>{ 3, 0, 1 }, Tags);
	TestTrue(TEXT("finished objectives drop their markers"), Tags.IsEmpty());

	Tags.Reset();
	VBQuestMarkers::CollectActiveMarkerTags(Quest, 0, TArray<int32>{}, Tags);
	TestTrue(TEXT("short progress array counts missing entries as zero"), Tags.HasTagExact(A) && Tags.HasTagExact(B));

	Tags.Reset();
	VBQuestMarkers::CollectActiveMarkerTags(Quest, 2, TArray<int32>{}, Tags);
	TestTrue(TEXT("stage out of range (finished quest) adds nothing"), Tags.IsEmpty());
	VBQuestMarkers::CollectActiveMarkerTags(nullptr, 0, TArray<int32>{}, Tags);
	TestTrue(TEXT("no definition adds nothing"), Tags.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWorldMarkerRegistryTest, "VB.Unit.ObjectiveMarker.Registry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWorldMarkerRegistryTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Child = VBGameplayTags::State_Combat_Guarding;
	const FGameplayTag Parent = FGameplayTag::RequestGameplayTag(TEXT("State.Combat"));
	UVBWorldMarkerSubsystem* Registry = NewObject<UVBWorldMarkerSubsystem>(GetTransientPackage());
	UVBWorldMarkerComponent* First = MakeMarker(Child);
	UVBWorldMarkerComponent* Second = MakeMarker(Child);
	UVBWorldMarkerComponent* Other = MakeMarker(VBGameplayTags::State_Combat_Dodging);
	if (!TestTrue(TEXT("tag was written through reflection"), First->GetMarkerTag() == Child))
	{
		return false;
	}

	Registry->Register(First);
	Registry->Register(Second);
	Registry->Register(Other);
	Registry->Register(First);
	TestEqual(TEXT("registering twice keeps one entry"), Registry->GetMarkerCount(), 3);

	TArray<UVBWorldMarkerComponent*> Found;
	Registry->FindMarkers(FGameplayTagContainer(Child), Found);
	TestEqual(TEXT("every marker with the tag is found"), Found.Num(), 2);
	Registry->FindMarkers(FGameplayTagContainer(Parent), Found);
	TestEqual(TEXT("a parent tag does not match its children (exact match)"), Found.Num(), 0);
	Registry->FindMarkers(FGameplayTagContainer(), Found);
	TestEqual(TEXT("empty query finds nothing"), Found.Num(), 0);

	Registry->Unregister(First);
	Registry->FindMarkers(FGameplayTagContainer(Child), Found);
	TestTrue(TEXT("unregistered marker is gone"), Found.Num() == 1 && Found[0] == Second);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBScreenEdgeTest, "VB.Unit.ObjectiveMarker.ScreenEdge",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBScreenEdgeTest::RunTest(const FString& Parameters)
{
	const FVector Camera = FVector::ZeroVector;
	const FRotator Facing = FRotator::ZeroRotator; // +X 를 본다
	const FVector2D Ahead = VBScreenEdge::CompassDirection(Camera, Facing, FVector(500, 0, 300));
	const FVector2D Behind = VBScreenEdge::CompassDirection(Camera, Facing, FVector(-500, 0, 0));
	const FVector2D Right = VBScreenEdge::CompassDirection(Camera, Facing, FVector(0, 500, 0));
	TestTrue(TEXT("ahead maps to screen up (height ignored)"), Ahead.Equals(FVector2D(0, -1), 1e-4));
	TestTrue(TEXT("behind maps to screen down"), Behind.Equals(FVector2D(0, 1), 1e-4));
	TestTrue(TEXT("right maps to screen right"), Right.Equals(FVector2D(1, 0), 1e-4));
	TestTrue(TEXT("straight above is the degenerate down"),
		VBScreenEdge::CompassDirection(Camera, Facing, FVector(0, 0, 500)).Equals(FVector2D(0, 1), 1e-4));

	const FVector2D View(1920, 1080);
	TestTrue(TEXT("right lands on the right inset edge"),
		VBScreenEdge::PointOnInsetRect(View, 72, Right).Equals(FVector2D(1920 - 72, 540), 1e-3));
	TestTrue(TEXT("up lands on the top inset edge"), VBScreenEdge::PointOnInsetRect(View, 72, Ahead).Equals(FVector2D(960, 72), 1e-3));
	const FVector2D Diagonal = VBScreenEdge::PointOnInsetRect(View, 72, FVector2D(1, 1).GetSafeNormal());
	TestTrue(TEXT("diagonal hits the nearer (bottom) edge first"), FMath::IsNearlyEqual(Diagonal.Y, 1080 - 72, 1e-3)
		&& Diagonal.X < 1920 - 72);
	TestTrue(TEXT("zero direction stays at the center"),
		VBScreenEdge::PointOnInsetRect(View, 72, FVector2D::ZeroVector).Equals(FVector2D(960, 540), 1e-3));

	TestTrue(TEXT("arrow drawn pointing up stays at 0 for up"),
		FMath::IsNearlyEqual(VBScreenEdge::ArrowAngleDegrees(FVector2D(0, -1)), 0.0f, 1e-3f));
	TestTrue(TEXT("arrow turns 90 for right"), FMath::IsNearlyEqual(VBScreenEdge::ArrowAngleDegrees(FVector2D(1, 0)), 90.0f, 1e-3f));
	return true;
}
#endif

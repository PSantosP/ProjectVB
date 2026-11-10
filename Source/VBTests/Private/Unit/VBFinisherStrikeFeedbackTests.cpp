#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Abilities/VBFinisherStrikeFeedback.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBANS_AttackTraceWindow.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Data/VBSwingCombatProfile.h"

// 판정에 안 닿은 타격의 가까운 점. 음수는 못 잰 점이라 건너뛰고, 못 잰 날은 찾지 못한 것으로 답한다.
//   대조: 음수 거름을 빼면 skips unmeasured 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherStrikeNearMissTest, "VB.Unit.Combat.FinisherStrikeNearMiss",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFinisherStrikeNearMissTest::RunTest(const FString& Parameters)
{
	const FVector Base(0.0, 0.0, 0.0);
	const FVector Tip(100.0, 0.0, 0.0);
	float Best = -1.0f;
	FVector Point = FVector::ZeroVector;
	TestTrue(TEXT("closest sample found"), FVBFinisherStrikeFeedback::FindClosestBladePoint(Base, Tip,
		[](const FVector& At) { return static_cast<float>(FMath::Abs(At.X - 70.0)); }, Best, Point));
	TestEqual(TEXT("distance of the closest sample"), Best, 5.0f);
	TestEqual(TEXT("closest sample sits on the blade"), Point.X, 75.0);

	Best = -1.0f;
	TestTrue(TEXT("skips unmeasured samples"), FVBFinisherStrikeFeedback::FindClosestBladePoint(Base, Tip,
		[](const FVector& At) { return At.X < 90.0 ? -1.0f : 3.0f; }, Best, Point));
	TestEqual(TEXT("only the measured sample counts"), Point.X, 100.0);

	Best = -1.0f;
	TestFalse(TEXT("nothing measured: nothing found"), FVBFinisherStrikeFeedback::FindClosestBladePoint(Base, Tip,
		[](const FVector&) { return -1.0f; }, Best, Point));
	TestEqual(TEXT("distance stays unset"), Best, -1.0f);

	FVBFinisherStrikeFeedback Feedback;
	TestTrue(TEXT("first listen subscribes"), Feedback.TryBeginListening());
	TestFalse(TEXT("second listen does not subscribe twice"), Feedback.TryBeginListening());
	Feedback.End(FVBFinisherStrikeContext());
	TestFalse(TEXT("end stops listening"), Feedback.IsListening());
	return true;
}

// 처형 타격의 화면 연출은 판정 창 노티가 든 스윙 설정(ScreenFeedback)에서만 나간다(FVBFinisherStrikeFeedback::FireStrike).
// 처형 몽타주에 설정을 안 꽂으면 화면 연출이 조용히 빠진다 - 모든 처형 몽타주가 설정을 갖는지 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherStrikeScreenFeedbackTest, "VB.Unit.Combat.FinisherStrikeScreenFeedback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFinisherStrikeScreenFeedbackTest::RunTest(const FString& Parameters)
{
	const FString Root = TEXT("/Game/VB/Animations/MotionMatching/Finisher/Weapons");
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.ScanPathsSynchronous({ Root }, /*bForceRescan=*/false);
	FARFilter Filter;
	Filter.PackagePaths.Add(*Root);
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UAnimMontage::StaticClass()->GetClassPathName());
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	// 판정 창이 있는 몽타주(공격자 쪽)만 센다. 하나도 없으면 경로가 바뀐 것이니 통과가 아니라 실패다.
	int32 Windows = 0;
	for (const FAssetData& Asset : Assets)
	{
		const UAnimMontage* Montage = Cast<UAnimMontage>(Asset.GetAsset());
		if (!TestNotNull(FString::Printf(TEXT("%s 로드"), *Asset.PackageName.ToString()), Montage))
		{
			continue;
		}

		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			const UVBANS_AttackTraceWindow* Window = Cast<UVBANS_AttackTraceWindow>(Event.NotifyStateClass);
			if (!Window)
			{
				continue;
			}

			++Windows;
			const UVBSwingCombatProfile* Profile = Window->SwingCombatProfile;
			TestTrue(FString::Printf(TEXT("%s @%.2f초 판정 창에 화면 연출이 든 스윙 설정"), *Asset.PackageName.ToString(), Event.GetTriggerTime()),
				Profile && Profile->ScreenFeedback);
		}
	}

	TestTrue(TEXT("처형 몽타주에서 판정 창을 하나 이상 찾았다"), Windows > 0);
	AddInfo(FString::Printf(TEXT("처형 몽타주 %d 개, 판정 창 %d 개"), Assets.Num(), Windows));
	return true;
}
#endif

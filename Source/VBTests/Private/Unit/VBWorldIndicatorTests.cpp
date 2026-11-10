#include "Misc/AutomationTest.h"

#include "Camera/VBViewQuery.h"
#include "UI/VBScreenEdge.h"
#include "UI/VBWorldIndicatorProjection.h"

// 월드 표시기 투영 한 곳의 단위 시험. 정책 계약은 칸별 정책 표다.
// 칸마다 프롬프트, 목표, 위협 정책이 그 표대로 자리를 내는지 본다. 못 보는 것: 엔진 투영(Place 의 PC 경로) - PIE 몫.

#if WITH_AUTOMATION_TESTS
namespace
{
	FVBIndicatorView MakeTestView(float Scale)
	{
		FVBIndicatorView View;
		View.ViewportScale = Scale;
		View.ViewportPixels = FIntPoint(1920, 1080);
		View.LocalSize = FVector2D(1920.0, 1080.0) / (Scale > KINDA_SMALL_NUMBER ? Scale : 1.0f);
		return View;
	}

	struct FCell
	{
		const TCHAR* Name;
		bool bProjected;
		FVector2D Pixels;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWorldIndicatorPolicyTest, "VB.Unit.WorldIndicator.PolicyTable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWorldIndicatorPolicyTest::RunTest(const FString& Parameters)
{
	const FCell Cells[] = {
		{ TEXT("behind"), false, FVector2D::ZeroVector },
		{ TEXT("ahead inside"), true, FVector2D(400.0, 300.0) },
		{ TEXT("ahead outside"), true, FVector2D(2500.0, 300.0) },
		{ TEXT("ahead on the far corner"), true, FVector2D(1920.0, 1080.0) },
	};
	// 방향 둘 - 가장자리 점이 픽셀이 아니라 나침반 방향에서 나오는지 가른다.
	const FVector2D Dirs[] = { FVector2D(1.0, 0.0), FVector2D(-1.0, -1.0).GetSafeNormal() };
	const float Margin = 80.0f;

	for (const FVector2D& Dir : Dirs)
	{
		for (const float Scale : { 1.5f, 0.0f })
		{
			const FVBIndicatorView View = MakeTestView(Scale);
			const FVector2D EdgePoint = VBScreenEdge::PointOnInsetRect(View.LocalSize, Margin, Dir);
			const float EdgeAngle = VBScreenEdge::ArrowAngleDegrees(Dir);
			const bool bScaleValid = Scale > KINDA_SMALL_NUMBER;

			for (const FCell& Cell : Cells)
			{
				const FString Where = FString::Printf(TEXT("%s, scale %.1f, dir (%.2f, %.2f)"), Cell.Name, Scale, Dir.X, Dir.Y);
				// 화면 안: 투영 성공 && 0 <= X <= SizeX && 0 <= Y <= SizeY(경계 포함).
				const bool bInside = Cell.bProjected && Cell.Pixels.X >= 0.0 && Cell.Pixels.Y >= 0.0
					&& Cell.Pixels.X <= 1920.0 && Cell.Pixels.Y <= 1080.0;

				// 프롬프트: 투영 실패면 숨김, 성공이면 사각형과 무관하게 Pixels / 배율. 가장자리, 화살표는 없다.
				const FVBIndicatorPlacement Prompt = VBWorldIndicatorProjection::Classify(
					View, Cell.bProjected, Cell.Pixels, Dir, EVBIndicatorScreenPolicy::FollowWhenProjected, Margin);
				TestEqual(*(TEXT("prompt visible: ") + Where), Prompt.bVisible, Cell.bProjected);
				TestFalse(*(TEXT("prompt never edges: ") + Where), Prompt.bEdge);
				TestEqual(*(TEXT("prompt has no arrow: ") + Where), Prompt.ArrowAngle, 0.0f);
				if (Cell.bProjected && bScaleValid)
				{
					TestTrue(*(TEXT("prompt follows the pixel: ") + Where), Prompt.Position.Equals(Cell.Pixels / Scale, 1e-3));
				}

				// 목표: 화면 안 && 배율 유효면 Pixels / 배율, 나머지는 가장자리 + 화살표.
				const FVBIndicatorPlacement Objective = VBWorldIndicatorProjection::Classify(
					View, Cell.bProjected, Cell.Pixels, Dir, EVBIndicatorScreenPolicy::ClampToEdge, Margin);
				TestTrue(*(TEXT("objective always visible: ") + Where), Objective.bVisible);
				const bool bObjectiveOnPoint = bInside && bScaleValid;
				TestEqual(*(TEXT("objective edge choice: ") + Where), Objective.bEdge, !bObjectiveOnPoint);
				if (bObjectiveOnPoint)
				{
					TestTrue(*(TEXT("objective on its pixel: ") + Where), Objective.Position.Equals(Cell.Pixels / Scale, 1e-3));
				}
				else
				{
					TestTrue(*(TEXT("objective on the edge: ") + Where), Objective.Position.Equals(EdgePoint, 1e-3));
					TestTrue(*(TEXT("objective arrow angle: ") + Where), FMath::IsNearlyEqual(Objective.ArrowAngle, EdgeAngle, 1e-3f));
				}

				// 락온 대상 바: 화면 안 && 배율 유효일 때만 그 자리, 나머지는 숨김. 가장자리, 화살표는 없다.
				const FVBIndicatorPlacement Inside = VBWorldIndicatorProjection::Classify(
					View, Cell.bProjected, Cell.Pixels, Dir, EVBIndicatorScreenPolicy::InsideOnly, Margin);
				TestEqual(*(TEXT("inside-only visible: ") + Where), Inside.bVisible, bInside && bScaleValid);
				TestFalse(*(TEXT("inside-only never edges: ") + Where), Inside.bEdge);
				if (bInside && bScaleValid)
				{
					TestTrue(*(TEXT("inside-only on its pixel: ") + Where), Inside.Position.Equals(Cell.Pixels / Scale, 1e-3));
				}
				else
				{
					TestTrue(*(TEXT("hidden inside-only places nothing: ") + Where), Inside.Position.IsZero());
				}

				// 위협: 화면 안이면 숨김(배율 무관, 자리도 안 낸다), 밖이면 가장자리.
				const FVBIndicatorPlacement Threat = VBWorldIndicatorProjection::Classify(
					View, Cell.bProjected, Cell.Pixels, Dir, EVBIndicatorScreenPolicy::EdgeOnly, Margin);
				TestEqual(*(TEXT("threat visible only off screen: ") + Where), Threat.bVisible, !bInside);
				if (bInside)
				{
					TestTrue(*(TEXT("hidden threat places nothing: ") + Where), Threat.Position.IsZero() && !Threat.bEdge);
				}
				else
				{
					TestTrue(*(TEXT("threat edges: ") + Where), Threat.bEdge);
					TestTrue(*(TEXT("threat on the edge: ") + Where), Threat.Position.Equals(EdgePoint, 1e-3));
					TestTrue(*(TEXT("threat arrow angle: ") + Where), FMath::IsNearlyEqual(Threat.ArrowAngle, EdgeAngle, 1e-3f));
				}
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWorldIndicatorInsideTest, "VB.Unit.WorldIndicator.InsidePixels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWorldIndicatorInsideTest::RunTest(const FString& Parameters)
{
	const FIntPoint Size(1920, 1080);
	TestTrue(TEXT("origin is inside"), VBViewQuery::IsInsidePixels(FVector2D(0.0, 0.0), Size));
	TestTrue(TEXT("far corner is inside (closed rect)"), VBViewQuery::IsInsidePixels(FVector2D(1920.0, 1080.0), Size));
	TestFalse(TEXT("just past the right edge is outside"), VBViewQuery::IsInsidePixels(FVector2D(1920.5, 10.0), Size));
	TestFalse(TEXT("negative y is outside"), VBViewQuery::IsInsidePixels(FVector2D(10.0, -0.5), Size));
	TestFalse(TEXT("no controller places nothing"),
		VBWorldIndicatorProjection::Place(FVBIndicatorView(), FVector::ZeroVector, EVBIndicatorScreenPolicy::ClampToEdge, 72.0f).bVisible);
	TestFalse(TEXT("no controller is not inside"), VBViewQuery::IsInsideViewport(nullptr, FVector::ZeroVector));
	return true;
}
#endif

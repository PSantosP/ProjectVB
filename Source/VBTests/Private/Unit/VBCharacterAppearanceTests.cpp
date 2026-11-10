#include "Misc/AutomationTest.h"
#include "Character/Appearance/VBCharacterAppearance.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "CharacterCommon/VBMeshRefresh.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 겉모습 교체 구조의 판정부. 실제 겉모습 액터를 붙이는 것은 PIE 몫이다.
#if WITH_DEV_AUTOMATION_TESTS

// 뼈 갱신 요청 합치기 - 근접 판정 창과 겉모습이 같은 메시에 겹쳐 쥘 때 먼저 끝난 쪽이 상대의 갱신을 끄면 안 된다(review-codex.md F2).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeshRefreshOrPolicyTest, "VB.Unit.Appearance.MeshRefreshOrPolicy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBMeshRefreshOrPolicyTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	Mesh->bEnableUpdateRateOptimizations = true;
	const auto IsForced = [&]() {
		return Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
			&& !Mesh->bEnableUpdateRateOptimizations;
	};
	const auto IsBaseline = [&]() {
		return Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPose
			&& Mesh->bEnableUpdateRateOptimizations;
	};

	// 순서 1: 공격 창 -> 겉모습 -> 창 끝 -> 겉모습 끝
	FVBMeshRefresh::Acquire(Mesh.Get());
	TestTrue(TEXT("first request forces refresh"), IsForced());
	FVBMeshRefresh::Acquire(Mesh.Get());
	FVBMeshRefresh::Release(Mesh.Get());
	TestTrue(TEXT("one of two released: still forced"), IsForced());
	FVBMeshRefresh::Release(Mesh.Get());
	TestTrue(TEXT("last release restores the original"), IsBaseline());
	TestEqual(TEXT("no requests left"), FVBMeshRefresh::GetRequestCount(Mesh.Get()), 0);

	// 순서 2: 겉모습이 이미 쥔 상태에서 원래 값이 바뀌지 않는다(두 번째 요청은 저장하지 않는다)
	FVBMeshRefresh::Acquire(Mesh.Get());
	FVBMeshRefresh::Acquire(Mesh.Get());
	FVBMeshRefresh::Release(Mesh.Get());
	FVBMeshRefresh::Release(Mesh.Get());
	TestTrue(TEXT("second order also restores the original, not the forced value"), IsBaseline());

	// 쥔 적 없는 반납은 아무것도 안 바꾼다
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	AddExpectedMessagePlain(TEXT("반납이 왔다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	FVBMeshRefresh::Release(Mesh.Get());
	TestEqual(TEXT("unmatched release leaves the option alone"),
		Mesh->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
	return true;
}

// 겉모습 클래스 검증 - 복제 클래스(클라에서 안 만들어진다)와 주 몸 이름 불일치를 거절하고, 맞는 것은 통과시킨다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceClassCheckTest, "VB.Unit.Appearance.ClassCheck",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBAppearanceClassCheckTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearanceRules;
	const TArray<FName> KellanMeshes = { TEXT("Body"), TEXT("Face"), TEXT("Torso"), TEXT("Legs"), TEXT("Feet") };
	TestTrue(TEXT("valid: non-replicated with Body"), CheckVisualClass(true, false, KellanMeshes, TEXT("Body")) == EClassCheck::Ok);
	TestTrue(TEXT("rejects missing class"), CheckVisualClass(false, false, KellanMeshes, TEXT("Body")) == EClassCheck::NoClass);
	TestTrue(TEXT("rejects replicated class"), CheckVisualClass(true, true, KellanMeshes, TEXT("Body")) == EClassCheck::Replicated);
	TestTrue(TEXT("rejects unknown primary body"),
		CheckVisualClass(true, false, KellanMeshes, TEXT("Manny")) == EClassCheck::NoPrimaryBody);
	TestTrue(TEXT("rejects empty primary body"), CheckVisualClass(true, false, KellanMeshes, NAME_None) == EClassCheck::NoPrimaryBody);

	// 리타겟 주인은 겉모습 하나에 하나 - GASP BP(몸이 이미 듦)는 자산 칸을 비우고, 조립한 MetaHuman(몸이 비었음)은 자산이 채운다.
	TestTrue(TEXT("BP owns retarget, data empty"), CheckRetargetOwner(false, false, true) == ERetargetCheck::Ok);
	TestTrue(TEXT("data owns retarget, body empty"), CheckRetargetOwner(true, true, false) == ERetargetCheck::Ok);
	TestTrue(TEXT("rejects anim without tag"), CheckRetargetOwner(true, false, false) == ERetargetCheck::HalfSet);
	TestTrue(TEXT("rejects tag without anim"), CheckRetargetOwner(false, true, false) == ERetargetCheck::HalfSet);
	TestTrue(TEXT("rejects two owners"), CheckRetargetOwner(true, true, true) == ERetargetCheck::TwoOwners);
	return true;
}

// 교체 허용 시점 - 평상시는 받고, 몸을 쓰는 동작 하나라도 있으면 막는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceSwapPolicyTest, "VB.Unit.Appearance.SwapPolicy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBAppearanceSwapPolicyTest::RunTest(const FString& Parameters)
{
	using namespace VBBodyControlPolicy;
	TestFalse(TEXT("idle allows swap"), BlocksAppearanceSwap(FAppearanceSwapFacts{}));
	const TArray<bool FAppearanceSwapFacts::*> Fields = {
		&FAppearanceSwapFacts::bDead, &FAppearanceSwapFacts::bFinisherScene, &FAppearanceSwapFacts::bPlayerRagdoll,
		&FAppearanceSwapFacts::bTraversalAction, &FAppearanceSwapFacts::bTraversalFacing, &FAppearanceSwapFacts::bSmartObjectInteracting,
		&FAppearanceSwapFacts::bAttacking, &FAppearanceSwapFacts::bDodging, &FAppearanceSwapFacts::bInteracting,
		&FAppearanceSwapFacts::bWeaponTransition };
	for (int32 Index = 0; Index < Fields.Num(); ++Index)
	{
		FAppearanceSwapFacts Facts;
		Facts.*Fields[Index] = true;
		TestTrue(FString::Printf(TEXT("fact %d blocks swap"), Index), BlocksAppearanceSwap(Facts));
	}

	return true;
}
#endif

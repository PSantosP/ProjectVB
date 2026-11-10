#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBMontageTransition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBAttackConfig.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMontageSectionBlendTest, "VB.Unit.Animation.MontageSectionBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBMontageSectionBlendTest::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	UAnimInstance* Anim = NewObject<UAnimInstance>(Mesh);
	UAnimMontage* Montage = NewObject<UAnimMontage>();
	UVBAttackConfig* Config = NewObject<UVBAttackConfig>();
	TestEqual(TEXT("existing attacks retain immediate jumps"), Config->ComboSectionBlendSeconds, 0.0f);
	TestTrue(TEXT("zero blend is accepted"), VBMontageTransition::RequestSectionBlend(Anim, Montage, 0.0f));
	TestTrue(TEXT("zero blend emits no request"), Anim->GetActiveInertializationRequests().IsEmpty());
	TestTrue(TEXT("late input requests authored blend"), VBMontageTransition::RequestSectionBlend(Anim, Montage, 0.12f));
	const auto* Request = Anim->GetActiveInertializationRequests().Find(Montage->GetGroupName());
	if (TestNotNull(TEXT("request reaches the montage slot group"), Request))
	{
		TestEqual(TEXT("duration is not hardcoded"), Request->Duration, 0.12f);
	}
	Anim->ClearActiveInertializationRequests();
	TestFalse(TEXT("negative duration rejected"), VBMontageTransition::RequestSectionBlend(Anim, Montage, -0.1f));
	TestFalse(TEXT("excessive duration rejected"), VBMontageTransition::RequestSectionBlend(Anim, Montage, 0.5f));
	TestFalse(TEXT("nonfinite duration rejected"), VBMontageTransition::RequestSectionBlend(Anim, Montage,
		std::numeric_limits<float>::quiet_NaN()));
	TestFalse(TEXT("missing montage is not a default group fallback"), VBMontageTransition::RequestSectionBlend(Anim, nullptr, 0.12f));
	TestTrue(TEXT("invalid input left no stale request"), Anim->GetActiveInertializationRequests().IsEmpty());
	return true;
}
#endif

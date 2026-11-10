#include "Misc/AutomationTest.h"
#include "CharacterCommon/VBStrideScale.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStrideScaleTest, "VB.Unit.Character.StrideScale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBStrideScaleTest::RunTest(const FString& Parameters)
{
	for (const float Invalid : {0.0f, -1.0f, KINDA_SMALL_NUMBER, NAN})
	{
		TestEqual(TEXT("invalid scale falls back to one"), VBStrideScale::Sanitize(Invalid), 1.0f);
	}
	TestEqual(TEXT("positive scale retained"), VBStrideScale::Sanitize(2.0f), 2.0f);

	TestEqual(TEXT("no actor is one"), VBStrideScale::Resolve(nullptr), 1.0f);
	const TStrongObjectPtr<AActor> Prop(NewObject<AActor>(GetTransientPackage()));
	TestEqual(TEXT("an actor that is not a character is one"), VBStrideScale::Resolve(Prop.Get()), 1.0f);

	// 플레이어와 NPC 도 같은 규칙이라 적이 아닌 캐릭터로 잰다.
	const TStrongObjectPtr<ACharacter> Body(NewObject<ACharacter>(GetTransientPackage()));
	if (!TestNotNull(TEXT("character body"), Body.Get()) || !TestNotNull(TEXT("character mesh"), Body->GetMesh()))
	{
		return false;
	}
	TestEqual(TEXT("default mesh is one"), VBStrideScale::Resolve(Body.Get()), 1.0f);
	Body->GetMesh()->SetRelativeScale3D(FVector(2.0));
	TestEqual(TEXT("mesh scale is the stride scale"), VBStrideScale::Resolve(Body.Get()), 2.0f);
	Body->GetMesh()->SetRelativeScale3D(FVector::ZeroVector);
	TestEqual(TEXT("zero mesh scale falls back to one"), VBStrideScale::Resolve(Body.Get()), 1.0f);
	return true;
}
#endif

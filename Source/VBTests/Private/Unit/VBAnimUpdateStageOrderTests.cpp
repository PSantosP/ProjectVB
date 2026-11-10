// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/VBAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace VBAnimUpdateStageOrderTest
{
	// BlueprintReadOnly 지만 C++ 에선 protected - reflection 으로 이름을 찾아 읽어 게임 헤더에 시험 접근을 늘리지 않는다.
	float ReadFloat(const UVBAnimInstance& Anim, const TCHAR* Name)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(UVBAnimInstance::StaticClass(), Name);
		return Property ? Property->GetPropertyValue_InContainer(&Anim) : TNumericLimits<float>::Lowest();
	}
}

// Blueprint graph 없는 native instance 에서 실제 NativeUpdateAnimation 의 단계 순서 두 가지를 못박는다:
//   (1) 지난 프레임 값은 프레임 시작에 남긴다(derive 단계가 덮기 전), (2) slide graph 가 없으면 요청된 slide pose 를 한 프레임 안에 놓는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAnimUpdateStageOrderTest, "VB.Unit.Animation.UpdateStageOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAnimUpdateStageOrderTest::RunTest(const FString& Parameters)
{
	using namespace VBAnimUpdateStageOrderTest;
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};
	ACharacter* Character = World->SpawnActor<ACharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	if (!TestNotNull(TEXT("character"), Character) || !TestNotNull(TEXT("player mesh"), Mesh))
	{
		return false;
	}

	Character->GetMesh()->SetSkeletalMeshAsset(Mesh);
	Character->GetMesh()->SetAnimInstanceClass(UVBAnimInstance::StaticClass());
	UVBAnimInstance* Anim = Cast<UVBAnimInstance>(Character->GetMesh()->GetAnimInstance());
	if (!TestNotNull(TEXT("native anim instance"), Anim))
	{
		return false;
	}

	UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
	CMC->SetMovementMode(MOVE_Walking);
	constexpr float Dt = 1.0f / 60.0f;

	// (1) 1프레임은 앞(+X), 2프레임은 오른쪽(+Y)으로 움직이고 yaw 는 0 그대로 - 각도 하한 50 cm/s 보다 넉넉히 빠르다.
	CMC->Velocity = FVector(400.0f, 0.0f, 0.0f);
	Anim->NativeUpdateAnimation(Dt);
	const float AngleFrame1 = ReadFloat(*Anim, TEXT("LocomotionAngle"));
	CMC->Velocity = FVector(0.0f, 400.0f, 0.0f);
	Anim->NativeUpdateAnimation(Dt);
	const float AngleFrame2 = ReadFloat(*Anim, TEXT("LocomotionAngle"));
	// 전제: 이 프레임에 각도가 정말 바뀌었다 - 아니면 다음 검사가 아무것도 증명하지 않는다.
	if (!TestTrue(*FString::Printf(TEXT("locomotion angle changed (%.2f -> %.2f)"), AngleFrame1, AngleFrame2),
		FMath::Abs(AngleFrame2 - AngleFrame1) > 1.0f))
	{
		return false;
	}

	TestEqual(TEXT("the previous angle is frame 1's angle (kept at frame start, before derive)"),
		ReadFloat(*Anim, TEXT("PreviousLocomotionAngle")), AngleFrame1, 1e-4f);

	// (2) slide graph 가 없는(이 native instance 는 안 잇는다) slide pose 요청은 slide handoff 단계가 놓는다.
	Anim->bUseGASPSlidePose = true;
	Anim->NativeUpdateAnimation(Dt);
	TestFalse(TEXT("slide pose released when there is no slide graph"), Anim->bUseGASPSlidePose);
	return true;
}
#endif

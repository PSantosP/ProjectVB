// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/VBGASPBridgeLibrary.h"
#include "Camera/CameraActor.h"
#include "Character/VBCharacter.h"
#include "Character/VBHeroComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// hero 가 플레이어 카메라를 켜고 끈다 - 몸은 카메라 부품만 만들고 엔진 입구를 넘긴다.
namespace VBHeroCameraTest
{
	const TCHAR* BodyClassPath = TEXT("/Game/VB/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C");

	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		APlayerController* PC = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->InitializeActorsForPlay(FURL());
			PC = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("controller"), PC))
			{
				return false;
			}

			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient));
			PC->Player = LocalPlayer.Get();
			LocalPlayer->PlayerController = PC;
			return Test.TestTrue(TEXT("local controller"), PC->IsLocalController());
		}

		AVBCharacter* SpawnShipped(FAutomationTestBase& Test) const
		{
			UClass* BodyClass = LoadClass<AVBCharacter>(nullptr, BodyClassPath);
			AVBCharacter* Body = BodyClass ? World->SpawnActor<AVBCharacter>(BodyClass, FVector::ZeroVector,
				FRotator::ZeroRotator) : nullptr;
			Test.TestNotNull(TEXT("shipped body"), Body);
			return Body;
		}

		~FFixture()
		{
			if (World)
			{
				if (PC)
				{
					PC->Player = nullptr;
				}

				World->DestroyWorld(false);
			}

			if (Package)
			{
				Package->SetDirtyFlag(false);
			}
		}
	};

	// GASP user struct 의 enum 칸을 저작 이름으로 읽는다(칸 이름은 "<Name>_<n>_<guid>")
	FString ReadEnumName(const UScriptStruct& Struct, const void* Data, const TCHAR* Name)
	{
		const FString Prefix = FString(Name) + TEXT("_");
		for (TFieldIterator<FProperty> It(&Struct); It; ++It)
		{
			if (!It->GetName().StartsWith(Prefix))
			{
				continue;
			}

			if (const FByteProperty* Byte = CastField<FByteProperty>(*It); Byte && Byte->Enum)
			{
				return Byte->Enum->GetAuthoredNameStringByValue(Byte->GetPropertyValue_InContainer(Data));
			}

			if (const FEnumProperty* Enum = CastField<FEnumProperty>(*It))
			{
				const int64 Value = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Data));
				return Enum->GetEnum()->GetAuthoredNameStringByValue(Value);
			}
		}

		return FString();
	}
}

// 카메라 자산이 있는 몸은 로컬 controller 가 빙의하면 그것을 쓴다. 다시 refresh 해도 게임이 고른 다른 view target(처형 카메라)은 지키고,
//   controller 를 잃으면 GameplayCamera 를 끈다. 두 엔진 입구 모두 hero 에 닿는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroCameraSelectionTest, "VB.Unit.Character.HeroCamera.Selection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroCameraSelectionTest::RunTest(const FString& Parameters)
{
	using namespace VBHeroCameraTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBCharacter* Body = F.SpawnShipped(*this);
	UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Body);
	if (!Body || !TestNotNull(TEXT("hero"), Hero) || !TestNotNull(TEXT("gameplay camera"), Body->GetGameplayCamera()))
	{
		return false;
	}

	TestTrue(TEXT("shipped body has a camera asset"), Hero->IsUsingGameplayCamera());
	TestFalse(TEXT("asset present is not active (no controller yet)"), Body->GetGameplayCamera()->IsActive());

	F.PC->Possess(Body); // NotifyControllerChanged + PawnClientRestart
	TestTrue(TEXT("possession turns the GameplayCamera on"), Body->GetGameplayCamera()->IsActive());
	TestTrue(TEXT("the body is the view target"), F.PC->GetViewTarget() == Body);

	// PawnClientRestart 가 아니라 hero 의 refresh 자체를 본다 - 엔진 APawn::PawnClientRestart 는 PC 가 카메라 대상을 자동 관리하면
	//   우리 refresh 전에 view target 을 pawn 으로 되돌린다.
	ACameraActor* Other = F.World->SpawnActor<ACameraActor>();
	F.PC->SetViewTarget(Other);
	Hero->RefreshActiveCamera();
	TestTrue(TEXT("a repeated refresh keeps the other view target"), F.PC->GetViewTarget() == Other);

	F.PC->UnPossess();
	TestFalse(TEXT("losing the controller turns the GameplayCamera off"), Body->GetGameplayCamera()->IsActive());
	return true;
}

// 카메라 자산이 없는 몸은 켤 카메라가 없다 - 빙의하면 경고를 남기고 GameplayCamera 는 꺼진 채다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroCameraMissingAssetTest, "VB.Unit.Character.HeroCamera.MissingAssetWarns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroCameraMissingAssetTest::RunTest(const FString& Parameters)
{
	using namespace VBHeroCameraTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBCharacter* Body = F.World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Body);
	if (!TestNotNull(TEXT("native body"), Body) || !TestNotNull(TEXT("hero"), Hero))
	{
		return false;
	}

	AddExpectedMessage(TEXT("GameplayCamera"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	TestFalse(TEXT("no camera asset"), Hero->IsUsingGameplayCamera());
	F.PC->Possess(Body);
	TestFalse(TEXT("GameplayCamera stays off"), Body->GetGameplayCamera()->IsActive());
	return true;
}

// camera rig 는 bridge 로 style 을 읽는다. 값 이름이 GASP enum 과 어긋나면 bridge 가 값을 찾지 못해 여기서 실패한다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroCameraStyleTest, "VB.Unit.Character.HeroCamera.CameraStyle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroCameraStyleTest::RunTest(const FString& Parameters)
{
	using namespace VBHeroCameraTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBCharacter* Body = F.World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Body);
	UScriptStruct* CameraStruct = LoadObject<UScriptStruct>(nullptr,
		TEXT("/Game/Blueprints/Data/S_CharacterPropertiesForCamera.S_CharacterPropertiesForCamera"));
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("hero"), Hero)
		|| !TestNotNull(TEXT("GASP camera properties struct"), CameraStruct))
	{
		return false;
	}

	TestTrue(TEXT("default style"), Hero->GetCameraStyle() == EVBCameraStyle::Medium);

	TArray<uint8> Memory;
	Memory.SetNumZeroed(CameraStruct->GetStructureSize());
	CameraStruct->InitializeStruct(Memory.GetData());
	for (const TCHAR* Style : {TEXT("Close"), TEXT("Medium"), TEXT("Far"), TEXT("Debug")})
	{
		const int64 Value = StaticEnum<EVBCameraStyle>()->GetValueByNameString(Style);
		if (!TestTrue(FString::Printf(TEXT("%s is a hero camera style"), Style), Value != INDEX_NONE))
		{
			continue;
		}

		Hero->SetCameraStyle(static_cast<EVBCameraStyle>(Value));
		UVBGASPBridgeLibrary::FillCharacterProperties(Body, CameraStruct, Memory.GetData());
		TestEqual(FString::Printf(TEXT("bridge passes %s to the camera rig"), Style),
			ReadEnumName(*CameraStruct, Memory.GetData(), TEXT("CameraStyle")), FString(Style));
	}

	CameraStruct->DestroyStruct(Memory.GetData());
	return true;
}
#endif

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableSubsystem.h"
#include "UObject/UnrealType.h"
#include "VBIsolatedGameWorld.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 켜짐 저장은 월드 하나의 '이름표 -> 켜짐' 표라, 두 대상이 같은 SaveId 를 쓰면 켜짐 하나를 나눠 쓴다.
//   편집기 검사가 두 액터 모두에 오류를 내고, 게임은 등록 때 경고 한 줄을 낸다.
//   대조: 이름표 비교를 빼면 'duplicate' 줄이, 등록 검사를 빼면 'register' 줄이 실패한다.
namespace VBInteractableSaveKeyTest
{
	UVBInteractableComponent* Spawn(UWorld& World, FName SaveId)
	{
		AActor* Owner = World.SpawnActor<AActor>();
		UVBInteractableComponent* Interactable = NewObject<UVBInteractableComponent>(Owner);
		// 저작 칸(SaveId)은 보호돼 있다 - 시험만을 위한 공개 API 를 만들지 않고 저작 도구처럼 리플렉션으로 채운다.
		if (const FNameProperty* IdProp = FindFProperty<FNameProperty>(UVBInteractableComponent::StaticClass(), TEXT("SaveId")))
		{
			IdProp->SetPropertyValue_InContainer(Interactable, SaveId);
		}

		Owner->AddInstanceComponent(Interactable);
		Interactable->RegisterComponent();
		return Interactable;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractableSaveKeyValidationTest, "VB.Unit.Interaction.SaveKey.DuplicateInWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractableSaveKeyValidationTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	using namespace VBInteractableSaveKeyTest;
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UVBInteractableComponent* First = Spawn(*Isolated.World, TEXT("Prologue.SupplyCupboard"));
	UVBInteractableComponent* Second = Spawn(*Isolated.World, TEXT("Prologue.SupplyCupboard"));
	UVBInteractableComponent* Own = Spawn(*Isolated.World, TEXT("Prologue.OtherCupboard"));
	const auto IsInvalid = [](const UVBInteractableComponent& Interactable)
	{
		FDataValidationContext Context;
		return Interactable.IsDataValid(Context) == EDataValidationResult::Invalid;
	};
	TestTrue(TEXT("duplicate: first actor invalid"), IsInvalid(*First));
	TestTrue(TEXT("duplicate: second actor invalid"), IsInvalid(*Second));
	TestFalse(TEXT("own key: valid"), IsInvalid(*Own));

	// 켜짐을 저장하지 않는 대상은 이름표가 같아도 칸을 쓰지 않는다.
	Second->SetSaveState(false);
	TestFalse(TEXT("other one not saved: no clash"), IsInvalid(*First));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractableSaveKeyRegisterTest, "VB.Unit.Interaction.SaveKey.RegisterWarns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractableSaveKeyRegisterTest::RunTest(const FString& Parameters)
{
	using namespace VBInteractableSaveKeyTest;
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UVBInteractableSubsystem* Interactables = Isolated.World->GetSubsystem<UVBInteractableSubsystem>();
	if (!TestNotNull(TEXT("interactable subsystem"), Interactables))
	{
		return false;
	}

	AddExpectedMessagePlain(TEXT("켜짐 저장 이름표 충돌"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Interactables->Register(Spawn(*Isolated.World, TEXT("Prologue.SupplyCupboard")));
	Interactables->Register(Spawn(*Isolated.World, TEXT("Prologue.OtherCupboard")));
	Interactables->Register(Spawn(*Isolated.World, TEXT("Prologue.SupplyCupboard")));
	TestEqual(TEXT("register: all three listed"), Interactables->GetInteractables().Num(), 3);
	return true;
}

#endif

// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/VBEnvironmentCameraComponent.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PostProcessComponent.h"
#include "Data/VBEnvironmentCameraConfig.h"
#include "InputMappingContext.h"
#include "Character/VBCharacter.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Input/VBInputDeviceManager.h"
#include "Interaction/VBGASPNPC.h"
#include "Character/VBCombatInputComponent.h"
#include "Interaction/VBInteractHighlightComponent.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "UI/Indicator/VBExecutePromptComponent.h"
#include "VBIsolatedGameWorld.h"

// 플레이어 전용 기능 컴포넌트는 C++ 생성자가 아니라 BP_VBCharacter 의 컴포넌트 목록에서 온다.
//   개수는 finder 가 아니라 actor 의 실제 컴포넌트 집합으로 센다.
namespace VBCharacterAssemblyTest
{
	const TCHAR* BodyClassPath = TEXT("/Game/VB/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C");
	const TCHAR* NpcClassPath = TEXT("/Game/VB/SmartObjects/BP_VBGASPNPC.BP_VBGASPNPC_C");
	// 플러그인 클래스는 경로로 찾아서 테스트 모듈에 플러그인 헤더나 Build.cs 항목이 필요 없다.
	const TCHAR* SceneClassPath = TEXT("/Script/ContextualAnimation.ContextualAnimSceneActorComponent");
	const TCHAR* TrajectoryClassPath = TEXT("/Script/MotionTrajectory.CharacterTrajectoryComponent");

	struct FExpected
	{
		UClass* Class;
		const TCHAR* Name;
	};

	TArray<FExpected> C9a()
	{
		return {
			{ UVBEnvironmentCameraComponent::StaticClass(), TEXT("EnvironmentCamera") },
			{ UVBInputDeviceManager::StaticClass(), TEXT("InputDeviceManager") },
			{ UVBPlayerRestoreComponent::StaticClass(), TEXT("PlayerRestoreComponent") },
			{ UVBInteractHighlightComponent::StaticClass(), TEXT("InteractHighlightComponent") },
		};
	}

	TArray<FExpected> C9b()
	{
		return {
			{ UVBMeleeTraceComponent::StaticClass(), TEXT("MeleeTraceComponent") },
			{ UVBFinisherComponent::StaticClass(), TEXT("FinisherComponent") },
			{ FindObject<UClass>(nullptr, SceneClassPath), TEXT("FinisherSceneComponent") },
		};
	}

	TArray<FExpected> C9c()
	{
		return {
			{ UVBCharacterAppearanceComponent::StaticClass(), TEXT("AppearanceComponent") },
			{ UPostProcessComponent::StaticClass(), TEXT("PostProcessComponent") },
		};
	}

	TArray<UActorComponent*> ComponentsOf(const AActor& Actor, UClass* Class)
	{
		TArray<UActorComponent*> Out;
		for (UActorComponent* Component : Actor.GetComponents())
		{
			if (Component && Component->IsA(Class))
			{
				Out.Add(Component);
			}
		}

		return Out;
	}

	// 실제 몸, C++ 몸 단독, AVBCharacter 를 상속한 실제 NPC 를 격리된 월드 하나에 둔다.
	struct FBodies
	{
		FVBIsolatedGameWorld Isolated;
		const AVBCharacter* Shipped = nullptr;
		const AVBCharacter* Native = nullptr;
		const AVBGASPNPC* Npc = nullptr;

		bool Spawn(FAutomationTestBase& Test)
		{
			UClass* BodyClass = LoadClass<AVBCharacter>(nullptr, BodyClassPath);
			UClass* NpcClass = LoadClass<AVBGASPNPC>(nullptr, NpcClassPath);
			if (!Test.TestNotNull(TEXT("shipped body class"), BodyClass) || !Test.TestNotNull(TEXT("shipped NPC class"), NpcClass))
			{
				return false;
			}

			if (!Isolated.Init(Test))
			{
				return false;
			}

			Shipped = Isolated.World->SpawnActor<AVBCharacter>(BodyClass, FVector::ZeroVector, FRotator::ZeroRotator);
			Native = Isolated.World->SpawnActor<AVBCharacter>(FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator);
			Npc = Isolated.World->SpawnActor<AVBGASPNPC>(NpcClass, FVector(1000.0, 0.0, 0.0), FRotator::ZeroRotator);
			return Test.TestNotNull(TEXT("shipped body"), Shipped) && Test.TestNotNull(TEXT("C++ body"), Native)
				&& Test.TestNotNull(TEXT("shipped NPC"), Npc);
		}
	};

	void CheckRows(FAutomationTestBase& Test, const TArray<FExpected>& Rows, const FBodies& Bodies)
	{
		for (const FExpected& Row : Rows)
		{
			if (!Test.TestNotNull(FString::Printf(TEXT("%s: component class loaded"), Row.Name), Row.Class))
			{
				continue;
			}

			const FString Type = Row.Class->GetName();
			const TArray<UActorComponent*> Found = ComponentsOf(*Bodies.Shipped, Row.Class);
			if (!Test.TestEqual(FString::Printf(TEXT("%s: exactly one on the shipped body"), *Type), Found.Num(), 1))
			{
				continue;
			}

			const UActorComponent* Component = Found[0];
			Test.TestTrue(FString::Printf(TEXT("%s: built by the construction script"), *Type), Component->IsCreatedByConstructionScript());
			// IsDefaultSubobject() 가 아니라 이 flag 를 본다 - 그쪽은 SCS template 같은 non-CDO archetype 인스턴스에도 true 다.
			Test.TestFalse(FString::Printf(TEXT("%s: not a C++ default subobject"), *Type), Component->HasAnyFlags(RF_DefaultSubObject));
			Test.TestEqual(FString::Printf(TEXT("%s: keeps the old name"), *Type), Component->GetFName(), FName(Row.Name));
			Test.TestTrue(FString::Printf(TEXT("%s: name stable for networking"), *Type), Component->IsNameStableForNetworking());
			Test.TestTrue(FString::Printf(TEXT("%s: registered"), *Type), Component->IsRegistered());
			Test.TestEqual(FString::Printf(TEXT("%s: none on the C++ body"), *Type), ComponentsOf(*Bodies.Native, Row.Class).Num(), 0);
			Test.TestEqual(FString::Printf(TEXT("%s: none on the NPC"), *Type), ComponentsOf(*Bodies.Npc, Row.Class).Num(), 0);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCharacterAssemblyC9aTest, "VB.Unit.Character.Assembly.C9a",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCharacterAssemblyC9aTest::RunTest(const FString& Parameters)
{
	using namespace VBCharacterAssemblyTest;
	FBodies Bodies;
	if (!Bodies.Spawn(*this))
	{
		return false;
	}

	const AVBCharacter* Shipped = Bodies.Shipped;
	const AVBCharacter* Native = Bodies.Native;
	CheckRows(*this, C9a(), Bodies);
	const TArray<UActorComponent*> Devices = ComponentsOf(*Shipped, UVBInputDeviceManager::StaticClass());
	const TArray<UActorComponent*> Environments = ComponentsOf(*Shipped, UVBEnvironmentCameraComponent::StaticClass());
	TestTrue(TEXT("input device finder reaches the shipped one"),
		Devices.Num() == 1 && UVBInputDeviceManager::FindFor(Shipped) == Devices[0]);
	TestTrue(TEXT("environment camera finder reaches the shipped one"),
		Environments.Num() == 1 && UVBEnvironmentCameraComponent::FindFor(Shipped) == Environments[0]);
	TestNull(TEXT("finders return null on a body without them"), UVBInputDeviceManager::FindFor(Native));

	// BP_VBCharacter 가 옛 default subobject 에 쥐던 값이 새 노드에 그대로 있다.
	if (const UVBEnvironmentCameraComponent* Environment = UVBEnvironmentCameraComponent::FindFor(Shipped))
	{
		TestEqual(TEXT("environment camera keeps its config"), GetPathNameSafe(Environment->Config.Get()),
			FString(TEXT("/Game/VB/Data/DA_EnvironmentCamera_Default.DA_EnvironmentCamera_Default")));
	}

	if (const UVBInputDeviceManager* DeviceManager = UVBInputDeviceManager::FindFor(Shipped))
	{
		TestEqual(TEXT("keyboard mapping context kept"),
			GetPathNameSafe(DeviceManager->GetMappingContext(ECommonInputType::MouseAndKeyboard)),
			FString(TEXT("/Game/VB/Input/Contexts/IMC_KeyboardMouse.IMC_KeyboardMouse")));
		TestEqual(TEXT("gamepad mapping context kept"), GetPathNameSafe(DeviceManager->GetMappingContext(ECommonInputType::Gamepad)),
			FString(TEXT("/Game/VB/Input/Contexts/IMC_Gamepad.IMC_Gamepad")));
	}

	TestNull(TEXT("finders return null for no actor"), UVBEnvironmentCameraComponent::FindFor(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCharacterAssemblyC9bTest, "VB.Unit.Character.Assembly.C9b",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCharacterAssemblyC9bTest::RunTest(const FString& Parameters)
{
	using namespace VBCharacterAssemblyTest;
	FBodies Bodies;
	if (!Bodies.Spawn(*this))
	{
		return false;
	}

	CheckRows(*this, C9b(), Bodies);

	// finisher scene 은 옛 default subobject 처럼 replicate 해야 한다.
	if (UClass* SceneClass = FindObject<UClass>(nullptr, SceneClassPath))
	{
		const TArray<UActorComponent*> Scenes = ComponentsOf(*Bodies.Shipped, SceneClass);
		TestTrue(TEXT("finisher scene replicates"), Scenes.Num() == 1 && Scenes[0]->GetIsReplicated());
	}

	TestTrue(TEXT("finisher finder reaches the shipped one, none on the C++ body"),
		UVBFinisherComponent::FindFor(Bodies.Shipped) != nullptr && UVBFinisherComponent::FindFor(Bodies.Native) == nullptr);
	TestTrue(TEXT("melee trace finder reaches the shipped one, none on the C++ body"),
		UVBMeleeTraceComponent::FindFor(Bodies.Shipped) != nullptr && UVBMeleeTraceComponent::FindFor(Bodies.Native) == nullptr);
	TestNull(TEXT("finisher finder returns null for no actor"), UVBFinisherComponent::FindFor(nullptr));
	TestNull(TEXT("melee trace finder returns null for no actor"), UVBMeleeTraceComponent::FindFor(nullptr));

	// 옛 default subobject 의 값이 새 노드에 있다. ShapeSet 은 protected 라 테스트용 accessor 대신 reflection 으로 읽는다.
	const FObjectPropertyBase* ShapeSetProperty = FindFProperty<FObjectPropertyBase>(UVBMeleeTraceComponent::StaticClass(),
		TEXT("ShapeSet"));
	const UVBMeleeTraceComponent* Trace = UVBMeleeTraceComponent::FindFor(Bodies.Shipped);
	if (TestNotNull(TEXT("ShapeSet property"), ShapeSetProperty) && Trace)
	{
		TestEqual(TEXT("melee trace keeps its shape set"), GetPathNameSafe(ShapeSetProperty->GetObjectPropertyValue_InContainer(Trace)),
			FString(TEXT("/Game/VB/Data/MeleeTrace/DA_MeleeShapes_Player.DA_MeleeShapes_Player")));
	}

	// 읽는 곳이 없어 지운 trajectory 컴포넌트는 어느 몸에도 없다.
	if (UClass* TrajectoryClass = FindObject<UClass>(nullptr, TrajectoryClassPath); TestNotNull(TEXT("trajectory class"), TrajectoryClass))
	{
		TestEqual(TEXT("no trajectory component on the shipped body"), ComponentsOf(*Bodies.Shipped, TrajectoryClass).Num(), 0);
		TestEqual(TEXT("no trajectory component on the NPC"), ComponentsOf(*Bodies.Npc, TrajectoryClass).Num(), 0);
	}

	return true;
}

// appearance 와 outline post-process 는 BP_VBCharacter 의 컴포넌트 목록에서 온다.
//   post-process 는 실제로 그려지는 상태로 본다 - weight 0 이나 꺼진 컴포넌트는 목록에 있어도 아무것도 안 그린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCharacterAssemblyC9cTest, "VB.Unit.Character.Assembly.C9c",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCharacterAssemblyC9cTest::RunTest(const FString& Parameters)
{
	using namespace VBCharacterAssemblyTest;
	FBodies Bodies;
	if (!Bodies.Spawn(*this))
	{
		return false;
	}

	CheckRows(*this, C9c(), Bodies);

	const TArray<UActorComponent*> Appearances = ComponentsOf(*Bodies.Shipped, UVBCharacterAppearanceComponent::StaticClass());
	TestTrue(TEXT("appearance replicates"), Appearances.Num() == 1 && Appearances[0]->GetIsReplicated());

	const TArray<UActorComponent*> PostProcesses = ComponentsOf(*Bodies.Shipped, UPostProcessComponent::StaticClass());
	if (PostProcesses.Num() != 1)
	{
		return true; // 개수 실패는 CheckRows 가 이미 적었다
	}

	const UPostProcessComponent* Outline = Cast<UPostProcessComponent>(PostProcesses[0]);
	TestTrue(TEXT("post-process unbound"), Outline->bUnbound);
	TestTrue(TEXT("post-process enabled"), Outline->bEnabled);
	TestEqual(TEXT("post-process full weight"), Outline->BlendWeight, 1.0f);
	TestEqual(TEXT("post-process priority"), Outline->Priority, 0.0f);
	TestTrue(TEXT("post-process attached to the capsule"), Outline->GetAttachParent() == Bodies.Shipped->GetCapsuleComponent());
	const TArray<FWeightedBlendable>& Blendables = Outline->Settings.WeightedBlendables.Array;
	TArray<FString> Seen;
	for (const FWeightedBlendable& Blendable : Blendables)
	{
		Seen.Add(FString::Printf(TEXT("%s@%.2f"), *GetPathNameSafe(Blendable.Object), Blendable.Weight));
	}

	TestEqual(TEXT("exactly the two outline materials at weight 1"), FString::Join(Seen, TEXT(" | ")),
		FString(TEXT("/Game/VB/VFX/Materials/M_VB_FX_Outline.M_VB_FX_Outline@1.00 | /Game/VB/VFX/Materials/M_VB_FX_OutlineInteract.M_VB_FX_OutlineInteract@1.00")));
	return true;
}

// 공격 누름 컴포넌트는 BP_VBCharacter 의 컴포넌트 목록에서 온다. 서버 RPC 를 가지므로 replicate 해야 한다.
//   조립만 보며 원격 전송을 증명하지는 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCharacterAssemblyC7Test, "VB.Unit.Character.Assembly.C7CombatInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCharacterAssemblyC7Test::RunTest(const FString& Parameters)
{
	using namespace VBCharacterAssemblyTest;
	FBodies Bodies;
	if (!Bodies.Spawn(*this))
	{
		return false;
	}

	CheckRows(*this, { { UVBCombatInputComponent::StaticClass(), TEXT("CombatInputComponent") } }, Bodies);
	UVBCombatInputComponent* Found = UVBCombatInputComponent::FindFor(Bodies.Shipped);
	TestNotNull(TEXT("finder reaches the shipped one"), Found);
	TestTrue(TEXT("combat input replicates"), Found && Found->GetIsReplicated());
	TestTrue(TEXT("owned by the shipped body"), Found && Found->GetOwner() == Bodies.Shipped);
	return true;
}

// 처형 표시는 플레이어 몸에만, BP 의 컴포넌트 목록에서 온다. 자산 단계 전에는 "exactly one" 이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCharacterAssemblyExecutePromptTest, "VB.Unit.Character.Assembly.ExecutePrompt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCharacterAssemblyExecutePromptTest::RunTest(const FString& Parameters)
{
	using namespace VBCharacterAssemblyTest;
	FBodies Bodies;
	if (!Bodies.Spawn(*this))
	{
		return false;
	}

	CheckRows(*this, { { UVBExecutePromptComponent::StaticClass(), TEXT("ExecutePromptComponent") } }, Bodies);
	return true;
}
#endif

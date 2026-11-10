// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/Appearance/VBCharacterAppearance.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Player/VBPlayerState.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 외형 컴포넌트는 pawn 의 controller 변경을 듣고 스스로 장비 source 를 잇는다 (몸이 SetEquipmentSource 를 부르지 않는다)
// setter 를 부르면 이 경로를 증명하지 못하므로 공개 구독(장비 변경, 세이브 마무리)으로만 관찰한다
namespace VBAppearancePossessionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		APlayerController* PC = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			// 이게 없으면 actor 가 초기화 없이(PostInitializeComponents 없이) 생겨 PlayerState 에 살아 있는 장비가 없다
			World->InitializeActorsForPlay(FURL());
			PC = World->SpawnActor<APlayerController>();
			return Test.TestNotNull(TEXT("controller"), PC);
		}

		AVBPlayerState* NewState() const
		{
			AVBPlayerState* State = World->SpawnActor<AVBPlayerState>();
			if (State)
			{
				State->SetOwner(PC);
			}

			return State;
		}

		ACharacter* NewPawn(FVector At) const
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<ACharacter>(At, FRotator::ZeroRotator, Params);
		}

		UVBCharacterAppearanceComponent* AddAppearance(ACharacter& Pawn) const
		{
			UVBCharacterAppearanceComponent* Appearance = NewObject<UVBCharacterAppearanceComponent>(&Pawn);
			Pawn.AddInstanceComponent(Appearance);
			Appearance->RegisterComponent();
			return Appearance;
		}

		bool IsSourceOf(const AVBPlayerState* State, const UVBCharacterAppearanceComponent* Appearance) const
		{
			const UVBEquipmentComponent* Equipment = UVBEquipmentComponent::FindFor(State);
			return Equipment && Equipment->OnEquipmentChanged.IsBoundToObject(Appearance);
		}

		bool HearsSaveFinalized(const UVBCharacterAppearanceComponent* Appearance) const
		{
			const UVBSaveParticipantSubsystem* Registry = World->GetSubsystem<UVBSaveParticipantSubsystem>();
			return Registry && Registry->OnSaveFinalized.IsBoundToObject(Appearance);
		}

		~FFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}

			if (Package)
			{
				Package->SetDirtyFlag(false);
			}
		}
	};
}

// 보통 possession - possess 하면 PlayerState 의 장비가 source, unpossess 하면 source 없음
// PlayerState 하나가 A -> B -> A 로 옮겨도 지금 possess 한 몸의 외형만 그 source 를 쓴다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearancePossessionFollowsTest, "VB.Unit.Appearance.Possession.FollowsController",
	VBAppearancePossessionTest::Flags)
bool FVBAppearancePossessionFollowsTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePossessionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBPlayerState* State = F.NewState();
	ACharacter* A = F.NewPawn(FVector::ZeroVector);
	ACharacter* B = F.NewPawn(FVector(500.0, 0.0, 0.0));
	if (!TestTrue(TEXT("actors"), State && A && B)
		|| !TestNotNull(TEXT("equipment on the PlayerState"), UVBEquipmentComponent::FindFor(State)))
	{
		return false;
	}

	F.PC->PlayerState = State;
	UVBCharacterAppearanceComponent* AppearanceA = F.AddAppearance(*A);
	UVBCharacterAppearanceComponent* AppearanceB = F.AddAppearance(*B);
	TestFalse(TEXT("0 not possessed: no source"), F.IsSourceOf(State, AppearanceA));

	F.PC->Possess(A);
	TestTrue(TEXT("1 A possessed: A's appearance listens to the equipment"), F.IsSourceOf(State, AppearanceA));
	TestTrue(TEXT("1 A possessed: and to save finalized"), F.HearsSaveFinalized(AppearanceA));
	TestFalse(TEXT("1 A possessed: B not"), F.IsSourceOf(State, AppearanceB));

	F.PC->Possess(B);
	TestFalse(TEXT("2 B possessed: A released"), F.IsSourceOf(State, AppearanceA));
	TestFalse(TEXT("2 B possessed: A no longer hears save finalized"), F.HearsSaveFinalized(AppearanceA));
	TestTrue(TEXT("2 B possessed: B listens"), F.IsSourceOf(State, AppearanceB));

	F.PC->Possess(A);
	TestTrue(TEXT("3 A again: A listens"), F.IsSourceOf(State, AppearanceA));
	TestFalse(TEXT("3 A again: B released"), F.IsSourceOf(State, AppearanceB));

	F.PC->UnPossess();
	TestFalse(TEXT("4 unpossessed: no source"), F.IsSourceOf(State, AppearanceA));
	return true;
}

// 등록 수명 - 이미 possess 된 몸에 등록하면 현재 source 를 바로 잡는다 (엔진은 controller 변경을 다시 알리지 않는다)
// 등록을 풀면 구독을 모두 놓고, 다시 등록하면 그때의 현재 값을 잡는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearancePossessionRegistrationTest, "VB.Unit.Appearance.Possession.RegistrationLifetime",
	VBAppearancePossessionTest::Flags)
bool FVBAppearancePossessionRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePossessionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBPlayerState* First = F.NewState();
	AVBPlayerState* Second = F.NewState();
	ACharacter* A = F.NewPawn(FVector::ZeroVector);
	if (!TestTrue(TEXT("actors"), First && Second && A))
	{
		return false;
	}

	F.PC->PlayerState = First;
	F.PC->Possess(A);

	UVBCharacterAppearanceComponent* Appearance = F.AddAppearance(*A);
	TestTrue(TEXT("late registration: current source at once"), F.IsSourceOf(First, Appearance));

	Appearance->UnregisterComponent();
	TestFalse(TEXT("unregistered: equipment subscription gone"), F.IsSourceOf(First, Appearance));
	TestFalse(TEXT("unregistered: save subscription gone"), F.HearsSaveFinalized(Appearance));

	// 등록이 풀린 동안 몸의 주인이 바뀐다 - 옛 PlayerState 는 돌아오면 안 되고 다시 등록할 때 새 것을 잡는다
	F.PC->UnPossess();
	F.PC->PlayerState = Second;
	F.PC->Possess(A);
	TestFalse(TEXT("unregistered: a possession change is not heard"), F.IsSourceOf(Second, Appearance));

	Appearance->RegisterComponent();
	TestTrue(TEXT("re-registered: the current PlayerState's equipment"), F.IsSourceOf(Second, Appearance));
	TestFalse(TEXT("re-registered: the old PlayerState stays released"), F.IsSourceOf(First, Appearance));
	TestTrue(TEXT("re-registered: save finalized heard again"), F.HearsSaveFinalized(Appearance));

	// pawn 이 아닌 actor 의 컴포넌트는 아무것도 듣지 않고 그대로 동작한다 (장비 투영 fixture 가 쓴다)
	AActor* Plain = F.World->SpawnActor<AActor>();
	UVBCharacterAppearanceComponent* OnPlain = Plain ? NewObject<UVBCharacterAppearanceComponent>(Plain) : nullptr;
	if (TestNotNull(TEXT("plain actor component"), OnPlain))
	{
		OnPlain->RegisterComponent();
		TestFalse(TEXT("plain actor: no save subscription from registration"), F.HearsSaveFinalized(OnPlain));
	}

	return true;
}

// 겉모습 바꾸기 효과는 SetAppearance 로 instigator 가 possess 한 몸에 닿고, 몸이나 컴포넌트가 없으면 아무것도 안 바꾼다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceSetEffectTest, "VB.Unit.Appearance.Possession.SetAppearanceEffect",
	VBAppearancePossessionTest::Flags)
bool FVBAppearanceSetEffectTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePossessionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	TArray<FPrimaryAssetId> Listed;
	UAssetManager::Get().GetPrimaryAssetIdList(UVBCharacterAppearance::GetAppearanceAssetType(), Listed);
	if (!TestTrue(TEXT("the asset manager lists at least one look"), Listed.Num() > 0))
	{
		return false;
	}

	AVBPlayerState* State = F.NewState();
	ACharacter* Body = F.NewPawn(FVector::ZeroVector);
	ACharacter* Bare = F.NewPawn(FVector(500.0, 0.0, 0.0));
	if (!TestTrue(TEXT("actors"), State && Body && Bare))
	{
		return false;
	}

	F.PC->PlayerState = State;
	UVBCharacterAppearanceComponent* Appearance = F.AddAppearance(*Body);

	FVBRuleContext Context;
	Context.World = F.World;
	FVBEffect_SetAppearance Effect;
	Effect.Appearance = Listed[0];
	AddExpectedMessagePlain(TEXT("효과(겉모습 바꾸기)"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
	AddExpectedMessagePlain(TEXT("겉모습 목록에 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	Effect.Apply(Context);
	TestFalse(TEXT("no instigator: nothing changes"), Appearance->GetAppearanceId().IsValid());

	F.PC->Possess(Bare);
	Context.Instigator = State;
	Effect.Apply(Context);
	TestFalse(TEXT("possessed body without the component: nothing changes"), Appearance->GetAppearanceId().IsValid());

	F.PC->Possess(Body);
	Effect.Apply(Context);
	TestEqual(TEXT("possessed body takes the listed look"), Appearance->GetAppearanceId(), Listed[0]);

	FVBEffect_SetAppearance Unknown;
	Unknown.Appearance = FPrimaryAssetId(UVBCharacterAppearance::GetAppearanceAssetType(), TEXT("NotAnAppearance"));
	Unknown.Apply(Context);
	TestEqual(TEXT("an unlisted id is refused and the look stays"), Appearance->GetAppearanceId(), Listed[0]);

	FVBEffect_SetAppearance BackToBody;
	BackToBody.Apply(Context);
	TestFalse(TEXT("empty id returns to the driver body"), Appearance->GetAppearanceId().IsValid());
	return true;
}
#endif

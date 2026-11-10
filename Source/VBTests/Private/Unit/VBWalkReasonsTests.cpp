// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBSlideMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/VBGASPNPC.h"
#include "Character/VBWalkReasons.h"
#include "Data/VBMovementConfig.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Misc/ScopeExit.h"

// 걷기 이유 값 - 토글, 아날로그 기울기, smart object override 가 각자 걷기를 말하고, override 가 이기며, sprint 는 토글만 지운다
// 몸도 월드도 없는 순수 값 시험이고, 몸 연결은 따로 시험한다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWalkReasonsValueTest, "VB.Unit.Character.WalkReasons.Value",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWalkReasonsValueTest::RunTest(const FString& Parameters)
{
	constexpr float Threshold = 0.7f;
	// 아날로그 - 놓으면 걷기 이유가 아니고, 작게 기울이면 걷고, 문턱 이상과 키보드(1, 대각 1.41)는 뛴다
	TestFalse(TEXT("released (0) does not walk"), FVBWalkReasons::IsAnalogWalk(0.f, Threshold));
	TestTrue(TEXT("tilt 0.4 walks"), FVBWalkReasons::IsAnalogWalk(0.4f, Threshold));
	TestFalse(TEXT("tilt at the threshold runs"), FVBWalkReasons::IsAnalogWalk(Threshold, Threshold));
	TestFalse(TEXT("tilt 0.9 runs"), FVBWalkReasons::IsAnalogWalk(0.9f, Threshold));
	TestFalse(TEXT("keyboard 1.0 runs"), FVBWalkReasons::IsAnalogWalk(1.f, Threshold));
	TestFalse(TEXT("keyboard diagonal 1.41 runs"), FVBWalkReasons::IsAnalogWalk(1.41f, Threshold));

	{
		FVBWalkReasons R;
		TestEqual(TEXT("no reason runs"), R.Resolve(), EVBGait::Run);
		R.ToggleWalk();
		TestEqual(TEXT("toggle on walks"), R.Resolve(), EVBGait::Walk);
		R.ToggleWalk();
		TestEqual(TEXT("toggle off runs"), R.Resolve(), EVBGait::Run);
	}

	{
		FVBWalkReasons R;
		R.SetAnalogWalk(true);
		TestEqual(TEXT("analog alone walks"), R.Resolve(), EVBGait::Walk);
		R.SetAnalogWalk(false);
		TestEqual(TEXT("analog released runs"), R.Resolve(), EVBGait::Run);
	}
	// sprint 시작은 토글을 지우지만 손의 아날로그 걷기는 지우지 않는다
	{
		FVBWalkReasons R;
		R.ToggleWalk();
		R.SetAnalogWalk(true);
		R.ClearToggleForSprint();
		TestEqual(TEXT("sprint keeps analog walk"), R.Resolve(), EVBGait::Walk);
		R.SetAnalogWalk(false);
		TestEqual(TEXT("sprint cleared the toggle"), R.Resolve(), EVBGait::Run);
	}
	// smart object override 는 양쪽 다 이기고, 아래 이유들은 건드리지 않는다
	{
		FVBWalkReasons R;
		R.ToggleWalk();
		R.SetSmartObjectOverride(EVBGait::Run);
		TestEqual(TEXT("override Run beats toggle"), R.Resolve(), EVBGait::Run);
		R.ClearSmartObjectOverride();
		TestEqual(TEXT("toggle survives the override"), R.Resolve(), EVBGait::Walk);
		R.ToggleWalk();
		R.SetSmartObjectOverride(EVBGait::Walk);
		TestEqual(TEXT("override Walk beats no reason"), R.Resolve(), EVBGait::Walk);
	}

	return true;
}

// intent owner 가 실제 입구(ToggleWalk, ReportMoveInputMagnitude, StartSprint/StopSprint, smart object 진입/종료)로 Gait 를 쓴다
// 몸의 tick 은 그 결과로 속도를 정한다. Standalone 월드라 server RPC 가 그 자리에서 돈다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWalkReasonsBodyTest, "VB.Unit.Character.WalkReasons.Body",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWalkReasonsBodyTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	AVBCharacter* Body = World->SpawnActor<AVBCharacter>();
	if (!TestNotNull(TEXT("body"), Body))
	{
		return false;
	}

	ON_SCOPE_EXIT { Body->Destroy(); };
	UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(Body);
	if (!TestNotNull(TEXT("the body made its gait owner"), Intent))
	{
		return false;
	}

	const float Threshold = Body->GetMovementConfig()->AnalogWalkRunThreshold;
	TestEqual(TEXT("starts running"), Intent->GetGait(), EVBGait::Run);

	// 아날로그 - 작게 기울이면 걷고, 끝까지 기울이거나 놓으면 뛰며, 키보드는 걷지 않는다
	Intent->ReportMoveInputMagnitude(0.4f, Threshold);
	TestEqual(TEXT("tilt 0.4 walks"), Intent->GetGait(), EVBGait::Walk);
	Intent->ReportMoveInputMagnitude(0.9f, Threshold);
	TestEqual(TEXT("tilt 0.9 runs"), Intent->GetGait(), EVBGait::Run);
	Intent->ReportMoveInputMagnitude(0.4f, Threshold);
	Intent->ReportMoveInputMagnitude(0.f, Threshold);
	TestEqual(TEXT("release runs"), Intent->GetGait(), EVBGait::Run);
	Intent->ReportMoveInputMagnitude(1.f, Threshold);
	TestEqual(TEXT("keyboard runs"), Intent->GetGait(), EVBGait::Run);

	// 토글 - 켜면 걷고 끄면 뛴다. 기울기로 이미 걷는 중에 눌러도 "Walk 의 반대" 가 아니라 토글이다
	Intent->ToggleWalk();
	TestEqual(TEXT("toggle on walks"), Intent->GetGait(), EVBGait::Walk);
	Intent->ReportMoveInputMagnitude(0.4f, Threshold);
	Intent->ToggleWalk();
	TestEqual(TEXT("toggle off with a small tilt still walks"), Intent->GetGait(), EVBGait::Walk);
	Intent->ReportMoveInputMagnitude(0.f, Threshold);
	TestEqual(TEXT("toggle off and released runs"), Intent->GetGait(), EVBGait::Run);

	// sprint 는 토글을 지우고 아날로그 걷기는 남긴다
	Intent->ToggleWalk();
	Intent->ReportMoveInputMagnitude(0.4f, Threshold);
	Intent->StartSprint();
	TestTrue(TEXT("sprint starts"), Intent->GetIsSprinting());
	Intent->StopSprint();
	TestFalse(TEXT("sprint stops"), Intent->GetIsSprinting());
	TestEqual(TEXT("after sprint the small tilt still walks"), Intent->GetGait(), EVBGait::Walk);
	Intent->ReportMoveInputMagnitude(0.f, Threshold);
	TestEqual(TEXT("after sprint the toggle is cleared"), Intent->GetGait(), EVBGait::Run);

	// smart object - 상호작용 중엔 override 가 이기고, 나오면 안에서 누른 토글까지 포함해 이유들이 다시 정한다
	Intent->EnterSmartObjectInputState(true);
	TestEqual(TEXT("interaction walks"), Intent->GetGait(), EVBGait::Walk);
	Intent->RestoreSmartObjectInputState(false, false);
	TestEqual(TEXT("exit with no reason runs"), Intent->GetGait(), EVBGait::Run);
	Intent->ToggleWalk();
	Intent->EnterSmartObjectInputState(false);
	TestEqual(TEXT("interaction override Run beats the toggle"), Intent->GetGait(), EVBGait::Run);
	Intent->RestoreSmartObjectInputState(false, false);
	TestEqual(TEXT("the toggle survives the interaction"), Intent->GetGait(), EVBGait::Walk);

	// movement component 의 tick 은 움직이기 전에 owner 의 gait 로 속도를 정한다 (맨손, 정지 = 앞쪽 구간)
	const FVBMovementProfile& Unarmed = Body->GetMovementConfig()->ResolveProfile(EVBWeaponType::None, false);
	UCharacterMovementComponent* Move = Body->GetCharacterMovement();
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("walking: the movement tick sets the walk speed"), Move->MaxWalkSpeed, static_cast<float>(Unarmed.WalkSpeeds.X));
	Intent->ToggleWalk();
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("running: the movement tick sets the run speed"), Move->MaxWalkSpeed, static_cast<float>(Unarmed.RunSpeeds.X));
	TestEqual(TEXT("crouched speed follows the profile"), Move->MaxWalkSpeedCrouched, static_cast<float>(Unarmed.CrouchSpeed));
	// ramp 가 있는 설정에서 누른 직후는 1단계, SprintRampDuration 을 넘기면 2단계다 (출시 맨손 프로필은 ramp 가 없다)
	// 누르기 전 시계를 0 에서 멀리 두어, 시작 시각을 안 적는 ramp 는 2단계로 읽혀 실패한다
	UVBMovementConfig* RampConfig = NewObject<UVBMovementConfig>(Body);
	RampConfig->StrafeSpeedMapCurve = NewObject<UCurveFloat>(RampConfig);
	RampConfig->UnarmedProfile.bSprintRampEnabled = true;
	RampConfig->UnarmedProfile.SprintRunSpeeds = FVector(600.0, 600.0, 600.0);
	RampConfig->UnarmedProfile.SprintSpeeds = FVector(700.0, 700.0, 700.0);
	UVBSlideMovementComponent* SlideMove = Cast<UVBSlideMovementComponent>(Move);
	if (!TestNotNull(TEXT("body uses the slide movement component"), SlideMove))
	{
		return false;
	}

	SlideMove->SetMovementConfig(RampConfig);
	Body->GetWorld()->TimeSeconds += 5.0f;
	Intent->StartSprint();
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("sprint stage 1 right after the press"), Move->MaxWalkSpeed, 600.0f);
	Body->GetWorld()->TimeSeconds += RampConfig->SprintRampDuration + 0.1f;
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("sprint stage 2 after the ramp"), Move->MaxWalkSpeed, 700.0f);
	Intent->StopSprint();
	return true;
}

// 모든 AVBCharacter 가 gait owner 를 가진다 - GASP NPC 도 같은 owner 입구로 smart object override 를 쓴다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWalkReasonsAssemblyTest, "VB.Unit.Character.WalkReasons.Assembly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWalkReasonsAssemblyTest::RunTest(const FString& Parameters)
{
	for (const UClass* BodyClass : { AVBCharacter::StaticClass(), AVBGASPNPC::StaticClass() })
	{
		const UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(BodyClass->GetDefaultObject<AActor>());
		const FString Name = BodyClass->GetName();
		if (!TestNotNull(FString::Printf(TEXT("%s has the gait owner"), *Name), Intent))
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s gait owner replicates"), *Name), Intent->GetIsReplicated());
		TestTrue(FString::Printf(TEXT("%s gait owner has a stable network name"), *Name), Intent->IsNameStableForNetworking());
	}
	// Gait 와 sprint 는 struct property 하나로 복제되어 RepNotify 한 번에 같이 온다
	// Lyra 의 가속(COND_SimulatedOnly)과 달리 owning client 도 받는다 - 두 값은 서버만 정한다
	const FStructProperty* IntentProperty = CastField<FStructProperty>(
		UVBLocomotionIntentComponent::StaticClass()->FindPropertyByName(TEXT("Intent")));
	TestTrue(TEXT("intent is one replicated struct property"), IntentProperty && IntentProperty->HasAnyPropertyFlags(CPF_Net)
		&& IntentProperty->Struct == FVBReplicatedLocomotionIntent::StaticStruct());
	TestNull(TEXT("the body no longer holds a sprint property"), AVBCharacter::StaticClass()->FindPropertyByName(TEXT("bIsSprinting")));
	return true;
}

// sprint 는 intent owner 에 있고, movement component 는 rotation mode 를 정할 때 읽는다
// Standalone 월드라 server RPC 가 그 자리에서 돈다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLocomotionIntentSprintTest, "VB.Unit.Character.WalkReasons.Sprint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLocomotionIntentSprintTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	AVBCharacter* Body = World->SpawnActor<AVBCharacter>();
	if (!TestNotNull(TEXT("body"), Body))
	{
		return false;
	}

	ON_SCOPE_EXIT { Body->Destroy(); };
	UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(Body);
	if (!TestNotNull(TEXT("intent owner"), Intent))
	{
		return false;
	}

	UCharacterMovementComponent* Move = Body->GetCharacterMovement();
	Move->bOrientRotationToMovement = false;
	Intent->StartSprint();
	Cast<UVBSlideMovementComponent>(Move)->RefreshRotationMode(); // movement component 는 다음 이동 전에 rotation mode 를 정한다
	TestTrue(TEXT("the body turned to free rotation on sprint start"), Move->bOrientRotationToMovement);
	Intent->StartSprint();
	TestTrue(TEXT("the static query sees the sprint"), UVBLocomotionIntentComponent::IsSprintingFor(Body));
	Intent->StopSprint();
	Cast<UVBSlideMovementComponent>(Move)->RefreshRotationMode(); // movement component 는 다음 이동 전에 rotation mode 를 정한다
	TestFalse(TEXT("the body returned to strafe on sprint stop"), Move->bOrientRotationToMovement);

	Intent->ToggleWalk();
	Intent->ToggleWalk();
	TestFalse(TEXT("an actor without the owner is not sprinting"), UVBLocomotionIntentComponent::IsSprintingFor(nullptr));
	return true;
}

// aim 과 strafe 도 intent owner 에 있다. 조준은 같은 호출에서 sprint 를 멈춘다
// smart object 진입은 sprint, aim, strafe 를 끄고, 나오면 저장한 둘을 되돌린다
// 대조: SetWantsToAim 에서 SetSprinting(false) 를 빼면 "aim stops the sprint" 가 실패한다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLocomotionIntentAimStrafeTest, "VB.Unit.Character.WalkReasons.AimStrafe",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLocomotionIntentAimStrafeTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("bWantsToAim"), TEXT("bWantsToStrafe") })
	{
		const FProperty* Property = UVBLocomotionIntentComponent::StaticClass()->FindPropertyByName(Name);
		TestTrue(FString::Printf(TEXT("%s replicates from the intent owner"), Name), Property && Property->HasAnyPropertyFlags(CPF_Net));
	}

	TestNull(TEXT("the body keeps no aim copy"), AVBCharacter::StaticClass()->FindPropertyByName(TEXT("bWantsToAim")));
	TestNull(TEXT("the body keeps no strafe copy"), AVBCharacter::StaticClass()->FindPropertyByName(TEXT("WantsToStrafe")));

	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	AVBCharacter* Body = World->SpawnActor<AVBCharacter>();
	if (!TestNotNull(TEXT("body"), Body))
	{
		return false;
	}

	ON_SCOPE_EXIT { Body->Destroy(); };
	UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(Body);
	if (!TestNotNull(TEXT("intent owner"), Intent))
	{
		return false;
	}

	TestTrue(TEXT("strafe starts on (designer default)"), Intent->WantsToStrafe());
	UCharacterMovementComponent* Move = Body->GetCharacterMovement();

	// strafe 를 먼저 꺼서, 아래 strafe 회전이 aim 에서만 나오게 한다
	Intent->SetWantsToStrafe(false);
	Cast<UVBSlideMovementComponent>(Move)->RefreshRotationMode(); // movement component 는 다음 이동 전에 rotation mode 를 정한다
	TestTrue(TEXT("no strafe, no aim, no lock, no combat: free rotation"), Move->bOrientRotationToMovement);
	Intent->StartSprint();
	Intent->SetWantsToAim(true);
	TestTrue(TEXT("aim is on"), Intent->WantsToAim());
	TestFalse(TEXT("aim stops the sprint"), Intent->GetIsSprinting());
	Cast<UVBSlideMovementComponent>(Move)->RefreshRotationMode(); // movement component 는 다음 이동 전에 rotation mode 를 정한다
	TestFalse(TEXT("aim alone holds strafe rotation"), Move->bOrientRotationToMovement);
	TestTrue(TEXT("the static query sees the aim"), UVBLocomotionIntentComponent::IsAimingFor(Body));
	Intent->SetWantsToAim(true);
	Intent->SetWantsToAim(false);
	Cast<UVBSlideMovementComponent>(Move)->RefreshRotationMode(); // movement component 는 다음 이동 전에 rotation mode 를 정한다
	TestTrue(TEXT("aim off returns to free rotation"), Move->bOrientRotationToMovement);
	Intent->SetWantsToStrafe(true);
	Cast<UVBSlideMovementComponent>(Move)->RefreshRotationMode(); // movement component 는 다음 이동 전에 rotation mode 를 정한다
	TestFalse(TEXT("strafe alone holds strafe rotation"), Move->bOrientRotationToMovement);

	Intent->SetWantsToAim(true);
	Intent->StartSprint();
	Intent->EnterSmartObjectInputState(true);
	TestFalse(TEXT("entry stops the sprint"), Intent->GetIsSprinting());
	TestFalse(TEXT("entry drops the aim"), Intent->WantsToAim());
	TestFalse(TEXT("entry drops the strafe"), Intent->WantsToStrafe());
	Intent->RestoreSmartObjectInputState(true, true);
	TestTrue(TEXT("exit restores the strafe"), Intent->WantsToStrafe());
	TestTrue(TEXT("exit restores the aim"), Intent->WantsToAim());
	TestFalse(TEXT("an actor without the owner is not aiming"), UVBLocomotionIntentComponent::IsAimingFor(nullptr));
	return true;
}

// GASP NPC 는 owner 입구를 통해 smart object 상호작용 중엔 걷고 끝나면 다시 뛴다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWalkReasonsNPCTest, "VB.Unit.Character.WalkReasons.NPC",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWalkReasonsNPCTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVBGASPNPC* NPC = World->SpawnActor<AVBGASPNPC>(Params);
	if (!TestNotNull(TEXT("npc"), NPC))
	{
		return false;
	}

	ON_SCOPE_EXIT { NPC->Destroy(); };
	UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(NPC);
	if (!TestNotNull(TEXT("npc gait owner"), Intent))
	{
		return false;
	}

	Intent->EnterSmartObjectInputState(true);
	TestEqual(TEXT("npc walks inside the interaction"), Intent->GetGait(), EVBGait::Walk);
	Intent->RestoreSmartObjectInputState(false, false);
	TestEqual(TEXT("npc runs after the interaction"), Intent->GetGait(), EVBGait::Run);
	return true;
}

#if WITH_EDITOR
// 문턱은 (0, 1] 안이어야 한다 - 밖이면 패드가 절대 안 걷거나 늘 걷는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWalkReasonsConfigTest, "VB.Unit.Character.WalkReasons.ConfigValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWalkReasonsConfigTest::RunTest(const FString& Parameters)
{
	UVBMovementConfig* Config = NewObject<UVBMovementConfig>();
	Config->StrafeSpeedMapCurve = NewObject<UCurveFloat>(Config); // 출시 설정은 방향 커브를 가진다 (IsDataValid 가 요구한다)
	FDataValidationContext Default;
	TestTrue(TEXT("default 0.7 is valid"), Config->IsDataValid(Default) != EDataValidationResult::Invalid);
	TestEqual(TEXT("default has no errors"), Default.GetNumErrors(), 0);
	for (const float Bad : { 0.f, -0.1f, 1.5f })
	{
		Config->AnalogWalkRunThreshold = Bad;
		FDataValidationContext Context;
		TestTrue(FString::Printf(TEXT("threshold %.2f is invalid"), Bad), Config->IsDataValid(Context) == EDataValidationResult::Invalid);
	}

	Config->AnalogWalkRunThreshold = 1.f;
	FDataValidationContext One;
	TestTrue(TEXT("threshold 1 is valid (only a full tilt runs)"), Config->IsDataValid(One) != EDataValidationResult::Invalid);
	Config->StrafeSpeedMapCurve = nullptr;
	FDataValidationContext NoCurve;
	TestTrue(TEXT("a config without the direction speed curve is invalid"), Config->IsDataValid(NoCurve) == EDataValidationResult::Invalid);
	return true;
}
#endif // WITH_EDITOR
#endif

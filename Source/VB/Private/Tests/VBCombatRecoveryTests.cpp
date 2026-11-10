#if WITH_DEV_AUTOMATION_TESTS
#include "Curves/CurveFloat.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBComboRuntime.h"
#include "AbilitySystem/Abilities/VBGA_LightAttack.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/VBANS_ComboWindow.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBMotionWarpingNames.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "Animation/VBGASPLocomotionStateLibrary.h"
#include "Animation/VBGASPLocomotionUpdateLibrary.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBSlideMovementComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Chooser.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBMovementConfig.h"
#include "Misc/DataValidation.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "HAL/IConsoleManager.h"
#include "MotionWarpingComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatComboWindowOwnershipTest, "VB.Combat.Recovery.ComboWindowOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBCombatComboWindowOwnershipTest::RunTest(const FString& Parameters)
{
	auto* Config = NewObject<UVBAttackConfig>();
	Config->bAutoChainCombo = false;
	auto* OldWindow = NewObject<UVBANS_ComboWindow>();
	auto* NewWindow = NewObject<UVBANS_ComboWindow>();
	FVBComboRuntime Combo;
	Combo.OpenWindow(OldWindow, Config, 0.0);
	Combo.OpenWindow(NewWindow, Config, 0.0);
	Combo.NoteInput(FVector2D::ZeroVector, 0.0);
	TestFalse(TEXT("Late end leaves nothing for the ability to continue"), Combo.CloseWindow(OldWindow, 1.0f));
	TestTrue(TEXT("Late end of previous swing cannot close the new combo window"), Combo.IsWindowOpen());
	TestTrue(TEXT("Late end cannot consume the next swing's buffered click"), Combo.IsAttackSaved());
	TestFalse(TEXT("Interrupted window leaves nothing for the ability to continue"), Combo.CloseWindow(NewWindow, 0.0f));
	TestFalse(TEXT("Interrupted current window closes"), Combo.IsWindowOpen());
	TestTrue(TEXT("Interrupted window cannot jump a section"), Combo.IsAttackSaved());
	Combo.ResetForActivation();
	Combo.OpenWindow(NewWindow, Config, 0.0);
	TestTrue(TEXT("Natural end lets the ability continue"), Combo.CloseWindow(NewWindow, 1.0f));
	TestFalse(TEXT("Natural end closes its own window"), Combo.IsWindowOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceWorldCollisionTest, "VB.Combat.MeleeTrace.WorldCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTraceWorldCollisionTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Trace physics world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AActor* Owner = World->SpawnActor<AActor>();
	auto* Root = NewObject<USceneComponent>(Owner); Owner->SetRootComponent(Root); Root->RegisterComponent();
	auto* Trace = NewObject<UVBMeleeTraceComponent>(Owner); Trace->RegisterComponent();
	auto MakeBox = [&](FVector Position, ECollisionChannel Channel, ECollisionResponse Response)
	{
		auto* Actor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(2, 2, 20)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(Channel, Response);
		Box->RegisterComponent(); Actor->SetActorLocation(Position); return Box;
	};
	UBoxComponent* Target = MakeBox(FVector(500, 0, 50), ECC_GameTraceChannel3, ECR_Overlap);
	UVBMeleeTraceComponent::FWindow Window;
	UVBMeleeTraceComponent::FPart& Blade = Window.Parts.AddDefaulted_GetRef();
	Blade.Shape.Bone = TEXT("root"); Blade.Shape.TipLocal = FVector(500, 0, 0); Blade.Shape.Radius = 5;
	Blade.PrevBone = FTransform(FVector(0, 0, 50)); Blade.bHasPrev = true;
	const FTransform HalfTurn(FQuat(FVector::UpVector, PI), FVector(0, 0, 50));
	TArray<FHitResult> Hits;
	Trace->GatherWindowHits(Window, {HalfTurn}, 1.f / 15, Hits);
	TestEqual(TEXT("Captured first pose remains hittable even when rotation sampling reaches its cap"), Hits.Num(), 1);
	if (Hits.Num())
	{
		TestTrue(TEXT("Overlap becomes a gameplay contact"), Hits[0].bBlockingHit);
	}

	Target->GetOwner()->SetActorLocation(FVector(-500, 0, 50)); Window.LastHitTime.Reset(); Hits.Reset();
	Trace->GatherWindowHits(Window, {HalfTurn}, 1.f / 15, Hits);
	TestEqual(TEXT("Captured final pose is queried"), Hits.Num(), 1);
	Hits.Reset(); Trace->GatherWindowHits(Window, {HalfTurn}, 1.f / 60, Hits);
	TestEqual(TEXT("A stationary window does not damage the same actor twice"), Hits.Num(), 0);
	Window.RehitInterval = 0.2f; Window.Elapsed += 0.21f;
	Hits.Reset(); Trace->GatherWindowHits(Window, {HalfTurn}, 0, Hits);
	TestEqual(TEXT("Authored multihit interval can rehit"), Hits.Num(), 1);
	UBoxComponent* Wall = MakeBox(FVector(-250, 0, 50), ECC_Visibility, ECR_Block);
	Wall->SetBoxExtent(FVector(5, 40, 100)); Window.LastHitTime.Reset(); Hits.Reset();
	Trace->GatherWindowHits(Window, {HalfTurn}, 0, Hits);
	TestEqual(TEXT("A wall between attacker and contact rejects damage"), Hits.Num(), 0);

	// 다중 부위 창: 한 프레임에 두 부위가 한 대상에 닿아도 한 번만 보고한다(이미 맞음은 부위가 아니라 창이 든다).
	//   대조: 단일 부위 창 둘은 같은 대상을 두 번 보고한다.
	Wall->GetOwner()->SetActorLocation(FVector(0, 0, -5000));
	Target->GetOwner()->SetActorLocation(FVector(0, 0, 50));
	UVBMeleeTraceComponent::FPart Foot;
	Foot.Shape.Bone = TEXT("root"); Foot.Shape.Radius = 30; // BaseLocal == TipLocal: 부위 원점을 감싼 구
	Foot.PrevBone = FTransform(FVector(0, 0, 50)); Foot.bHasPrev = true;
	const FTransform Still(FVector(0, 0, 50));
	UVBMeleeTraceComponent::FWindow Feet;
	Feet.Parts = {Foot, Foot};
	Hits.Reset(); Trace->GatherWindowHits(Feet, {Still, Still}, 0, Hits);
	TestEqual(TEXT("Two parts touching one target in one frame report it once"), Hits.Num(), 1);
	if (Hits.Num())
	{
		TestEqual(TEXT("The hit names the contacting part's bone"), Hits[0].MyBoneName, FName(TEXT("root")));
	}

	Hits.Reset(); Trace->GatherWindowHits(Feet, {Still, Still}, 1.f / 60, Hits);
	TestEqual(TEXT("Neither part rehits the target later in the same window"), Hits.Num(), 0);
	UVBMeleeTraceComponent::FWindow LeftOnly;
	LeftOnly.Parts = {Foot};
	UVBMeleeTraceComponent::FWindow RightOnly;
	RightOnly.Parts = {Foot};
	Hits.Reset();
	Trace->GatherWindowHits(LeftOnly, {Still}, 0, Hits);
	Trace->GatherWindowHits(RightOnly, {Still}, 0, Hits);
	TestEqual(TEXT("Control: two single-part windows report the same target twice"), Hits.Num(), 2);

	// 다른 뼈 둘, 재타격 간격 0.2초: 창 시계는 부위마다가 아니라 프레임마다 한 번 간다(부위 시계면 일찍 재타격한다).
	UVBMeleeTraceComponent::FPart OtherFoot = Foot;
	OtherFoot.Shape.Bone = TEXT("foot_r");
	UVBMeleeTraceComponent::FWindow Pair;
	Pair.Parts = {Foot, OtherFoot};
	Pair.RehitInterval = 0.2f;
	Hits.Reset(); Trace->GatherWindowHits(Pair, {Still, Still}, 0, Hits);
	TestEqual(TEXT("Two bones on one target: one hit"), Hits.Num(), 1);
	// 시험은 한 엔진 프레임 안에서 돈다. 마지막 샘플 프레임을 지워 다음 프레임을 흉내 낸다.
	Pair.LastSampleFrame = MAX_uint64;
	Hits.Reset(); Trace->GatherWindowHits(Pair, {Still, Still}, 0.15f, Hits);
	TestEqual(TEXT("0.15 s into a 0.2 s rehit interval: no rehit"), Hits.Num(), 0);
	Pair.LastSampleFrame = MAX_uint64;
	Hits.Reset(); Trace->GatherWindowHits(Pair, {Still, Still}, 0.06f, Hits);
	TestEqual(TEXT("0.21 s: one rehit for the window, not one per part"), Hits.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatAnimationRecoveryTest, "VB.Combat.Recovery.AnimationIntent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBCombatAnimationRecoveryTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Animation fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACharacter* Character = World->SpawnActor<ACharacter>();
	auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	auto* Montage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/VB/Animations/Montage/AM_BigSword_LightAttack.AM_BigSword_LightAttack"));
	if (!TestNotNull(TEXT("Player skeleton"), Mesh) || !TestNotNull(TEXT("Attack montage"), Montage))
	{
		return false;
	}

	Character->GetMesh()->SetSkeletalMeshAsset(Mesh); Character->GetMesh()->SetAnimInstanceClass(UVBAnimInstance::StaticClass());
	auto* Anim = Cast<UVBAnimInstance>(Character->GetMesh()->GetAnimInstance());
	if (!TestNotNull(TEXT("Actual native anim instance"), Anim))
	{
		return false;
	}

	auto* ASC = NewObject<UAbilitySystemComponent>(Character); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Character, Character);
	auto* CMC = Character->GetCharacterMovement(); CMC->SetMovementMode(MOVE_Walking); CMC->Velocity = FVector(300, 0, 0);
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	TestTrue(TEXT("Authored attack montage can play"), Anim->Montage_Play(Montage) > 0);
	TestFalse(TEXT("Other root motion is not an attack"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
	TestTrue(TEXT("Grounded attack root motion without input is separate from locomotion"),
		UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	Anim->bSuppressAttackLocomotion = true; Anim->UpdateEssentialValues(1.f / 60, Character, CMC);
	TestEqual(TEXT("Locomotion speed is zero while the actual capsule retains attack speed"), Anim->Speed2D, 0.0f);
	TestEqual(TEXT("Attack lunge is preserved"), CMC->Velocity.X, 300.0);
	FTransformTrajectory History, Generated; float Yaw = 0;
	UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(Anim, FPoseSearchTrajectoryData(), 1.f / 60, true, History, Yaw,
		Generated);
	FVector Future;
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Generated, 0.4f, 0.5f, Future, false);
	TestTrue(TEXT("Future trajectory cannot request a running recovery"), Future.IsNearlyZero());
	TestEqual(TEXT("GASP sampling contract is retained"), Generated.Samples.Num(), 46);
	CMC->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("Airborne movement is preserved"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	CMC->SetMovementMode(MOVE_Walking);
	const auto* Accel = FindFProperty<FStructProperty>(UCharacterMovementComponent::StaticClass(), TEXT("Acceleration"));
	if (!TestNotNull(TEXT("CMC input acceleration fixture"), Accel))
	{
		return false;
	}

	*Accel->ContainerPtrToValuePtr<FVector>(CMC) = FVector(100, 0, 0);
	TestFalse(TEXT("Movement input immediately resumes locomotion"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	Anim->bIsSprinting = true; Anim->LocomotionState = EVBLocomotionState::ArmedCombat; Anim->bIsLockedOn = true;
	Anim->UpdateStates(); TestTrue(TEXT("Armed sprint follows CMC free rotation even while locked on"),
		Anim->RotationMode == EVBRotationMode::OrientToMovement);
	Anim->bIsSprinting = false; Anim->UpdateStates(); TestTrue(TEXT("Lock-on returns after sprint release"),
		Anim->RotationMode == EVBRotationMode::OrientToTarget);
	Anim->bIsLockedOn = false; Anim->UpdateStates(); TestTrue(TEXT("Armed jog remains strafe"),
		Anim->RotationMode == EVBRotationMode::Strafe);
	Anim->RotationMode_LastFrame = EVBRotationMode::OrientToMovement;
	Anim->MovementMode = Anim->MovementMode_LastFrame = EVBMovementMode::Grounded;
	Anim->MMState = Anim->MMState_LastFrame = EVBMMState::Moving;
	Anim->Gait = Anim->Gait_LastFrame; Anim->Stance = Anim->Stance_LastFrame;
	TestTrue(TEXT("Rotation mode handoff refreshes the database search"),
		Anim->GetMMInterruptMode() == EPoseSearchInterruptMode::InterruptOnDatabaseChange);
	*Accel->ContainerPtrToValuePtr<FVector>(CMC) = FVector::ZeroVector;
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("Root-motion instance before recovery"), Instance))
	{
		return false;
	}

	Instance->UpdateWeight(0.1f);
	Anim->Montage_Stop(0.25f, Montage);
	TestNull(TEXT("Engine clears root-motion owner at blend-out start"), Character->GetRootMotionAnimMontageInstance());
	TestTrue(TEXT("Attack intent remains idle throughout weighted recovery"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	AVBCharacter* RecoveryCharacter = World->SpawnActor<AVBCharacter>();
	RecoveryCharacter->GetMesh()->SetSkeletalMeshAsset(Mesh);
	RecoveryCharacter->GetMesh()->SetAnimInstanceClass(UVBAnimInstance::StaticClass());
	auto* RecoveryAnim = RecoveryCharacter->GetMesh()->GetAnimInstance();
	RecoveryCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (!TestTrue(TEXT("Recovery montage can play"), RecoveryAnim->Montage_Play(Montage) > 0))
	{
		return false;
	}

	RecoveryAnim->GetActiveInstanceForMontage(Montage)->UpdateWeight(0.1f);
	RecoveryAnim->Montage_Stop(0.25f, Montage);
	auto* RecoveryMove = Cast<UVBSlideMovementComponent>(RecoveryCharacter->GetCharacterMovement());
	if (!TestNotNull(TEXT("Character uses the slide movement component"), RecoveryMove))
	{
		return false;
	}

	RecoveryMove->StopResidualAttackMotion(0.03f, Montage);
	TestEqual(TEXT("Blending instance suppresses the last extracted attack translation"),
		RecoveryCharacter->GetAnimRootMotionTranslationScale(), 0.0f);
	// 이동 컴포넌트가 자기 틱에서 억제를 쥔다. 컨트롤러가 없어 이 틱은 아무것도 움직이지 않는다.
	RecoveryMove->Velocity = FVector(300.0, 0.0, 0.0);
	RecoveryMove->TickComponent(0.001f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Suppression zeroes horizontal velocity on the movement tick"), RecoveryMove->Velocity.IsNearlyZero());
	TestEqual(TEXT("Suppression keeps translation scale 0"), RecoveryCharacter->GetAnimRootMotionTranslationScale(), 0.0f);
	// 다음 틱의 입력으로 풀린다.
	*Accel->ContainerPtrToValuePtr<FVector>(RecoveryMove) = FVector(100, 0, 0);
	RecoveryMove->TickComponent(0.001f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Movement input releases the suppression"), RecoveryCharacter->GetAnimRootMotionTranslationScale(), 1.0f);
	*Accel->ContainerPtrToValuePtr<FVector>(RecoveryMove) = FVector::ZeroVector;
	// 순서: 검사는 이번 프레임 이동 뒤에 돈다. 이동 전에 남은 옛 가속으로 억제가 풀리면 안 된다.
	RecoveryMove->StopResidualAttackMotion(0.03f, Montage);
	if (!TestEqual(TEXT("Suppression restarted"), RecoveryCharacter->GetAnimRootMotionTranslationScale(), 0.0f))
	{
		return false;
	}

	*Accel->ContainerPtrToValuePtr<FVector>(RecoveryMove) = FVector(100, 0, 0);
	RecoveryMove->bRunPhysicsWithNoController = true;
	RecoveryMove->TickComponent(0.016f, LEVELTICK_All, nullptr);
	RecoveryMove->bRunPhysicsWithNoController = false;
	TestTrue(TEXT("This tick's move cleared the stale acceleration"), RecoveryMove->GetCurrentAcceleration().IsNearlyZero());
	TestEqual(TEXT("Suppression check runs after the move (stale acceleration does not release)"),
		RecoveryCharacter->GetAnimRootMotionTranslationScale(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRecoveryAssetsTest, "VB.Combat.Recovery.AuthoredAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBCombatRecoveryAssetsTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Weapon : {TEXT("BigSword"), TEXT("Blade"), TEXT("Fighter")})
	{
		const FString Path = FString::Printf(TEXT("/Game/VB/Data/DA_Attack_%s_Light.DA_Attack_%s_Light"), Weapon, Weapon);
		const auto* Config = LoadObject<UVBAttackConfig>(nullptr, *Path);
		if (!TestNotNull(Weapon, Config))
		{
			continue;
		}

		TestFalse(TEXT("A single click retains authored recovery"), Config->bCancelRecoveryAtWindowClose);
		const UAnimMontage* Montage = Config->AttackMontage;
		if (!TestNotNull(TEXT("Attack montage"), Montage))
		{
			continue;
		}

		for (const FCompositeSection& Section : Montage->CompositeSections)
		{
			TestTrue(TEXT("No montage section automatically skips into another swing"), Section.NextSectionName.IsNone());
		}
	}

	UClass* Class = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/VB/Blueprints/Characters/ABP_VBCharacter_58.ABP_VBCharacter_58_C"));
	if (!TestNotNull(TEXT("Actual player AnimBP"), Class))
	{
		return false;
	}

	const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(Class);
	bool bFound = false;
	for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
	{
		if (Property->Struct == FAnimNode_Slot::StaticStruct())
		{
			const auto* Slot = Property->ContainerPtrToValuePtr<FAnimNode_Slot>(Class->GetDefaultObject());
			if (Slot->SlotName == TEXT("DefaultSlot"))
			{
				bFound = true; TestTrue(TEXT("Locomotion keeps updating while the attack owns the full pose"),
					Slot->bAlwaysUpdateSourcePose);
			}
		}
	}

	TestTrue(TEXT("Full body attack slot is wired"), bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMouseLookTargetFacingTest, "VB.Animation.CMC.MouseLookTargetFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMouseLookTargetFacingTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Mouse-look movement fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Config = LoadObject<UVBMovementConfig>(nullptr, TEXT("/Game/VB/Data/DA_Movement_Default.DA_Movement_Default"));
	auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	auto* Montage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/VB/Animations/Montage/AM_BigSword_LightAttack.AM_BigSword_LightAttack"));
	if (!TestNotNull(TEXT("Authored movement config"), Config) || !TestNotNull(TEXT("Player skeleton"), Mesh)
		|| !TestNotNull(TEXT("Authored root-motion attack"), Montage))
	{
		return false;
	}

	TestTrue(TEXT("GASP grounded target facing uses authored instant rotation"), Config->DefaultRotationRate.Yaw < 0.0);
	TestTrue(TEXT("Armed idle retains a finite authored rotation rate"), Config->ArmedGroundedRotationRate.Yaw > 0.0);
	IConsoleVariable* FacingFromMesh = IConsoleManager::Get().FindConsoleVariable(TEXT("a.PoseSearchTrajectory.FacingFromMesh"));
	if (!TestNotNull(TEXT("Actual trajectory facing policy"), FacingFromMesh))
	{
		return false;
	}

	TestEqual(TEXT("Production trajectory starts from mesh facing"), FacingFromMesh->GetInt(), 1);

	const auto* ConfigProperty = FindFProperty<FObjectPropertyBase>(AVBCharacter::StaticClass(), TEXT("MovementConfig"));
	const auto* AccelerationProperty = FindFProperty<FStructProperty>(UCharacterMovementComponent::StaticClass(), TEXT("Acceleration"));
	const auto* AnalogProperty = FindFProperty<FFloatProperty>(UCharacterMovementComponent::StaticClass(), TEXT("AnalogInputModifier"));
	const auto* StateProperty = FindFProperty<FEnumProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("LocomotionState"));
	const auto* WeaponProperty = FindFProperty<FEnumProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("CurrentWeaponType"));
	if (!TestNotNull(TEXT("Character config fixture property"), ConfigProperty)
		|| !TestNotNull(TEXT("CMC acceleration fixture property"), AccelerationProperty)
		|| !TestNotNull(TEXT("CMC analog input fixture property"), AnalogProperty)
		|| !TestNotNull(TEXT("Weapon state fixture property"), StateProperty)
		|| !TestNotNull(TEXT("Weapon type fixture property"), WeaponProperty))
	{
		return false;
	}

	AVBCharacter* Character = World->SpawnActor<AVBCharacter>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Player fixture"), Character) || !TestNotNull(TEXT("Controller fixture"), Controller))
	{
		return false;
	}
	// 컨트롤러 참조만 연결한다. Possess를 호출하면 게임 재시작과 카메라 설정까지 실행된다.
	Character->SetController(Controller);
	ConfigProperty->SetObjectPropertyValue_InContainer(Character, Config);
	Character->GetMesh()->SetSkeletalMeshAsset(Mesh);
	Character->GetMesh()->SetAnimInstanceClass(UVBAnimInstance::StaticClass());
	auto* Anim = Cast<UVBAnimInstance>(Character->GetMesh()->GetAnimInstance());
	if (!TestNotNull(TEXT("Native animation route fixture"), Anim))
	{
		return false;
	}

	Anim->bUseGASPLocomotion = true;
	auto* Weapons = UVBWeaponStateComponent::FindFor(Character);
	if (!TestNotNull(TEXT("Weapon state fixture"), Weapons))
	{
		return false;
	}

	StateProperty->GetUnderlyingProperty()->SetIntPropertyValue(StateProperty->ContainerPtrToValuePtr<void>(Weapons),
		static_cast<uint64>(EVBLocomotionState::ArmedCombat));
	Cast<UVBSlideMovementComponent>(Character->GetCharacterMovement())->RefreshRotationMode();
	auto* CMC = Character->GetCharacterMovement();
	CMC->SetMovementMode(MOVE_Walking);
	CMC->MaxAcceleration = Config->MaxAcceleration;
	CMC->GroundFriction = Config->GroundFriction;
	CMC->BrakingDecelerationWalking = Config->BrakingDecelerationWalking;
	AnalogProperty->SetPropertyValue_InContainer(CMC, 1.0f);
	constexpr float FrameSeconds = 1.f / 60;
	auto SetAcceleration = [&](const FVector& Value)
	{
		*AccelerationProperty->ContainerPtrToValuePtr<FVector>(CMC) = Value;
	};
	auto ResetMouseTurn = [&]
	{
		Character->SetActorRotation(FRotator::ZeroRotator);
		Controller->SetControlRotation(FRotator(0, 90, 0));
	};
	for (EVBWeaponType Weapon : { EVBWeaponType::BigSword, EVBWeaponType::Blade, EVBWeaponType::Fighter })
	{
		WeaponProperty->GetUnderlyingProperty()->SetIntPropertyValue(WeaponProperty->ContainerPtrToValuePtr<void>(Weapons),
			static_cast<uint64>(Weapon));
		CMC->MaxWalkSpeed = Config->ResolveProfile(Weapon, true).RunSpeeds.X;
		ResetMouseTurn();
		SetAcceleration(FVector(0, Config->MaxAcceleration, 0)); // W만 유지한 채 새 카메라 방향으로 이동한다.
		CMC->Velocity = FVector(CMC->MaxWalkSpeed, 0, 0); // 회전 직전의 실제 속도 관성을 유지한다.
		CMC->RotationRate = Config->ArmedGroundedRotationRate;
		CMC->PhysicsRotation(FrameSeconds);
		TestTrue(TEXT("The first moving physics frame updates its target-facing rate"),
			CMC->RotationRate.Equals(Config->DefaultRotationRate));
		TestTrue(TEXT("Armed W plus mouse aligns the capsule target this frame"),
			FMath::Abs(Character->GetActorRotation().Yaw - 90.0) < 0.01);
		FPoseSearchTrajectoryData Data;
		Data.RotateTowardsMovementSpeed = Data.MaxControllerYawRate = Data.BendVelocityTowardsAcceleration = 0;
		FTransformTrajectory History, Generated;
		float LastYaw = 90;
		UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(Anim, Data, FrameSeconds, false, History, LastYaw, Generated);
		FVector Future;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Generated, 0.4f, 0.5f, Future, false);
		const FVector LocalFuture = Character->GetActorRotation().UnrotateVector(Future);
		const double FutureYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalFuture.Y, LocalFuture.X));
		TestTrue(FString::Printf(TEXT("Weapon %d future query stays forward after mouse-only steering (yaw %.2f)"),
			static_cast<int32>(Weapon), FutureYaw),
			Future.Size2D() > 1.0 && FMath::Abs(FutureYaw) < 15.0);
		FTransformTrajectorySample Sample;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleAtTime(Generated, 0.5f, Sample, false);
		const FQuat PredictedActorFacing = UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleTransform(Sample).GetRotation()
			* Character->GetMesh()->GetRelativeRotation().Quaternion().Inverse();
		TestTrue(TEXT("Zero-rate GASP prediction retains the aligned actor-facing reference"),
			FMath::Abs(FRotator::NormalizeAxis(PredictedActorFacing.Rotator().Yaw - 90.0)) < 0.01);
	}

	for (float WorldXSign : { -1.f, 1.f })
	{
		// 카메라 Yaw=90일 때 실제 D는 월드 -X, A는 월드 +X 방향이다.
		Character->SetActorRotation(FRotator(0, 90, 0));
		Controller->SetControlRotation(FRotator(0, 90, 0));
		SetAcceleration(FVector(WorldXSign * Config->MaxAcceleration, 0, 0));
		CMC->Velocity = FVector(WorldXSign * CMC->MaxWalkSpeed, 0, 0);
		CMC->PhysicsRotation(FrameSeconds);
		TestTrue(TEXT("Actual lateral input preserves camera-facing strafe"), FMath::Abs(Character->GetActorRotation().Yaw - 90.0) < 0.01);
		FPoseSearchTrajectoryData StrafeData;
		StrafeData.RotateTowardsMovementSpeed = StrafeData.MaxControllerYawRate = StrafeData.BendVelocityTowardsAcceleration = 0;
		FTransformTrajectory StrafeHistory, StrafeGenerated;
		float StrafeLastYaw = 90;
		UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(Anim, StrafeData, FrameSeconds, false, StrafeHistory,
			StrafeLastYaw, StrafeGenerated);
		FVector StrafeFuture;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(StrafeGenerated, 0.4f, 0.5f, StrafeFuture, false);
		const FVector LocalStrafe = Character->GetActorRotation().UnrotateVector(StrafeFuture);
		const double StrafeYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalStrafe.Y, LocalStrafe.X));
		const double ExpectedYaw = -WorldXSign * 90.0;
		TestTrue(FString::Printf(TEXT("Actual %s input keeps its lateral future query (yaw %.2f)"),
			WorldXSign < 0 ? TEXT("D") : TEXT("A"), StrafeYaw),
			StrafeFuture.Size2D() > 1.0 && FMath::Abs(FRotator::NormalizeAxis(StrafeYaw - ExpectedYaw)) < 15.0);
	}

	Anim->bUseGASPLocomotion = false;
	SetAcceleration(FVector(0, Config->MaxAcceleration, 0));
	CMC->Velocity = FVector(CMC->MaxWalkSpeed, 0, 0);
	ResetMouseTurn(); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Legacy animation retains the armed finite rate"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	TestTrue(TEXT("Legacy physics rotation still advances by its finite frame allowance"),
		FMath::Abs(Character->GetActorRotation().Yaw - FMath::Min(Config->ArmedGroundedRotationRate.Yaw * FrameSeconds, 90.0)) < 0.01);
	FPoseSearchTrajectoryData LegacyData;
	LegacyData.RotateTowardsMovementSpeed = LegacyData.MaxControllerYawRate = LegacyData.BendVelocityTowardsAcceleration = 0;
	FTransformTrajectory LegacyHistory, LegacyGenerated;
	float LegacyLastYaw = 90;
	UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(Anim, LegacyData, FrameSeconds, false, LegacyHistory,
		LegacyLastYaw, LegacyGenerated);
	FVector LegacyFuture;
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(LegacyGenerated, 0.4f, 0.5f, LegacyFuture, false);
	const FVector LegacyLocalFuture = Character->GetActorRotation().UnrotateVector(LegacyFuture);
	const double LegacyFutureYaw = FMath::RadiansToDegrees(FMath::Atan2(LegacyLocalFuture.Y, LegacyLocalFuture.X));
	// 같은 입력, 예측 설정에서 지연된 캡슐 기준은 미래 이동을 측면 방향으로 읽는다.
	TestTrue(FString::Printf(TEXT("Finite facing preserves the prior lateral future-query counterexample (yaw %.2f)"), LegacyFutureYaw),
		LegacyFuture.Size2D() > 1.0 && FMath::Abs(LegacyFutureYaw) > 60.0);
	Anim->bUseGASPLocomotion = true;
	SetAcceleration(FVector::ZeroVector); CMC->Velocity = FVector(250, 0, 0);
	CMC->RotationRate = Config->DefaultRotationRate;
	ResetMouseTurn(); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("The first released-input frame restores armed idle rate despite residual velocity"),
		CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	SetAcceleration(FVector(0, Config->MaxAcceleration, 0));
	CMC->SetMovementMode(MOVE_Falling); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Airborne configured rotation is preserved"), CMC->RotationRate.Equals(Config->AirborneRotationRate));
	CMC->SetMovementMode(MOVE_Flying); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Flying does not enter the grounded target-facing override"),
		CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	CMC->SetMovementMode(MOVE_Walking);
	auto* Warping = Character->FindComponentByClass<UMotionWarpingComponent>();
	if (!TestNotNull(TEXT("Motion-warping ownership fixture"), Warping))
	{
		return false;
	}

	Warping->AddOrUpdateWarpTargetFromTransform(VBMotionWarpingNames::AttackTarget, FTransform(FVector(100, 0, 0)));
	CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Feature-owned facing retains its prior rotation policy"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	Warping->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
	auto Source = MakeShared<FRootMotionSource_ConstantForce>();
	Source->InstanceName = TEXT("MouseLookTestRootMotion"); Source->Duration = 1; Source->StartTime = 0; Source->Force = FVector(100, 0, 0);
	CMC->ApplyRootMotionSource(Source); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Root-motion sources retain their prior rotation policy"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	CMC->CurrentRootMotion.Clear();
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	if (!TestTrue(TEXT("Attack ownership fixture can play"), Anim->Montage_Play(Montage) > 0))
	{
		return false;
	}

	FAnimMontageInstance* Attack = Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("Active root-motion attack fixture"), Attack))
	{
		return false;
	}

	Attack->UpdateWeight(0.1f); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Active attack does not gain locomotion's instant rotation"),
		CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	Anim->Montage_Stop(0.25f, Montage); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Weighted attack blend-out still keeps its existing rotation policy"),
		CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));

	UClass* AnimClass = LoadClass<UVBAnimInstance>(nullptr,
		TEXT("/Game/VB/Blueprints/Characters/ABP_VBCharacter_58.ABP_VBCharacter_58_C"));
	if (!TestNotNull(TEXT("Actual player animation class fallback"), AnimClass))
	{
		return false;
	}

	TestTrue(TEXT("Actual animation class declares GASP ownership"), AnimClass->GetDefaultObject<UVBAnimInstance>()->bUseGASPLocomotion);
	// 그래프 전체를 초기화하지 않고 AnimClass만 존재하며 실행 인스턴스가 없는 경계를 만든다.
	Character->GetMesh()->ClearAnimScriptInstance();
	Character->GetMesh()->AnimClass = AnimClass;
	TestNull(TEXT("Class fallback fixture has no live animation instance"), Character->GetMesh()->GetAnimInstance());
	CMC->RotationRate = Config->ArmedGroundedRotationRate;
	ResetMouseTurn(); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("AnimClass defaults retain the moving policy without an animation tick"),
		CMC->RotationRate.Equals(Config->DefaultRotationRate));
	TestTrue(TEXT("Class fallback also aligns this physics frame"), FMath::Abs(Character->GetActorRotation().Yaw - 90.0) < 0.01);
	return true;
}
namespace
{
	struct FVBActualWeaponIdleFixture
	{
		FAutomationTestBase* Test = nullptr;
		UWorld* World = nullptr;
		AVBCharacter* Character = nullptr;
		UVBAnimInstance* Main = nullptr;
		const FEnumProperty* WeaponProperty = nullptr;
		const FEnumProperty* LocomotionProperty = nullptr;
		int32 WarmFrames = 0;
		int32 HandoffFrames = 0;
		uint64 LastFrame = MAX_uint64;

		~FVBActualWeaponIdleFixture()
		{
			if (World)
			{
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}

				World->DestroyWorld(false);
			}
		}

		void SetWeaponState(EVBWeaponType Weapon, EVBLocomotionState Locomotion) const
		{
			UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Character);
			WeaponProperty->GetUnderlyingProperty()->SetIntPropertyValue(WeaponProperty->ContainerPtrToValuePtr<void>(Weapons),
				static_cast<uint64>(Weapon));
			LocomotionProperty->GetUnderlyingProperty()->SetIntPropertyValue(
				LocomotionProperty->ContainerPtrToValuePtr<void>(Weapons), static_cast<uint64>(Locomotion));
		}

		void TickPose() const
		{
			constexpr float FrameSeconds = 1.f / 60;
			// 시간만 진행하고 이동 physics/BeginPlay는 실행하지 않는다. 실제 AnimGraph는 평가한다.
			World->Tick(LEVELTICK_TimeOnly, FrameSeconds);
			Character->GetMesh()->TickAnimation(FrameSeconds, false);
			Character->GetMesh()->RefreshBoneTransforms();
		}
		// linked thread-safe update 는 GFrameCounter 별 1회라 엔진 프레임마다 한 번 포즈를 진행한다. 120 프레임 데워지면 참.
		bool TickWarmFrame()
		{
			if (LastFrame == GFrameCounter)
			{
				return false;
			}

			LastFrame = GFrameCounter;
			TickPose();
			return WarmFrames >= 120 || ++WarmFrames >= 120;
		}
	};

	// 실제 BP_VBCharacter + ABP_VBCharacter_58 + 프로덕션 CMC 경로. 납도 None 풀에서 시작한다(소환/GAS/몽타주는 실행하지 않음).
	TSharedPtr<FVBActualWeaponIdleFixture> MakeActualWeaponFixture(FAutomationTestBase& Test)
	{
		const auto Fixture = MakeShared<FVBActualWeaponIdleFixture>();
		Fixture->Test = &Test;
		const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
			.CreateNavigation(false).CreateAISystem(false);
		Fixture->World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!Test.TestNotNull(TEXT("Actual CMC animation fixture world"), Fixture->World))
		{
			return nullptr;
		}

		if (!Test.TestNotNull(TEXT("Fixture world context owner"), GEngine))
		{
			return nullptr;
		}

		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Fixture->World);
		UClass* CharacterClass = LoadClass<AVBCharacter>(nullptr,
			TEXT("/Game/VB/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C"));
		UClass* AnimClass = LoadClass<UVBAnimInstance>(nullptr,
			TEXT("/Game/VB/Blueprints/Characters/ABP_VBCharacter_58.ABP_VBCharacter_58_C"));
		auto* SkeletonMesh = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
		if (!Test.TestNotNull(TEXT("Actual player character defaults"), CharacterClass)
			|| !Test.TestNotNull(TEXT("Actual player animation class"), AnimClass)
			|| !Test.TestNotNull(TEXT("Actual player skeletal mesh"), SkeletonMesh))
		{
			return nullptr;
		}

		Fixture->Character = Fixture->World->SpawnActor<AVBCharacter>(CharacterClass);
		APlayerController* Controller = Fixture->World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Actual player fixture"), Fixture->Character)
			|| !Test.TestNotNull(TEXT("Fixture controller"), Controller))
		{
			return nullptr;
		}

		Fixture->Character->SetController(Controller);
		if (!Test.TestNotNull(TEXT("Actual character movement configuration"), Fixture->Character->GetMovementConfig()))
		{
			return nullptr;
		}

		Fixture->WeaponProperty = FindFProperty<FEnumProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("CurrentWeaponType"));
		Fixture->LocomotionProperty = FindFProperty<FEnumProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("LocomotionState"));
		if (!Test.TestNotNull(TEXT("Actual weapon component"), UVBWeaponStateComponent::FindFor(Fixture->Character))
			|| !Test.TestNotNull(TEXT("Weapon type fixture field"), Fixture->WeaponProperty)
			|| !Test.TestNotNull(TEXT("Locomotion fixture field"), Fixture->LocomotionProperty))
		{
			return nullptr;
		}

		Fixture->SetWeaponState(EVBWeaponType::None, EVBLocomotionState::CombatSheathed);
		USkeletalMeshComponent* Mesh = Fixture->Character->GetMesh();
		Mesh->SetComponentTickEnabled(false);
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetSkeletalMeshAsset(SkeletonMesh);
		Mesh->SetAnimInstanceClass(AnimClass);
		Fixture->Main = Cast<UVBAnimInstance>(Mesh->GetAnimInstance());
		if (!Test.TestNotNull(TEXT("Actual running player AnimBP"), Fixture->Main))
		{
			return nullptr;
		}

		Fixture->Main->bUseMultiThreadedAnimationUpdate = false;
		Fixture->Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Fixture->Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		IConsoleVariable* Setup = IConsoleManager::Get().FindConsoleVariable(TEXT("DDCVar.LocomotionSetupCMC"));
		if (!Test.TestNotNull(TEXT("Production CMC setup CVar"), Setup)
			|| !Test.TestEqual(TEXT("Fixture observes the production CMC route"), Setup->GetInt(), 1))
		{
			return nullptr;
		}

		return Fixture;
	}

	class FVBActualWeaponIdleWarmHistoryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVBActualWeaponIdleWarmHistoryCommand(TSharedPtr<FVBActualWeaponIdleFixture> InFixture) : Fixture(MoveTemp(InFixture)) {}
		virtual bool Update() override
		{
			FVBActualWeaponIdleFixture& F = *Fixture;
			if (!F.TickWarmFrame())
			{
				return false;
			}

			UAnimInstance* Linked = F.Main->GetLinkedAnimGraphInstanceByTag(TEXT("GASPLocomotion"));
			if (!F.Test->TestNotNull(TEXT("Actual running linked GASP graph"), Linked))
			{
				return true;
			}

			const auto* StateProperty = FindFProperty<FStructProperty>(Linked->GetClass(), TEXT("CMCState"));
			if (!F.Test->TestNotNull(TEXT("Actual linked native CMC state"), StateProperty)
				|| !F.Test->TestTrue(TEXT("Linked state has the production CMC struct"),
					StateProperty->Struct == FVBGASPCMCState::StaticStruct()))
			{
				return true;
			}

			auto& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(Linked);
			if (F.HandoffFrames == 0)
			{
				const FPoseHistoryReference History = UVBGASPLocomotionStateLibrary::Get_PoseHistoryReference(Linked);
				const int32 Entries = History.PoseHistory.IsValid() ? History.PoseHistory->GetNumEntries() : 0;
				F.Test->AddInfo(FString::Printf(TEXT("Warm None: history_entries=%d route=%d pool=%d failed=%d has_selection=%d asset=%s"),
					Entries, State.bUsesWeaponLocomotion, State.CapturedWeaponPool, State.bSelectionContractFailed,
					State.bHasSelection, *GetNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Linked).Anim)));
				if (!F.Test->TestTrue(TEXT("Actual PoseHistory collector was warmed by graph evaluation"), Entries >= 2)
					|| !F.Test->TestTrue(TEXT("None pool is on the weapon chooser route"), State.bUsesWeaponLocomotion)
					|| !F.Test->TestFalse(TEXT("None idle lifecycle remains valid before weapon handoff"),
						State.bSelectionContractFailed))
				{
					return true;
				}

				F.SetWeaponState(EVBWeaponType::Blade, EVBLocomotionState::ArmedCombat);
				F.Test->TestTrue(TEXT("Actual weapon data selects Blade's authored pool"),
					UVBWeaponStateComponent::FindFor(F.Character)->GetMMPoolForState(EVBLocomotionState::ArmedCombat)
						== EVBWeaponType::Blade);
				// 부모 값과 linked GT snapshot만 갱신해 이전 실제 재생 샘플로 첫 warm 검색을 분리한다.
				F.Main->NativeUpdateAnimation(1.f / 60);
				const auto* CollisionProperty = FindFProperty<FStructProperty>(F.Main->GetClass(), TEXT("TrajectoryCollision"));
				if (!F.Test->TestNotNull(TEXT("Original raw collision fixture field"), CollisionProperty))
				{
					return true;
				}

				auto& Collision = *CollisionProperty->ContainerPtrToValuePtr<FPoseSearchTrajectory_WorldCollisionResults>(F.Main);
				const float GeneratedCollisionTime = Collision.TimeToLand;
				// collision utility 에는 양의 TimeToLand 가 남을 수 있어 snapshot 입력을 고정한다.
				Collision.TimeToLand = 0.06f;
				UVBGASPLocomotionUpdateLibrary::Update_PropertiesFromCharacter(Linked);
				const TArray<UPoseSearchDatabase*> OriginalDatabases = F.Main->GetAllActivePoseSearchDBs();
				const bool bOriginalIdle = OriginalDatabases.ContainsByPredicate([](const UPoseSearchDatabase* Database)
				{
					return Database && Database->GetFName() == TEXT("PSD_VB_Blade_Stand_Idles");
				});
				F.Test->AddInfo(FString::Printf(TEXT("Grounded TimeToLand contract: main_helper=%.9g generated_collision=%.9g representative_collision=%.9g captured_query=%.9g original_databases=%d original_katana_idle=%d"),
					F.Main->TimeToLand(), GeneratedCollisionTime, Collision.TimeToLand,
					State.WeaponQuery.TimeToLand, OriginalDatabases.Num(), bOriginalIdle));
				F.Test->TestEqual(TEXT("Original grounded chooser's TimeToLand function returns zero"), F.Main->TimeToLand(), 0.0f);
				F.Test->TestTrue(TEXT("Original player chooser admits Blade idle for the same grounded player"), bOriginalIdle);
				F.Test->AddInfo(FString::Printf(TEXT("TimeToLand snapshot=%g original_chooser_function=%g"),
					State.WeaponQuery.TimeToLand, F.Main->TimeToLand()));
				F.Test->TestEqual(TEXT("CMC snapshot preserves the original TimeToLand function semantics"),
					State.WeaponQuery.TimeToLand, F.Main->TimeToLand());
				F.Test->TestEqual(TEXT("CMC snapshot separately preserves raw collision prediction"),
					State.WeaponQuery.TrajectoryTimeToLand, Collision.TimeToLand);
				UAnimationAsset* Animation = nullptr;
				FVBGASPCMCChooserOutput Output;
				const bool bEvaluated = UVBGASPLocomotionStateLibrary::EvaluateCMCAnimation(Linked,
					State.SelectionContext.Get(), State.ActiveChooser.Get(), Animation, Output);
				F.Test->AddInfo(FString::Printf(TEXT("Warm Blade query: history=%d phase=%d phase_data=%d candidate_chooser=%d phase_candidates=%d allowed=%d cost=%.9g source=%s animation=%s evaluated=%d valid=%d"),
					Output.bHistoryReady, static_cast<int32>(Output.Phase), Output.bPhaseDataValid,
					Output.bCandidateChooserEvaluated, Output.bHasPhaseCandidates, Output.AllowedDatabases.Num(),
					Output.SearchCost, *GetPathNameSafe(Output.SourceDatabase.Get()), *GetPathNameSafe(Animation), bEvaluated,
					UVBGASPLocomotionStateLibrary::IsValidCMCSelection(Animation, Output)));
				F.Test->TestTrue(TEXT("Warm weapon query has a valid context contract"), bEvaluated);
				F.Test->TestTrue(TEXT("Standing Blade loop has an admitted phase candidate"), Output.bHasPhaseCandidates);
				F.Test->TestTrue(TEXT("Admitted warm Blade candidate produces a valid PoseSearch result"),
					UVBGASPLocomotionStateLibrary::IsValidCMCSelection(Animation, Output));
				F.HandoffFrames = 1;
				return false;
			}

			if (++F.HandoffFrames < 3)
			{
				return false;
			}

			F.Test->AddInfo(FString::Printf(TEXT("Blade handoff lifecycle: pool=%d failed=%d waiting=%d selected_pool=%d selected=%s"),
				State.CapturedWeaponPool, State.bSelectionContractFailed, State.bWaitingForPhase,
				State.SelectedWeaponPool, *GetPathNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Linked).Anim)));
			F.Test->TestFalse(TEXT("A warmed None to Blade idle handoff does not permanently disable CMC"), State.bSelectionContractFailed);
			// 실제 착지 프레임에서는 Linked GT snapshot 이 Main 갱신보다 먼저다. 물리는 땅에 두고 Main 의 옛 공중 캐시를 재현한다.
			const auto* ModeProperty = FindFProperty<FEnumProperty>(F.Main->GetClass(), TEXT("MovementMode"));
			const auto* VelocityProperty = FindFProperty<FStructProperty>(F.Main->GetClass(), TEXT("Velocity"));
			if (!F.Test->TestNotNull(TEXT("Landing fixture cached movement mode"), ModeProperty)
				|| !F.Test->TestNotNull(TEXT("Landing fixture cached velocity"), VelocityProperty))
			{
				return true;
			}

			ModeProperty->GetUnderlyingProperty()->SetIntPropertyValue(ModeProperty->ContainerPtrToValuePtr<void>(F.Main),
				static_cast<uint64>(EVBMovementMode::InAir));
			*VelocityProperty->ContainerPtrToValuePtr<FVector>(F.Main) = FVector(0, 0, -500);
			F.Test->TestTrue(TEXT("Previous Main airborne helper is nonzero on this grounded physics frame"), F.Main->TimeToLand() > 0);
			UVBGASPLocomotionUpdateLibrary::Update_PropertiesFromCharacter(Linked);
			F.Test->TestEqual(TEXT("Grounded native snapshot does not retain previous Main airborne time"),
				State.WeaponQuery.TimeToLand, 0.0f);
			// 실제 Landed 콜백이 애니 snapshot 전에 가벼운 착지 펄스를 준다. 이 시험은 그 전달과 별개로 옛 캐시만 본다.
			State.WeaponQuery.JustLandedLight = true;
			State.WeaponQuery.JustLandedHeavy = false;
			UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToIdle(Linked, State.ActiveChooser.Get());
			F.Test->AddInfo(FString::Printf(
				TEXT("Grounded stale-cache landing: main_helper=%g captured=%g source=%s asset=%s waiting=%d failed=%d"),
				F.Main->TimeToLand(), State.WeaponQuery.TimeToLand, *GetNameSafe(State.LastSelection.SourceDatabase.Get()),
				*GetNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Linked).Anim),
				State.bWaitingForPhase, State.bSelectionContractFailed));
			F.Test->TestTrue(TEXT("Actual Blade transition selects the light landing database before Idle Loop"),
				State.LastSelection.SourceDatabase
					&& State.LastSelection.SourceDatabase->GetFName() == TEXT("PSD_VB_Stand_Idle_Lands_Light"));
			F.Test->TestFalse(TEXT("Grounded landing has no empty-phase hold"), State.bWaitingForPhase);
			F.Test->TestFalse(TEXT("Grounded landing keeps CMC enabled"), State.bSelectionContractFailed);
			return true;
		}
	private:
		TSharedPtr<FVBActualWeaponIdleFixture> Fixture;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBActualWeaponIdleWarmHistoryTest, "VB.Animation.CMC.ActualWeaponIdleWarmHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBActualWeaponIdleWarmHistoryTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FVBActualWeaponIdleFixture> Fixture = MakeActualWeaponFixture(*this);
	if (!Fixture)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FVBActualWeaponIdleWarmHistoryCommand(Fixture));
	return true;
}

namespace
{
	// GASP 제자리 회전 요청은 EvaluateCMCAnimation 이 채우는 WeaponQuery.ShouldTurnInPlace 로만 무기 chooser 에 닿는다.
	// Fighter chooser 가 그 칸으로 TurnInPlace 를 연다.
	// 플레이처럼 카메라를 돌리면 캡슐이 따라 돈다. bAim 은 조준 키를 쥐었는지.
	// 대조: 그 칸에 false 를 넣으면 TurnInPlace 줄, 회전을 WantsToAim 으로 막으면 조준 없음 시험이 실패한다.
	class FVBActualFighterTurnInPlaceCommand : public IAutomationLatentCommand
	{
	public:
		FVBActualFighterTurnInPlaceCommand(TSharedPtr<FVBActualWeaponIdleFixture> InFixture, bool bInAim)
			: Fixture(MoveTemp(InFixture)), bAim(bInAim) {}
		virtual bool Update() override
		{
			FVBActualWeaponIdleFixture& F = *Fixture;
			if (!F.TickWarmFrame())
			{
				return false;
			}

			UAnimInstance* Linked = F.Main->GetLinkedAnimGraphInstanceByTag(TEXT("GASPLocomotion"));
			if (!F.Test->TestNotNull(TEXT("Actual running linked GASP graph"), Linked))
			{
				return true;
			}

			const auto* StateProperty = FindFProperty<FStructProperty>(Linked->GetClass(), TEXT("CMCState"));
			if (!F.Test->TestNotNull(TEXT("Actual linked native CMC state"), StateProperty))
			{
				return true;
			}

			const auto& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(Linked);
			F.SetWeaponState(EVBWeaponType::Fighter, EVBLocomotionState::ArmedCombat);
			if (!F.Test->TestTrue(TEXT("Actual weapon data selects Fighter's authored pool"),
				UVBWeaponStateComponent::FindFor(F.Character)->GetMMPoolForState(EVBLocomotionState::ArmedCombat)
					== EVBWeaponType::Fighter))
			{
				return true;
			}
			// 선 채로 카메라를 90도 돌린다. 캡슐은 따라가고 linked root 는 아직 안 따라갔다.
			UVBLocomotionIntentComponent::FindFor(F.Character)->SetWantsToAim(bAim);
			Cast<UVBSlideMovementComponent>(F.Character->GetCharacterMovement())->RefreshRotationMode();
			APlayerController* Controller = Cast<APlayerController>(F.Character->GetController());
			UCharacterMovementComponent* Movement = F.Character->GetCharacterMovement();
			if (!F.Test->TestNotNull(TEXT("Fixture player controller"), Controller))
			{
				return true;
			}

			if (!F.Test->TestTrue(TEXT("Precondition: the standing capsule follows the camera"), Movement->bUseControllerDesiredRotation))
			{
				return true;
			}

			const FRotator CameraYaw = F.Character->GetActorRotation() + FRotator(0, 90, 0);
			Controller->SetControlRotation(CameraYaw);
			for (int32 Frame = 0; Frame < 120 && !F.Character->GetActorRotation().Equals(CameraYaw, 1.0); ++Frame)
			{
				Movement->PhysicsRotation(1.f / 60);
			}

			if (!F.Test->TestTrue(TEXT("Precondition: the movement component turned the capsule to the camera"),
				F.Character->GetActorRotation().Equals(CameraYaw, 1.0)))
			{
				return true;
			}

			F.Main->NativeUpdateAnimation(1.f / 60);
			UVBGASPLocomotionUpdateLibrary::Update_PropertiesFromCharacter(Linked);
			if (!F.Test->TestNotNull(TEXT("The snapshot carries the owner's movement config"), State.MovementConfig.Get()))
			{
				return true;
			}

			F.Test->TestTrue(TEXT("The snapshot sees the capsule following the camera"), State.bCapsuleFollowsCamera);
			F.Test->AddInfo(FString::Printf(TEXT("Turn threshold %.1f, aim %d"), State.MovementConfig->TurnInPlaceYawThreshold, bAim));
			if (!F.Test->TestTrue(bAim ? TEXT("The GASP turn request is true for the aiming player who turned the camera")
					: TEXT("The GASP turn request is true for the player who turned the camera without aiming"),
				UVBGASPLocomotionLibrary::ShouldTurnInPlace(Linked)))
			{
				return true;
			}
			// 그래프는 Transition to Idle Loop 진입으로 회전한다(Transition 단계 선택). Idle Loop 평가는 1회 게이트를 지우는 Loop 단계라 쓰지 않는다.
			UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToIdle(Linked, State.ActiveChooser.Get());
			const UPoseSearchDatabase* Source = State.LastSelection.SourceDatabase.Get();
			F.Test->AddInfo(FString::Printf(TEXT("Fighter turn: route=%d waiting=%d failed=%d source=%s asset=%s"),
				State.bUsesWeaponLocomotion, State.bWaitingForPhase, State.bSelectionContractFailed, *GetPathNameSafe(Source),
				*GetNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Linked).Anim)));
			F.Test->TestTrue(TEXT("The GASP turn request reaches the Fighter chooser: the transition selects its TurnInPlace database"),
				Source && Source->GetFName() == TEXT("PSD_VB_Fighter_Stand_TurnInPlace"));
			return true;
		}
	private:
		TSharedPtr<FVBActualWeaponIdleFixture> Fixture;
		bool bAim = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBActualFighterTurnInPlaceTest, "VB.Animation.CMC.ActualFighterTurnInPlaceReachesChooser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBActualFighterTurnInPlaceTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FVBActualWeaponIdleFixture> Fixture = MakeActualWeaponFixture(*this);
	if (!Fixture)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FVBActualFighterTurnInPlaceCommand(Fixture, true));
	return true;
}

// 조준 키 없이 선 채로 카메라를 돌리면 발이 돈다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBActualFighterCameraTurnInPlaceTest,
	"VB.Animation.CMC.ActualFighterCameraTurnInPlaceReachesChooser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBActualFighterCameraTurnInPlaceTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FVBActualWeaponIdleFixture> Fixture = MakeActualWeaponFixture(*this);
	if (!Fixture)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FVBActualFighterTurnInPlaceCommand(Fixture, false));
	return true;
}

#if WITH_EDITOR
// 회전 임계는 (0, 180] 이어야 한다: 0 이면 매 프레임 돌고, 180 초과면 영영 안 돈다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTurnInPlaceThresholdConfigTest, "VB.Animation.CMC.TurnInPlaceThresholdValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTurnInPlaceThresholdConfigTest::RunTest(const FString& Parameters)
{
	UVBMovementConfig* Config = NewObject<UVBMovementConfig>();
	Config->StrafeSpeedMapCurve = NewObject<UCurveFloat>(Config); // 출시 설정은 방향 커브를 갖는다(IsDataValid 요구)
	FDataValidationContext Default;
	TestTrue(TEXT("default 50 is valid"), Config->IsDataValid(Default) != EDataValidationResult::Invalid);
	for (const float Bad : { 0.f, -10.f, 181.f })
	{
		Config->TurnInPlaceYawThreshold = Bad;
		FDataValidationContext Context;
		TestTrue(FString::Printf(TEXT("threshold %.0f is invalid"), Bad), Config->IsDataValid(Context) == EDataValidationResult::Invalid);
	}

	Config->TurnInPlaceYawThreshold = 180.f;
	FDataValidationContext Max;
	TestTrue(TEXT("threshold 180 is valid"), Config->IsDataValid(Max) != EDataValidationResult::Invalid);
	return true;
}
#endif

// 플레이어 애니는 착지를 소유자의 ACharacter::LandedDelegate 에서만 받는다.
// 대조: UVBAnimInstance::NativeInitializeAnimation 의 AddUniqueDynamic 을 빼면 JustLanded 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLandingReadByAnimTest, "VB.Animation.CMC.LandingReadByAnim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLandingReadByAnimTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FVBActualWeaponIdleFixture> Fixture = MakeActualWeaponFixture(*this);
	if (!Fixture)
	{
		return false;
	}

	TestFalse(TEXT("No landing before the character lands"), Fixture->Main->HasJustLanded());
	const FVector ImpactVelocity(120.0, -40.0, -650.0);
	Fixture->Character->GetCharacterMovement()->Velocity = ImpactVelocity;
	Fixture->Character->Landed(FHitResult());
	TestTrue(TEXT("The anim sees the landing through the engine delegate"), Fixture->Main->HasJustLanded());
	TestTrue(TEXT("Land velocity is the velocity at the landing moment"), Fixture->Main->GetLandVelocity().Equals(ImpactVelocity, 0.01));
	return true;
}
#endif

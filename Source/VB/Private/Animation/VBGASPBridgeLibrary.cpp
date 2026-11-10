#include "Animation/VBGASPBridgeLibrary.h"

#include "Animation/VBAnimInstance.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "VBGASPStateView.h"
#include "Chooser.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBHeroComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Data/VBMovementConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"
#include "VB/VB.h" // VB_LOG
#include "UObject/Stack.h"

namespace
{
	FProperty* Field(UScriptStruct* Struct, const TCHAR* Name)
	{
		const FString Prefix = FString(Name) + TEXT("_");
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->GetName().StartsWith(Prefix))
			{
				return *It;
			}
		}

		return nullptr;
	}

	template<typename T>
	bool SetStruct(UScriptStruct* Struct, void* Output, const TCHAR* Name, const T& Value)
	{
		FStructProperty* Property = CastField<FStructProperty>(Field(Struct, Name));
		if (!Property || Property->Struct != TBaseStructure<T>::Get())
		{
			VB_LOG(Error, "GASP struct field %s: actual=%s expected=%s", Name,
				Property ? *Property->Struct->GetPathName() : TEXT("missing"), *TBaseStructure<T>::Get()->GetPathName());
			return false;
		}

		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Output), &Value);
		return true;
	}

	bool SetBool(UScriptStruct* Struct, void* Output, const TCHAR* Name, bool Value)
	{
		FBoolProperty* Property = CastField<FBoolProperty>(Field(Struct, Name));
		if (!Property) { VB_LOG(Error, "GASP missing bool %s", Name); return false; }
		Property->SetPropertyValue_InContainer(Output, Value);
		return true;
	}

	bool SetNumber(UScriptStruct* Struct, void* Output, const TCHAR* Name, double Value)
	{
		FNumericProperty* Property = CastField<FNumericProperty>(Field(Struct, Name));
		if (!Property || !Property->IsFloatingPoint()) { VB_LOG(Error, "GASP missing number %s", Name); return false; }
		Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Output), Value);
		return true;
	}

	// User-defined enum 의 표시 순서와 NewEnumerator ID 가 다르다. native 순번을 cast 하지 않고
	// cooked 빌드에도 UUserDefinedEnum 이 지키는 저작 이름으로 찾는다.
	bool SetEnum(UScriptStruct* Struct, void* Output, const TCHAR* Name, const TCHAR* Value)
	{
		FByteProperty* Property = CastField<FByteProperty>(Field(Struct, Name));
		if (!Property || !Property->Enum) { VB_LOG(Error, "GASP missing enum %s", Name); return false; }
		for (int32 Index = 0; Index < Property->Enum->NumEnums(); ++Index)
		{
			if (Property->Enum->GetAuthoredNameStringByIndex(Index) == Value)
			{
				Property->SetPropertyValue_InContainer(Output, Property->Enum->GetValueByIndex(Index));
				return true;
			}
		}

		VB_LOG(Error, "GASP missing enum value %s.%s", Name, Value);
		return false;
	}
}

void UVBGASPBridgeLibrary::ReadCharacterProperties(ACharacter*, int32&)
{
	checkNoEntry(); // Wildcard calls must execute through the VM thunk.
}

DEFINE_FUNCTION(UVBGASPBridgeLibrary::execReadCharacterProperties)
{
	P_GET_OBJECT(ACharacter, Character);
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const FStructProperty* Property = CastField<FStructProperty>(Stack.MostRecentProperty);
	void* Output = Stack.MostRecentPropertyAddress;
	P_FINISH;
	P_NATIVE_BEGIN;
	if (!Property || !Output || !FillCharacterProperties(Character, Property->Struct, Output))
	{
		FFrame::KismetExecutionMessage(TEXT("VB GASP bridge: incompatible character/property contract"), ELogVerbosity::Error);
	}

	P_NATIVE_END;
}

bool UVBGASPBridgeLibrary::FillCharacterProperties(ACharacter* Character, UScriptStruct* Struct, void* Output)
{
	if (!Character || !Struct || !Output)
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		return false;
	}

	const UVBAnimInstance* Animation = Character->GetMesh() ? Cast<UVBAnimInstance>(Character->GetMesh()->GetAnimInstance()) : nullptr;
	const UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(Character);
	const bool bSprint = Intent && Intent->GetIsSprinting();
	const bool bWalk = Intent && Intent->GetGait() == EVBGait::Walk;
	const bool bAim = Intent && Intent->WantsToAim();
	const bool bStrafe = !Movement->bOrientRotationToMovement;
	const TCHAR* Gait = bSprint ? TEXT("Sprint") : bWalk ? TEXT("Walk") : TEXT("Run");
	const TCHAR* Mode = Movement->IsFalling() || Movement->IsSwimming() ? TEXT("InAir") : TEXT("OnGround");
	// 원래 CMC 계약과 같게 - traversal 포즈는 main instance 가 준다
	bool bValid = SetEnum(Struct, Output, TEXT("MovementMode"), Mode);
	bValid &= SetEnum(Struct, Output, TEXT("Stance"), Character->bIsCrouched ? TEXT("Crouch") : TEXT("Stand"));
	bValid &= SetEnum(Struct, Output, TEXT("Gait"), Gait);
	if (Struct->GetFName() == TEXT("S_CharacterPropertiesForCamera"))
	{
		const UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Character); // UVBHeroComponent 가 없는 캐릭터는 GASP 기본 Medium
		bValid &= SetEnum(Struct, Output, TEXT("CameraStyle"),
			Hero ? *StaticEnum<EVBCameraStyle>()->GetNameStringByValue(static_cast<int64>(Hero->GetCameraStyle())) : TEXT("Medium"));
		bValid &= SetEnum(Struct, Output, TEXT("CameraMode"), bAim ? TEXT("Aim") : bStrafe ? TEXT("Strafe") : TEXT("FreeCam"));
		return bValid;
	}

	if (Struct->GetFName() != TEXT("S_CharacterPropertiesForAnimation"))
	{
		return false;
	}

	bValid &= SetEnum(Struct, Output, TEXT("RotationMode"), bStrafe ? TEXT("Strafe") : TEXT("OrientToMovement"));
	bValid &= SetEnum(Struct, Output, TEXT("MovementDirection"), TEXT("F")); // CMC source leaves direction to the ABP.
	bValid &= SetStruct(Struct, Output, TEXT("ActorTransform"), Character->GetActorTransform());
	bValid &= SetStruct(Struct, Output, TEXT("Velocity"), Animation && Animation->IsSuppressingAttackLocomotion()
		? FVector::ZeroVector : Movement->Velocity);
	bValid &= SetStruct(Struct, Output, TEXT("InputAcceleration"), Movement->GetCurrentAcceleration());
	bValid &= SetNumber(Struct, Output, TEXT("CurrentMaxAcceleration"), Movement->GetMaxAcceleration());
	bValid &= SetNumber(Struct, Output, TEXT("CurrentMaxDeceleration"), Movement->BrakingDecelerationWalking);
	bValid &= SetStruct(Struct, Output, TEXT("OrientationIntent"), Character->GetActorRotation());
	bValid &= SetStruct(Struct, Output, TEXT("AimingRotation"),
		Character->IsLocallyControlled() ? Character->GetControlRotation() : Character->GetBaseAimRotation());
	bValid &= SetBool(Struct, Output, TEXT("JustLanded"), Animation && Animation->HasJustLanded());
	bValid &= SetStruct(Struct, Output, TEXT("LandVelocity"), Animation ? Animation->GetLandVelocity() : FVector::ZeroVector);
	bValid &= SetNumber(Struct, Output, TEXT("SteeringTime"), 0.0);
	bValid &= SetStruct(Struct, Output, TEXT("GroundNormal"), FVector(Movement->CurrentFloor.HitResult.ImpactNormal));
	bValid &= SetStruct(Struct, Output, TEXT("GroundLocation"), FVector(Movement->CurrentFloor.HitResult.ImpactPoint));
	// CMC 원본은 이 값을 항등으로 둔다. 델타는 Mover 가 채운다.
	bValid &= SetStruct(Struct, Output, TEXT("BasedMovementDelta"), FTransform::Identity);
	FStructProperty* Input = CastField<FStructProperty>(Field(Struct, TEXT("InputState")));
	if (!Input)
	{
		return false;
	}

	void* InputValue = Input->ContainerPtrToValuePtr<void>(Output);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToSprint"), bSprint);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToWalk"), bWalk);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToStrafe"), bStrafe);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToAim"), bAim);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToCrouch"), Movement->bWantsToCrouch);
	return bValid;
}

void UVBGASPBridgeLibrary::ApplyWeaponDatabases(UAnimInstance* LinkedInstance, const FAnimNodeReference& Node)
{
	if (!LinkedInstance || !LinkedInstance->GetSkelMeshComponent())
	{
		return;
	}

	const UVBAnimInstance* Main = Cast<UVBAnimInstance>(LinkedInstance->GetSkelMeshComponent()->GetAnimInstance());
	if (!Main || !Main->UsesWeaponLocomotion())
	{
		return;
	}

	EAnimNodeReferenceConversionResult Result;
	const FMotionMatchingAnimNodeReference Matching = UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Node, Result);
	if (Result == EAnimNodeReferenceConversionResult::Succeeded)
	{
		UMotionMatchingAnimNodeLibrary::SetDatabasesToSearch(Matching, Main->GetAllActivePoseSearchDBs(), Main->GetMMInterruptMode());
	}
}

EOffsetRootBoneMode UVBGASPBridgeLibrary::ConstrainRootOffsetMode(UAnimInstance* LinkedInstance, EOffsetRootBoneMode Mode)
{
	const UVBAnimInstance* Main = LinkedInstance && LinkedInstance->GetSkelMeshComponent()
		? Cast<UVBAnimInstance>(LinkedInstance->GetSkelMeshComponent()->GetAnimInstance()) : nullptr;
	return Main && Main->ShouldReleaseGASPRootOffset() ? EOffsetRootBoneMode::Release : Mode;
}

namespace
{
	// 걷는 바닥, 캐릭터 회전, 이동 설정을 상태에 잡아 둔다.
	void CaptureMovementFacts(FVBGASPCMCState& State, const ACharacter* Character, const UCharacterMovementComponent* Movement)
	{
		State.bHasWalkableFloor = Movement && Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor();
		State.GroundNormal = State.bHasWalkableFloor ? Movement->CurrentFloor.HitResult.ImpactNormal : FVector::UpVector;
		State.bCapsuleFollowsCamera = Movement && Movement->bUseControllerDesiredRotation;
		const AVBCharacter* VBCharacter = Cast<AVBCharacter>(Character);
		State.MovementConfig = VBCharacter ? VBCharacter->GetMovementConfig() : nullptr;
		ensureMsgf(!Character || State.MovementConfig,
			TEXT("GASP CMC contract on %s without a movement config: turn in place stays off"), *GetNameSafe(Character));
	}

	// 지금 경로의 선택기를 고른다. 무장이면 가드 이동 요청이 이긴다.
	UChooserTable* ResolveCMCChooser(const VBGASP::FStateView& View, UAnimInstance* LinkedInstance, UVBAnimInstance* Main, bool bWeapon)
	{
		const auto* ChooserProperty = CastField<FObjectPropertyBase>(
			View.Find(bWeapon ? TEXT("CMCWeaponChooser") : TEXT("CMCUnarmedChooser")));
		UChooserTable* Chooser = ChooserProperty
			? Cast<UChooserTable>(ChooserProperty->GetObjectPropertyValue_InContainer(LinkedInstance)) : nullptr;
		if (bWeapon)
		{
			if (UChooserTable* GuardChooser = Main->GetRequestedGuardLocomotionChooser())
			{
				Chooser = GuardChooser;
			}
		}

		return Chooser;
	}

	// 선택기나 경로가 바뀌었으면 지난 선택을 무르고 다시 고르게 한다.
	void ApplyCMCChooser(FVBGASPCMCState& State, UChooserTable* Chooser, bool bChangedRoute)
	{
		if (State.ActiveChooser != Chooser)
		{
			// 선택기 교체 전의 성공을 새 자세의 준비 완료로 오인하지 않는다.
			// 실패한 가드 선택기를 빠져나갈 때도 원래 이동 선택기를 새로 평가한다.
			State.bHasSelection = false;
			State.bWaitingForPhase = false;
			State.bSelectionContractFailed = false;
			State.bTransitionToLoopRequested = false;
		}

		State.bNeedsReselect |= bChangedRoute || State.ActiveChooser != Chooser;
		State.ActiveChooser = Chooser;
	}

	// Main 의 enum 변수 하나를 같은 형일 때만 복사한다.
	template<typename T>
	bool CopyMainEnum(const VBGASP::FStateView& MainView, UVBAnimInstance* Main, FName Name, T& Value)
	{
		const auto* Property = CastField<FEnumProperty>(MainView.Find(Name));
		if (!Property || Property->GetEnum() != StaticEnum<T>())
		{
			return false;
		}

		Value = static_cast<T>(Property->GetUnderlyingProperty()->GetSignedIntPropertyValue(
			Property->ContainerPtrToValuePtr<void>(Main)));
		return true;
	}

	// enum 을 뺀 나머지 무기 질의 값을 Main 에서 읽는다.
	void ReadWeaponQueryValues(FVBGASPCMCWeaponQuery& Query, const VBGASP::FStateView& MainView, UVBAnimInstance* Main,
		const UCharacterMovementComponent* Movement)
	{
		Query.MMDatabaseLOD = static_cast<float>(MainView.Number(TEXT("MMDatabaseLOD")));
		Query.MMSprintGateSpeed = static_cast<float>(MainView.Number(TEXT("MMSprintGateSpeed")));
		// Linked GT update 가 Main NativeUpdate 보다 먼저라 착지 첫 프레임에 Main 의 캐시 모드가 아직 InAir 일 수 있다.
		// 지금 캐릭터 스냅샷에 맞춘다.
		Query.TimeToLand = Movement && !Movement->IsFalling() ? 0.0f : Main->TimeToLand();
		Query.TrajectoryTimeToLand = static_cast<float>(MainView.Child(TEXT("TrajectoryCollision")).Number(TEXT("TimeToLand")));
		Query.JustTraversed = Main->JustTraversed();
		Query.JustLandedLight = Main->JustLandedLight();
		Query.JustLandedHeavy = Main->JustLandedHeavy();
	}

	// 무기 풀 교체를 수렴 지연에 맞춰 반영하고 질의를 상태에 전달한다.
	void CommitWeaponQuery(FVBGASPCMCState& State, FVBGASPCMCWeaponQuery& Query, bool bChangedRoute)
	{
		const int32 Pool = static_cast<int32>(Query.MMWeaponType);
		const bool bPoolChanged = Pool != State.CapturedWeaponPool;
		const bool bInterrupt = State.CurrentInterruptMode != EPoseSearchInterruptMode::DoNotInterrupt;
		// main 의 무기 교체 수렴 지연을 지킨다. 보통 start/stop 끊기는 MM 의 다른 이동 조건이 아니라
		// CMC 상태 전이가 맡는다.
		if (bPoolChanged && State.bHasSelection && !bChangedRoute && !bInterrupt)
		{
			Query.MMWeaponType = State.WeaponQuery.MMWeaponType;
		}
		else
		{
			State.bNeedsReselect |= bPoolChanged || Query.LocomotionState != State.WeaponQuery.LocomotionState;
			State.CapturedWeaponPool = Pool;
		}

		State.CurrentInterruptMode = bPoolChanged && bInterrupt
			? EPoseSearchInterruptMode::InterruptOnDatabaseChange : EPoseSearchInterruptMode::DoNotInterrupt;
		State.WeaponQuery = Query;
	}
}

void UVBGASPBridgeLibrary::CaptureCMCSelection(UAnimInstance* LinkedInstance)
{
	if (!LinkedInstance || !ensure(IsInGameThread()))
	{
		return;
	}

	const VBGASP::FStateView View(LinkedInstance);
	const auto* StateProperty = CastField<FStructProperty>(View.Find(TEXT("CMCState")));
	// 원래 샘플 instance 에는 프로젝트 계약이 없다
	if (!StateProperty || StateProperty->Struct != FVBGASPCMCState::StaticStruct())
	{
		return;
	}

	FVBGASPCMCState& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(LinkedInstance);
	const ACharacter* Character = Cast<ACharacter>(LinkedInstance->GetOwningActor());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	CaptureMovementFacts(State, Character, Movement);
	USkeletalMeshComponent* Mesh = LinkedInstance->GetSkelMeshComponent();
	UVBAnimInstance* Main = Mesh ? Cast<UVBAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	const bool bWeapon = Main && Main->UsesWeaponLocomotion();
	const bool bChangedRoute = State.bUsesWeaponLocomotion != bWeapon;
	State.bUsesWeaponLocomotion = bWeapon;
	State.CurrentInterruptMode = Main ? Main->GetMMInterruptMode() : EPoseSearchInterruptMode::DoNotInterrupt;
	State.SelectionContext = bWeapon ? static_cast<UAnimInstance*>(Main) : LinkedInstance;
	ApplyCMCChooser(State, ResolveCMCChooser(View, LinkedInstance, Main, bWeapon), bChangedRoute);
	if (!bWeapon)
	{
		State.AllowedDatabases.Reset();
		State.WeaponSelectionKey = 0;
		return;
	}

	const VBGASP::FStateView MainView(Main);
	FVBGASPCMCWeaponQuery Query;
	// reflected 접근과 main-instance 도우미는 모두 game thread 에서 한다.
	// worker 선택은 평범한 값만 받고 linked CMC 상태만 다시 계산한다.
	const bool bSnapshotValid = CopyMainEnum(MainView, Main, TEXT("MMWeaponType"), Query.MMWeaponType) &&
		CopyMainEnum(MainView, Main, TEXT("LocomotionState"), Query.LocomotionState) &&
		CopyMainEnum(MainView, Main, TEXT("Gait"), Query.Gait);
	if (!bSnapshotValid)
	{
		State.bSelectionContractFailed = true;
		ensureMsgf(false, TEXT("Weapon CMC snapshot enum contract changed"));
		return;
	}

	ReadWeaponQueryValues(Query, MainView, Main, Movement);
	CommitWeaponQuery(State, Query, bChangedRoute);
}

#pragma once

#include "CoreMinimal.h"
#include "AlphaBlend.h"
#include "Animation/VBAnimationTypes.h"
#include "Types/VBWeaponStateTypes.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "VBGASPCMCSelectionTypes.generated.h"

class UAnimationAsset;
class UAnimInstance;
class UBlendProfile;
class UChooserTable;
class UPoseSearchDatabase;
class UVBMovementConfig;

/** CMC 재생 역할. 무기, 걸음 DB 선택과 무관하다. */
UENUM(BlueprintType)
enum class EVBGASPCMCPhase : uint8
{
	Any,
	Loop,
	Transition
};

/** 값만 담는 후보 문맥. 게임 스레드가 무기 데이터를 담고 CMC 선택이 자기 상태를 채운다. */
USTRUCT(BlueprintType)
struct VB_API FVBGASPCMCWeaponQuery
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float MMDatabaseLOD = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBWeaponType MMWeaponType = EVBWeaponType::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBLocomotionState LocomotionState = EVBLocomotionState::Unarmed;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBMovementMode MovementMode = EVBMovementMode::Grounded;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBStance Stance = EVBStance::Stand;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBMMState MMState = EVBMMState::Idle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBGait Gait = EVBGait::Run;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float Speed2D = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float TimeToLand = 999.0f;
	/** 궤적 충돌 예측값. 메인 인스턴스의 TimeToLand() 와 다르다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float TrajectoryTimeToLand = 999.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float MMSprintGateSpeed = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool IsStarting = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool IsPivoting = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool JustTraversed = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool JustLandedLight = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool JustLandedHeavy = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool ShouldSpinTransition = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool ShouldTurnInPlace = false;
};

/** 캐릭터별 경사 입력. 각도는 도, 노멀은 월드 공간. */
USTRUCT(BlueprintType)
struct VB_API FVBGSlopeSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC", meta=(ClampMin="0"))
	float NormalInterpSpeed = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC", meta=(ClampMin="0"))
	float DeadZoneDegrees = 15.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC", meta=(ClampMin="0.01"))
	float MaxSlopeDegrees = 30.0f;
};

/** Chooser 문맥 1. 평가마다 새 값으로 시작한다. */
USTRUCT(BlueprintType)
struct VB_API FVBGASPCMCChooserOutput
{
	GENERATED_BODY()
	/** 무기 MasterChooser 가 고른 활성 풀의 입력 스냅샷. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TArray<TObjectPtr<UPoseSearchDatabase>> AllowedDatabases;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	EVBGASPCMCPhase Phase = EVBGASPCMCPhase::Any;
	/** 필터 결과. 잠깐 빈 단계와 깨진 표를 구별한다. */
	bool bHasPhaseCandidates = false;
	bool bPhaseDataValid = true;
	/** 실제 무기 선택에서는 필수, 필터 단독 시험에서는 선택. */
	bool bRequireCandidateChooser = false;
	bool bCandidateChooserEvaluated = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UPoseSearchDatabase> SourceDatabase = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	EPoseSearchInterruptMode InterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float StartTime = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float SearchCost = MAX_flt;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float MaxAcceptedCost = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bPoseMatched = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bMirror = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bForceBlend = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float BlendTime = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	FName BlendProfileName = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	EAlphaBlendOption BlendCurve = EAlphaBlendOption::QuadraticInOut;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TArray<FName> Tags;
	/** 필터 열 평가 전에 네이티브 코드가 채운다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bHistoryReady = false;
	/** 명시적으로 저작한 시작/실패 대체 행에서만 참. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bFallback = false;
};

/** 확정된 재생 명령. 선택 사이에는 BlendParameters 만 바뀐다. */
USTRUCT(BlueprintType)
struct VB_API FVBGASPCMCBlendInputs
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UAnimationAsset> Anim = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bLoop = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float StartTime = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bMirror = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float BlendTime = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UBlendProfile> BlendProfile = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	EAlphaBlendOption BlendCurve = EAlphaBlendOption::QuadraticInOut;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TArray<FName> Tags;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	FVector BlendParameters = FVector::ZeroVector;
};

/** 링크된 CMC AnimBlueprint 인스턴스가 소유하는 상태. 전역 캐시를 두지 않는다. */
USTRUCT(BlueprintType)
struct VB_API FVBGASPCMCState
{
	GENERATED_BODY()
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bUsesWeaponLocomotion = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UAnimInstance> SelectionContext = nullptr;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UChooserTable> ActiveChooser = nullptr;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	TArray<TObjectPtr<UPoseSearchDatabase>> AllowedDatabases;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	int32 WeaponSelectionKey = 0;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	EPoseSearchInterruptMode CurrentInterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	int32 CapturedWeaponPool = -1;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVBGASPCMCWeaponQuery WeaponQuery;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	float SecondsSinceSearch = 0.0f;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bNeedsReselect = true;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVector GroundNormal = FVector::UpVector;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bHasWalkableFloor = false;
	// 이동 컴포넌트가 캡슐을 카메라 쪽으로 돌리는 중(스트레이프, 조준, 락온, 무장).
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bCapsuleFollowsCamera = false;
	// 소유자의 이동 튜닝. 워커 스레드 판정이 문턱값을 여기서 읽는다.
	UPROPERTY(Transient, BlueprintReadOnly, Category="GASP|CMC")
	TObjectPtr<const UVBMovementConfig> MovementConfig = nullptr;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVector SmoothedGroundNormal = FVector::UpVector;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVector2D SlopeAngle = FVector2D::ZeroVector;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bSlopeInitialized = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bSetupWasActive = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bHistoryWasReady = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bHasSelection = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bSelectionContractFailed = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	int32 SelectedWeaponKey = 0;
	/** 실제 무기 풀. SelectedWeaponKey 는 DB 묶음 해시일 뿐이다. */
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	int32 SelectedWeaponPool = -1;
	// 선택 당시의 걸음. 재생 중 걸음 변화 비교 기준(ShouldReselectCMCForGait).
	EVBGait SelectedGait = EVBGait::Run;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bSelectedWeaponLocomotion = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bWaitingForPhase = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bTransitionToLoopRequested = false;
	// 피벗 콘텐츠 없는 무장 풀에서 피벗 전이가 반복 클립으로 대체되면 켠다. 켜진 동안 IsPivoting 이 거짓이다.
	// 원판정(IsPivotingRaw)이 거짓이 되거나 설정, 경로, 무기 풀이 바뀌면 끈다.
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bPivotFallbackLatched = false;
	// 래치를 켤 때의 자세(앉음이면 참). 래치는 풀, 자세별 사실이라 자세가 바뀌면 지운다.
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bPivotFallbackCrouched = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	uint8 SelectedState = 0;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVBGASPCMCChooserOutput LastSelection;
	// 일련번호는 네이티브 전용. BP 의 영속 bool 이 매 프레임 다시 시작되지 않게 한다.
	uint64 SelectionSerial = 0;
	uint64 PendingForceSerial = 0;
	uint64 ConsumedForceSerial = 0;
};

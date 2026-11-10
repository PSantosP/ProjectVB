#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Types/VBSlideMovementSettings.h"
#include "VBSlideMovementComponent.generated.h"

struct FVBMovementProfile;
struct FVBGuardSet;
enum class EVBGait : uint8;

class FSavedMove_VBSlide;
class UVBLocomotionIntentComponent;
class UVBMovementConfig;
class UVBWeaponStateComponent;
class UVBTargetLockComponent;

/** CMC 걷기 바닥 위 슬라이드 - 충돌과 crouch 는 엔진의 예측 walking 경로에 그대로 둔다. */
UCLASS()
class VB_API UVBSlideMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	void RequestSlide();
	void CancelSlide(bool bRemainCrouched = false);
	bool IsSliding() const { return bSlideActive; }
	bool CanRequestSlide() const;

	// 공격을 중간에 끊을 때 남는 이동을 버린다 - 속도를 지우고 블렌드아웃 동안 루트모션 이동, 수평 속도를 매 틱 누른다.
	//   이동 입력, 공중, 다른 몽타주, 루트모션 소스, 마감 시각 중 하나면 바로 푼다.
	void StopResidualAttackMotion(float RestoreAfterSeconds, const UAnimMontage* SourceMontage);

	// 걷기, 앉은 속도를 정하는 설정. 캐릭터가 설정을 적용할 때 전달한다(없으면 클래스 기본값 - 속도 방향 곡선이 없다).
	void SetMovementConfig(const UVBMovementConfig* InConfig);

	// 캡슐 회전 방식을 지금 사실(의도, 락온, 전투, 이동 방향 담당 기능)로 정한다 - 매 이동 직전에 돈다.
	void RefreshRotationMode();
	// 이 컴포넌트의 틱을 멈춘다 - 멈추기 전에 회전 방식을 한 번 정한다. 틱을 멈추는 쪽은 SetComponentTickEnabled(false) 대신 이것을 쓴다.
	void StopMovementTick();

	// 이번 이동의 걸음 속도 셋(앞, 옆, 뒤)(순수). bGuarding = 가드 걷기를 가진 무기로 가드 중 - 그때만 질주, 달리기 대신 걷기.
	// 회피 클립이 ExitTime 에 닿을 때 이동 입력이 회피 방향 MaxAngleDeg 안이면 이어 달리기 클립으로 전환한다(회피 능력이 끝난 뒤에도 돈다).
	void ScheduleRunHandover(const UAnimMontage* FromMontage, UAnimMontage* ToRunMontage, float ExitTime,
		const FVector& DodgeDirection, float MaxAngleDeg);
	// 지금 입력이 넘길 만큼 회피 방향을 향하나(순수, 수평만 본다).
	static bool ShouldHandOverToRun(const FVector& Input, const FVector& DodgeDirection, float MaxAngleDeg);

	// 가드 중 걷기 속도로 묶는가(순수): 가드 중이고 손의 무기 가드 세트가 걷기까지 덮을 때만(Fighter, 한손검).
	static bool ShouldClampGuardToWalk(bool bGuardingTag, const FVBGuardSet* HeldGuard);
	static FVector PickGaitSpeeds(const FVBMovementProfile& Profile, bool bGuarding, bool bSprinting, bool bSprintRampDone, EVBGait Gait);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Slide", meta=(ClampMin="0"))
	float SlideStartSpeed = 380.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Slide", meta=(ClampMin="0"))
	float SlideEndSpeed = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Slide",
		meta=(ClampMin="0", ToolTip="0 disables the optional duration limit"))
	float SlideMaxDuration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Slide")
	FVBSlideMovementSettings SlideSettings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Slide", meta=(ClampMin="0"))
	float SlideMaxSpeed = 800.f;

	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	friend class FSavedMove_VBSlide;
	bool bWantsToSlide = false;
	bool bSlideActive = false;
	bool bCrouchAfterSlide = false;
	float SlideElapsed = 0.f;
	float GetSlideSlopeAngle() const;
	// 이번 프레임의 이동(Super::TickComponent) 뒤에 남은 이동을 누르거나 푼다. 이동이 일찍 끝난 틱에도 돈다.
	void TickResidualMotion();
	// 이번 프레임 이동 전에(모든 역할) MaxWalkSpeed, MaxWalkSpeedCrouched 를 채운다(GetMaxSpeed 는 미끄럼만 덮는다).
	void UpdateWalkSpeeds();
	void TickRunHandover();

	struct FRunHandover
	{
		TWeakObjectPtr<const UAnimMontage> From;
		TWeakObjectPtr<UAnimMontage> To;
		float ExitTime = 0.0f;
		FVector Direction = FVector::ZeroVector;
		float MaxAngleDeg = 0.0f;
	};
	FRunHandover PendingRunHandover;

	UPROPERTY(Transient)
	TObjectPtr<const UVBMovementConfig> MovementConfig;
	TWeakObjectPtr<UVBLocomotionIntentComponent> LocomotionIntent;
	TWeakObjectPtr<UVBWeaponStateComponent> WeaponState;
	TWeakObjectPtr<UVBTargetLockComponent> TargetLock;
	// 질주 2단 가속: 누르기 시작한 월드 시각과 지난 틱의 질주 여부.
	float SprintHoldStartTime = 0.f;
	bool bWasSprinting = false;
	bool bWarnedNoSpeedCurve = false;
	bool bWarnedNoIntent = false;

	// 남은 이동을 누르는 구간의 마감 시각(0 이하 = 비활성)과 그 몽타주 - 한 번만 지우면 같은 프레임 루트모션이 다시 채운다.
	double ResidualMotionSuppressUntil = 0.0;
	int32 ResidualMotionMontageInstanceId = INDEX_NONE;
	void SetSlideActive(bool bNewActive);
};

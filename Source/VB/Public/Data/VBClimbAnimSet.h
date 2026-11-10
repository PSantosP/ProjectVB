// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBClimbAnimSet.generated.h"

class UAnimMontage;
class UBlendSpace;

/**
 * 매달리기, 자유 등반, 사다리 클립 묶음과, 그 클립을 재서 얻은 자세 수치.
 * 수치는 클립을 바꾸면 같이 바뀌는 값이다(판정 임계는 UVBTraversalConfig).
 * 매달림 루프(대기, 옆걸음)는 ABP 의 BS_VB_LedgeHang 이 들고, C++ 는 축 값만 준다.
 */
UCLASS(BlueprintType)
class VB_API UVBClimbAnimSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 매달림, 등반, 사다리 몽타주를 멈추거나 갈아끼울 때의 블렌드아웃 시간(초)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb", meta=(ClampMin="0"))
	float MontageStopBlendTime = 0.15f;

	// 매달린 채 위로 올라서기(MotionWarping 노티 FrontLedge 가 렛지 안쪽 stand 지점에 맞춘다). 비면 매달림이 꺼지고 공중 잡기는 맨틀로 떨어진다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	TObjectPtr<UAnimMontage> HangClimbUpMontage;

	// 매달림 자세에서 렛지가 root 보다 위에 있는 거리(cm)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangRootBelowLedge = 130.0f;

	// 옆걸음 위상 0 에서의 렛지-root 높이(cm). 매 틱 클립 root 의 상하 흔들림을 더해 쓴다(고정하면 손이 뜨거나 박힌다)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangRootBelowLedgeShimmy = 142.0f;

	// 매달림 자세에서 손이 root 보다 벽 쪽으로 나간 거리(cm). 발 깊이와의 차이가 손 IK 보정량이 된다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangHandDepth = 33.0f;

	// 매달림 자세에서 발가락이 root 보다 벽 쪽으로 나간 거리(cm). 캐릭터를 벽에서 띄우는 기준이며 깊은 쪽(발)에 맞추고 손을 IK 로 당긴다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangFootDepth = 53.4f;

	// 옆걸음 위상 0 에서의 손 깊이(cm). 매 틱 클립 root 의 앞뒤 흔들림을 더해 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangHandDepthShimmy = 40.1f;

	// 대기와 옆걸음 사이 깊이를 옮겨가는 속도(cm/s). 한 번에 바꾸면 캐릭터가 벽에서 튄다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangDepthInterpSpeed = 60.0f;

	// 두 손이 렛지를 따라 벌어진 간격의 절반(cm). 손 IK 목표를 렛지 위 좌우로 잡는다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangHandLateralOffset = 13.5f;

	// 손 IK 목표를 렛지 모서리에서 위로 띄우는 양(cm)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangHandGripRise = 3.0f;

	// 심은 손의 IK 세기(0..1). 뻗는 중인 손에는 안 걸린다. 심은 손도 클립에서 흘러 한 점에 묶으면 IK 가 클립과 싸워 떨린다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangHandIKWeight = 1.0f;

	// 손을 벽 바깥으로 평행이동하는 양(cm). 0 이면 안 민다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangHandPushOut = 0.0f;

	// 벽 바깥 방향의 손뼈 로컬 표현. 손뼈 방향이 크게 다른 클립을 쓰면 다시 잰다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	FVector HangHandPushDirLocal = FVector(-0.35f, 0.93f, -0.13f);

	// 옆걸음 한 주기에서 앞선 손과 뒤따르는 손이 뻗는 구간(위상 0..1). 이 구간에는 그 손 IK 를 놓는다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HandReachLeadStart = 0.025f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HandReachLeadEnd = 0.150f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HandReachTrailStart = 0.575f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HandReachTrailEnd = 0.725f;

	// 뻗음과 심음 전환 블렌드 길이(위상). 0 이면 손이 튄다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HandReachBlendPhase = 0.05f;

	// 발목을 벽면에서 띄우는 양(cm)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangFootWallClearance = 8.0f;

	// 발 IK 세기(0..1)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangFootIKWeight = 1.0f;

	// 발 IK 의 벽면 탐색 거리(cm). 못 찾으면 그 발은 애니 그대로 둔다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangFootProbeDistance = 60.0f;

	// IK 가 0 에서 1 로 차오르는 시간(s)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangIKBlendInTime = 0.2f;

	// 옆걸음 몽타주. 있으면 클립 루트모션이 캡슐을 몰아 심은 손이 안 미끄러지고, 비면 HangShimmySpeed 일정 속도로 간다(손이 미끄러진다)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	TObjectPtr<UAnimMontage> ShimmyMontageL;

	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	TObjectPtr<UAnimMontage> ShimmyMontageR;

	// 몽타주 없이 갈 때의 일정 속도(cm/s)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangShimmySpeed = 54.0f;

	// 이 값을 넘는 좌우 입력만 옆걸음으로 본다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang", meta=(ClampMin="0", ClampMax="1"))
	float HangShimmyInputThreshold = 0.2f;

	// 질주(Shift)를 누른 옆걸음의 몽타주 배속. 1 이면 끔. 루트모션 이동이라 배속이 곧 이동 배율이다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang", meta=(ClampMin="1", ClampMax="3"))
	float ShimmyFastRate = 2.0f;

	// 올라선 뒤 root 가 렛지 모서리에서 안쪽으로 들어가는 거리(cm). 워프 타겟 지점이다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangClimbUpStandInset = 35.0f;

	// 손이 닿는 여유(cm). 렛지가 HangRootBelowLedge + 이 값보다 높으면 다음 틱에 다시 본다(바로 잡으면 캡슐이 점프 없이 솟는다)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangReachTolerance = 40.0f;

	// 잡는 순간 캡슐이 매달림 자리로 미끄러져 들어가는 시간(s)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangGrabBlendTime = 0.15f;

	// 옆걸음 축(-1..+1) 보간 속도
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Hang")
	float HangMoveAxisInterpSpeed = 8.0f;

	// 벽면 등반 2D 블렌드스페이스(가로 좌우, 세로 상하). 비어 있으면 자유 등반이 꺼진다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UBlendSpace> ClimbBlendSpace;

	// 방향별 등반 몽타주. 있으면 그 클립 루트모션이 캐릭터를 움직인다(블렌드스페이스 루트모션은 이동 컴포넌트에 안 닿는다). 오른쪽은 왼쪽 클립을 미러해 구웠다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageUp;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageDown;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageLeft;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageRight;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageUpLeft;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageUpRight;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageDownLeft;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageDownRight;

	// 몽타주가 비어 있는 방향의 일정 속도(cm/s). 그 방향은 발이 미끄러지고 부팅 경고가 알린다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbSpeed = 72.0f;

	// 한 틱 이동 상한이 ClimbSpeed 의 몇 배인가. 넘으면 잘라 내고 로그를 남긴다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free", meta=(ClampMin="0"))
	float ClimbMaxStepSpeedScale = 8.0f;

	// 입력 크기의 제곱이 이 값을 넘어야 움직이는 입력이다. 몽타주 선택과 벽 깊이가 같이 쓴다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free", meta=(ClampMin="0"))
	float ClimbMoveInputSizeSquaredMin = 0.04f;

	// 8방향을 고를 때 그 축을 밀고 있다고 보는 입력 문턱
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free", meta=(ClampMin="0", ClampMax="1"))
	float ClimbAxisInputThreshold = 0.4f;

	// 질주(Shift)를 누른 자유 등반의 몽타주 배속. 1 이면 끔. ClimbSpeed 는 한 틱 상한과 폴백에만 쓰인다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free", meta=(ClampMin="1", ClampMax="3"))
	float ClimbFastRate = 2.0f;

	// 손목, 발목을 벽면에서 띄우는 여유(cm)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbLimbWallClearance = 3.0f;

	// 발 전용 여유(cm). 발끝이 스켈레톤에 없어 뼈로 못 재고 눈으로 맞춘 값이다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbFootWallClearance = 8.0f;

	// 말단을 면에 맞출 때 한 번에 옮길 최대 거리(cm). 붙은 말단은 온전히 맞추고 뻗는 말단은 상한만큼만 당긴다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbIKMaxCorrection = 6.0f;

	// 오차를 한 프레임에 따라잡는 비율(0..1). 1 이면 진동하므로 조금씩 고친다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbIKGain = 0.35f;

	// IK 세기가 잡음과 뻗음 사이를 오갈 때의 보간 속도(초당)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbIKWeightInterpSpeed = 10.0f;

	// 면을 못 찾은 말단의 보정을 클립으로 되돌리는 보간 속도(FInterpTo)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free", meta=(ClampMin="0"))
	float ClimbIKCorrectionFadeSpeed = 10.0f;

	// 손, 발 IK 세기(0..1). 0 이면 그 말단은 클립 그대로다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbHandIKWeight = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbFootIKWeight = 1.0f;

	// 벽면에서 root 를 떼어 놓는 거리(cm), 대기와 이동. 가장 깊은 말단의 최대값으로 잰다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbWallDepthIdle = 48.7f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbWallDepthMoving = 45.0f;

	// 손발이 root 에서 옆으로 가장 멀리 뻗는 거리(cm). 옆 끝 판정과 진입이 이 폭만큼 면이 있는지 본다. 클립을 바꾸면 다시 잰다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free", meta=(ClampMin="1"))
	float ClimbLimbHalfSpan = 73.0f;

	// 대기와 이동 사이 깊이 보간 속도(cm/s)와 축 보간 속도
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbDepthInterpSpeed = 60.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbMoveAxisInterpSpeed = 8.0f;

	// 붙는 순간 캡슐이 벽 자리로 미끄러져 들어가는 시간(s)
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbEnterBlendTime = 0.15f;

	// 바뀐 방향이 이만큼 유지돼야 몽타주를 갈아끼운다(초). 키보드 전환 중 중간 방향을 스치는 것을 막으며 대기에서 처음 거는 것에는 안 걸린다
	UPROPERTY(EditDefaultsOnly, Category="VB|Climb|Free")
	float ClimbDirectionHoldTime = 0.12f;

	// 사다리 대기 자세. 제자리 클립을 몽타주로 건다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	TObjectPtr<UAnimMontage> LadderIdleMontage;

	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	TObjectPtr<UAnimMontage> LadderUpMontage;

	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	TObjectPtr<UAnimMontage> LadderDownMontage;

	// 좌우를 뒤집은 대기. 비면 LadderIdleMontage 하나로 가며, 한 벌뿐이면 절반의 위상에서 손발이 교차하며 튄다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	TObjectPtr<UAnimMontage> LadderIdleMirrorMontage;

	// 사다리 면에서 root 까지의 거리(cm). 손 접촉 깊이에 맞추고 발 깊이 차이는 사다리 IK 노드가 보정한다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderDepth = 38.0f;

	// 클립 안에서 발이 가로대를 딛는 높이 위상(cm, 가로대 간격으로 나눈 나머지). 몽타주 시작 위상을 골라 순간이동 없이 디딤을 가로대에 맞춘다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderStancePhase = 1.67f;

	// 위쪽 손이 root 보다 위에 있는 높이(cm). 사다리 높이에서 이 값을 뺀 데까지만 올라 손이 허공을 잡지 않는다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderTopHandReach = 189.0f;

	// 대기 자세에서 아래쪽 발이 root 보다 위에 있는 높이(cm). 멈출 때 그 발이 가로대에 놓이도록 높이를 당긴다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderIdleFootOffset = 42.7f;

	// 멈출 때 가로대 자리로 당겨 앉는 속도(FInterpTo). 0 이면 안 당긴다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderIdleSettleSpeed = 8.0f;

	// 입력이 끊긴 뒤 이만큼 조용해야 당겨 앉기가 시작된다(초). 연타 사이에 끼어들지 않게 탭 간격보다 길어야 한다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderIdleSettleDelay = 0.2f;

	// 한 말단의 최대 보정 거리(cm). 닿지 못하는 목표는 상한이 아니라 캐릭터 배치부터 고친다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder", meta=(ClampMin="0"))
	float LadderIKMaxCorrection = 36.0f;

	// 지지 발볼이 root 보다 앞에 놓이는 거리(cm). 0 이면 원본 발 궤적을 유지한다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder", meta=(ClampMin="0"))
	float LadderFootPoseDepth = 0.0f;

	// 기본 포즈 보정 뒤 발판에 고정할 잔차 한도이자 접촉 획득 거리(cm)
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder", meta=(ClampMin="0"))
	float LadderFootIKMaxCorrection = 12.0f;

	// hand_l/r 뼈 로컬 공간의 봉 그립 중심(cm). 0 이면 손목 원점. 클립과 체형별로 잰다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	FVector LadderLeftHandGripOffset = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	FVector LadderRightHandGripOffset = FVector::ZeroVector;

	// 잡은 손과 밟은 발의 IK 세기(0..1). 0 이면 그 말단은 클립 그대로다(접촉/해제 속도는 AnimNode_VBLadderIK 설정)
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderHandIKWeight = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderFootIKWeight = 1.0f;

	// 붙는 순간 사다리 자리로 옮겨 가는 시간(초)
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderEnterBlendTime = 0.15f;

	// 오르내리는 기준 속도(cm/s). 이동은 클립 루트모션이 맡고 몽타주가 없을 때의 폴백과 한 틱 상한에만 쓴다. AVBLadder::RungSpacing 과 같은 클립에서 나와 클립을 갈면 둘 다 다시 잰다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder")
	float LadderSpeed = 33.5f;

	// 한 틱 이동 상한이 LadderSpeed 의 몇 배인가. 클립이 낼 수 없는 값이라 잘렸다면 다른 이동이 더해진 것이다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder", meta=(ClampMin="0"))
	float LadderMaxStepSpeedScale = 8.0f;

	// 이 값을 넘는 위아래 입력만 오르내림으로 본다. 바닥 내리기 판정도 같이 쓴다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder", meta=(ClampMin="0", ClampMax="1"))
	float LadderMoveInputThreshold = 0.2f;

	// 질주(Shift)를 누른 오르기와 내리기 몽타주 배속. 1 이면 끔. 3 을 초과하면 IK 가 손발 교차를 못 따라간다
	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder", meta=(ClampMin="1", ClampMax="3"))
	float LadderFastUpRate = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Ladder", meta=(ClampMin="1", ClampMax="3"))
	float LadderFastDownRate = 2.0f;
};

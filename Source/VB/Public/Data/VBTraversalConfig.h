// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h" // ECollisionChannel
#include "VBTraversalConfig.generated.h"

class UChooserTable;

/**
 * 트래버설 트레이스와 ActionType 판정 임계값. GASP TryTraversalAction 의 리터럴을 데이터로 뺀 것.
 * Vault = 얇음 + 뒤 바닥 없음, Hurdle = 얇음 + 뒤 바닥 있음, Mantle = 두꺼움(chooser 가 높이로 Mantle/Climb 분기).
 */
UCLASS(BlueprintType)
class VB_API UVBTraversalConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 트래버설 몽타주 chooser(컨텍스트 FVBTraversalChooserParams). 비어 있으면 맨틀, 볼트, 허들이 거절된다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Chooser")
	TObjectPtr<UChooserTable> TraversalChooserTable;

	// 전방 트레이스 거리 매핑: 로컬 전방 속도 Min..Max 를 거리 Min..Max 로 옮긴다(MapRangeClamped)
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	float TraceForwardSpeedMin = 0.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	float TraceForwardSpeedMax = 500.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	float TraceForwardDistanceMin = 75.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	float TraceForwardDistanceMax = 350.0f;

	// 공중 맨틀 전용 고정 전방 거리(cm). 공중엔 run-up 이 없어 속도 비례 거리를 쓰면 먼 벽에 끌려간다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	float AirMantleTraceDistance = 100.0f;

	// 전방, 공간, 상단, 하향 트레이스 채널
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// 등반 가능한 면만 이 채널에 Block 으로 응답한다(기본 Ignore). 응답만 켜야 다른 질의에 영향이 없다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Climb")
	TEnumAsByte<ECollisionChannel> ClimbTraceChannel = ECC_GameTraceChannel2;

	// 붙을 면을 앞에서 찾는 거리(cm)
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Climb")
	float ClimbProbeDistance = 100.0f;

	// 붙어 있는 동안 면을 다시 찾을 때 면 바깥으로 물러나 쏘는 거리와 파고드는 깊이(cm)
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Climb")
	float ClimbProbeOutset = 60.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Climb")
	float ClimbProbeDepth = 120.0f;

	// 등반 대상 면의 법선 |Z| 상한(0.35 = 약 70도 이상). 바닥에 붙어 기어가는 것을 막는다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Climb")
	float ClimbMaxSurfaceTiltZ = 0.35f;

	// 벽에 붙어 있는지 보는 두 번째 탐침 높이(root 위 cm). 옆 끝은 UVBClimbAnimSet::ClimbLimbHalfSpan 이 맡는다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Climb")
	float ClimbBodyProbeRise = 150.0f;

	// 갈 수 있는 방향인지 미리 보는 거리(cm). 한 틱 이동량보다 커야 한다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Climb")
	float ClimbEdgeProbeAhead = 12.0f;

	// 꼭대기 올라서기를 거는 위 입력의 최소 크기(0..1)
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Climb", meta=(ClampMin="0.01", ClampMax="1"))
	float ClimbTopOutMinUpInput = 0.5f;

	// 사다리를 찾는 반경(cm). 이 안의 AVBLadder 중 앞쪽에서 마주 보는 것을 고른다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Ladder")
	float LadderFindRadius = 220.0f;

	// 사다리를 마주 보고 있다고 볼 각도의 코사인. 0.5 = 60도 안.
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Ladder")
	float LadderFacingDot = 0.5f;

	// 사다리 꼭대기 올라서기 허용 높이(cm). 위 바닥이 축 꼭대기에서 이만큼 위 안이어야 올라서고, 밖이면 꼭대기에 남는다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Ladder")
	float LadderTopOutFloorTolerance = 40.0f;

	// ObstacleDepth 임계: 값 미만 -> 얇음(Vault/Hurdle), 이상 -> 두꺼움(Mantle)
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Decision")
	float ObstacleDepthThreshold = 59.0f;

	// Vault: Height 가 Min..Max, Depth 가 임계 미만, 뒤 바닥 없음
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Vault")
	float VaultHeightMin = 50.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Vault")
	float VaultHeightMax = 125.0f;

	// Hurdle: Height 가 Min..Max, Depth 가 임계 미만, 뒤 바닥 있음, BackLedgeHeight 가 MinBackLedgeHeight 초과
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Hurdle")
	float HurdleHeightMin = 50.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Hurdle")
	float HurdleHeightMax = 125.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Hurdle")
	float HurdleMinBackLedgeHeight = 50.0f;

	// Mantle: Height 가 Min..Max, Depth 가 임계 이상 (chooser 가 150 이하 Mantle, 150 초과 Climb)
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Mantle")
	float MantleHeightMin = 50.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Mantle")
	float MantleHeightMax = 275.0f;

	// 하향 프로브(TraceFloorBelowLedge) 시작점을 렛지 면에서 띄우는 여유(cm). 표면에 붙이면 시작 지점 히트가 나온다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	float LedgeProbeSkin = 2.0f;

	// 뒤 렛지 아래 바닥 탐색 깊이 여유(cm). 깊이 = ObstacleHeight - 캡슐 반높이 + 이 값. 없으면 Hurdle 이 Vault 로 잘못 분류된다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	float BackFloorProbeExtraDepth = 50.0f;

	// 공중 맨틀에서 벽 실높이를 재는 앞면 하향 프로브 여유(cm). 깊이 = MantleHeightMax + 이 값
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Trace")
	float FrontFloorProbeExtraDepth = 50.0f;

	// 놓은 직후 같은 렛지를 다시 잡지 못하는 시간(s). 점프 키를 누른 채 놓아도 바로 다시 잡지 않는다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Hang")
	float HangRegrabDelay = 0.5f;

	// 질주 + 앞 입력 중에 수동 점프와 같은 판정(TryTraversalAction)을 주기마다 부른다. 못 찾으면 아무것도 안 한다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Auto")
	bool bAutoTraversalOnSprint = true;

	// 판정 주기(s). 질주 속도에서도 전방 탐색 거리 안에서 놓치지 않을 만큼 짧아야 한다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Auto", meta=(ClampMin="0.02"))
	float AutoTraversalCheckInterval = 0.1f;

	// 입력 방향과 캐릭터 앞 방향의 내적 하한. 옆으로 스치거나 물러나는 중에는 붙지 않는다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Auto", meta=(ClampMin="-1", ClampMax="1"))
	float AutoTraversalMinForwardDot = 0.7f;

	// 수평 속도 하한(cm/s). 0 = 속도는 안 본다. 0 보다 크면 사다리 Blocker 에 막혀 멈춘 순간 못 붙을 수 있다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Auto", meta=(ClampMin="0"))
	float AutoTraversalMinSpeed = 0.0f;

	// 클라가 보낸 렛지/워프 좌표와 서버 캐릭터 사이 허용 최대 거리(cm). 넘으면 거부한다(안티치트 상한). 트레이스, 맨틀 값을 올리면 사람이 같이 올린다
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Network")
	float MaxPlausibleLedgeDistance = 600.0f;

	// 사다리 서버 검사의 지연 여유(cm). 캐릭터가 LadderFindRadius + 이 값 안, 타는 면 쪽이어야 붙인다(안티치트 상한)
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal|Network", meta=(ClampMin="0"))
	float LadderServerDistanceSlack = 100.0f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

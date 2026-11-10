// Copyright VB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBLadder.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * 오를 수 있는 사다리. 트레이스로는 축과 위아래 끝을 못 얻어서 전용 액터로 둔다.
 * +X 가 바깥 법선(타는 사람은 -X 를 본다), +Z 가 축, 액터 위치가 축의 바닥이다.
 */
UCLASS()
class VB_API AVBLadder : public AActor
{
	GENERATED_BODY()

public:
	AVBLadder();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	// 걸어서 지나가지 못하게 막는 상자. 타는 사람은 자기 캡슐만 이것을 무시한다(FVBLadderMode::Start).
	FORCEINLINE UBoxComponent* GetBlocker() const { return Blocker; }

	// 형체가 차지하는 액터 지역 범위. 막는 상자의 크기다.
	static FBox ComputeBlockerBounds(float Height, float Width, float Depth, float Thickness);

	// 축 위의 한 점. Height 는 0(바닥)에서 LadderHeight(꼭대기) 사이다.
	FVector GetAxisPoint(float Height) const;

	// 바깥 법선. 타는 사람은 이 반대를 본다.
	FVector GetFacingNormal() const;

	FORCEINLINE float GetLadderHeight() const { return LadderHeight; }

	// 월드 한 점을 축 위 높이로 바꾼다. 범위 밖이면 잘라 준다.
	float ProjectToHeight(const FVector& WorldPoint) const;

protected:
	// 세로대 둘과 가로대를 인스턴스로 찍는다. 높이만 바꾸면 가로대 수가 따라온다.
	void RebuildVisual();

	// 막는 상자를 지금 치수에 맞춘다. 예전에 놓인 사다리도 맞도록 BeginPlay 에서도 부른다.
	void UpdateBlocker();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	TObjectPtr<USceneComponent> Pivot;

	// 걸어서 관통하지 못하게 Pawn 만 막는 상자. 형체(Parts)는 충돌이 없다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	TObjectPtr<UBoxComponent> Blocker;

	// 편집기에서 타는 면 쪽을 보여 주는 상자. 콜리전 없음(찾기는 액터 검색으로).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	TObjectPtr<UBoxComponent> Marker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	TObjectPtr<UInstancedStaticMeshComponent> Parts;

	// 세로대, 가로대 메시. 비워도 사다리는 동작한다(시각물).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	TObjectPtr<UStaticMesh> PartMesh;

	// 축 길이(cm). 액터 위치가 바닥이고 여기까지가 꼭대기다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	float LadderHeight = 400.0f;

	// 세로대 사이 간격(cm). 가로대 길이이기도 하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	float LadderWidth = 66.0f;

	// 가로대 간격(cm). 사다리 클립이 한 번 디딜 때 오르는 높이(총 상승 / 디딤 횟수)라 주기마다 어긋남이 없다.
	//   UVBClimbAnimSet::LadderSpeed 와 같은 클립에서 나왔으므로 클립을 바꾸면 둘 다 다시 잰다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	float RungSpacing = 40.255f;

	// 가로대를 보기에 몇 칸으로 쪼갤지. 디딤 간격(RungSpacing)을 줄이면 발이 비껴가서 정수로만 쪼갠다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	int32 RungSubdivision = 2;

	// 가로대가 세로대 면보다 타는 사람 반대쪽으로 뻗은 길이(cm). 0 이면 평범한 사다리다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	float RungDepth = 0.0f;

	// 가로대 격자를 통째로 내리는 양(cm). 클립의 손, 발 높이 차를 어느 쪽에 맞출지 정한다(0 = 발 기준).
	//   GetRungTopOffset 에 들어가므로 꼭대기 한계, 시작 위상, 당겨 앉기가 함께 따라온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	float RungDropOffset = 6.6f;

	// 대의 굵기(지름, cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ladder")
	float PartThickness = 4.0f;

public:
	FORCEINLINE float GetRungSpacing() const { return RungSpacing; }
	FORCEINLINE int32 GetRungSubdivision() const { return RungSubdivision; }
	FORCEINLINE float GetLadderWidth() const { return LadderWidth; }
	FORCEINLINE float GetRungDepth() const { return RungDepth; }
	FORCEINLINE float GetPartThickness() const { return PartThickness; }

	FORCEINLINE float GetRungDropOffset() const { return RungDropOffset; }

	// 가로대 윗면의 높이 오프셋. 발이 닿는 자리라 배치, 정렬이 이 값을 본다.
	FORCEINLINE float GetRungTopOffset() const { return PartThickness * 0.5f - RungDropOffset; }
};

// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Traversal/VBTraversalTypes.h"
#include "VBLevelBlock_Traversable.generated.h"

class USplineComponent;
class UStaticMeshComponent;
class UVBPushableComponent;

/**
 * 트래버설 가능한 장애물(GASP LevelBlock_Traversable). 스플라인 컴포넌트가 렛지 모서리다.
 * 캐릭터에 가장 가까운 렛지가 front, OppositeLedges 로 찾은 것이 back 이다.
 */
UCLASS()
class VB_API AVBLevelBlock_Traversable : public AActor
{
	GENERATED_BODY()

public:
	AVBLevelBlock_Traversable();

	/** 트레이스 적중 위치와 캐릭터 위치로 Result 의 front/back 렛지 필드를 채운다. */
	UFUNCTION(BlueprintCallable, Category="VB|Traversal")
	void GetLedgeTransforms(const FVector& HitLocation, const FVector& ActorLocation, UPARAM(ref) FVBTraversalCheckResult& Result) const;

	FORCEINLINE bool ShouldAlwaysHang() const { return bAlwaysHang; }
	FORCEINLINE float GetMinLedgeWidth() const { return MinLedgeWidth; }

	// 상자(Box, 회전 없는 블록 공간) 윗면 네 모서리의 띠와 그 아래 옆면 띠를 100cm 상자 메시 기준 트랜스폼으로 낸다.
	// 앞 4개가 윗면, 뒤 4개가 옆면(FaceHeight 가 0 이면 윗면 4개만).
	static void ComputeLedgePaintTransforms(const FBox& Box, float Width, float FaceHeight, TArray<FTransform>& Out);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	// 넘을 수 있다는 표시 띠를 다시 그린다. 레벨 액터는 게임에서 생성 스크립트를 다시 돌지 않으므로 BeginPlay 도 부른다.
	void RebuildLedgePaint();

	// 장애물 메시(트레이스 대상).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Traversal")
	TObjectPtr<UStaticMeshComponent> ObstacleMesh;
	// 밀기 설정을 준 블록만 활성. 렛지와 부착물을 함께 옮긴다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UVBPushableComponent> Pushable;

	// 렛지 스플라인들. OnConstruction 이 자동 수집한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Traversal")
	TArray<TObjectPtr<USplineComponent>> Ledges;

	// front 렛지 -> 반대편 back 렛지(Vault/Hurdle 용).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	TMap<TObjectPtr<USplineComponent>, TObjectPtr<USplineComponent>> OppositeLedges;

	// 렛지 최소 폭. 이보다 좁으면 front 무효, 모서리에서 절반만큼 안쪽으로 클램프한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	float MinLedgeWidth = 60.0f;

	// 높이와 무관하게 매달림으로만 잡는다. false 면 렛지 아래 바닥 유무가 정한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	bool bAlwaysHang = false;

	// 캐릭터에 가장 가까운 렛지 스플라인(없으면 nullptr).
	USplineComponent* FindLedgeClosestToActor(const FVector& ActorLocation) const;

	// OnConstruction 이 자동 생성한 옆면 렛지(재구성 시 정리).
	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineComponent>> DerivedSideLedges;

	// RebuildLedgePaint 가 만든 표시 띠. 충돌 없음, 저장 안 함.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LedgePaint;

	// 앞/뒤 모서리 끝점을 이어 옆면 렛지를 만든다(앞뒤만 두면 좌우가 생긴다).
	USplineComponent* CreateSideLedge(const FVector& A, const FVector& B, const FVector& Center);
};

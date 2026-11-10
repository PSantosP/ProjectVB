// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"   // EVisibilityBasedAnimTickOption
#include "Engine/EngineTypes.h"                 // ECollisionChannel
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"
#include "Types/VBWeaponStateTypes.h"      // EVBWeaponType - 날 풀기 창구가 값으로 받는다
#include "VBMeleeTraceComponent.generated.h"

namespace VBCollision
{
	// 근접 판정 채널(DefaultEngine.ini 의 VBMeleeTrace). 판정 쪽과 맞는 쪽(쓰러진 캐릭터의 래그돌)이 같이 읽는다.
	inline constexpr ECollisionChannel MeleeTrace = ECC_GameTraceChannel3;
}

class UVBMeleeTraceShapeSet;
class UVBSwingTrailProfile;
class UVBWeaponVisualConfig;
class USkeletalMeshComponent;
struct FVBMeleeTraceShapeDef;

/** 풀어 낸 날 하나 - 어느 뼈 공간의 어느 선분인가. */
struct FVBResolvedMeleeShape
{
	FGameplayTag ShapeTag;
	FName Bone;
	// 실제 외형 파트 번호. 뼈 모양으로 풀렸으면 INDEX_NONE이다.
	int32 VisualPartIndex = INDEX_NONE;
	FVector BaseLocal = FVector::ZeroVector;
	FVector TipLocal = FVector::ZeroVector;
	float Radius = 0.0f;
	// 실제로 풀린 모양의 기본 궤적(대체 모양으로 떨어졌으면 그 모양의 것). 판정은 안 쓴다.
	TWeakObjectPtr<const UVBSwingTrailProfile> TrailProfile;
	// 실제로 풀린 모양의 타격 큐(FVBMeleeTraceShapeDef::ImpactCueTag, 대체 모양이면 그 모양의 것). 비면 공격 설정의 큐.
	FGameplayTag ImpactCueTag;

	// 날은 두 끝이 달라야 한다. 구(두 끝이 같음)는 Bone 모양으로 저작했을 때만 유효하다.
	bool IsValid() const { return !Bone.IsNone() && Radius > 0.0f && !BaseLocal.Equals(TipLocal); }
};

// 이번 프레임에 새로 맞은 표적들. 표적당 가장 이른 접촉 하나씩이고, 벽, 중복 검사를 통과한 것만 온다.
DECLARE_DELEGATE_OneParam(FVBMeleeTraceHits, const TArray<FHitResult>& /*NewHits*/);

/**
 * 무기 궤적 적중 판정. 창이 열린 동안 매 프레임 부위마다 지나간 자리를 캡슐로 쓸어 새로 맞은 표적을 GA 에 전달한다.
 * 뼈를 애니 평가 뒤에 읽으려고 TG_PostUpdateWork 에 메시 뒤로 틱한다. 창은 서버에서만 연다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBMeleeTraceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBMeleeTraceComponent();
	// 그 액터에 붙은 것. 없으면 nullptr - 근접 판정을 하지 않는 액터다.
	static UVBMeleeTraceComponent* FindFor(const AActor* Actor);

	// 창을 연다. 여러 부위가 한 창(한 적중 목록)을 쓰고, RehitIntervalSeconds 가 0 이면 표적당 한 번이다.
	//   풀리는 부위가 없으면 경고하고 INDEX_NONE. 적중의 MyBoneName 이 닿은 부위의 뼈다.
	int32 BeginWindow(const FGameplayTagContainer& ShapeTags, float RehitIntervalSeconds, FVBMeleeTraceHits OnHits,
		float RadiusScale = 1.0f);
	int32 BeginWindow(FGameplayTag ShapeTag, float RehitIntervalSeconds, FVBMeleeTraceHits OnHits, float RadiusScale = 1.0f);

	// 판정 창 notify 의 이벤트 태그 중 Melee.Shape.* 자식들. 공격 GA 가 창의 모양을 이것으로 읽는다.
	static FGameplayTagContainer ShapesFromEventTags(const FGameplayTagContainer& EventTags);

	// 모양 반지름 x 스윙 배율. 유한하지 않거나 0 이하인 배율은 1 로 읽는다 - 잘못된 배율이 적중을 지우지 않게.
	static float ScaleShapeRadius(float Radius, float Scale);
	// 정상 노티 종료만 마지막 자세까지 검사한다. 취소, 섹션 점프는 즉시 닫는다.
	void EndWindow(int32 WindowId, bool bSweepFinalPose = false);
	void EndAllWindows();
	bool HasOpenWindows() const { return Windows.Num() > 0; }

	// 날을 푼다. 휘두름 이펙트도 이 함수로 같은 날을 따른다(판정과 궤적이 같은 칼).
	bool ResolveShape(FGameplayTag ShapeTag, FVBResolvedMeleeShape& OutShape) const;

	// 날 풀기의 홈. 위 함수와 저작 도구가 같은 계산을 쓴다.
	static bool ResolveShapeFromData(const UVBMeleeTraceShapeSet* InShapeSet, FGameplayTag ShapeTag,
		const UVBWeaponVisualConfig* VisualConfig,
		EVBWeaponType WeaponType, FVBResolvedMeleeShape& OutShape);

	// 저작 도구(Content/Python/weapon_trace_tools.py)가 부르는 창구. 게임 코드는 쓰지 않는다.
	UFUNCTION(BlueprintCallable, Category="VB|Combat")
	static bool ResolveShapeForTools(const UVBMeleeTraceShapeSet* InShapeSet, FGameplayTag ShapeTag,
		const UVBWeaponVisualConfig* VisualConfig,
		EVBWeaponType WeaponType, FName& OutBone, FVector& OutBaseLocal, FVector& OutTipLocal, float& OutRadius);
	// 모양의 뼈가 사는 메시. 휘두름 궤적도 이 메시를 읽는다 - 따로 찾으면 판정과 궤적이 다른 메시를 볼 수 있다.
	USkeletalMeshComponent* GetMesh() const;
	// 끝점 사이 거리뿐 아니라 회전, 배율 변화가 만드는 이동량으로 보간 밀도를 정한다.
	static int32 CalculateSweepSteps(const FVBResolvedMeleeShape& Shape, const FTransform& A, const FTransform& B, int32 StepLimit);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 이 캐릭터가 휘두를 수 있는 날들. 캐릭터 BP 마다 꽂는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TObjectPtr<UVBMeleeTraceShapeSet> ShapeSet;

	// 판정 채널. 캡슐이 아니라 바디 메시에만 닿게 한다. 래그돌은 VBCollision::MeleeTrace 를 읽으므로 바꾸지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TEnumAsByte<ECollisionChannel> TraceChannel = VBCollision::MeleeTrace;

	// 한 프레임의 이동, 회전 거리 / 반경 만큼 쪼갠다. 끝점 간 직선 거리만 재면 긴 칼의 회전을 적게 나눈다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="1", ClampMax="16"))
	int32 MaxSubsteps = 16;

	// 벽 너머 표적을 버린다: 가슴 높이에서 접점까지 Visibility 선이 막히면 안 맞은 것으로 친다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	bool bRejectHitsBehindWalls = true;

	// 벽 검사 선의 시작 높이(액터 위치 기준, cm).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(EditCondition="bRejectHitsBehindWalls"))
	float WallCheckHeight = 50.0f;

private:
	friend class FVBMeleeTraceWorldCollisionTest;
	// 창의 모양 하나와 그 스윕 기록.
	struct FPart
	{
		FVBResolvedMeleeShape Shape;
		FTransform PrevBone;
		bool bHasPrev = false;
		bool bQueriedInitialPose = false;
	};
	struct FWindow
	{
		int32 Id = INDEX_NONE;
		TArray<FPart> Parts;
		float RehitInterval = 0.0f;
		FVBMeleeTraceHits OnHits;
		// 창이 열린 뒤 흐른 시간. 공격자의 시간 배율(히트스톱)이 곱해진 틱 시간이라 애니 시간과 같이 간다.
		float Elapsed = 0.0f;
		// 정상 종료 검사와 컴포넌트 틱이 같은 프레임에 와도 다단 히트 시간을 두 번 더하지 않는다.
		uint64 LastSampleFrame = MAX_uint64;
		TMap<TWeakObjectPtr<AActor>, float> LastHitTime;
	};
	TArray<FWindow> Windows;
	int32 NextWindowId = 1;

	static bool ResolveShapeDef(const UVBMeleeTraceShapeSet* InShapeSet, FGameplayTag ShapeTag, const FVBMeleeTraceShapeDef& Def,
		const UVBWeaponVisualConfig* VisualConfig,
		EVBWeaponType WeaponType, FVBResolvedMeleeShape& OutShape, int32 Depth);

	// 날 하나를 A 자세에서 B 자세까지 캡슐로 쓴다. 표적마다 가장 이른 접촉을 BestPerActor 에 모은다(단계 번호가 이른 쪽).
	void SweepSegment(const FVBResolvedMeleeShape& Shape, const FTransform& A, const FTransform& B, int32 StepIndex,
		TMap<AActor*, TPair<int32, FHitResult>>& BestPerActor) const;
	// 첫 겹침, 프레임 사이 이동, 마지막 구간이 같은 판정을 쓴다. Current 는 부위마다 뼈 변환 하나(Window.Parts 순서).
	void GatherWindowHits(FWindow& Window, TConstArrayView<FTransform> Current, float DeltaTime, TArray<FHitResult>& OutHits) const;
	void SampleParts(const FWindow& Window, const USkeletalMeshComponent& Mesh, TArray<FTransform>& OutCurrent) const;
	// 표적 접점을 채우고 벽 너머면 거짓.
	bool FinalizeContact(FHitResult& Hit) const;

	// 뼈 갱신 강제. 창마다 FVBMeshRefresh 요청 하나를 유지하고 닫힐 때 해제한다.
	void AcquireBoneRefresh();
	void ReleaseBoneRefresh();
	int32 BoneRefreshCount = 0;
	TWeakObjectPtr<USkeletalMeshComponent> RefreshedMesh;
};

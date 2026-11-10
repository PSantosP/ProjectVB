// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Character/VBMeleeTraceComponent.h" // FVBResolvedMeleeShape - 궤적은 판정과 같은 날을 따른다
#include "VBSwingTrailComponent.generated.h"

class UProceduralMeshComponent;
class UVBSwingTrailProfile;
class UParticleSystemComponent;
class UMeshComponent;
struct FVBAnimTrailBinding;

/**
 * 휘두름 궤적 띠. 궤적 노티가 열고 닫으며, 날 모양은 판정(UVBMeleeTraceComponent)과 같은 데이터를 쓴다.
 * 노티 인스턴스는 몽타주마다 공유되므로 상태는 이 컴포넌트가 든다. 치장이라 처음 쓸 때 붙이고, 전용 서버에서는 만들지 않는다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBSwingTrailComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBSwingTrailComponent();

	// 액터에 없으면 만들어 붙인다. 전용 서버면 nullptr.
	static UVBSwingTrailComponent* FindOrAdd(AActor* Owner);

	// Key = 궤적을 연 노티 객체. ShapeTags 마다 띠 하나, 휘두름 소리는 한 번. 끝 신호가 안 오면 ExpectedDuration 의 몇 배 뒤 닫는다.
	void BeginTrails(const void* Key, const FGameplayTagContainer& ShapeTags, const UVBSwingTrailProfile* OverrideProfile,
		float ExpectedDuration);
	// 새 조각을 더 쌓지 않는다. 남은 조각이 다 사라지면 띠를 치운다.
	void EndTrail(const void* Key);
	// 외형 메시를 옮기거나 없애기 전에 해당 메시를 쓰는 궤적을 정리한다.
	void RetireVisualSources();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FSegment
	{
		FVector Base;
		FVector Tip;
		float Age = 0.0f;
	};

	struct FTrail
	{
		const void* Key = nullptr;
		FVBResolvedMeleeShape Shape;
		TWeakObjectPtr<const UVBSwingTrailProfile> Profile;
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;
		TWeakObjectPtr<UParticleSystemComponent> Particle;
		TWeakObjectPtr<UMeshComponent> ParticleSource;
		bool bUsesAnimTrail = false;
		float TailTime = 0.0f;
		float MaxTailTime = 0.0f;
		FTransform PrevBone;
		bool bHasPrev = false;
		bool bEnding = false;
		// 열린 뒤 흐른 시간(공격자 시간)과 스스로 닫는 한계. 끝 신호를 잃은 궤적이 영원히 붙어 있지 않게 한다.
		float OpenTime = 0.0f;
		float MaxOpenTime = 0.0f;
		TArray<FSegment> Segments;
	};
	TArray<FTrail> Trails;

	// 모양과 메시를 관리하는 컴포넌트. 궤적은 판정 컴포넌트가 보는 메시의 뼈를 읽는다(모양의 뼈가 그 메시 기준이다).
	TWeakObjectPtr<const UVBMeleeTraceComponent> Trace;

	// 다 쓴 띠를 숨겨 두었다가 다음 스윙에 다시 쓴다. Outer 가 액터라 GC 에 안전하다.
	TArray<TWeakObjectPtr<UProceduralMeshComponent>> FreeMeshes;

	// 띠를 다시 지을 때 쓰는 정점 버퍼. 매 프레임 새로 할당하지 않게 멤버로 두고 용량을 유지한다.
	TArray<FVector> ScratchVertices;
	TArray<int32> ScratchTriangles;
	TArray<FVector> ScratchNormals;
	TArray<FVector2D> ScratchUV0;
	TArray<FLinearColor> ScratchColors;

	USkeletalMeshComponent* GetOwnerMesh() const;
	// 모양 하나에 띠 하나를 연다. 휘두름 소리를 틀었는지 돌려준다.
	bool OpenTrail(const void* Key, FGameplayTag ShapeTag, const UVBSwingTrailProfile* OverrideProfile, float ExpectedDuration,
		bool bPlayWhoosh);
	UProceduralMeshComponent* AcquireTrailMesh(const UVBSwingTrailProfile& Profile);
	void ReleaseTrailMesh(FTrail& Trail);
	bool OpenAnimTrail(FTrail& Trail, FGameplayTag ShapeTag, const FVBAnimTrailBinding& Binding);
	void StopTrail(FTrail& Trail);
	void DestroyAnimTrail(FTrail& Trail);
	bool TickAnimTrail(FTrail& Trail, float DeltaTime);
	void AppendSegments(FTrail& Trail, const FTransform& Current, float DeltaTime) const;
	void RebuildMesh(FTrail& Trail);
	// 한 구역을 그 구역의 수명 안에 있는 조각만으로 짓는다(띠 = SegmentLifetime, 왜곡 = DistortionLifetime).
	void BuildSection(UProceduralMeshComponent& TrailMesh, const FTrail& Trail, int32 SectionIndex, float Lifetime);
};

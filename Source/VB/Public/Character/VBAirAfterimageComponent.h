// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBAirAfterimageComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPoseableMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UVBAirAfterimageConfig;
class UVBCharacterAppearanceComponent;

USTRUCT()
struct FVBAirAfterimageSnapshot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPoseableMeshComponent>> Meshes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	float Age = -1.0f;
};

/** 보이는 바디와 의상의 자세를 묶어 저장하고 제한된 풀에서 사라지게 하는 로컬 연출. */
UCLASS(ClassGroup=(VB))
class VB_API UVBAirAfterimageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBAirAfterimageComponent();
	static UVBAirAfterimageComponent* FindOrAdd(AActor* Owner);

	// 반환한 요청 번호를 가진 호출자만 생성 중단을 요청할 수 있다. 0은 시작 실패다.
	uint64 BeginSampling(const UVBAirAfterimageConfig* InConfig);
	void StopSampling(uint64 RequestId);
	int32 GetLiveGhostCount() const;
	bool IsSampling() const { return ActiveRequestId != 0; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FSource
	{
		TWeakObjectPtr<USkeletalMeshComponent> Visual;
		TWeakObjectPtr<USkeletalMeshComponent> Pose;
		TWeakObjectPtr<USkeletalMesh> Asset;
		TWeakObjectPtr<USkeletalMesh> PoseAsset;
		bool operator==(const FSource& Other) const
		{
			return Visual == Other.Visual && Pose == Other.Pose && Asset == Other.Asset && PoseAsset == Other.PoseAsset;
		}
	};

	static USkeletalMeshComponent* PoseSourceFor(USkeletalMeshComponent* Visual);
	bool GatherSources(TArray<FSource>& OutSources, FString& OutError) const;
	bool SourcesRemainValid() const;
	void AcquireMeshUpdates();
	void ReleaseMeshUpdates();
	void BindAppearance();
	void UnbindAppearance();
	void HandleAppearanceChanged(UVBCharacterAppearanceComponent* Appearance);
	FVBAirAfterimageSnapshot* AcquireSnapshot();
	void Snapshot();
	void ClearSnapshots();
	void ResetVisuals();

	UPROPERTY(Transient)
	TObjectPtr<const UVBAirAfterimageConfig> Config;

	UPROPERTY(Transient)
	TArray<FVBAirAfterimageSnapshot> Ghosts;

	TArray<FSource> Sources;
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> RefreshMeshes;
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> TickMeshes;
	TWeakObjectPtr<UVBCharacterAppearanceComponent> ObservedAppearance;
	TWeakObjectPtr<UMaterialInterface> PoolMaterial;
	FDelegateHandle AppearanceChangedHandle;
	uint64 ActiveRequestId = 0;
	uint64 NextRequestId = 0;
	int32 PoolCapacity = 0;
	float SinceSample = 0.0f;
	FVector LastSampleLocation = FVector::ZeroVector;
};

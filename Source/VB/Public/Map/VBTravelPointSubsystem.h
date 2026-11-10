// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SaveSystem/VBSavedWorldSubsystem.h"
#include "VBTravelPointSubsystem.generated.h"

class AVBSavePoint;

DECLARE_MULTICAST_DELEGATE(FVBOnTravelPointsDiscoveredChanged);

/**
 * 빠른 이동 지점(늘 떠 있는 AVBSavePoint) 등록부와 "발견한 지점" 을 관리한다. 발견은 서버만 쓰고 저장한다(이 맵에 없는 ID 도 유지).
 * 복제하지 않는다 - 원격 클라는 UVBFastTravelComponent 의 RPC 로 받은 사본을 든다.
 */
UCLASS()
class VB_API UVBTravelPointSubsystem : public UVBSavedWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVBTravelPointSubsystem* Get(const UObject* WorldContextObject);


	//~ IVBSaveParticipant
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

	// 저장점이 BeginPlay, EndPlay 에서 부른다. ID 가 없거나 이미 있으면 등록하지 않고 false(경고, 배치 실수).
	bool Register(AVBSavePoint* Point);
	void Unregister(const AVBSavePoint* Point);

	AVBSavePoint* FindPoint(const FGameplayTag& PointId) const;
	// 살아 있는 지점만, 등록 순서대로.
	TArray<AVBSavePoint*> GetPoints() const;

	// 서버에서만. 처음 발견이면 true. ID 가 비었으면 false.
	bool Discover(const FGameplayTag& PointId);
	bool IsDiscovered(const FGameplayTag& PointId) const { return Discovered.Contains(PointId); }
	const TArray<FGameplayTag>& GetDiscovered() const { return Discovered; }

	// 원격 클라에서만: 서버가 보낸 목록으로 갈아끼운다(쓸 수 없는 ID 는 버린다). UVBFastTravelComponent 의 클라 RPC 만 부른다.
	void ApplyServerDiscovered(const TArray<FGameplayTag>& ServerDiscovered);

	// 발견 목록이 바뀌었다: 새 발견, 불러오기(ReadSave), 서버 사본 받기.
	FVBOnTravelPointsDiscoveredChanged OnDiscoveredChanged;

	// 쓸 수 있는 지점 ID 인가: 비어 있지 않고 뿌리(Map.TravelPoint)만이 아닌 것.
	static bool IsUsablePointId(const FGameplayTag& PointId);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TArray<TWeakObjectPtr<AVBSavePoint>> Points;

	// 발견한 지점 ID(발견 순서). 이 맵에 배치되지 않은 ID 도 들어 있을 수 있다.
	TArray<FGameplayTag> Discovered;
};

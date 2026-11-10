// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "VBWorldMarkerSubsystem.generated.h"

class UVBWorldMarkerComponent;

/**
 * 월드에 현재 있는 마커의 등록부. 마커 컴포넌트가 스스로 등록하고 해제한다 - 화면은 태그로 묻기만 한다.
 * 월드 단위인 이유: 마커는 레벨에 속해 레벨과 함께 생기고 사라진다(월드 파티션 셀이 내려가면 그 마커도 빠진다).
 */
UCLASS()
class VB_API UVBWorldMarkerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Register(UVBWorldMarkerComponent* Marker);
	void Unregister(UVBWorldMarkerComponent* Marker);

	// 태그가 Tags 중 하나와 정확히 같은 마커들. 사라진 것은 건너뛴다.
	void FindMarkers(const FGameplayTagContainer& Tags, TArray<UVBWorldMarkerComponent*>& OutMarkers) const;

	int32 GetMarkerCount() const { return Markers.Num(); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TArray<TWeakObjectPtr<UVBWorldMarkerComponent>> Markers;
};

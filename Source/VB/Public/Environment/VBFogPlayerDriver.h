// ProjectVB
// Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBFogPlayerDriver.generated.h"

class UMaterialParameterCollection;

/** 로컬 플레이어 위치를 안개 재질에 전달한다. */
UCLASS()
class VB_API AVBFogPlayerDriver : public AActor
{
	GENERATED_BODY()

public:
	AVBFogPlayerDriver();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBFogPlayerDriverTest;
#endif

	/** 공유 안개 위치 파라미터를 보유한다. */
	UPROPERTY(EditInstanceOnly, Category="VB|Fog")
	TObjectPtr<UMaterialParameterCollection> FogParameters;

	/** 플레이어 위치와 유효 여부를 기록한다. */
	void UpdateFogPosition(bool bClear);

	/** 컬렉션 누락 경고 출력 여부를 보관한다. */
	bool bWarnedMissingCollection = false;

	/** 컬렉션 인스턴스 누락 경고 출력 여부를 보관한다. */
	bool bWarnedMissingInstance = false;

	/** 파라미터 누락 경고 출력 여부를 보관한다. */
	bool bWarnedMissingParameter = false;
};

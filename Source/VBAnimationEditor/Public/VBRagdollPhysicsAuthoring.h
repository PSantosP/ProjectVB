// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBRagdollPhysicsAuthoring.generated.h"

class UPhysicsAsset;

/** 래그돌 관절 프레임을 조회하고 편집한 제약 값을 명시한 프로필로 보존한다. */
UCLASS()
class VBANIMATIONEDITOR_API UVBRagdollPhysicsAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="VB|Animation|Editor")
	static FString DescribeConstraints(UPhysicsAsset* Asset);

	// 패키지 저장은 검증을 마친 호출자가 수행한다.
	UFUNCTION(BlueprintCallable, Category="VB|Animation|Editor")
	static bool StoreConstraintProfile(UPhysicsAsset* Asset, FName ProfileName);
};

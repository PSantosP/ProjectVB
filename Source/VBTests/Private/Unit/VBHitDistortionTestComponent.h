// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CharacterCommon/VBHitFlashComponent.h"
#include "Data/VBHitDistortionConfig.h"
#include "VBHitDistortionTestComponent.generated.h"

// Niagara 호출만 기록으로 바꾼 실제 hit flash 컴포넌트다
// nullrhi 시험은 Niagara 컴포넌트를 못 얻으므로(FApp::CanEverRender) 어느 system 을 어디에 띄울지 판정만 본다
UCLASS(NotBlueprintable)
class UVBHitDistortionTestComponent : public UVBHitFlashComponent
{
	GENERATED_BODY()

public:
	struct FRequest
	{
		const UNiagaraSystem* System = nullptr;
		FVector Location = FVector::ZeroVector;
	};
	TArray<FRequest> Requests;

protected:
	virtual void SpawnDistortionEffect(UWorld& World, const FVBHitDistortionProfile& Profile, const FVector& Location) override
	{
		Requests.Add({Profile.System, Location});
	}
};
